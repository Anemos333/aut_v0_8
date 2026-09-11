#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'Source/ModernPitchEngine.h'
ENGINE = ROOT / 'Source/ModernPitchEngine.cpp'
TEST = ROOT / 'Tests/SupervisorContinuityTest.cpp'
MARKER = 'AUTHORITATIVE_SINGLE_GLIDE_V12'


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)


def replace_section(text, start, end, replacement, label):
    first = text.find(start)
    if first < 0:
        raise RuntimeError(f'{label}: start marker not found')
    last = text.find(end, first + len(start))
    if last < 0:
        raise RuntimeError(f'{label}: end marker not found')
    return text[:first] + replacement + text[last:]


h = HEADER.read_text(encoding='utf-8')
cpp = ENGINE.read_text(encoding='utf-8')

if MARKER in h and MARKER in cpp:
    print(f'{MARKER}=already_materialized')
    raise SystemExit(0)

# ---------------------------------------------------------------------------
# Header: there is no longer a privileged zero-prudence endpoint. The latest
# accepted F0 is the correction coordinate for every setting; user controls
# soften one common law instead of selecting a second authority path.
h = replace_once(
    h,
    '''        // LIVE_CORRECTION_COORDINATE_V5: latest accepted live F0 in the same\n        // register, used only by the fully rigid Scale Lock endpoint. This\n        // prevents continuity smoothing from becoming audible pitch residual.\n        float correctionFrequencyHz = 0.0f;\n''',
    '''        // AUTHORITATIVE_SINGLE_GLIDE_V12: latest accepted live F0 in the\n        // same register. Every setting uses this coordinate; Amount, Response,\n        // Humanize, Hold and Vibrato only soften one common correction law.\n        float correctionFrequencyHz = 0.0f;\n''',
    'correction coordinate comment')

h = replace_once(
    h,
    '        void setImmediateAuthority(bool enabled) noexcept { immediateAuthority_ = enabled; } // AUTHORITY_CONTROLS_EXPLICIT_V1\n',
    '',
    'remove endpoint-only tracker authority setter')
h = replace_once(
    h,
    '        bool immediateAuthority_ = false; // AUTHORITY_CONTROLS_EXPLICIT_V1\n',
    '',
    'remove endpoint-only tracker authority state')
h = replace_once(
    h,
    '    [[nodiscard]] static bool exactScaleLockAuthority(const Parameters& parameters) noexcept; // AUTHORITY_CONTROLS_EXPLICIT_V1\n    [[nodiscard]] static bool zeroPrudenceAuthority(const Parameters& parameters) noexcept;\n',
    '',
    'remove endpoint authority predicates')

# ---------------------------------------------------------------------------
# Decoder: current detector evidence owns pitch identity on every setting.
# History may measure continuity, but it cannot outvote a current hypothesis or
# manufacture a held branch that delays the new musical identity.
new_decoder = r'''void ModernPitchEngine::MultiRatePitchTracker::updateDecoderBeam(
    const std::array<ConsensusHypothesis, maxConsensusHypotheses>& hypotheses,
    int hypothesisCount,
    bool onsetPending) noexcept
{
    (void) onsetPending;
    std::array<DecoderState, maxConsensusHypotheses> proposals {};
    int proposalCount = 0;

    for (int hypothesisIndex = 0;
         hypothesisIndex < hypothesisCount && proposalCount < maxConsensusHypotheses;
         ++hypothesisIndex)
    {
        const auto& hypothesis = hypotheses[static_cast<std::size_t>(hypothesisIndex)];
        if (!hypothesis.valid)
            continue;

        DecoderState proposal;
        proposal.valid = true;
        proposal.logFrequency = safeLog2(hypothesis.frequencyHz);
        proposal.score = hypothesis.evidenceScore + 0.26f * hypothesis.consensus;
        proposal.octaveIndex = octaveState_;
        proposal.ageInHops = 0;
        proposals[static_cast<std::size_t>(proposalCount++)] = proposal;
    }

    std::sort(proposals.begin(),
              proposals.begin() + proposalCount,
              [](const DecoderState& left, const DecoderState& right)
              {
                  return left.score > right.score;
              });

    decoderBeam_.fill({});
    int accepted = 0;
    for (int proposalIndex = 0;
         proposalIndex < proposalCount && accepted < decoderBeamWidth;
         ++proposalIndex)
    {
        const auto& proposal = proposals[static_cast<std::size_t>(proposalIndex)];
        bool duplicate = false;
        for (int existing = 0; existing < accepted; ++existing)
        {
            const float distance = static_cast<float>(1200.0
                * std::abs(proposal.logFrequency
                         - decoderBeam_[static_cast<std::size_t>(existing)].logFrequency));
            if (distance < 24.0f)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            decoderBeam_[static_cast<std::size_t>(accepted++)] = proposal;
    }
}

'''
cpp = replace_section(
    cpp,
    'void ModernPitchEngine::MultiRatePitchTracker::updateDecoderBeam(',
    'ModernPitchEngine::MultiRatePitchTracker::DecoderDecision\nModernPitchEngine::MultiRatePitchTracker::decodeCandidate',
    new_decoder,
    'authoritative detector beam')

