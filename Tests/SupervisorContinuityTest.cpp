#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

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
}

int main()
{
    bool success = true;

    // A non-exceptional initial register decision must repeat once before it
    // can drive correction. This targets the audible first-note octave alias.
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

    ModernPitchEngine::CorrectionState releaseState;
    releaseState.targetValid = true;
    releaseState.desiredCents = 100.0;
    releaseState.currentCents = 100.0;
    releaseState.trackingState = ModernPitchEngine::TrackingState::stable;
    ModernPitchEngine::PitchObservation invalid;
    for (int i = 0; i < 3; ++i)
        engine->updateCorrectionState(releaseState, quantizer, invalid, parameters);
    success &= check(std::abs(releaseState.desiredCents - 100.0) < 1.0e-9,
                     "short_detector_holes_hold_musical_decision");
    engine->updateCorrectionState(releaseState, quantizer, invalid, parameters);
    success &= check(releaseState.trackingState == ModernPitchEngine::TrackingState::release
                     && std::abs(releaseState.desiredCents) < 1.0e-9,
                     "confirmed_unvoiced_releases_same_trajectory_to_unity");

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
    success &= check(std::abs(releaseState.currentCents) < 0.05,
                     "unvoiced_release_reaches_unity");

    parameters.retuneTimeMs = 0.0f;
    parameters.transitionTimeMs = 40.0f;
    const double transitionResponse = engine->responseTimeMs(parameters, true, 100.0);
    std::cerr << "single_path_transition_response_ms=" << transitionResponse << '\n';
    success &= check(transitionResponse > 8.0 && transitionResponse < 32.1,
                     "target_revision_uses_bounded_single_path_transition");

    // Native API semantics: one semitone means 100 cents, with no adapter hack.
    const double unison = 1.0;
    quantizer.setScale(&unison, 1, 440.0);
    ModernPitchEngine::CorrectionState capState;
    ModernPitchEngine::PitchObservation voiced;
    voiced.valid = true;
    voiced.frequencyHz = static_cast<float>(440.0 * std::exp2(2.0 / 12.0));
    voiced.confidence = 1.0f;
    voiced.periodicity = 1.0f;
    voiced.consensus = 1.0f;
    voiced.voicing = 1.0f;
    parameters.maximumCorrectionSemitones = 1.0f;
    parameters.amount = 1.0f;
    parameters.humanize = 0.0f;
    parameters.preserveVibrato = 0.0f;
    engine->updateCorrectionState(capState, quantizer, voiced, parameters);
    std::cerr << "one_semitone_cap_cents=" << capState.desiredCents << '\n';
    success &= check(std::abs(capState.desiredCents) <= 100.001,
                     "native_semitone_limit_is_100_cents");
    success &= check(std::abs(capState.desiredCents) > 95.0,
                     "native_semitone_limit_is_not_divided_by_twelve");

    // PARCOR envelope memory should not be replaced by a transient/noisy frame.
    juce::AudioBuffer<float> block(1, 1024);
    for (int i = 0; i < block.getNumSamples(); ++i)
    {
        const double phase = 2.0 * 3.14159265358979323846 * 220.0
                           * static_cast<double>(i) / 48000.0;
        block.setSample(0, i, static_cast<float>(0.7 * std::sin(phase)
                                               + 0.2 * std::sin(2.0 * phase)));
    }
    engine->linkedCorrection_.trackingState = ModernPitchEngine::TrackingState::stable;
    ModernPitchEngine::PitchObservation stableObservation;
    stableObservation.valid = true;
    stableObservation.frequencyHz = 220.0f;
    stableObservation.periodicity = 0.95f;
    stableObservation.onsetStrength = 0.0f;
    parameters.formantPreservation = 1.0f;
    engine->updateLpcTarget(block, 1, block.getNumSamples(), parameters,
                            stableObservation);
    const auto stableReflection = engine->currentReflectionTarget_;

    block.clear();
    block.setSample(0, 0, 1.0f);
    engine->linkedCorrection_.trackingState = ModernPitchEngine::TrackingState::attack;
    ModernPitchEngine::PitchObservation transientObservation = stableObservation;
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
