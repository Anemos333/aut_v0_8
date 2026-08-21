from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_between(text: str, start: str, end: str, replacement: str, label: str) -> str:
    a = text.find(start)
    if a < 0:
        raise RuntimeError(f"{label}: start marker not found")
    b = text.find(end, a)
    if b < 0:
        raise RuntimeError(f"{label}: end marker not found")
    if text.find(start, a + 1) >= 0:
        raise RuntimeError(f"{label}: start marker is not unique")
    return text[:a] + replacement + text[b:]


cpp_path = Path("Source/ModernPitchEngine.cpp")
cpp = cpp_path.read_text()

# Freeze period-guidance state while the musical supervisor is not stable.
cpp = replace_once(
    cpp,
    """    if (state.trackingState != TrackingState::stable\n        || !state.noteBodyLatched || state.transportPeriodHz <= 0.0)\n    {\n        return 0.0f;\n    }\n""",
    """    if (!state.noteBodyLatched || state.transportPeriodHz <= 0.0)\n        return 0.0f;\n\n    // Negative strength means hold the already-established geometry. State\n    // changes must not themselves ramp the read-head separation. The stored\n    // guidance is allowed to evolve again only after the note body is stable.\n    if (state.trackingState != TrackingState::stable)\n        return -1.0f;\n""",
    "transport supervision state gate",
)

cpp = replace_once(
    cpp,
    """    if (periodAware)\n    {\n        const double nominalSeparation = plan.delayB - plan.delayA;\n""",
    """    if (periodAware)\n    {\n        const bool holdPeriodGuidance = std::isfinite(syncStrength)\n            && syncStrength < 0.0f;\n        const double nominalSeparation = plan.delayB - plan.delayA;\n""",
    "period guidance hold flag",
)

cpp = replace_once(
    cpp,
    """        periodNudgeSamples_ += static_cast<double>(periodSyncSmoothing_)\n            * (requestedNudge - periodNudgeSamples_);\n        periodSyncAmount_ += periodSyncSmoothing_\n            * (requestedSync - periodSyncAmount_);\n\n        const double sign = nominalSeparation >= 0.0 ? 1.0 : -1.0;\n        double adjustedMagnitude = std::max(\n            0.0,\n            nominalMagnitude\n                + static_cast<double>(periodSyncAmount_) * periodNudgeSamples_);\n""",
    """        if (!holdPeriodGuidance)\n        {\n            periodNudgeSamples_ += static_cast<double>(periodSyncSmoothing_)\n                * (requestedNudge - periodNudgeSamples_);\n            periodSyncAmount_ += periodSyncSmoothing_\n                * (requestedSync - periodSyncAmount_);\n        }\n\n        // sweepPresence is applied at render time as well as acquisition time.\n        // Thus exact unity always collapses to the declared latency even when a\n        // non-stable state is deliberately holding the previous period model.\n        const double appliedPeriodAmount = static_cast<double>(periodSyncAmount_)\n            * static_cast<double>(sweepPresence);\n        const double sign = nominalSeparation >= 0.0 ? 1.0 : -1.0;\n        double adjustedMagnitude = std::max(\n            0.0,\n            nominalMagnitude + appliedPeriodAmount * periodNudgeSamples_);\n""",
    "period guidance freeze and unity collapse",
)