# Remove the old endpoint member reset now that the detector law is uniform.
cpp = cpp.replace('    immediateAuthority_ = false; // AUTHORITY_CONTROLS_EXPLICIT_V1\n', '')

# ---------------------------------------------------------------------------
# Quantizer: Hold is the only target-identity hold. Confidence, hard-lock flags,
# onset and hidden confirmation counts cannot postpone a degree once it wins the
# user-selected hysteresis boundary.
new_quantizer = r'''double ModernPitchEngine::ScaleQuantizer::chooseTargetLog2(
    double inputLog2,
    float hysteresisCents,
    float strictness,
    float confidence,
    bool hardLock,
    bool onset,
    int& pendingObservations) noexcept
{
    (void) strictness;
    (void) confidence;
    (void) hardLock;
    (void) onset;
    pendingObservations = 0;

    if (!std::isfinite(inputLog2) || ratioCount_ <= 0)
        return inputLog2;

    const double relative = inputLog2 - rootLog2_;
    const double octave = std::floor(relative);
    double nearest = inputLog2;
    double nearestDistance = std::numeric_limits<double>::infinity();

    for (int i = 0; i < ratioCount_; ++i)
    {
        const double degree = logRatios_[static_cast<std::size_t>(i)];
        for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset)
        {
            const double candidate = rootLog2_ + octave
                + static_cast<double>(octaveOffset) + degree;
            const double distance = std::abs(candidate - inputLog2);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = candidate;
            }
        }
    }

    if (targetValid_)
    {
        const double previousDistance = std::abs(targetLog2_ - inputLog2);
        const double hysteresisOctaves = std::max(0.0f, hysteresisCents) / 1200.0;
        if (previousDistance <= nearestDistance + hysteresisOctaves)
            nearest = targetLog2_;
    }

    targetLog2_ = nearest;
    targetValid_ = true;
    pendingValid_ = false;
    pendingCount_ = 0;
    return targetLog2_;
}

'''
cpp = replace_section(
    cpp,
    'double ModernPitchEngine::ScaleQuantizer::chooseTargetLog2(',
    '//==============================================================================\n// ModernPitchEngine control and processing',
    new_quantizer,
    'single-law target quantizer')

# Delete the special-setting predicates entirely.
cpp = replace_section(
    cpp,
    '// AUTHORITY_CONTROLS_EXPLICIT_V1: only visible user controls define musical',
    'int ModernPitchEngine::latencyForMode(LatencyMode mode) noexcept',
    '',
    'remove special endpoint authority functions')

# ---------------------------------------------------------------------------
# Hold and Response now have literal, orthogonal meanings. Hold never depends on
# confidence/mode/tempo; scale density only caps an unsafe oversized Hold. A
# Response of zero requests the fastest finite glide, not an alternate hard jump.
new_hysteresis = r'''float ModernPitchEngine::adaptiveHysteresis(
    const Parameters& parameters,
    const ScaleQuantizer& quantizer,
    const PitchObservation& observation) const noexcept
{
    (void) observation;
    const float minimumStep = std::max(0.1f, quantizer.minimumStepCents());

    if (!parameters.scaleLock)
    {
        return std::clamp(0.22f * minimumStep * clamp01(parameters.humanize),
                          0.0f, 80.0f);
    }

    const float requested = std::clamp(finiteOr(parameters.lockHysteresis, 24.0f),
                                       0.0f, 80.0f);
    const float degreeSafeCap = std::clamp(minimumStep * 0.30f, 0.35f, 36.0f);
    return std::min(requested, degreeSafeCap);
}

'''
cpp = replace_section(
    cpp,
    'float ModernPitchEngine::adaptiveHysteresis(',
    'double ModernPitchEngine::responseTimeMs(',
    new_hysteresis,
    'orthogonal Hold law')

