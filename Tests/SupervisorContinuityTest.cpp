#include <JuceHeader.h>

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


    auto rescueTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    rescueTracker->prepare(48000.0);
    rescueTracker->trackedPitchHz_ = 220.0f;
    rescueTracker->trackedConfidence_ = 0.88f;
    rescueTracker->trackedPeriodicity_ = 0.90f;
    rescueTracker->trackedConsensus_ = 0.75f;
    rescueTracker->trackedSupportCount_ = 2;
    auto& rescueSlot = rescueTracker->halfRateCandidate_;
    rescueSlot.candidate.valid = true;
    rescueSlot.candidate.frequencyHz = static_cast<float>(220.0 * std::exp2(200.0 / 1200.0));
    rescueSlot.candidate.confidence = 0.74f;
    rescueSlot.candidate.periodicity = 0.82f;
    rescueSlot.candidate.pathIndex = 1;
    rescueSlot.candidate.ageInHops = 0;
    rescueSlot.ageInHops = 0;

    rescueTracker->setRescueMode(false);
    auto normalSingleFamily = rescueTracker->decodeCandidate(false);
    success &= check(!normalSingleFamily.valid,
                     "single_family_does_not_override_normal_tracking");

    rescueTracker->decoderBeam_.fill({});
    rescueTracker->setRescueMode(true);
    auto rescuedSingleFamily = rescueTracker->decodeCandidate(false);
    success &= check(rescuedSingleFamily.valid
                     && std::abs(1200.0 * std::log2(
                         rescuedSingleFamily.candidate.frequencyHz / 220.0f)) > 140.0,
                     "stale_f0_accepts_credible_single_family_rescue");

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

    // More than 70 ms without F0 is still the same sung note, but it is no
    // longer allowed to report stable pitch. Body and correction hold while
    // the detector returns to acquire/search.
    for (int i = 0; i < 180; ++i)
    {
        engine->updateCorrectionState(dropoutState, quantizer, invalid, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(dropoutState));
    }
    success &= check(dropoutState.trackingState == ModernPitchEngine::TrackingState::acquire
                     && dropoutState.noteBodyLatched
                     && dropoutState.pitchStaleSamples > 0
                     && std::abs(dropoutState.desiredCents - 100.0) < 1.0e-9,
                     "stale_pitch_reacquires_without_reducing_correction");
    success &= check(std::abs(dropoutState.transportPeriodHz - 220.0) < 1.0e-9,
                     "pitch_dropout_keeps_latched_transport_period");


    // Reproduces the real failure mode: normal-level sung body survives while
    // the primary F0 is missing, then a recovered F0 has moved musically. The
    // correction must stop using the stale anchor and return to stable tracking.
    ModernPitchEngine::ScaleQuantizer recoveryQuantizer;
    recoveryQuantizer.reset();
    const double recoveryUnison = 1.0;
    recoveryQuantizer.setScale(&recoveryUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState recoveryState;
    auto recoveryObservation = strongPitch(220.0f);
    for (int hop = 0; hop < 16; ++hop)
    {
        engine->updateCorrectionState(recoveryState, recoveryQuantizer,
                                      recoveryObservation, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(recoveryState));
    }
    for (int hop = 0; hop < 130; ++hop)
    {
        engine->updateCorrectionState(recoveryState, recoveryQuantizer,
                                      invalid, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(recoveryState));
    }
    const double staleCentre = recoveryState.pitchCentreLog2;
    const double staleDesired = recoveryState.desiredCents;
    recoveryObservation = strongPitch(static_cast<float>(220.0
        * std::exp2(200.0 / 1200.0)));
    for (int hop = 0; hop < 24; ++hop)
    {
        engine->updateCorrectionState(recoveryState, recoveryQuantizer,
                                      recoveryObservation, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(recoveryState));
    }
    const double recoveredCentreMove = std::abs(
        (recoveryState.pitchCentreLog2 - staleCentre) * 1200.0);
    std::cerr << "recovered_pitch_centre_move_cents=" << recoveredCentreMove << '\n';
    success &= check(recoveryState.pitchStaleSamples == 0
                     && recoveryState.noteBodyLatched
                     && recoveryState.trackingState != ModernPitchEngine::TrackingState::acquire,
                     "rescued_f0_refreshes_anchor_and_exits_acquire");
    success &= check(recoveredCentreMove > 80.0
                     && std::abs(recoveryState.desiredCents - staleDesired) > 40.0,
                     "rescued_f0_retargets_instead_of_freezing_old_correction");

    // Acquire persists while pitch is stale; positive body evidence must not
    // falsely promote a five-second detector hole back to stable.
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
    success &= check(acquireState.trackingState == ModernPitchEngine::TrackingState::acquire
                     && acquireState.noteBodyLatched
                     && std::abs(acquireState.desiredCents - 100.0) < 1.0e-9,
                     "stale_f0_cannot_masquerade_as_stable_note");

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
    bool vibratoTargetCaptured = false;
    bool vibratoChangedTargetIdentity = false;
    double vibratoTargetReference = 0.0;
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
        if (hop > 100 && vibratoState.targetValid)
        {
            if (!vibratoTargetCaptured)
            {
                vibratoTargetReference = vibratoState.targetLog2;
                vibratoTargetCaptured = true;
            }
            else if (std::abs(vibratoState.targetLog2 - vibratoTargetReference) * 1200.0 > 0.5)
            {
                vibratoChangedTargetIdentity = true;
            }
        }
    }
    success &= check(!leftMusicalBody
                     && vibratoState.noteBodyLatched
                     && vibratoState.trackingState == ModernPitchEngine::TrackingState::stable,
                     "long_vibrato_is_classified_as_stable_note_body");
    success &= check(vibratoTargetCaptured && !vibratoChangedTargetIdentity,
                     "vibrato_does_not_become_a_note_identity_change");

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

    return success ? 0 : 1;
}