new_update = r'''void ModernPitchEngine::updateCorrectionState(
    CorrectionState& state,
    ScaleQuantizer& quantizer,
    const PitchObservation& observation,
    const Parameters& parameters) noexcept
{
    const int hopSamples = MultiRatePitchTracker::hopSize();
    const double hopSeconds = static_cast<double>(hopSamples) / sampleRate_;
    const float humanize = clamp01(parameters.humanize);
    const bool richEvidence = parameters.voiceEvidenceValid;
    const bool validPitch = observation.valid && observation.frequencyHz > 0.0f;

    const float trackerBody = validPitch
        ? clamp01(0.34f * observation.voicing
                + 0.28f * observation.periodicity
                + 0.23f * observation.confidence
                + 0.15f * observation.consensus)
        : 0.0f;
    const float analysedBody = richEvidence
        ? clamp01(0.44f * parameters.voiceBodyEnergy
                + 0.24f * parameters.voiceHarmonicity
                + 0.18f * parameters.voiceSpectralReliability
                + 0.14f * (1.0f - parameters.voiceBreathiness))
        : trackerBody;
    const float bodyScore = richEvidence
        ? std::max(0.72f * analysedBody, trackerBody)
        : trackerBody;

    // Entering a note requires stronger evidence than staying in one. This
    // hysteresis is about note identity only: it never scales Amount or the
    // correction destination.
    const float enterBodyThreshold = 0.46f - 0.06f * humanize;
    const float holdBodyThreshold = 0.34f - 0.05f * humanize;
    const float bodyThreshold = state.noteBodyLatched
        ? holdBodyThreshold : enterBodyThreshold;
    const bool bodyPresent = bodyScore >= bodyThreshold
        && (!richEvidence || parameters.voiceBreathiness < 0.76f
            || parameters.voiceHarmonicity > 0.48f);

    const float breathScore = richEvidence
        ? clamp01(0.58f * parameters.voiceBreathiness
                + 0.22f * (1.0f - parameters.voiceBodyEnergy)
                + 0.12f * (1.0f - parameters.voiceHarmonicity)
                + 0.08f * (1.0f - parameters.voiceSpectralReliability))
        : 0.0f;
    const bool confirmedBreathFrame = richEvidence
        && breathScore > 0.62f
        && parameters.voiceBreathiness > 0.56f
        && parameters.voiceBodyEnergy < 0.48f
        && parameters.voiceEventStrength < 0.82f;
    const bool confirmedAbsenceFrame = richEvidence
        && parameters.voiceBodyEnergy < 0.20f
        && parameters.voiceHarmonicity < 0.22f
        && parameters.voiceSpectralReliability < 0.28f
        && parameters.voiceEventStrength < 0.72f;

    const float bodyAttack = std::clamp(static_cast<float>(
        1.0 - std::exp(-hopSeconds / 0.018)), 0.001f, 1.0f);
    const float bodyRelease = std::clamp(static_cast<float>(
        1.0 - std::exp(-hopSeconds / 0.070)), 0.001f, 1.0f);
    const float bodyAlpha = bodyScore >= state.noteBodyConfidence
        ? bodyAttack : bodyRelease;
    state.noteBodyConfidence += bodyAlpha
        * (bodyScore - state.noteBodyConfidence);
    state.noteBodyConfidence = clamp01(state.noteBodyConfidence);

    const auto setState = [&state](TrackingState next) noexcept
    {
        if (state.trackingState != next)
        {
            state.trackingState = next;
            state.stateAgeSamples = 0;
        }
    };

    bool bodyCounterAdvanced = false;
    if (state.noteBodyLatched)
    {
        if (bodyPresent)
        {
            state.stableBodyObservations = std::min(32,
                state.stableBodyObservations + 1);
            bodyCounterAdvanced = true;
            state.breathEvidenceSamples = std::max(0,
                state.breathEvidenceSamples - 2 * hopSamples);
            state.uncertainSamples = std::max(0,
                state.uncertainSamples - 2 * hopSamples);
        }
        else if (confirmedBreathFrame)
        {
            state.breathEvidenceSamples += hopSamples;
            state.uncertainSamples = std::max(0,
                state.uncertainSamples - hopSamples);
        }
        else if (confirmedAbsenceFrame || !validPitch)
        {
            state.uncertainSamples += hopSamples;
            state.breathEvidenceSamples = std::max(0,
                state.breathEvidenceSamples - hopSamples);
        }
        else
        {
            // A valid but weak/ambiguous F0 is not enough to keep the latch
            // forever. Accumulate absence slowly while giving the body sensors
            // time to recover from consonants and vibrato minima.
            state.uncertainSamples += std::max(1, hopSamples / 2);
            state.breathEvidenceSamples = std::max(0,
                state.breathEvidenceSamples - hopSamples);
        }
    }

    const int breathConfirmSamples = static_cast<int>(std::lround(
        sampleRate_ * (0.040 + 0.020 * static_cast<double>(humanize))));
    const int ambiguousReleaseSamples = static_cast<int>(std::lround(
        sampleRate_ * (0.160 + 0.080 * static_cast<double>(humanize))));
    const bool confirmedBreath = state.noteBodyLatched
        && state.breathEvidenceSamples >= breathConfirmSamples;
    const bool confirmedAbsence = state.noteBodyLatched
        && state.uncertainSamples >= ambiguousReleaseSamples;

    // Breath/absence is positive evidence and therefore wins even if a noisy
    // frame happens to yield a formally valid F0. This prevents breaths from
    // keeping the pitch engine latched through a spurious detector result.
    if (state.targetValid && (confirmedBreath || confirmedAbsence))
    {
        setState(TrackingState::release);
        state.desiredCents = 0.0;
        state.stableBodyObservations = 0;
        const double protection = static_cast<double>(
            clamp01(parameters.transientProtection));
        state.responseMs = std::clamp(32.0 - 20.0 * protection,
                                      8.0, 32.0);
        if (confirmedAbsence
            || state.breathEvidenceSamples > static_cast<int>(0.12 * sampleRate_))
        {
            state.pitchCentreValid = false;
        }
        return;
    }

    if (!validPitch)
    {
        ++state.invalidObservations;

        // Missing F0 is not missing voice. A latched note keeps the exact
        // destination while body evidence survives. Acquire/attack may also
        // settle to stable from body evidence alone after a prior valid lock.
        if (state.noteBodyLatched)
        {
            const int minimumStableSamples = static_cast<int>(std::lround(
                0.012 * sampleRate_));
            if (bodyPresent && state.targetValid
                && (state.trackingState == TrackingState::attack
                    || state.trackingState == TrackingState::acquire)
                && state.stableBodyObservations >= 4
                && state.stateAgeSamples >= minimumStableSamples)
            {
                setState(TrackingState::stable);
            }
            return;
        }

        if (state.targetValid && std::abs(state.currentCents) > 0.001)
        {
            setState(TrackingState::release);
            state.desiredCents = 0.0;
            state.responseMs = std::clamp(32.0 - 20.0
                * static_cast<double>(clamp01(parameters.transientProtection)),
                8.0, 32.0);
        }
        else
        {
            setState(TrackingState::unvoiced);
        }
        return;
    }

    state.invalidObservations = 0;

    // A strong breath/absence before any body latch must not become a note just
    // because the pitch tracker found a periodic accident in the noise.
    if (!state.noteBodyLatched && richEvidence
        && (confirmedBreathFrame || confirmedAbsenceFrame))
    {
        setState(TrackingState::unvoiced);
        state.desiredCents = 0.0;
        return;
    }

    if (bodyPresent || trackerBody > 0.58f)
    {
        if (!state.noteBodyLatched)
        {
            state.noteBodyLatched = true;
            state.stableBodyObservations = 1;
            state.noteBodyConfidence = std::max(state.noteBodyConfidence,
                                                bodyScore);
        }
        else if (!bodyCounterAdvanced)
        {
            state.stableBodyObservations = std::min(32,
                state.stableBodyObservations + 1);
        }
    }
    else if (!bodyCounterAdvanced)
    {
        state.stableBodyObservations = std::max(0,
            state.stableBodyObservations - 1);
    }

    // A tracker onset inside an already-latched note is usually consonant or
    // energy modulation, not a new note identity. Legato note changes are
    // represented by target identity and transition below.
    const bool musicalOnset = observation.onset
        && (!state.noteBodyLatched
            || state.trackingState == TrackingState::unvoiced
            || state.trackingState == TrackingState::release);
    if (musicalOnset)
    {
        setState(TrackingState::attack);
        state.stableObservations = 0;
        state.stableBodyObservations = bodyPresent ? 1 : 0;
    }
    else if (state.trackingState == TrackingState::unvoiced
             || state.trackingState == TrackingState::release)
    {
        setState(TrackingState::acquire);
        state.stableObservations = 0;
    }

    const double observedLog2 = safeLog2(observation.frequencyHz);
    if (!state.pitchCentreValid || musicalOnset)
    {
        state.pitchCentreLog2 = observedLog2;
        state.pitchCentreValid = true;
        state.stableObservations = 0;
    }
    else
    {
        const double distanceCents = std::abs(observedLog2 - state.pitchCentreLog2) * 1200.0;
        const double scaleStep = std::max(0.1,
            static_cast<double>(quantizer.minimumStepCents()));
        const double maximumWithinNoteTolerance = std::clamp(
            0.42 * scaleStep, 0.5, 60.0);
        const double withinNoteTolerance = std::min(
            22.0 + 38.0 * static_cast<double>(humanize),
            maximumWithinNoteTolerance);
        double baseAlpha = distanceCents > 95.0 ? 0.30 : 0.07;
        if (state.noteBodyLatched && distanceCents <= withinNoteTolerance)
            baseAlpha = 0.018 + 0.035 * static_cast<double>(1.0f - humanize);
        const double stableGate = 0.35
            + 0.65 * static_cast<double>(clamp01(observation.confidence)
                                      * clamp01(observation.periodicity));
        state.pitchCentreLog2 += baseAlpha * stableGate
            * (observedLog2 - state.pitchCentreLog2);
        ++state.stableObservations;
    }

    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);
    int pending = 0;
    double newTarget = quantizer.chooseTargetLog2(
        state.pitchCentreLog2,
        hysteresis,
        parameters.lockStrictness,
        observation.confidence,
        parameters.scaleLock && parameters.hardLockActive,
        musicalOnset,
        pending);
    newTarget += std::round(state.pitchCentreLog2 - newTarget);

    const bool targetChanged = !state.targetValid
        || std::abs(newTarget - state.targetLog2) * 1200.0 > 0.1;
    const double targetJump = state.targetValid
        ? (newTarget - state.targetLog2) * 1200.0 : 0.0;
    const double identityThreshold = std::clamp(
        0.18 * static_cast<double>(quantizer.minimumStepCents()), 0.5, 30.0);
    const bool targetIdentityChanged = state.targetValid
        && std::abs(targetJump) >= identityThreshold;
    if (targetChanged)
    {
        ++state.revision;
        state.lastTargetJumpCents = targetJump;
        if (targetIdentityChanged && state.trackingState != TrackingState::transition)
        {
            setState(TrackingState::transition);
            state.stableObservations = 0;
            state.stableBodyObservations = bodyPresent ? 1 : 0;
        }
    }
    state.targetLog2 = newTarget;
    state.targetValid = true;

    // The period model follows musical identity, not vibrato-rate detector
    // motion. A real target identity change is acquired while period guidance
    // is frozen; within a stable note the central period moves on a long time
    // constant so vibrato cannot become delay modulation.
    if (state.noteBodyLatched && bodyPresent)
    {
        const double observedHz = static_cast<double>(observation.frequencyHz);
        if (!(state.transportPeriodHz > 0.0)
            || !std::isfinite(state.transportPeriodHz)
            || musicalOnset || targetIdentityChanged)
        {
            state.transportPeriodHz = observedHz;
        }
        else
        {
            const double periodTauSeconds = 0.28
                + 0.55 * static_cast<double>(humanize);
            const double alpha = std::clamp(
                1.0 - std::exp(-hopSeconds / periodTauSeconds),
                0.0002, 0.05);
            const double currentLog = safeLog2(state.transportPeriodHz);
            state.transportPeriodHz = std::exp2(currentLog + alpha
                * (safeLog2(observedHz) - currentLog));
        }
    }

    const double vibratoComponent = observedLog2 - state.pitchCentreLog2;
    const float stable = clamp01(0.45f * observation.confidence
                               + 0.35f * observation.consensus
                               + 0.20f * std::min(1.0f,
                                   static_cast<float>(state.stableObservations) / 5.0f));
    const float periodic = clamp01(observation.periodicity);
    const double halfStep = 0.5 * static_cast<double>(quantizer.minimumStepCents());
    const double centreError = std::abs((state.targetLog2 - state.pitchCentreLog2) * 1200.0);
    const float boundarySafety = 1.0f - smoothStep(
        static_cast<float>(0.58 * halfStep),
        static_cast<float>(0.92 * halfStep),
        static_cast<float>(centreError));

    float preserve = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve + 0.35f * humanize)
        : clamp01(parameters.preserveVibrato);
    preserve *= stable * periodic * boundarySafety;

    const double correctedLog2 = state.targetLog2
        + static_cast<double>(preserve) * vibratoComponent;
    double errorCents = wrapToNearestOctave(
        (correctedLog2 - observedLog2) * 1200.0);

    const double humanWindow = parameters.scaleLock
        ? 2.0 + 10.0 * static_cast<double>(humanize)
        : 1.5 + 16.0 * static_cast<double>(humanize);
    if (std::abs(errorCents) <= humanWindow)
        errorCents = 0.0;
    else
        errorCents = std::copysign(std::abs(errorCents) - humanWindow, errorCents);

    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    errorCents = std::clamp(errorCents, -maximumCents, maximumCents);

    // Sensors determine how carefully identity is interpreted, never how much
    // of the requested correction is applied.
    state.desiredCents = errorCents
        * static_cast<double>(clamp01(parameters.amount));
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);

    if (!musicalOnset)
    {
        const int minimumStableSamples = static_cast<int>(std::lround(
            0.012 * sampleRate_));
        if (state.trackingState == TrackingState::transition)
        {
            if (!targetIdentityChanged && bodyPresent
                && state.stableBodyObservations >= 4
                && state.stateAgeSamples >= minimumStableSamples)
            {
                setState(TrackingState::stable);
            }
        }
        else if (state.trackingState == TrackingState::attack
                 || state.trackingState == TrackingState::acquire)
        {
            if (state.noteBodyLatched && bodyPresent
                && state.stableBodyObservations >= 4
                && state.stateAgeSamples >= minimumStableSamples)
            {
                setState(TrackingState::stable);
            }
        }
    }

    meterPendingOctave_.store(pending, std::memory_order_relaxed);
    meterOctaveState_.store(observation.octaveState, std::memory_order_relaxed);
}

'''
cpp = replace_between(
    cpp,
    "void ModernPitchEngine::updateCorrectionState(\n",
    "double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept\n",
    new_update,
    "musical note-body supervisor",
)