new_response = r'''double ModernPitchEngine::responseTimeMs(
    const Parameters& parameters,
    bool targetChanged,
    double targetJumpCents) const noexcept
{
    const double requested = std::clamp(
        static_cast<double>(finiteOr(parameters.retuneTimeMs, 50.0f)),
        0.0, 500.0);

    // One trajectory always exists. Response=0 therefore means the fastest
    // finite trajectory (0.35 ms), not a bypass around the glide controller.
    double response = std::max(0.35, requested);
    if (targetChanged && std::abs(targetJumpCents) > 0.1
        && parameters.tempo.mode != CreativeTempo::Mode::off)
    {
        const double transitionMs = std::clamp(
            static_cast<double>(finiteOr(parameters.transitionTimeMs, 35.0f)),
            0.0, 500.0);
        response = std::max(response, transitionMs);
    }
    return std::clamp(response, 0.35, 500.0);
}

'''
cpp = replace_section(
    cpp,
    'double ModernPitchEngine::responseTimeMs(',
    'void ModernPitchEngine::updateCorrectionState(',
    new_response,
    'continuous Response law')

# ---------------------------------------------------------------------------
# Supervisor: signal plus a valid target is voiced/stable immediately. Breath,
# consensus, confidence, body labels and latency mode never scale correction or
# decide whether the selected target is allowed to own the voice. Pitch centre
# remains only a modulation reference for explicit Vibrato/Humanize.
new_update = r'''void ModernPitchEngine::updateCorrectionState(
    CorrectionState& state,
    ScaleQuantizer& quantizer,
    const PitchObservation& observation,
    const Parameters& parameters) noexcept
{
    const int hopSamples = MultiRatePitchTracker::hopSize();
    const float humanize = clamp01(parameters.humanize);
    const bool validPitch = observation.valid
        && std::isfinite(observation.frequencyHz)
        && observation.frequencyHz > 0.0f;
    const bool audioPresent = observation.audioPresent || validPitch;

    const auto setState = [&state](TrackingState next) noexcept
    {
        if (state.trackingState != next)
        {
            state.trackingState = next;
            state.stateAgeSamples = 0;
        }
    };

    if (!validPitch)
    {
        ++state.invalidObservations;
        if (state.noteBodyLatched)
        {
            state.pitchStaleSamples = std::min(
                std::numeric_limits<int>::max() - hopSamples,
                state.pitchStaleSamples + hopSamples);
        }

        if (audioPresent)
        {
            state.noteBodyLatched = true;
            state.noteBodyConfidence = 1.0f;
            state.stableBodyObservations = std::max(1, state.stableBodyObservations);
            setState(state.targetValid ? TrackingState::stable
                                       : TrackingState::acquire);
            return;
        }

        // Silence does not rewrite the musical destination. There is no dry
        // return path; the stored target simply waits for the next signal.
        if (state.targetValid)
        {
            setState(TrackingState::stable);
            return;
        }

        setState(TrackingState::unvoiced);
        return;
    }

    state.invalidObservations = 0;
    state.pitchStaleSamples = 0;
    state.noteBodyLatched = true;
    state.noteBodyConfidence = 1.0f;
    state.stableBodyObservations = std::min(32, state.stableBodyObservations + 1);

    const double observedLog2 = safeLog2(observation.frequencyHz);
    const float correctionFrequencyHz =
        std::isfinite(observation.correctionFrequencyHz)
        && observation.correctionFrequencyHz > 0.0f
        ? observation.correctionFrequencyHz
        : observation.frequencyHz;
    const double correctionObservedLog2 = safeLog2(correctionFrequencyHz);

    // Pitch centre is not target authority. It exists only to extract the
    // modulation component that the user may explicitly preserve.
    if (!state.pitchCentreValid || observation.onset)
    {
        state.pitchCentreLog2 = observedLog2;
        state.pitchCentreValid = true;
    }
    else
    {
        const double centreAlpha = 0.12 + 0.28 * (1.0 - static_cast<double>(humanize));
        state.pitchCentreLog2 += centreAlpha * (observedLog2 - state.pitchCentreLog2);
    }

    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);
    int pending = 0;
    double newTarget = quantizer.chooseTargetLog2(
        correctionObservedLog2,
        hysteresis,
        0.0f,
        1.0f,
        false,
        observation.onset,
        pending);
    newTarget += std::round(correctionObservedLog2 - newTarget);

    const double targetJump = state.targetValid
        ? (newTarget - state.targetLog2) * 1200.0
        : 0.0;
    const bool targetChanged = !state.targetValid || std::abs(targetJump) > 0.5;
    state.lastTargetJumpCents = targetJump;
    if (targetChanged)
        ++state.revision;
    state.targetLog2 = newTarget;
    state.targetValid = true;
    ++state.stableObservations;

    // A target exists and signal exists: this is a voiced/stable musical state.
    // The glide is represented by currentCents != desiredCents, not by a long
    // acquire/transition permission state.
    setState(TrackingState::stable);
    state.transportPeriodHz = static_cast<double>(correctionFrequencyHz);

    const double vibratoComponent = observedLog2 - state.pitchCentreLog2;
    const double halfStep = 0.5 * static_cast<double>(quantizer.minimumStepCents());
    const double centreError = std::abs((state.targetLog2 - state.pitchCentreLog2) * 1200.0);
    const float boundarySafety = 1.0f - smoothStep(
        static_cast<float>(0.58 * halfStep),
        static_cast<float>(0.92 * halfStep),
        static_cast<float>(centreError));

    float preserve = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve + 0.35f * humanize)
        : clamp01(parameters.preserveVibrato);
    preserve *= clamp01(observation.periodicity) * boundarySafety;

    double humanWindow = 16.0 * static_cast<double>(humanize);
    double correctedLog2 = state.targetLog2
        + static_cast<double>(preserve) * vibratoComponent;

    if (parameters.scaleLock)
    {
        const double minimumStep = std::max(
            0.1, static_cast<double>(quantizer.minimumStepCents()));
        const double residualBudgetCents = std::clamp(minimumStep * 0.18, 1.0, 6.0);
        humanWindow = std::min(2.0 * static_cast<double>(humanize),
                               0.30 * residualBudgetCents);
        const double vibratoBudgetCents = std::max(0.0,
            residualBudgetCents - humanWindow);
        const double requestedVibratoCents =
            static_cast<double>(preserve) * vibratoComponent * 1200.0;
        const double preservedVibratoCents = std::clamp(
            requestedVibratoCents,
            -vibratoBudgetCents,
            vibratoBudgetCents);
        correctedLog2 = state.targetLog2 + preservedVibratoCents / 1200.0;
    }

    double errorCents = (correctedLog2 - correctionObservedLog2) * 1200.0;
    const double errorMagnitude = std::abs(errorCents);
    const double liveMagnitude = std::max(0.0, errorMagnitude - humanWindow);
    errorCents = std::copysign(liveMagnitude, errorCents);

    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    errorCents = std::clamp(errorCents, -maximumCents, maximumCents);
    state.desiredCents = errorCents * static_cast<double>(clamp01(parameters.amount));
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);

    meterPendingOctave_.store(0, std::memory_order_relaxed);
    meterOctaveState_.store(observation.octaveState, std::memory_order_relaxed);
}

'''
cpp = replace_section(
    cpp,
    'void ModernPitchEngine::updateCorrectionState(',
    'double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept',
    new_update,
    'authoritative correction supervisor')

