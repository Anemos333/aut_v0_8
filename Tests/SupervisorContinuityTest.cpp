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


    auto delayedRescueTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    delayedRescueTracker->prepare(48000.0);
    delayedRescueTracker->trackedPitchHz_ = 220.0f;
    delayedRescueTracker->trackedConfidence_ = 0.88f;
    delayedRescueTracker->trackedPeriodicity_ = 0.90f;
    delayedRescueTracker->trackedConsensus_ = 0.75f;
    delayedRescueTracker->trackedSupportCount_ = 2;
    delayedRescueTracker->setReacquisitionAnchor(220.0f);
    ModernPitchEngine::PitchObservation expiredObservation;
    const int dropoutSamples = static_cast<int>(0.075 * 48000.0);
    for (int sample = 0; sample < dropoutSamples; ++sample)
        static_cast<void>(delayedRescueTracker->processSample(0.0f, expiredObservation));
    success &= check(delayedRescueTracker->trackedPitchHz_ == 0.0f
                     && std::abs(delayedRescueTracker->reacquisitionAnchorHz_ - 220.0f) < 0.01f,
                     "current_f0_can_expire_without_erasing_note_body_anchor");

    auto& delayedSlot = delayedRescueTracker->halfRateCandidate_;
    delayedSlot.candidate.valid = true;
    delayedSlot.candidate.frequencyHz = static_cast<float>(220.0 * std::exp2(200.0 / 1200.0));
    delayedSlot.candidate.confidence = 0.74f;
    delayedSlot.candidate.periodicity = 0.82f;
    delayedSlot.candidate.pathIndex = 1;
    delayedSlot.candidate.ageInHops = 0;
    delayedSlot.ageInHops = 0;
    delayedRescueTracker->decoderBeam_.fill({});
    delayedRescueTracker->setRescueMode(false);
    const auto delayedNormalDecision = delayedRescueTracker->decodeCandidate(false);
    success &= check(!delayedNormalDecision.valid,
                     "expired_tracker_anchor_does_not_weaken_normal_tracking");

    delayedRescueTracker->decoderBeam_.fill({});
    delayedRescueTracker->setRescueMode(true);
    const auto delayedRescueDecision = delayedRescueTracker->decodeCandidate(false);
    success &= check(delayedRescueDecision.valid
                     && delayedRescueTracker->trackedPitchHz_ == 0.0f,
                     "rescue_uses_persistent_anchor_after_sixty_ms_detector_hole");
    delayedRescueTracker->clearReacquisitionAnchor();
    delayedRescueTracker->decoderBeam_.fill({});
    const auto noBodyAnchorDecision = delayedRescueTracker->decodeCandidate(false);
    success &= check(!noBodyAnchorDecision.valid,
                     "released_note_body_removes_rescue_authority");


    // A strong low-period alias must never be allowed to restart the register
    // just because trackedPitchHz_ expired.  This was the real-audio failure:
    // acquire became shorter, but a subharmonic could be promoted to F0.
    auto subharmonicTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    subharmonicTracker->prepare(48000.0);
    subharmonicTracker->setReacquisitionAnchor(220.0f);
    subharmonicTracker->setRescueMode(true);
    ModernPitchEngine::MultiRatePitchTracker::DecoderDecision subharmonicDecision;
    subharmonicDecision.valid = true;
    subharmonicDecision.candidate.valid = true;
    subharmonicDecision.candidate.frequencyHz = 110.0f;
    subharmonicDecision.candidate.confidence = 0.99f;
    subharmonicDecision.candidate.periodicity = 0.99f;
    subharmonicDecision.consensus = 0.94f;
    subharmonicDecision.supportCount = 4;
    subharmonicDecision.directSupportCount = 4;
    subharmonicDecision.freshSupportMask = 0x0f;
    const bool subharmonicCommitted = subharmonicTracker->confirmOctaveTransition(
        subharmonicDecision, false);
    success &= check(!subharmonicCommitted && !subharmonicDecision.valid,
                     "rescue_subharmonic_cannot_restart_register");

    // Even high-confidence raw evidence outside the anchor window cannot use
    // sufficientInitialEvidence to bypass rescue continuity.
    auto bypassTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    bypassTracker->prepare(48000.0);
    bypassTracker->setReacquisitionAnchor(220.0f);
    bypassTracker->setRescueMode(true);
    auto& bypassSlot = bypassTracker->halfRateCandidate_;
    bypassSlot.candidate.valid = true;
    bypassSlot.candidate.frequencyHz = 130.0f;
    bypassSlot.candidate.confidence = 0.99f;
    bypassSlot.candidate.periodicity = 0.96f;
    bypassSlot.candidate.pathIndex = 1;
    bypassSlot.candidate.ageInHops = 0;
    bypassSlot.ageInHops = 0;
    const auto bypassDecision = bypassTracker->decodeCandidate(false);
    success &= check(!bypassDecision.valid
                     || ModernPitchEngine::MultiRatePitchTracker::centsDistance(
                         bypassDecision.candidate.frequencyHz, 220.0f) <= 360.0f,
                     "strong_subharmonic_cannot_bypass_rescue_anchor");


    // A phonetic/raw onset is not a musical note transition.  A wide rescue
    // challenger must therefore persist across several fresh observations
    // before it can replace the latched register.  This models a voiced word
    // onset such as /j/ in "Your": transient periodic structure may be strong,
    // but one or two hops must never become audible pitch control.
    auto phoneticTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    phoneticTracker->prepare(48000.0);
    phoneticTracker->setReacquisitionAnchor(220.0f);
    phoneticTracker->setRescueMode(true);
    auto makeWideChallenger = []
    {
        ModernPitchEngine::MultiRatePitchTracker::DecoderDecision d;
        d.valid = true;
        d.candidate.valid = true;
        d.candidate.frequencyHz = 165.0f; // ~-498 cents: plausible alias / perfect-fourth challenger
        d.candidate.confidence = 0.97f;
        d.candidate.periodicity = 0.93f;
        d.consensus = 0.86f;
        d.supportCount = 4;
        d.directSupportCount = 3;
        d.freshSupportMask = 0x0f;
        return d;
    };
    bool phoneticBurstCommitted = false;
    for (int hop = 0; hop < 4; ++hop)
    {
        auto d = makeWideChallenger();
        phoneticBurstCommitted = phoneticTracker->confirmOctaveTransition(d, true)
            || phoneticBurstCommitted;
    }
    success &= check(!phoneticBurstCommitted,
                     "raw_phonetic_onset_cannot_immediately_authorize_wide_rescue");

    // A genuine large melodic move is still recoverable: strong direct
    // evidence that persists beyond the transient window eventually owns the
    // new register even without relying on raw onsetPending.
    phoneticTracker->pendingOctaveCount_ = 0;
    phoneticTracker->pendingOctaveFrequencyHz_ = 0.0f;
    bool persistentWideCommitted = false;
    for (int hop = 0; hop < 8; ++hop)
    {
        auto d = makeWideChallenger();
        persistentWideCommitted = phoneticTracker->confirmOctaveTransition(d, false);
    }
    success &= check(persistentWideCommitted,
                     "persistent_multi_evidence_wide_rescue_can_commit_real_note_change");

    // Rap/rough-speech regression: even deliberately aperiodic non-zero audio
    // must publish authoritative voice presence and must keep detector paths
    // alive instead of collapsing to 0/4 before the supervisor can search F0.
    auto rapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    rapTracker->prepare(48000.0);
    rapTracker->setRange(45.0f, 900.0f);
    rapTracker->setSensitivity(0.70f);
    std::uint32_t rapNoise = 0x12345678u;
    int rapPresenceHops = 0;
    int rapMaxDetectorSupport = 0;
    int rapMinDetectorSupport = 4;
    int rapPitchlessPresentHops = 0;
    float rapMinimumVoicing = 1.0f;
    ModernPitchEngine::PitchObservation rapObservation;
    for (int sample = 0; sample < 12000; ++sample)
    {
        rapNoise = 1664525u * rapNoise + 1013904223u;
        const float noise = (static_cast<float>((rapNoise >> 8) & 0x00ffffffu)
            / static_cast<float>(0x007fffffu) - 1.0f) * 0.085f;
        const float syllabic = (sample % 1100) < 760 ? 1.0f : 0.22f;
        if (rapTracker->processSample(noise * syllabic, rapObservation)
            && sample > 1400 && rapObservation.audioPresent)
        {
            ++rapPresenceHops;
            rapMaxDetectorSupport = std::max(rapMaxDetectorSupport,
                                             rapObservation.detectorSupport);
            rapMinDetectorSupport = std::min(rapMinDetectorSupport,
                                             rapObservation.detectorSupport);
            rapMinimumVoicing = std::min(rapMinimumVoicing,
                                         rapObservation.voicing);
            if (!rapObservation.valid || rapObservation.frequencyHz <= 0.0f)
                ++rapPitchlessPresentHops;
        }
    }
    success &= check(rapPresenceHops > 20 && rapMinimumVoicing > 0.99f,
                     "nonzero_audio_cannot_be_unvoiced");
    success &= check(rapMaxDetectorSupport > 0,
                     "nonzero_audio_keeps_detector_paths_alive");
    success &= check(rapPresenceHops > 20 && rapMinDetectorSupport > 0,
                     "nonzero_audio_never_reports_zero_detector_paths");
    success &= check(rapPresenceHops > 20 && rapPitchlessPresentHops == 0,
                     "nonzero_audio_never_reports_pitchless_stable");

    // Zero consensus is explicitly allowed to drive correction. A single weak
    // path is still better than pitchless "stable": presence must publish an
    // F0 and the supervisor must turn that F0 into a real target/correction.
    auto zeroConsensusTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    zeroConsensusTracker->prepare(48000.0);
    zeroConsensusTracker->presenceMode_ = true;
    auto& zeroConsensusSlot = zeroConsensusTracker->halfRateCandidate_;
    zeroConsensusSlot.candidate.valid = true;
    zeroConsensusSlot.candidate.frequencyHz = 452.0f;
    zeroConsensusSlot.candidate.confidence = 0.01f;
    zeroConsensusSlot.candidate.periodicity = 0.01f;
    zeroConsensusSlot.candidate.pathIndex = 1;
    zeroConsensusSlot.candidate.ageInHops = 0;
    zeroConsensusSlot.ageInHops = 0;
    zeroConsensusTracker->decoderBeam_.fill({});
    const auto zeroConsensusDecision = zeroConsensusTracker->decodeCandidate(false);
    success &= check(zeroConsensusDecision.valid
                     && zeroConsensusDecision.candidate.frequencyHz > 0.0f
                     && std::abs(zeroConsensusDecision.consensus) < 1.0e-7f,
                     "zero_consensus_presence_fallback_yields_valid_f0");

    auto firstPresenceLock = zeroConsensusDecision;
    const bool firstPresenceAccepted = zeroConsensusTracker->confirmOctaveTransition(
        firstPresenceLock, false);
    success &= check(firstPresenceAccepted && firstPresenceLock.valid,
                     "presence_first_lock_does_not_wait_for_consensus");

    auto engine = std::make_unique<ModernPitchEngine>();
    engine->prepare(48000.0, 256, 1, ModernPitchEngine::LatencyMode::live);
    ModernPitchEngine::ScaleQuantizer quantizer;
    quantizer.reset();
    ModernPitchEngine::Parameters parameters;
    ModernPitchEngine::ScaleQuantizer zeroConsensusQuantizer;
    zeroConsensusQuantizer.reset();
    const double zeroConsensusUnison = 1.0;
    zeroConsensusQuantizer.setScale(&zeroConsensusUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState zeroConsensusState;
    ModernPitchEngine::PitchObservation zeroConsensusObservation;
    zeroConsensusObservation.audioPresent = true;
    zeroConsensusObservation.valid = true;
    zeroConsensusObservation.frequencyHz = 452.0f;
    zeroConsensusObservation.voicing = 1.0f;
    zeroConsensusObservation.confidence = 0.01f;
    zeroConsensusObservation.periodicity = 0.01f;
    zeroConsensusObservation.consensus = 0.0f;
    zeroConsensusObservation.detectorSupport = 1;
    engine->updateCorrectionState(zeroConsensusState, zeroConsensusQuantizer,
                                  zeroConsensusObservation, parameters);
    success &= check(zeroConsensusState.targetValid
                     && std::abs(zeroConsensusState.desiredCents) > 5.0,
                     "zero_consensus_valid_f0_drives_real_correction");

    ModernPitchEngine::PitchObservation meterConsensusObservation;
    meterConsensusObservation.valid = true;
    meterConsensusObservation.audioPresent = true;
    meterConsensusObservation.frequencyHz = 452.0f;
    meterConsensusObservation.confidence = 0.12f;
    meterConsensusObservation.periodicity = 0.73f;
    meterConsensusObservation.voicing = 1.0f;
    meterConsensusObservation.consensus = 0.0f;
    ModernPitchEngine::CorrectionState meterConsensusState;
    meterConsensusState.targetValid = true;
    meterConsensusState.targetLog2 = std::log2(440.0);
    CreativeTempo::Metering meterTempo;
    engine->publishMetering(meterConsensusObservation, meterConsensusState,
                            -37.0, meterTempo);
    const auto zeroConsensusMeter = engine->getMetering();
    success &= check(std::abs(zeroConsensusMeter.consensus) < 1.0e-7f
                     && zeroConsensusMeter.harmonicity > 0.70f,
                     "meter_consensus_is_real_detector_consensus");
    success &= check(std::abs(zeroConsensusMeter.correctionCents + 37.0f) < 0.01f,
                     "zero_consensus_meter_can_report_active_correction");

    // Even if the rich analyzer describes the current block as breath/noise,
    // explicit input presence owns the voice state. Detector uncertainty is
    // allowed to affect F0 search, never voiced/unvoiced authority.
    ModernPitchEngine::ScaleQuantizer presenceQuantizer;
    presenceQuantizer.reset();
    ModernPitchEngine::Parameters presenceParameters;
    setBreathEvidence(presenceParameters);
    ModernPitchEngine::CorrectionState presenceState;
    ModernPitchEngine::PitchObservation presentWithoutF0;
    presentWithoutF0.audioPresent = true;
    presentWithoutF0.voicing = 1.0f;
    engine->updateCorrectionState(presenceState, presenceQuantizer,
                                  presentWithoutF0, presenceParameters);
    success &= check(presenceState.noteBodyLatched
                     && presenceState.trackingState == ModernPitchEngine::TrackingState::stable,
                     "audio_presence_overrides_unvoiced_and_acquire_timidity");

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