cpp = replace_once(
    cpp,
    """    const float modelSupport = std::max(periodicity,\n        0.58f * analysedBody + 0.42f * analysedHarmonicity);\n    const float formantStrength = clamp01(parameters.formantPreservation)\n        * (0.55f + 0.45f * clamp01(modelSupport));\n""",
    """    // The sensors decide whether a new PARCOR model is trustworthy; they\n    // do not turn the user's formant control down. Holding the last stable\n    // envelope is the cautious behaviour.\n    const float formantStrength = clamp01(parameters.formantPreservation);\n""",
    "formant authority",
)

cpp_path.write_text(cpp)


live_path = Path("Source/LivePitchProcessor.h")
live = live_path.read_text()

live = replace_once(
    live,
    """        voiceEvidenceAnalyzer_.prepare(sampleRate_, maximumBlockSize_, channelCount_);\n        activeModeIndex_.store(toModeIndex(latencyMode),\n""",
    """        voiceEvidenceAnalyzer_.prepare(sampleRate_, maximumBlockSize_, channelCount_);\n        voiceEvidencePrimed_ = false;\n        activeModeIndex_.store(toModeIndex(latencyMode),\n""",
    "evidence prepare state",
)
live = replace_once(
    live,
    """        voiceEvidenceAnalyzer_.reset();\n    }\n""",
    """        voiceEvidenceAnalyzer_.reset();\n        voiceEvidencePrimed_ = false;\n    }\n""",
    "evidence reset state",
)