new_advance = r'''double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept
{
    if (!state.targetValid)
        return state.currentCents;

    if (state.stateAgeSamples < std::numeric_limits<int>::max())
        ++state.stateAgeSamples;

    const double dt = 1.0 / sampleRate_;
    const double responseSeconds = std::max(0.00035, state.responseMs * 0.001);
    const double omega = std::min(0.22 / dt, 4.6 / responseSeconds);
    double acceleration = omega * omega
        * (state.desiredCents - state.currentCents)
        - 2.0 * omega * state.velocityCentsPerSecond;
    const double maximumVelocity = std::max(3600.0,
        10.0 * std::max(120.0, std::abs(state.desiredCents)) / responseSeconds);
    const double maximumAcceleration = maximumVelocity
        / std::max(0.0005, responseSeconds * 0.30);
    acceleration = std::clamp(acceleration,
                              -maximumAcceleration,
                              maximumAcceleration);
    state.velocityCentsPerSecond += acceleration * dt;
    state.velocityCentsPerSecond = std::clamp(state.velocityCentsPerSecond,
                                              -maximumVelocity,
                                              maximumVelocity);
    state.currentCents += state.velocityCentsPerSecond * dt;

    // Exact convergence is part of the contract: once the continuous glide has
    // arrived, numerical residue is snapped to the exact requested destination.
    if (std::abs(state.desiredCents - state.currentCents) < 0.001
        && std::abs(state.velocityCentsPerSecond) < 0.02)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
    }
    return state.currentCents;
}

'''
cpp = replace_section(
    cpp,
    'double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept',
    'void ModernPitchEngine::process(\n    juce::AudioBuffer<float>& buffer,\n    const double* scaleRatios,',
    new_advance,
    'single continuous glide controller')

# Process block: remove the endpoint switch and the stale-anchor rescue mode.
cpp = replace_once(
    cpp,
    '    safe.latencyMode = static_cast<int>(latencyMode_);\n    const bool immediateAuthority = zeroPrudenceAuthority(safe); // AUTHORITY_CONTROLS_EXPLICIT_V1\n',
    '    safe.latencyMode = static_cast<int>(latencyMode_);\n',
    'remove block endpoint predicate')
cpp = replace_once(
    cpp,
    '''    linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);\n    linkedTracker_.setSensitivity(safe.detectorSensitivity);\n    linkedTracker_.setImmediateAuthority(immediateAuthority);\n    for (int channel = 0; channel < channels; ++channel)\n    {\n        channelTrackers_[static_cast<std::size_t>(channel)].setRange(\n            safe.minimumPitchHz, safe.maximumPitchHz);\n        channelTrackers_[static_cast<std::size_t>(channel)].setSensitivity(\n            safe.detectorSensitivity);\n        channelTrackers_[static_cast<std::size_t>(channel)].setImmediateAuthority(\n            immediateAuthority);\n    }\n''',
    '''    linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);\n    linkedTracker_.setSensitivity(safe.detectorSensitivity);\n    linkedTracker_.setRescueMode(false);\n    linkedTracker_.clearReacquisitionAnchor();\n    for (int channel = 0; channel < channels; ++channel)\n    {\n        channelTrackers_[static_cast<std::size_t>(channel)].setRange(\n            safe.minimumPitchHz, safe.maximumPitchHz);\n        channelTrackers_[static_cast<std::size_t>(channel)].setSensitivity(\n            safe.detectorSensitivity);\n        channelTrackers_[static_cast<std::size_t>(channel)].setRescueMode(false);\n        channelTrackers_[static_cast<std::size_t>(channel)].clearReacquisitionAnchor();\n    }\n''',
    'uniform tracker setup')

old_dual_rescue = '''                if (correction.noteBodyLatched && correction.transportPeriodHz > 0.0)\n                    tracker.setReacquisitionAnchor(static_cast<float>(correction.transportPeriodHz));\n                else\n                    tracker.clearReacquisitionAnchor();\n                const bool rescueSearch = correction.noteBodyLatched\n                    && correction.pitchStaleSamples >= static_cast<int>(0.060 * sampleRate_);\n                tracker.setRange(rescueSearch ? std::min(safe.minimumPitchHz, 28.0f) : safe.minimumPitchHz,\n                                 safe.maximumPitchHz);\n                tracker.setSensitivity(rescueSearch ? std::max(safe.detectorSensitivity, 0.98f)\n                                                    : safe.detectorSensitivity);\n                tracker.setRescueMode(rescueSearch); // PITCH_RESCUE_V1\n'''
new_dual_rescue = '''                tracker.clearReacquisitionAnchor();\n                tracker.setRange(safe.minimumPitchHz, safe.maximumPitchHz);\n                tracker.setSensitivity(safe.detectorSensitivity);\n                tracker.setRescueMode(false);\n'''
cpp = replace_once(cpp, old_dual_rescue, new_dual_rescue, 'remove dual-mono rescue authority')