live = replace_once(
    live,
    """        const auto evidence = analyseEvidence(buffer);\n        const auto conditioned = conditionedParameters(evidence);\n\n        activeModernEngine().process(buffer,\n""",
    """        // Use only evidence published before this host block. The analyzer\n        // may inspect the current input to prepare the next block, but it cannot\n        // use future samples from this block to classify its first sample.\n        const auto evidence = voiceEvidenceAnalyzer_.getLatest();\n        const bool evidenceValid = voiceEvidencePrimed_;\n        static_cast<void>(analyseEvidence(buffer));\n        voiceEvidencePrimed_ = true;\n        const auto conditioned = conditionedParameters(evidence, evidenceValid);\n\n        activeModernEngine().process(buffer,\n""",
    "buffer causal evidence",
)
live = replace_once(
    live,
    """        const auto evidence = analyseEvidence(view);\n        const auto conditioned = conditionedParameters(evidence);\n\n        activeModernEngine().process(view,\n""",
    """        const auto evidence = voiceEvidenceAnalyzer_.getLatest();\n        const bool evidenceValid = voiceEvidencePrimed_;\n        static_cast<void>(analyseEvidence(view));\n        voiceEvidencePrimed_ = true;\n        const auto conditioned = conditionedParameters(evidence, evidenceValid);\n\n        activeModernEngine().process(view,\n""",
    "mono causal evidence",
)
live = replace_once(
    live,
    """        static_cast<void>(analyseEvidence(buffer));\n        activeModernEngine().processBypassed(buffer);\n""",
    """        static_cast<void>(analyseEvidence(buffer));\n        voiceEvidencePrimed_ = true;\n        activeModernEngine().processBypassed(buffer);\n""",
    "bypass evidence priming",
)

new_conditioning = r'''    [[nodiscard]] ModernPitchEngine::Parameters conditionedParameters(
        const VoiceEvidence& evidence,
        bool evidenceValid) const noexcept
    {
        ModernPitchEngine::Parameters conditioned = parameters_;

        // Sensor output is supervision data only. No user-authoritative audio
        // or correction parameter is multiplied by confidence, breathiness or
        // state evidence here. In particular Amount, Formant, Transient,
        // Vibrato and Breath Reduction keep the exact values selected in the UI.
        conditioned.voiceEvidenceValid = evidenceValid;
        conditioned.voiceHarmonicity = std::clamp(evidence.harmonicity, 0.0f, 1.0f);
        conditioned.voiceBreathiness = std::clamp(evidence.breathiness, 0.0f, 1.0f);
        conditioned.voiceBodyEnergy = std::clamp(evidence.voicedBodyEnergy, 0.0f, 1.0f);
        conditioned.voiceSpectralReliability = std::clamp(
            evidence.spectralReliability, 0.0f, 1.0f);
        conditioned.voiceEventStrength = std::clamp(evidence.eventStrength, 0.0f, 1.0f);
        conditioned.voiceFormantStability = std::clamp(
            evidence.formantStability, 0.0f, 1.0f);
        return conditioned;
    }

'''
live = replace_between(
    live,
    "    [[nodiscard]] ModernPitchEngine::Parameters conditionedParameters(\n",
    "    [[nodiscard]] static float calculateMinScaleStepCents(\n",
    new_conditioning,
    "sensor-only conditioning",
)

live = replace_once(
    live,
    """    VoiceEvidenceAnalyzer voiceEvidenceAnalyzer_;\n    double sampleRate_ = 0.0;\n""",
    """    VoiceEvidenceAnalyzer voiceEvidenceAnalyzer_;\n    bool voiceEvidencePrimed_ = false;\n    double sampleRate_ = 0.0;\n""",
    "evidence primed member",
)
live_path.write_text(live)