old_linked_rescue = '''            if (linkedCorrection_.noteBodyLatched && linkedCorrection_.transportPeriodHz > 0.0)\n                linkedTracker_.setReacquisitionAnchor(\n                    static_cast<float>(linkedCorrection_.transportPeriodHz));\n            else\n                linkedTracker_.clearReacquisitionAnchor();\n            const bool rescueSearch = linkedCorrection_.noteBodyLatched\n                && linkedCorrection_.pitchStaleSamples >= static_cast<int>(0.060 * sampleRate_);\n            linkedTracker_.setRange(rescueSearch ? std::min(safe.minimumPitchHz, 28.0f) : safe.minimumPitchHz,\n                                    safe.maximumPitchHz);\n            linkedTracker_.setSensitivity(rescueSearch ? std::max(safe.detectorSensitivity, 0.98f)\n                                                       : safe.detectorSensitivity);\n            linkedTracker_.setRescueMode(rescueSearch); // PITCH_RESCUE_V1\n'''
new_linked_rescue = '''            linkedTracker_.clearReacquisitionAnchor();\n            linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);\n            linkedTracker_.setSensitivity(safe.detectorSensitivity);\n            linkedTracker_.setRescueMode(false);\n'''
cpp = replace_once(cpp, old_linked_rescue, new_linked_rescue, 'remove linked rescue authority')

# ---------------------------------------------------------------------------
# Replace the prudence-oriented supervisor suite with direct invariants for the
# new musical contract. Other detector/renderer regression executables remain in
# the repository; this suite now owns authority, stable-voiced state and glide.
test = r'''#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <iostream>

namespace
{
bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

ModernPitchEngine::PitchObservation pitch(float frequency)
{
    ModernPitchEngine::PitchObservation observation;
    observation.valid = true;
    observation.audioPresent = true;
    observation.frequencyHz = frequency;
    observation.correctionFrequencyHz = frequency;
    observation.confidence = 0.95f;
    observation.periodicity = 0.95f;
    observation.consensus = 0.88f;
    observation.voicing = 1.0f;
    return observation;
}
}

int main()
{
    bool success = true;
    ModernPitchEngine engine;
    engine.prepare(48000.0, 512, 1, ModernPitchEngine::LatencyMode::live);

    const double unison = 1.0;
    ModernPitchEngine::ScaleQuantizer quantizer;
    quantizer.reset();
    quantizer.setScale(&unison, 1, 440.0);

    ModernPitchEngine::Parameters hard;
    hard.amount = 1.0f;
    hard.retuneTimeMs = 0.0f;
    hard.humanize = 0.0f;
    hard.preserveVibrato = 0.0f;
    hard.scaleLock = true;
    hard.lockHysteresis = 0.0f;
    hard.vibratoPreserve = 0.0f;
    hard.lockStrictness = 1.0f;      // must not add hidden prudence
    hard.hardLockActive = true;     // must not select a second law
    hard.maximumCorrectionSemitones = 24.0f;
    hard.tempo.mode = CreativeTempo::Mode::off;

    auto observation = pitch(452.0f);
    ModernPitchEngine::CorrectionState state;
    engine.updateCorrectionState(state, quantizer, observation, hard);
    const double targetHz = std::exp2(state.targetLog2);
    const double expected = 1200.0 * std::log2(targetHz / 452.0);

    success &= check(state.targetValid
                     && state.trackingState == ModernPitchEngine::TrackingState::stable,
                     "signal_with_target_is_immediately_voiced_stable");
    success &= check(std::abs(state.desiredCents - expected) < 1.0e-9,
                     "amount100_humanize0_vibrato0_requests_exact_target");
    success &= check(state.responseMs >= 0.349 && state.responseMs <= 0.351,
                     "response_zero_is_fastest_finite_glide");

    const double firstGlide = engine.advanceCorrection(state);
    success &= check(std::abs(firstGlide - state.desiredCents) > 1.0e-6
                     && std::abs(firstGlide) > 0.0,
                     "response_zero_does_not_bypass_glide");
    for (int sample = 0; sample < 4800; ++sample)
        static_cast<void>(engine.advanceCorrection(state));
    success &= check(state.currentCents == state.desiredCents,
                     "glide_converges_exactly_to_requested_target");

    // Low confidence/consensus describes measurement quality; once the tracker
    // publishes a valid F0 it cannot reduce correction authority or voice state.
    auto weak = pitch(452.0f);
    weak.confidence = 0.01f;
    weak.periodicity = 0.05f;
    weak.consensus = 0.0f;
    ModernPitchEngine::CorrectionState weakState;
    ModernPitchEngine::ScaleQuantizer weakQuantizer;
    weakQuantizer.reset();
    weakQuantizer.setScale(&unison, 1, 440.0);
    engine.updateCorrectionState(weakState, weakQuantizer, weak, hard);
    success &= check(weakState.trackingState == ModernPitchEngine::TrackingState::stable
                     && std::abs(weakState.desiredCents - expected) < 1.0e-9,
                     "weak_evidence_cannot_reduce_valid_pitch_authority");

    // Consonant / transient hole: audible signal remains the same voice and the
    // already selected musical destination is held without falling to acquire.
    ModernPitchEngine::PitchObservation consonant;
    consonant.audioPresent = true;
    consonant.voicing = 1.0f;
    const double held = weakState.desiredCents;
    engine.updateCorrectionState(weakState, weakQuantizer, consonant, hard);
    success &= check(weakState.trackingState == ModernPitchEngine::TrackingState::stable
                     && weakState.targetValid
                     && std::abs(weakState.desiredCents - held) < 1.0e-12,
                     "audible_consonant_keeps_voiced_target_and_correction");

    // Hold owns target identity by itself. Even hostile hidden confidence and
    // hard-lock values cannot require repeated confirmation after Hold is crossed.
    std::array<double, 12> chromatic {};
    for (int degree = 0; degree < 12; ++degree)
        chromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    ModernPitchEngine::ScaleQuantizer targetQuantizer;
    targetQuantizer.reset();
    targetQuantizer.setScale(chromatic.data(), static_cast<int>(chromatic.size()), 440.0);
    int pending = 99;
    const double a4 = std::log2(440.0);
    static_cast<void>(targetQuantizer.chooseTargetLog2(
        a4, 0.0f, 1.0f, 0.0f, true, false, pending));
    const double as4 = std::log2(466.1637615180899);
    const double movedTarget = targetQuantizer.chooseTargetLog2(
        as4, 0.0f, 1.0f, 0.0f, true, false, pending);
    success &= check(std::abs((movedTarget - as4) * 1200.0) < 0.001
                     && pending == 0,
                     "hold_crossing_changes_degree_without_confidence_confirmation");

    success &= check(std::abs(engine.adaptiveHysteresis(hard, targetQuantizer, weak)) < 1.0e-12,
                     "hold_zero_is_literally_zero_for_every_evidence_level");

    // Amount is ordinary scaling of the same target error, not another path.
    ModernPitchEngine::Parameters half = hard;
    half.amount = 0.5f;
    ModernPitchEngine::CorrectionState halfState;
    ModernPitchEngine::ScaleQuantizer halfQuantizer;
    halfQuantizer.reset();
    halfQuantizer.setScale(&unison, 1, 440.0);
    engine.updateCorrectionState(halfState, halfQuantizer, observation, half);
    success &= check(std::abs(halfState.desiredCents - 0.5 * expected) < 1.0e-9,
                     "amount_softens_same_authoritative_target_law");

    return success ? 0 : 1;
}
'''