test_path = Path("Tests/SupervisorContinuityTest.cpp")
test_path.write_text(r'''#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

ModernPitchEngine::PitchObservation strongPitch(float frequency = 220.0f)
{
    ModernPitchEngine::PitchObservation observation;
    observation.valid = true;
    observation.frequencyHz = frequency;
    observation.confidence = 0.95f;
    observation.periodicity = 0.95f;
    observation.consensus = 0.88f;
    observation.voicing = 0.95f;
    return observation;
}

void setBodyEvidence(ModernPitchEngine::Parameters& parameters)
{
    parameters.voiceEvidenceValid = true;
    parameters.voiceBodyEnergy = 0.92f;
    parameters.voiceHarmonicity = 0.90f;
    parameters.voiceSpectralReliability = 0.88f;
    parameters.voiceBreathiness = 0.04f;
    parameters.voiceEventStrength = 0.0f;
}

void setBreathEvidence(ModernPitchEngine::Parameters& parameters)
{
    parameters.voiceEvidenceValid = true;
    parameters.voiceBodyEnergy = 0.08f;
    parameters.voiceHarmonicity = 0.10f;
    parameters.voiceSpectralReliability = 0.18f;
    parameters.voiceBreathiness = 0.92f;
    parameters.voiceEventStrength = 0.0f;
}
}

int main()
{
    bool success = true;

    auto tracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    tracker->prepare(48000.0);
    auto makeInitialDecision = []
    {
        ModernPitchEngine::MultiRatePitchTracker::DecoderDecision d;
        d.valid = true;
        d.candidate.valid = true;
        d.candidate.frequencyHz = 440.0f;
        d.candidate.confidence = 0.86f;
        d.candidate.periodicity = 0.90f;
        d.consensus = 0.72f;
        d.supportCount = 2;
        d.directSupportCount = 1;
        d.freshSupportMask = 1;
        return d;
    };
    auto first = makeInitialDecision();
    const bool firstAccepted = tracker->confirmOctaveTransition(first, true);
    auto second = makeInitialDecision();
    const bool secondAccepted = tracker->confirmOctaveTransition(second, true);
    success &= check(!firstAccepted && !first.valid,
                     "initial_register_waits_for_repeat");
    success &= check(secondAccepted,
                     "repeated_initial_register_is_committed");

    auto engine = std::make_unique<ModernPitchEngine>();
    engine->prepare(48000.0, 256, 1, ModernPitchEngine::LatencyMode::live);
    ModernPitchEngine::ScaleQuantizer quantizer;
    quantizer.reset();
    ModernPitchEngine::Parameters parameters;
    parameters.transientProtection = 1.0f;
    parameters.humanize = 0.65f;
    setBodyEvidence(parameters);

    ModernPitchEngine::CorrectionState dropoutState;
    dropoutState.targetValid = true;
    dropoutState.desiredCents = 100.0;
    dropoutState.currentCents = 100.0;
    dropoutState.trackingState = ModernPitchEngine::TrackingState::stable;
    dropoutState.noteBodyLatched = true;
    dropoutState.noteBodyConfidence = 0.9f;
    dropoutState.transportPeriodHz = 220.0;
    ModernPitchEngine::PitchObservation invalid;

    // More than 200 ms without F0 is still the same sung note when the body
    // sensors remain positive. Correction authority and period identity hold.
    for (int i = 0; i < 180; ++i)
    {
        engine->updateCorrectionState(dropoutState, quantizer, invalid, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(dropoutState));
    }
    success &= check(dropoutState.trackingState == ModernPitchEngine::TrackingState::stable
                     && dropoutState.noteBodyLatched
                     && std::abs(dropoutState.desiredCents - 100.0) < 1.0e-9,
                     "long_voiced_note_survives_pitch_dropouts");
    success &= check(std::abs(dropoutState.transportPeriodHz - 220.0) < 1.0e-9,
                     "pitch_dropout_keeps_latched_transport_period");

    // Acquire is a musical state, not an F0-validity state. Once a target has
    // been acquired, sustained body evidence can settle it even through a hole.
    ModernPitchEngine::CorrectionState acquireState = dropoutState;
    acquireState.trackingState = ModernPitchEngine::TrackingState::acquire;
    acquireState.stateAgeSamples = 0;
    acquireState.stableBodyObservations = 0;
    for (int i = 0; i < 20; ++i)
    {
        engine->updateCorrectionState(acquireState, quantizer, invalid, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(acquireState));
    }
    success &= check(acquireState.trackingState == ModernPitchEngine::TrackingState::stable,
                     "latched_body_does_not_stick_in_acquire_during_f0_hole");

    // Positive breath evidence releases the note even if the tracker happens to
    // produce a strong spurious F0 on the noise.
    ModernPitchEngine::CorrectionState spuriousBreath = dropoutState;
    setBreathEvidence(parameters);
    const auto falsePitchOnBreath = strongPitch(231.0f);
    bool validBreathReleased = false;
    for (int i = 0; i < 100; ++i)
    {
        engine->updateCorrectionState(spuriousBreath, quantizer,
                                      falsePitchOnBreath, parameters);
        if (spuriousBreath.trackingState == ModernPitchEngine::TrackingState::release)
        {
            validBreathReleased = true;
            break;
        }
    }
    success &= check(validBreathReleased
                     && std::abs(spuriousBreath.desiredCents) < 1.0e-9,
                     "breath_wins_over_spurious_valid_f0");

    // The same must hold when the pitch detector correctly reports no F0.
    ModernPitchEngine::CorrectionState releaseState = dropoutState;
    bool invalidBreathReleased = false;
    for (int i = 0; i < 100; ++i)
    {
        engine->updateCorrectionState(releaseState, quantizer, invalid, parameters);
        if (releaseState.trackingState == ModernPitchEngine::TrackingState::release)
        {
            invalidBreathReleased = true;
            break;
        }
    }
    success &= check(invalidBreathReleased
                     && std::abs(releaseState.desiredCents) < 1.0e-9,
                     "confirmed_breath_releases_missing_f0_note");

    double maximumReleaseStep = 0.0;
    double previous = releaseState.currentCents;
    for (int i = 0; i < 4800; ++i)
    {
        const double current = engine->advanceCorrection(releaseState);
        maximumReleaseStep = std::max(maximumReleaseStep, std::abs(current - previous));
        previous = current;
    }
    std::cerr << "maximum_release_step_cents=" << maximumReleaseStep << '\n';
    success &= check(maximumReleaseStep < 1.0,
                     "unvoiced_release_has_no_correction_jump");
    success &= check(std::abs(releaseState.currentCents) < 0.05
                     && releaseState.trackingState == ModernPitchEngine::TrackingState::unvoiced
                     && !releaseState.noteBodyLatched,
                     "unvoiced_release_reaches_unity_and_clears_note_latch");

    parameters.retuneTimeMs = 0.0f;
    parameters.transitionTimeMs = 40.0f;
    const double transitionResponse = engine->responseTimeMs(parameters, true, 100.0);
    std::cerr << "single_path_transition_response_ms=" << transitionResponse << '\n';
    success &= check(transitionResponse > 8.0 && transitionResponse < 32.1,
                     "target_revision_uses_bounded_single_path_transition");

    ModernPitchEngine::CorrectionState boundedTransition;
    boundedTransition.targetValid = true;
    boundedTransition.noteBodyLatched = true;
    boundedTransition.noteBodyConfidence = 0.95f;
    boundedTransition.trackingState = ModernPitchEngine::TrackingState::transition;
    boundedTransition.desiredCents = 420.0;
    boundedTransition.currentCents = 0.0;
    boundedTransition.responseMs = 500.0;
    for (int i = 0; i < 5900; ++i)
        static_cast<void>(engine->advanceCorrection(boundedTransition));
    std::cerr << "bounded_transition_velocity="
              << boundedTransition.velocityCentsPerSecond << '\n';
    success &= check(boundedTransition.trackingState == ModernPitchEngine::TrackingState::stable,
                     "transition_has_hard_musical_time_bound");
    success &= check(std::abs(boundedTransition.velocityCentsPerSecond) > 0.02,
                     "stable_state_does_not_require_zero_controller_velocity");

    setBodyEvidence(parameters);
    const auto syncObservation = strongPitch();
    ModernPitchEngine::CorrectionState syncState;
    syncState.targetValid = true;
    syncState.noteBodyLatched = true;
    syncState.noteBodyConfidence = 0.9f;
    syncState.transportPeriodHz = 220.0;
    syncState.trackingState = ModernPitchEngine::TrackingState::transition;
    const float transitionSync = engine->transportSyncStrength(syncObservation, syncState, parameters);
    syncState.trackingState = ModernPitchEngine::TrackingState::stable;
    const float stableSync = engine->transportSyncStrength(syncObservation, syncState, parameters);
    std::cerr << "transition_period_sync=" << transitionSync << '\n';
    std::cerr << "stable_period_sync=" << stableSync << '\n';
    success &= check(transitionSync < 0.0f && stableSync > 0.25f,
                     "nonstable_note_holds_period_guidance_instead_of_ramping_it");

    ModernPitchEngine::TransportClock guidanceClock;
    guidanceClock.prepare(48000.0, 256);
    const double ratio = std::exp2(180.0 / 1200.0);
    const double period = 48000.0 / 220.0;
    for (int i = 0; i < 6000; ++i)
        static_cast<void>(guidanceClock.next(ratio, period, 1.0f));
    const double heldNudge = guidanceClock.periodNudgeSamples_;
    const float heldAmount = guidanceClock.periodSyncAmount_;
    for (int i = 0; i < 2400; ++i)
        static_cast<void>(guidanceClock.next(ratio, period * 0.75, -1.0f));
    success &= check(std::abs(guidanceClock.periodNudgeSamples_ - heldNudge) < 1.0e-12
                     && std::abs(guidanceClock.periodSyncAmount_ - heldAmount) < 1.0e-7f,
                     "nonstable_state_cannot_move_period_guidance_memory");
    const auto unityHeld = guidanceClock.next(1.0, period, -1.0f);
    success &= check(std::abs(unityHeld.delayA - 256.0) < 1.0e-9
                     && std::abs(unityHeld.delayB - 256.0) < 1.0e-9,
                     "held_period_guidance_still_collapses_at_unity");

    // A long vibrato around one quantized note is stable musical content, not
    // an endless note transition.
    std::array<double, 12> chromatic {};
    for (int degree = 0; degree < 12; ++degree)
        chromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    ModernPitchEngine::ScaleQuantizer vibratoQuantizer;
    vibratoQuantizer.reset();
    vibratoQuantizer.setScale(chromatic.data(), static_cast<int>(chromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState vibratoState;
    ModernPitchEngine::Parameters vibratoParameters = parameters;
    setBodyEvidence(vibratoParameters);
    vibratoParameters.humanize = 0.75f;
    bool leftMusicalBody = false;
    for (int hop = 0; hop < 1800; ++hop)
    {
        const double vibratoCents = 34.0 * std::sin(2.0 * 3.14159265358979323846
            * static_cast<double>(hop) / 150.0);
        auto vibratoObservation = strongPitch(static_cast<float>(440.0
            * std::exp2(vibratoCents / 1200.0)));
        engine->updateCorrectionState(vibratoState, vibratoQuantizer,
                                      vibratoObservation, vibratoParameters);
        for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
            static_cast<void>(engine->advanceCorrection(vibratoState));
        if (hop > 100
            && (vibratoState.trackingState == ModernPitchEngine::TrackingState::unvoiced
                || vibratoState.trackingState == ModernPitchEngine::TrackingState::release))
        {
            leftMusicalBody = true;
        }
    }
    success &= check(!leftMusicalBody
                     && vibratoState.noteBodyLatched
                     && vibratoState.trackingState == ModernPitchEngine::TrackingState::stable,
                     "long_vibrato_is_classified_as_stable_note_body");

    // Humanize must not create a 12-TET-sized note-body tolerance on dense
    // microtonal material. With zero lock hysteresis a sustained 15-cent move in
    // 48-EDO must eventually be able to cross the 12.5-cent degree boundary.
    std::array<double, 48> denseScale {};
    for (int degree = 0; degree < 48; ++degree)
        denseScale[static_cast<std::size_t>(degree)] = std::exp2(degree / 48.0);
    ModernPitchEngine::ScaleQuantizer denseQuantizer;
    denseQuantizer.reset();
    denseQuantizer.setScale(denseScale.data(), static_cast<int>(denseScale.size()), 440.0);
    ModernPitchEngine::CorrectionState denseState;
    ModernPitchEngine::Parameters denseParameters = vibratoParameters;
    denseParameters.scaleLock = true;
    denseParameters.hardLockActive = false;
    denseParameters.lockHysteresis = 0.0f;
    denseParameters.lockStrictness = 0.0f;
    denseParameters.humanize = 1.0f;
    auto denseObservation = strongPitch(440.0f);
    for (int hop = 0; hop < 12; ++hop)
    {
        engine->updateCorrectionState(denseState, denseQuantizer,
                                      denseObservation, denseParameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(denseState));
    }
    const double initialDenseTarget = denseState.targetLog2;
    denseObservation.frequencyHz = static_cast<float>(440.0 * std::exp2(15.0 / 1200.0));
    for (int hop = 0; hop < 36; ++hop)
    {
        engine->updateCorrectionState(denseState, denseQuantizer,
                                      denseObservation, denseParameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(denseState));
    }
    const double denseTargetMove = std::abs(
        (denseState.targetLog2 - initialDenseTarget) * 1200.0);
    std::cerr << "dense_scale_target_move_cents=" << denseTargetMove << '\n';
    success &= check(denseTargetMove > 20.0,
                     "humanize_respects_dense_microtonal_degree_spacing");

    // Native API semantics: one semitone means 100 cents, with no adapter hack.
    const double unison = 1.0;
    quantizer.setScale(&unison, 1, 440.0);
    ModernPitchEngine::CorrectionState capState;
    auto voiced = strongPitch(static_cast<float>(440.0 * std::exp2(2.0 / 12.0)));
    parameters.maximumCorrectionSemitones = 1.0f;
    parameters.amount = 1.0f;
    parameters.humanize = 0.0f;
    parameters.preserveVibrato = 0.0f;
    setBodyEvidence(parameters);
    engine->updateCorrectionState(capState, quantizer, voiced, parameters);
    std::cerr << "one_semitone_cap_cents=" << capState.desiredCents << '\n';
    success &= check(std::abs(capState.desiredCents) <= 100.001,
                     "native_semitone_limit_is_100_cents");
    success &= check(std::abs(capState.desiredCents) > 95.0,
                     "native_semitone_limit_is_not_divided_by_twelve");

    // PARCOR envelope memory should not be replaced by a transient/noisy frame.
    setBodyEvidence(parameters);
    juce::AudioBuffer<float> block(1, 1024);
    for (int i = 0; i < block.getNumSamples(); ++i)
    {
        const double phase = 2.0 * 3.14159265358979323846 * 220.0
                           * static_cast<double>(i) / 48000.0;
        block.setSample(0, i, static_cast<float>(0.7 * std::sin(phase)
                                               + 0.2 * std::sin(2.0 * phase)));
    }
    engine->linkedCorrection_.trackingState = ModernPitchEngine::TrackingState::stable;
    auto stableObservation = strongPitch();
    stableObservation.onsetStrength = 0.0f;
    parameters.formantPreservation = 1.0f;
    engine->updateLpcTarget(block, 1, block.getNumSamples(), parameters,
                            stableObservation);
    const auto stableReflection = engine->currentReflectionTarget_;

    block.clear();
    block.setSample(0, 0, 1.0f);
    engine->linkedCorrection_.trackingState = ModernPitchEngine::TrackingState::attack;
    auto transientObservation = stableObservation;
    transientObservation.onsetStrength = 1.0f;
    engine->updateLpcTarget(block, 1, block.getNumSamples(), parameters,
                            transientObservation);
    double reflectionDifference = 0.0;
    for (std::size_t i = 0; i < stableReflection.size(); ++i)
        reflectionDifference += std::abs(static_cast<double>(stableReflection[i]
                                      - engine->currentReflectionTarget_[i]));
    std::cerr << "transient_reflection_target_difference="
              << reflectionDifference << '\n';
    success &= check(reflectionDifference < 1.0e-9,
                     "transient_freezes_last_trustworthy_parcor_envelope");

    return success ? 0 : 1;
}
''')

print("Applied robust musical note-body supervisor v2")