# Source contracts: special endpoints and correction-authority breath semantics
# are gone; the renderer remains the already-established single wet path.
for forbidden in (
    'exactScaleLockAuthority',
    'zeroPrudenceAuthority',
    'immediateAuthority_',
    'state.responseMs = 0.0',
    'breathConfirmSamples',
    'ambiguousReleaseSamples',
    'setImmediateAuthority(',
):
    if forbidden in cpp or forbidden in h:
        raise RuntimeError(f'forbidden authority token survived: {forbidden}')

if 'parameters.voiceBreathiness' in cpp[cpp.find('void ModernPitchEngine::updateCorrectionState('):cpp.find('double ModernPitchEngine::advanceCorrection(')]:
    raise RuntimeError('breath evidence still owns correction supervisor')
if 'TrackingState::transition' in cpp[cpp.find('void ModernPitchEngine::updateCorrectionState('):cpp.find('double ModernPitchEngine::advanceCorrection(')]:
    raise RuntimeError('transition permission state survived inside correction supervisor')
if 'TrackingState::acquire' not in cpp:
    raise RuntimeError('acquire telemetry state unexpectedly removed entirely')

HEADER.write_text(h, encoding='utf-8')
ENGINE.write_text(cpp, encoding='utf-8')
TEST.write_text(test, encoding='utf-8')
print(f'{MARKER}=PASS')
