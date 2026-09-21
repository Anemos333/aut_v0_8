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
    success &= check(firstAccepted && first.valid,
                     "credible_multi_evidence_initial_register_commits_immediately");

    auto cautiousTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    cautiousTracker->prepare(48000.0);
    auto makeSingleFamilyInitial = []
    {
        ModernPitchEngine::MultiRatePitchTracker::DecoderDecision d;
        d.valid = true;
        d.candidate.valid = true;
        d.candidate.frequencyHz = 440.0f;
        d.candidate.confidence = 0.54f;
        d.candidate.periodicity = 0.62f;
        d.consensus = 0.18f;
        d.supportCount = 1;
        d.directSupportCount = 1;
        d.freshSupportMask = 1;
        return d;
    };
    auto cautiousFirst = makeSingleFamilyInitial();
    const bool cautiousFirstAccepted = cautiousTracker->confirmOctaveTransition(
        cautiousFirst, false);
    auto cautiousSecond = makeSingleFamilyInitial();
    const bool cautiousSecondAccepted = cautiousTracker->confirmOctaveTransition(
        cautiousSecond, false);
    success &= check(cautiousFirstAccepted && cautiousFirst.valid,
                     "fresh_single_family_initial_measurement_owns_immediately");
    success &= check(cautiousSecondAccepted && cautiousSecond.valid,
                     "repeated_single_family_measurement_remains_valid");

    // REAL_VOICE_BOOTSTRAP_V1: exercise the actual raw-candidate -> consensus
    // -> decoder -> initial-register path. The previous test constructed an
    // already-approved DecoderDecision and therefore missed the real vocal
    // failure where consensus attenuation could veto acquire forever.
    auto vocalBootstrapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    vocalBootstrapTracker->prepare(48000.0);
    vocalBootstrapTracker->presenceMode_ = true;
    auto& vocalSlot = vocalBootstrapTracker->halfRateCandidate_;
    vocalSlot.candidate.valid = true;
    vocalSlot.candidate.frequencyHz = 220.0f;
    vocalSlot.candidate.confidence = 0.54f;
    vocalSlot.candidate.periodicity = 0.62f;
    vocalSlot.candidate.pathIndex = 1;
    vocalSlot.candidate.ageInHops = 0;
    vocalSlot.ageInHops = 0;
    auto vocalBootstrapDecision = vocalBootstrapTracker->decodeCandidate(false);
    const bool vocalBootstrapAccepted = vocalBootstrapTracker->confirmOctaveTransition(
        vocalBootstrapDecision, false);
    success &= check(vocalBootstrapDecision.valid && vocalBootstrapAccepted,
                     "real_voice_single_fresh_path_can_bootstrap_first_target");

    auto weakBootstrapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    weakBootstrapTracker->prepare(48000.0);
    weakBootstrapTracker->presenceMode_ = true;
    auto& weakSlot = weakBootstrapTracker->halfRateCandidate_;
    weakSlot.candidate.valid = true;
    weakSlot.candidate.frequencyHz = 220.0f;
    weakSlot.candidate.confidence = 0.24f;
    weakSlot.candidate.periodicity = 0.38f;
    weakSlot.candidate.pathIndex = 1;
    weakSlot.candidate.ageInHops = 0;
    weakSlot.ageInHops = 0;
    auto weakBootstrapDecision = weakBootstrapTracker->decodeCandidate(false);
    const bool weakBootstrapAccepted = weakBootstrapTracker->confirmOctaveTransition(
        weakBootstrapDecision, false);
    success &= check(weakBootstrapDecision.valid && weakBootstrapAccepted,
                     "weak_real_measurement_is_not_blocked_by_confidence");

    auto emptyBootstrapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    emptyBootstrapTracker->prepare(48000.0);
    emptyBootstrapTracker->presenceMode_ = true;
    const auto emptyBootstrapDecision = emptyBootstrapTracker->decodeCandidate(false);
    success &= check(!emptyBootstrapDecision.valid,
                     "absence_of_measurement_never_invents_f0");


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
    success &= check(normalSingleFamily.valid,
                     "fresh_single_family_can_report_real_note_change");

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
    std::uint32_t aperiodicGapRng = 0x7f4a7c15u;
    for (int sample = 0; sample < dropoutSamples; ++sample)
    {
        aperiodicGapRng ^= aperiodicGapRng << 13;
        aperiodicGapRng ^= aperiodicGapRng >> 17;
        aperiodicGapRng ^= aperiodicGapRng << 5;
        const float noise = (static_cast<float>(aperiodicGapRng & 0xffffu)
                           / 32767.5f - 1.0f) * 0.004f;
        static_cast<void>(delayedRescueTracker->processSample(noise, expiredObservation));
    }
    success &= check(delayedRescueTracker->trackedPitchHz_ == 0.0f
                     && std::abs(delayedRescueTracker->reacquisitionAnchorHz_ - 220.0f) < 0.01f,
                     "current_f0_can_expire_during_aperiodic_presence_without_erasing_physical_anchor");

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
    success &= check(delayedNormalDecision.valid,
                     "stale_anchor_cannot_veto_new_live_measurement");

    delayedRescueTracker->decoderBeam_.fill({});
    delayedRescueTracker->setRescueMode(true);
    const auto delayedRescueDecision = delayedRescueTracker->decodeCandidate(false);
    success &= check(delayedRescueDecision.valid
                     && delayedRescueTracker->trackedPitchHz_ == 0.0f,
                     "rescue_uses_physical_anchor_after_aperiodic_detector_hole");
    delayedRescueTracker->clearReacquisitionAnchor();
    delayedRescueTracker->decoderBeam_.fill({});
    const auto noBodyAnchorDecision = delayedRescueTracker->decodeCandidate(false);
    success &= check(noBodyAnchorDecision.valid,
                     "released_anchor_leaves_live_measurement_authoritative");


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

    // An exact octave/subharmonic challenger is measured and reported, but
    // confirmation applies a bounded negative veto before register ownership.
    auto bypassTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    bypassTracker->prepare(48000.0);
    bypassTracker->setReacquisitionAnchor(220.0f);
    bypassTracker->setRescueMode(true);
    auto& bypassSlot = bypassTracker->halfRateCandidate_;
    bypassSlot.candidate.valid = true;
    bypassSlot.candidate.frequencyHz = 110.0f;
    bypassSlot.candidate.confidence = 0.99f;
    bypassSlot.candidate.periodicity = 0.96f;
    bypassSlot.candidate.pathIndex = 1;
    bypassSlot.candidate.ageInHops = 0;
    bypassSlot.ageInHops = 0;
    auto bypassDecision = bypassTracker->decodeCandidate(false);
    const bool bypassCommitted = bypassTracker->confirmOctaveTransition(
        bypassDecision, false);
    success &= check(!bypassCommitted && !bypassDecision.valid,
                     "octave_alias_receives_bounded_negative_veto");


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
    success &= check(rapPresenceHops > 20,
                     "audio_presence_is_reported_independently_of_f0");
    success &= check(rapPresenceHops > 20 && rapPitchlessPresentHops > 0,
                     "aperiodic_presence_may_report_no_trustworthy_f0");
    success &= check(rapMinimumVoicing >= 0.0f && rapMinimumVoicing <= 1.0f
                     && rapMaxDetectorSupport >= 0 && rapMinDetectorSupport >= 0,
                     "detector_diagnostics_remain_bounded_without_fabricating_pitch");

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
    success &= check(zeroConsensusDecision.valid,
                     "zero_consensus_does_not_erase_real_measurement");

    auto firstPresenceLock = zeroConsensusDecision;
    const bool firstPresenceAccepted = zeroConsensusTracker->confirmOctaveTransition(
        firstPresenceLock, false);
    success &= check(firstPresenceAccepted && firstPresenceLock.valid,
                     "first_real_measurement_needs_no_confidence_permission");

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
                     && !presenceState.targetValid
                     && presenceState.trackingState == ModernPitchEngine::TrackingState::acquire,
                     "presence_without_f0_cannot_claim_stable_without_target");

    // SOUND_EQUALS_CORRECTION_V1 regression: a stereo vocal that is
    // exactly anti-phase must still create a target and audible correction.
    // The old linked L+R detector input cancelled this signal to zero forever.
    ModernPitchEngine antiphaseEngine;
    antiphaseEngine.prepare(48000.0, 256, 2, ModernPitchEngine::LatencyMode::live);
    ModernPitchEngine::Parameters antiphaseParameters;
    antiphaseParameters.amount = 1.0f;
    antiphaseParameters.retuneTimeMs = 0.0f;
    antiphaseParameters.humanize = 0.0f;
    antiphaseParameters.minimumPitchHz = 70.0f;
    antiphaseParameters.maximumPitchHz = 1000.0f;
    antiphaseParameters.stereoMode = ModernPitchEngine::StereoMode::linkedMidSide;
    const double authorityUnison = 1.0;
    double antiphasePhase = 0.0;
    constexpr double antiphaseFrequency = 452.0;
    juce::AudioBuffer<float> antiphaseBlock(2, 256);
    for (int blockIndex = 0; blockIndex < 220; ++blockIndex)
    {
        auto* left = antiphaseBlock.getWritePointer(0);
        auto* right = antiphaseBlock.getWritePointer(1);
        for (int sample = 0; sample < antiphaseBlock.getNumSamples(); ++sample)
        {
            antiphasePhase += 2.0 * 3.14159265358979323846
                * antiphaseFrequency / 48000.0;
            if (antiphasePhase >= 2.0 * 3.14159265358979323846)
                antiphasePhase -= 2.0 * 3.14159265358979323846;
            const float value = static_cast<float>(0.28 * std::sin(antiphasePhase));
            left[sample] = value;
            right[sample] = -value;
        }
        antiphaseEngine.process(antiphaseBlock, &authorityUnison, 1, 440.0,
                                antiphaseParameters);
    }
    const auto antiphaseMeter = antiphaseEngine.getMetering();
    success &= check(antiphaseMeter.detectedPitchHz > 430.0f
                     && antiphaseMeter.detectedPitchHz < 470.0f
                     && antiphaseMeter.targetPitchHz > 430.0f
                     && std::abs(antiphaseMeter.correctionCents) > 5.0f,
                     "sound_equals_correction_antiphase_stereo");

    // A stale rescue anchor is never allowed to veto a new live F0. This is the
    // sung-note failure mode that could otherwise hold an implausible register
    // for seconds after pitch reacquisition.
    auto liveRescueTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    liveRescueTracker->prepare(48000.0);
    liveRescueTracker->presenceMode_ = true;
    liveRescueTracker->setRescueMode(true);
    liveRescueTracker->setReacquisitionAnchor(220.0f);
    ModernPitchEngine::MultiRatePitchTracker::DecoderDecision liveRescueDecision;
    liveRescueDecision.valid = true;
    liveRescueDecision.candidate.valid = true;
    liveRescueDecision.candidate.frequencyHz = 440.0f;
    liveRescueDecision.candidate.confidence = 0.18f;
    liveRescueDecision.candidate.periodicity = 0.24f;
    liveRescueDecision.consensus = 0.0f;
    liveRescueDecision.supportCount = 1;
    liveRescueDecision.directSupportCount = 1;
    liveRescueDecision.freshSupportMask = 0x01;
    bool octaveCommittedTooEarly = false;
    bool octaveCommittedInFiniteTime = false;
    for (int hop = 0; hop < 24; ++hop)
    {
        auto decision = liveRescueDecision;
        decision.valid = true;
        decision.candidate.valid = true;
        const bool accepted = liveRescueTracker->confirmOctaveTransition(
            decision, false);
        if (hop < 23)
            octaveCommittedTooEarly = octaveCommittedTooEarly || accepted;
        else
            octaveCommittedInFiniteTime = accepted && decision.valid;
    }
    success &= check(!octaveCommittedTooEarly && octaveCommittedInFiniteTime,
                     "octave_veto_is_bounded_not_confidence_gated");

    // Acquire is permitted to describe detector search, but it must never mute
    // an already acquired correction. Presence plus a temporary F0 dropout holds
    // the exact destination until the next live F0 arrives.
    ModernPitchEngine::CorrectionState heldCorrectionState;
    heldCorrectionState.targetValid = true;
    heldCorrectionState.targetLog2 = std::log2(440.0);
    heldCorrectionState.desiredCents = -42.0;
    heldCorrectionState.currentCents = -42.0;
    heldCorrectionState.noteBodyLatched = true;
    heldCorrectionState.trackingState = ModernPitchEngine::TrackingState::stable;
    ModernPitchEngine::PitchObservation presentDropout;
    presentDropout.audioPresent = true;
    presentDropout.voicing = 1.0f;
    engine->updateCorrectionState(heldCorrectionState, presenceQuantizer,
                                  presentDropout, presenceParameters);
    success &= check(heldCorrectionState.trackingState
                         == ModernPitchEngine::TrackingState::stable
                     && heldCorrectionState.targetValid
                     && std::abs(heldCorrectionState.desiredCents + 42.0) < 1.0e-9,
                     "explicit_unvoiced_label_never_mutes_scale_correction");

    // SOUND_EQUALS_CORRECTION_V2 regression: reproduce the audible
    // intermittent bypass. The supervisor starts one octave stale, then a live
    // F0 arrives around 452 Hz with zero consensus and almost no confidence.
    // The old octave wrap converted the stale -1200-ish-cent relation to 0.
    ModernPitchEngine::ScaleQuantizer staleRegisterQuantizer;
    staleRegisterQuantizer.reset();
    const double staleRegisterUnison = 1.0;
    staleRegisterQuantizer.setScale(&staleRegisterUnison, 1, 440.0);
    ModernPitchEngine::Parameters staleRegisterParameters;
    staleRegisterParameters.amount = 1.0f;
    staleRegisterParameters.humanize = 0.0f;
    staleRegisterParameters.maximumCorrectionSemitones = 12.0f;
    ModernPitchEngine::CorrectionState staleRegisterState;
    staleRegisterState.pitchCentreValid = true;
    staleRegisterState.pitchCentreLog2 = std::log2(220.0);
    staleRegisterState.targetValid = true;
    staleRegisterState.targetLog2 = std::log2(220.0);
    staleRegisterState.transportPeriodHz = 220.0;
    staleRegisterState.noteBodyLatched = true;
    staleRegisterState.noteBodyConfidence = 1.0f;
    staleRegisterState.trackingState = ModernPitchEngine::TrackingState::stable;
    ModernPitchEngine::PitchObservation zeroConsensusRegisterJump;
    zeroConsensusRegisterJump.valid = true;
    zeroConsensusRegisterJump.audioPresent = true;
    zeroConsensusRegisterJump.frequencyHz = 452.0f;
    zeroConsensusRegisterJump.voicing = 1.0f;
    zeroConsensusRegisterJump.confidence = 0.01f;
    zeroConsensusRegisterJump.periodicity = 0.05f;
    zeroConsensusRegisterJump.consensus = 0.0f;
    zeroConsensusRegisterJump.detectorSupport = 1;
    const double staleInitialTarget = staleRegisterState.targetLog2;
    const double staleInitialTransport = staleRegisterState.transportPeriodHz;
    const double staleInitialCorrection = staleRegisterState.desiredCents;
    engine->updateCorrectionState(staleRegisterState,
                                  staleRegisterQuantizer,
                                  zeroConsensusRegisterJump,
                                  staleRegisterParameters);
    success &= check(std::abs(staleRegisterState.targetLog2 - staleInitialTarget) < 1.0e-12
                     && std::abs(staleRegisterState.transportPeriodHz - staleInitialTransport) < 1.0e-12
                     && std::abs(staleRegisterState.desiredCents - staleInitialCorrection) < 1.0e-12,
                     "single_zero_consensus_register_hop_has_zero_audible_authority");
    for (int hop = 0; hop < 120; ++hop)
        engine->updateCorrectionState(staleRegisterState,
                                      staleRegisterQuantizer,
                                      zeroConsensusRegisterJump,
                                      staleRegisterParameters);
    const double staleRegisterTargetHz = std::exp2(staleRegisterState.targetLog2);
    const double staleRegisterOutputHz = staleRegisterState.transportPeriodHz
        * std::exp2(staleRegisterState.desiredCents / 1200.0);
    success &= check(staleRegisterTargetHz > 430.0
                     && staleRegisterTargetHz < 450.0
                     && std::abs(staleRegisterState.desiredCents) > 5.0
                     && std::abs(1200.0 * std::log2(
                         staleRegisterOutputHz / staleRegisterTargetHz)) < 1.0e-6,
                     "persistent_zero_consensus_register_change_reaches_scale");

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
    success &= check(dropoutState.trackingState == ModernPitchEngine::TrackingState::stable
                     && dropoutState.noteBodyLatched
                     && dropoutState.pitchStaleSamples > 0
                     && std::abs(dropoutState.desiredCents - 100.0) < 1.0e-9,
                     "body_evidence_keeps_stale_pitch_musically_stable");
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
    const double recoveredTransportMove = std::abs(1200.0 * std::log2(
        recoveryState.transportPeriodHz / 220.0));
    success &= check(recoveredCentreMove > 80.0
                     && recoveredTransportMove > 80.0
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
    success &= check(acquireState.trackingState == ModernPitchEngine::TrackingState::stable
                     && acquireState.noteBodyLatched
                     && std::abs(acquireState.desiredCents - 100.0) < 1.0e-9,
                     "body_signal_cannot_be_stuck_in_acquire");

    // BREATH_IS_TARGETED_NOT_DRY_V1: once a target exists, breath/noise may
    // not nominate another degree. A sustained measured breath F0 may refine
    // source transport, but the correction remains non-zero and scale-owned.
    ModernPitchEngine::CorrectionState spuriousBreath = dropoutState;
    const double breathHoldTargetHz = 220.0 * std::exp2(100.0 / 1200.0);
    spuriousBreath.targetLog2 = std::log2(breathHoldTargetHz);
    setBreathEvidence(parameters);
    auto falsePitchOnBreath = strongPitch(231.0f);
    falsePitchOnBreath.audioPresent = false;
    falsePitchOnBreath.correctionFrequencyHz = 231.0f;
    const double breathTargetBefore = spuriousBreath.targetLog2;
    for (int i = 0; i < 100; ++i)
        engine->updateCorrectionState(spuriousBreath, quantizer,
                                      falsePitchOnBreath, parameters);
    success &= check(spuriousBreath.trackingState != ModernPitchEngine::TrackingState::release
                     && spuriousBreath.targetValid
                     && std::abs(spuriousBreath.targetLog2 - breathTargetBefore) < 1.0e-12
                     && std::abs(spuriousBreath.desiredCents) > 1.0,
                     "breath_cannot_release_existing_target_to_source");

    // The same invariant holds when the detector correctly reports no F0.
    ModernPitchEngine::CorrectionState pitchlessTail = dropoutState;
    for (int i = 0; i < 100; ++i)
        engine->updateCorrectionState(pitchlessTail, quantizer, invalid, parameters);
    success &= check(pitchlessTail.trackingState != ModernPitchEngine::TrackingState::release
                     && pitchlessTail.targetValid
                     && std::abs(pitchlessTail.desiredCents - 100.0) < 1.0e-9,
                     "pitchless_tail_keeps_arrival_scale_degree");

    for (int i = 0; i < 4800; ++i)
        static_cast<void>(engine->advanceCorrection(pitchlessTail));
    success &= check(std::abs(pitchlessTail.currentCents - 100.0) < 1.0e-9,
                     "pitchless_tail_never_glides_back_to_source");

    parameters.retuneTimeMs = 0.0f;
    parameters.transitionTimeMs = 40.0f;
    const double transitionResponse = engine->responseTimeMs(parameters, true, 100.0);
    std::cerr << "single_path_transition_response_ms=" << transitionResponse << '\n';
    success &= check(std::abs(transitionResponse) < 1.0e-12,
                     "response_zero_remains_literal_across_target_revision");

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
    success &= check(std::abs(boundedTransition.velocityCentsPerSecond) < 1.0e-12
                     && std::abs(boundedTransition.currentCents
                                 - boundedTransition.desiredCents) < 1.0e-9,
                     "transition_hard_bound_finishes_without_controller_momentum");

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
        const double vibratoCents = 70.0 * std::sin(2.0 * 3.14159265358979323846
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

    // HOLD_IS_LITERAL_USER_CENTS_V2: Hold is a literal user radius, not a
    // density-dependent hidden fraction of a scale degree. A wide Hold on a
    // microtonal scale is therefore explicit user intent, while independently
    // qualified new-note evidence can still override it.
    ModernPitchEngine::Parameters hardDenseParameters = denseParameters;
    hardDenseParameters.scaleLock = true;
    hardDenseParameters.hardLockActive = true;
    hardDenseParameters.lockHysteresis = 80.0f;
    hardDenseParameters.lockStrictness = 1.0f;
    hardDenseParameters.humanize = 1.0f;
    hardDenseParameters.vibratoPreserve = 1.0f;
    auto hardDenseObservation = strongPitch(440.0f);
    const float denseEffectiveHysteresis = engine->adaptiveHysteresis(
        hardDenseParameters, denseQuantizer, hardDenseObservation);
    std::cerr << "dense_effective_hold_radius_cents="
              << denseEffectiveHysteresis << '\n';
    success &= check(std::abs(denseEffectiveHysteresis - 80.0f) < 1.0e-6f,
                     "hold_radius_is_literal_on_dense_scales");

    // HOLD_OWNERSHIP_BOUNDARY_V1: Hold belongs to musical ownership,
    // not ScaleQuantizer state. The quantizer is geometry-only.
    const double holdSemitone = std::exp2(1.0 / 12.0);
    const std::array<double, 2> holdScale { 1.0, holdSemitone };
    ModernPitchEngine::ScaleQuantizer lowHoldQuantizer;
    ModernPitchEngine::ScaleQuantizer highHoldQuantizer;
    lowHoldQuantizer.setScale(holdScale.data(), 2, 440.0);
    highHoldQuantizer.setScale(holdScale.data(), 2, 440.0);

    auto makeHoldState = []()
    {
        ModernPitchEngine::CorrectionState s;
        const double root = std::log2(440.0);
        s.targetValid = true;
        s.pitchCentreValid = true;
        s.targetLog2 = root;
        s.pitchCentreLog2 = root;
        s.stableObservations = 20;
        s.stableBodyObservations = 20;
        s.noteBodyLatched = true;
        s.noteBodyConfidence = 1.0f;
        s.transportPeriodHz = 440.0;
        s.trackingState = ModernPitchEngine::TrackingState::stable;
        return s;
    };

    auto lowHoldState = makeHoldState();
    auto highHoldState = makeHoldState();
    auto holdObservation = strongPitch(static_cast<float>(
        440.0 * std::exp2(75.0 / 1200.0)));

    auto lowHoldParameters = parameters;
    auto highHoldParameters = parameters;
    setBodyEvidence(lowHoldParameters);
    setBodyEvidence(highHoldParameters);
    lowHoldParameters.scaleLock = true;
    highHoldParameters.scaleLock = true;
    lowHoldParameters.hardLockActive = true;
    highHoldParameters.hardLockActive = true;
    lowHoldParameters.lockHysteresis = 0.0f;
    highHoldParameters.lockHysteresis = 80.0f;

    for (int hop = 0; hop < 4; ++hop)
    {
        engine->updateCorrectionState(
            lowHoldState, lowHoldQuantizer, holdObservation, lowHoldParameters);
        engine->updateCorrectionState(
            highHoldState, highHoldQuantizer, holdObservation, highHoldParameters);
    }

    const double lowHoldMove =
        (lowHoldState.targetLog2 - std::log2(440.0)) * 1200.0;
    const double highHoldMove =
        (highHoldState.targetLog2 - std::log2(440.0)) * 1200.0;
    success &= check(lowHoldMove > 90.0 && std::abs(highHoldMove) < 0.5,
                     "hold_is_owned_by_engine_identity_boundary");

    // At maximum Humanize + Vibrato Preserve, a 10-cent input deviation on
    // 48-EDO must still request enough correction to leave <=3 cents residual
    // under strict hard lock. This is the steady-state pitch contract; renderer
    // and trajectory tests cover convergence separately.
    ModernPitchEngine::ScaleQuantizer hardDenseResidualQuantizer;
    hardDenseResidualQuantizer.reset();
    hardDenseResidualQuantizer.setScale(
        denseScale.data(), static_cast<int>(denseScale.size()), 440.0);
    ModernPitchEngine::CorrectionState hardDenseState;
    auto hardDenseOffset = strongPitch(static_cast<float>(
        440.0 * std::exp2(10.0 / 1200.0)));
    hardDenseOffset.audioPresent = true;
    engine->updateCorrectionState(hardDenseState, hardDenseResidualQuantizer,
                                  hardDenseOffset, hardDenseParameters);
    const double hardDenseObservedCents = 1200.0 * std::log2(
        static_cast<double>(hardDenseOffset.frequencyHz) / 440.0);
    const double hardDenseResidualCents = std::abs(
        hardDenseObservedCents + hardDenseState.desiredCents);
    std::cerr << "dense_scale_lock_residual_budget_cents="
              << hardDenseResidualCents << '\n';
    success &= check(hardDenseResidualCents <= 3.05,
                     "dense_scale_lock_residual_budget_is_degree_safe");

    // The target-change path must remain inside Scale Lock's own fast range;
    // the general 35-40 ms transition control may not stretch it again.
    hardDenseParameters.retuneTimeMs = 500.0f;
    hardDenseParameters.transitionTimeMs = 80.0f;
    hardDenseParameters.tempo.mode = CreativeTempo::Mode::off;
    const double denseScaleLockResponse = engine->responseTimeMs(
        hardDenseParameters, true, 25.0);
    std::cerr << "dense_scale_lock_response_ms="
              << denseScaleLockResponse << '\n';
    success &= check(denseScaleLockResponse <= 3.001,
                     "scale_lock_target_change_stays_in_live_speed_budget");

    // ABSOLUTE_SCALE_LOCK_V4: the rigid endpoint is a mathematical
    // reference lock, not a tolerance band.  The selected degree may still be
    // chosen with hysteresis, but once chosen the requested steady-state pitch
    // must contain zero voluntary residual.
    ModernPitchEngine::Parameters absoluteLockParameters;
    setBodyEvidence(absoluteLockParameters);
    absoluteLockParameters.scaleLock = true;
    absoluteLockParameters.hardLockActive = true;
    absoluteLockParameters.lockStrictness = 1.0f;
    absoluteLockParameters.lockHysteresis = 80.0f;
    absoluteLockParameters.amount = 1.0f;
    absoluteLockParameters.retuneTimeMs = 0.0f;
    absoluteLockParameters.humanize = 0.0f;
    absoluteLockParameters.vibratoPreserve = 0.0f;
    absoluteLockParameters.maximumCorrectionSemitones = 24.0f;

    const auto checkAbsoluteScaleLock = [&](int edo,
                                            double sourceOffsetCents,
                                            float consensus,
                                            const char* name)
    {
        std::vector<double> ratios(static_cast<std::size_t>(edo));
        for (int degree = 0; degree < edo; ++degree)
            ratios[static_cast<std::size_t>(degree)] = std::exp2(
                static_cast<double>(degree) / static_cast<double>(edo));

        ModernPitchEngine::ScaleQuantizer exactQuantizer;
        exactQuantizer.reset();
        exactQuantizer.setScale(ratios.data(), edo, 440.0);
        ModernPitchEngine::CorrectionState exactState;
        auto exactObservation = strongPitch(static_cast<float>(
            440.0 * std::exp2(sourceOffsetCents / 1200.0)));
        exactObservation.audioPresent = true;
        exactObservation.consensus = consensus;
        if (consensus <= 0.0f)
        {
            exactObservation.confidence = 0.01f;
            exactObservation.periodicity = 0.05f;
        }

        engine->updateCorrectionState(exactState, exactQuantizer,
                                      exactObservation, absoluteLockParameters);
        const double observedLog2 = std::log2(
            static_cast<double>(exactObservation.frequencyHz));
        const double requestedTargetError =
            (exactState.targetLog2 - observedLog2) * 1200.0;
        const double destinationResidual = std::abs(
            (observedLog2 + exactState.desiredCents / 1200.0
             - exactState.targetLog2) * 1200.0);
        std::cerr << name << "_destination_residual_cents="
                  << destinationResidual << '\n';

        bool localSuccess = exactState.targetValid
            && std::abs(exactState.desiredCents - requestedTargetError) < 1.0e-9
            && destinationResidual < 1.0e-9;

        // Verify the single correction trajectory also reaches the exact
        // destination rather than merely requesting it.
        for (int sample = 0; sample < 4800; ++sample)
            static_cast<void>(engine->advanceCorrection(exactState));
        const double convergedResidual = std::abs(
            (observedLog2 + exactState.currentCents / 1200.0
             - exactState.targetLog2) * 1200.0);
        std::cerr << name << "_converged_residual_cents="
                  << convergedResidual << '\n';
        localSuccess = localSuccess && convergedResidual < 0.001;
        return check(localSuccess, name);
    };

    success &= checkAbsoluteScaleLock(12, 37.0, 0.88f,
                                      "absolute_scale_lock_zero_residual_12tet");
    success &= checkAbsoluteScaleLock(31, 15.0, 0.88f,
                                      "absolute_scale_lock_zero_residual_31edo");
    success &= checkAbsoluteScaleLock(48, 10.0, 0.88f,
                                      "absolute_scale_lock_zero_residual_48edo");
    success &= checkAbsoluteScaleLock(96, 5.0, 0.88f,
                                      "absolute_scale_lock_zero_residual_96edo");
    success &= checkAbsoluteScaleLock(48, 10.0, 0.0f,
                                      "absolute_scale_lock_zero_consensus_zero_residual");

    ModernPitchEngine::ScaleQuantizer vetoQuantizer;
    vetoQuantizer.reset();
    std::array<double, 12> vetoChromatic {};
    for (int degree = 0; degree < 12; ++degree)
        vetoChromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    vetoQuantizer.setScale(vetoChromatic.data(), 12, 440.0);
    ModernPitchEngine::CorrectionState vetoState;
    auto vetoInitial = strongPitch(440.0f);
    vetoInitial.audioPresent = true;
    engine->updateCorrectionState(vetoState, vetoQuantizer,
                                  vetoInitial, absoluteLockParameters);
    auto weakRealChange = strongPitch(505.0f);
    weakRealChange.audioPresent = true;
    weakRealChange.confidence = 0.01f;
    weakRealChange.periodicity = 0.05f;
    weakRealChange.consensus = 0.0f;
    weakRealChange.detectorSupport = 1;
    for (int hop = 0; hop < 8; ++hop)
        engine->updateCorrectionState(vetoState, vetoQuantizer,
                                      weakRealChange, absoluteLockParameters);
    const double vetoTargetHz = std::exp2(vetoState.targetLog2);
    const double vetoDestinationHz = vetoState.transportPeriodHz
        * std::exp2(vetoState.desiredCents / 1200.0);
    success &= check(vetoTargetHz > 490.0 && vetoTargetHz < 497.0
                     && std::abs(1200.0 * std::log2(vetoDestinationHz / vetoTargetHz)) < 1.0e-9,
                     "weak_real_note_change_reaches_exact_scale_degree_in_bounded_time");

    // An asymmetric custom scale with a very narrow local interval receives
    // the same exact-target contract; density changes target selection safety,
    // never steady-state authority.
    std::array<double, 6> asymmetricScale {
        1.0,
        std::exp2(17.0 / 1200.0),
        std::exp2(143.0 / 1200.0),
        std::exp2(311.0 / 1200.0),
        std::exp2(702.0 / 1200.0),
        std::exp2(947.0 / 1200.0)
    };
    ModernPitchEngine::ScaleQuantizer asymmetricQuantizer;
    asymmetricQuantizer.reset();
    asymmetricQuantizer.setScale(asymmetricScale.data(),
                                 static_cast<int>(asymmetricScale.size()),
                                 440.0);
    ModernPitchEngine::CorrectionState asymmetricState;
    auto asymmetricObservation = strongPitch(static_cast<float>(
        440.0 * std::exp2(7.0 / 1200.0)));
    asymmetricObservation.audioPresent = true;
    engine->updateCorrectionState(asymmetricState, asymmetricQuantizer,
                                  asymmetricObservation, absoluteLockParameters);
    const double asymmetricObservedLog2 = std::log2(
        static_cast<double>(asymmetricObservation.frequencyHz));
    const double asymmetricResidual = std::abs(
        (asymmetricObservedLog2 + asymmetricState.desiredCents / 1200.0
         - asymmetricState.targetLog2) * 1200.0);
    std::cerr << "absolute_custom_scale_residual_cents="
              << asymmetricResidual << '\n';
    success &= check(asymmetricState.targetValid && asymmetricResidual < 1.0e-9,
                     "absolute_scale_lock_zero_residual_asymmetric_custom");

    // SCALE_OWNS_TRANSPORT_V1: correctionFrequencyHz is detector diagnostic
    // data only. A raw candidate that disagrees with the persistent tracker
    // coordinate must not command the audible correction.
    ModernPitchEngine::ScaleQuantizer liveCoordinateQuantizer;
    liveCoordinateQuantizer.reset();
    const double liveCoordinateUnison = 1.0;
    liveCoordinateQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState liveCoordinateState;
    auto liveCoordinateObservation = strongPitch(445.0f);
    liveCoordinateObservation.audioPresent = true;
    liveCoordinateObservation.correctionFrequencyHz = 452.0f;
    engine->updateCorrectionState(liveCoordinateState,
                                  liveCoordinateQuantizer,
                                  liveCoordinateObservation,
                                  absoluteLockParameters);
    const double liveCoordinateTargetHz = std::exp2(
        liveCoordinateState.targetLog2);
    const double transportExpectedCorrection = 1200.0 * std::log2(
        liveCoordinateTargetHz / liveCoordinateState.transportPeriodHz);
    const double transportResidual = std::abs(1200.0 * std::log2(
        liveCoordinateState.transportPeriodHz
        * std::exp2(liveCoordinateState.desiredCents / 1200.0)
        / liveCoordinateTargetHz));
    success &= check(liveCoordinateState.targetValid
                     && std::abs(liveCoordinateTargetHz - 440.0) < 0.1
                     && std::abs(liveCoordinateState.desiredCents
                                 - transportExpectedCorrection) < 1.0e-6
                     && transportResidual < 1.0e-6,
                     "continuous_local_measurement_updates_transport_without_owning_target");

    // Existing softness remains target-owned, but it is now measured from the
    // same persistent transport coordinate rather than the raw detector hop.
    ModernPitchEngine::Parameters softCoordinateParameters = absoluteLockParameters;
    softCoordinateParameters.humanize = 0.25f;
    ModernPitchEngine::ScaleQuantizer softCoordinateQuantizer;
    softCoordinateQuantizer.reset();
    softCoordinateQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState softCoordinateState;
    engine->updateCorrectionState(softCoordinateState,
                                  softCoordinateQuantizer,
                                  liveCoordinateObservation,
                                  softCoordinateParameters);
    const double softOutputHz = softCoordinateState.transportPeriodHz
        * std::exp2(softCoordinateState.desiredCents / 1200.0);
    const double softResidualCents = std::abs(
        1200.0 * std::log2(softOutputHz / liveCoordinateTargetHz));
    success &= check(std::abs(softCoordinateState.desiredCents) > 0.5
                     && softResidualCents < 18.1,
                     "soft_scale_lock_uses_persistent_transport_inside_target_cell");

    // AUTHORITY_CONTROLS_EXPLICIT_V1: the six visible controls define the
    // zero-prudence endpoint. Hidden hardLockActive/lockStrictness values are not
    // allowed to withhold exact centering or add a response floor.
    ModernPitchEngine::Parameters explicitAuthorityParameters;
    setBodyEvidence(explicitAuthorityParameters);
    explicitAuthorityParameters.scaleLock = true;
    explicitAuthorityParameters.hardLockActive = false;
    explicitAuthorityParameters.lockStrictness = 0.0f;
    explicitAuthorityParameters.lockHysteresis = 0.0f;
    explicitAuthorityParameters.amount = 1.0f;
    explicitAuthorityParameters.retuneTimeMs = 0.0f;
    explicitAuthorityParameters.humanize = 0.0f;
    explicitAuthorityParameters.vibratoPreserve = 0.0f;
    explicitAuthorityParameters.maximumCorrectionSemitones = 24.0f;

    ModernPitchEngine::ScaleQuantizer explicitAuthorityQuantizer;
    explicitAuthorityQuantizer.reset();
    explicitAuthorityQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState explicitAuthorityState;
    auto explicitAuthorityObservation = strongPitch(445.0f);
    explicitAuthorityObservation.audioPresent = true;
    explicitAuthorityObservation.correctionFrequencyHz = 452.0f;
    explicitAuthorityObservation.confidence = 0.01f;
    explicitAuthorityObservation.periodicity = 0.05f;
    explicitAuthorityObservation.consensus = 0.0f;
    engine->updateCorrectionState(explicitAuthorityState,
                                  explicitAuthorityQuantizer,
                                  explicitAuthorityObservation,
                                  explicitAuthorityParameters);
    const double explicitTargetHz = std::exp2(explicitAuthorityState.targetLog2);
    const double explicitExpectedCents = 1200.0 * std::log2(
        explicitTargetHz / explicitAuthorityState.transportPeriodHz);
    success &= check(std::abs(explicitAuthorityState.desiredCents
                              - explicitExpectedCents) < 1.0e-6,
                     "visible_controls_own_exact_lock_without_hidden_flags");
    success &= check(std::abs(explicitAuthorityState.responseMs) < 1.0e-12,
                     "response_zero_has_no_hidden_mode_floor");
    const double immediateController = engine->advanceCorrection(explicitAuthorityState);
    success &= check(std::abs(immediateController - explicitAuthorityState.desiredCents) < 1.0e-9
                     && std::abs(explicitAuthorityState.velocityCentsPerSecond) < 1.0e-12,
                     "response_zero_reaches_destination_in_one_sample");
    success &= check(engine->adaptiveHysteresis(explicitAuthorityParameters,
                                                 explicitAuthorityQuantizer,
                                                 explicitAuthorityObservation) == 0.0f,
                     "hold_zero_means_exactly_zero_hysteresis");

    // HOLD_IS_EXPLICIT_EVERYWHERE_V1: Hold=0 means zero hysteresis even
    // when Scale Lock is disabled. There is no hidden Humanize-based prudence.
    ModernPitchEngine::Parameters unlockedZeroHold = explicitAuthorityParameters;
    unlockedZeroHold.scaleLock = false;
    unlockedZeroHold.lockHysteresis = 0.0f;
    success &= check(engine->adaptiveHysteresis(unlockedZeroHold,
                                                 explicitAuthorityQuantizer,
                                                 explicitAuthorityObservation) == 0.0f,
                     "hold_zero_has_no_hidden_unlocked_hysteresis");

    // SINGLE_VISIBLE_VIBRATO_AUTHORITY_V2: Vibrato Preserve is the only
    // vibrato-residual control. At zero there is no hidden non-lock preserve.
    ModernPitchEngine::Parameters unlockedVibrato = explicitAuthorityParameters;
    unlockedVibrato.scaleLock = false;
    unlockedVibrato.lockHysteresis = 0.0f;
    unlockedVibrato.vibratoPreserve = 0.0f;
    ModernPitchEngine::ScaleQuantizer unlockedVibratoQuantizer;
    unlockedVibratoQuantizer.reset();
    unlockedVibratoQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState unlockedVibratoState;
    double maximumUnlockedResidual = 0.0;
    for (int hop = 0; hop < 500; ++hop)
    {
        const double cents = 38.0 * std::sin(2.0 * 3.14159265358979323846
            * static_cast<double>(hop) / 150.0);
        auto vibratoHop = strongPitch(static_cast<float>(
            440.0 * std::exp2(cents / 1200.0)));
        vibratoHop.audioPresent = true;
        vibratoHop.correctionFrequencyHz = vibratoHop.frequencyHz;
        engine->updateCorrectionState(unlockedVibratoState,
                                      unlockedVibratoQuantizer,
                                      vibratoHop,
                                      unlockedVibrato);
        if (hop > 40 && unlockedVibratoState.targetValid)
        {
            const double actualOutputHz = static_cast<double>(vibratoHop.frequencyHz)
                * std::exp2(unlockedVibratoState.desiredCents / 1200.0);
            const double targetHz = std::exp2(unlockedVibratoState.targetLog2);
            maximumUnlockedResidual = std::max(maximumUnlockedResidual,
                std::abs(1200.0 * std::log2(actualOutputHz / targetHz)));
        }
    }
    success &= check(maximumUnlockedResidual < 0.15,
                     "visible_vibrato_zero_removes_hidden_nonlock_preserve");

    // SCALE_LOCK_NEVER_OWNS_DEPTH_V1: at equal visible softness controls, the
    // steady-state target-owned residual budget is identical with Scale Lock on
    // or off. Scale Lock is not a secret correction-depth switch.
    ModernPitchEngine::Parameters commonSoftness = explicitAuthorityParameters;
    commonSoftness.amount = 0.55f;
    commonSoftness.humanize = 0.45f;
    commonSoftness.vibratoPreserve = 0.0f;
    commonSoftness.lockHysteresis = 0.0f;
    auto softInput = strongPitch(450.0f);
    softInput.audioPresent = true;
    softInput.correctionFrequencyHz = 450.0f;
    ModernPitchEngine::ScaleQuantizer lockOffDepthQuantizer;
    ModernPitchEngine::ScaleQuantizer lockOnDepthQuantizer;
    lockOffDepthQuantizer.reset();
    lockOnDepthQuantizer.reset();
    lockOffDepthQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    lockOnDepthQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState lockOffDepthState;
    ModernPitchEngine::CorrectionState lockOnDepthState;
    auto lockOffSoftness = commonSoftness;
    auto lockOnSoftness = commonSoftness;
    lockOffSoftness.scaleLock = false;
    lockOnSoftness.scaleLock = true;
    engine->updateCorrectionState(lockOffDepthState, lockOffDepthQuantizer,
                                  softInput, lockOffSoftness);
    engine->updateCorrectionState(lockOnDepthState, lockOnDepthQuantizer,
                                  softInput, lockOnSoftness);
    const double lockOffResidual = std::abs(1200.0 * std::log2(
        lockOffDepthState.transportPeriodHz
        * std::exp2(lockOffDepthState.desiredCents / 1200.0) / 440.0));
    const double lockOnResidual = std::abs(1200.0 * std::log2(
        lockOnDepthState.transportPeriodHz
        * std::exp2(lockOnDepthState.desiredCents / 1200.0) / 440.0));
    success &= check(std::abs(lockOffResidual - lockOnResidual) < 1.0e-9,
                     "scale_lock_never_changes_correction_depth");

    // BREATH_IS_TARGETED_NOT_DRY_V1: a breathy note with a valid F0 cannot
    // nominate another degree, but it must continue being corrected to the
    // already-owned degree even when audioPresent telemetry is false.
    ModernPitchEngine::ScaleQuantizer breathTargetQuantizer;
    breathTargetQuantizer.reset();
    breathTargetQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState breathTargetState;
    auto breathBase = strongPitch(450.0f);
    breathBase.audioPresent = true;
    breathBase.correctionFrequencyHz = 450.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(breathTargetState, breathTargetQuantizer,
                                      breathBase, explicitAuthorityParameters);
    const double breathOwnedTarget = breathTargetState.targetLog2;
    auto breathParameters = explicitAuthorityParameters;
    setBreathEvidence(breathParameters);
    auto breathF0 = strongPitch(454.0f);
    breathF0.audioPresent = false;
    breathF0.correctionFrequencyHz = 454.0f;
    for (int hop = 0; hop < 16; ++hop)
        engine->updateCorrectionState(breathTargetState, breathTargetQuantizer,
                                      breathF0, breathParameters);
    const double breathActualOutputHz = 454.0
        * std::exp2(breathTargetState.desiredCents / 1200.0);
    const double breathTargetHz = std::exp2(breathTargetState.targetLog2);
    success &= check(std::abs(breathTargetState.targetLog2 - breathOwnedTarget) < 1.0e-12
                     && std::abs(1200.0 * std::log2(
                         breathActualOutputHz / breathTargetHz)) < 0.15,
                     "breathy_valid_f0_stays_on_owned_scale_target");

    // DETECTOR_IS_OBSERVER_V1: rigid correction settings do not alter the
    // detector decoder. Static CI below forbids that API from returning.

    // A consonant/transient may temporarily remove a usable F0, but while audio
    // is present the already-selected correction remains on the same wet path.
    // There is no permission to return to dry/unshifted audio between voiced hops.
    ModernPitchEngine::PitchObservation explicitDropout;
    explicitDropout.audioPresent = true;
    explicitDropout.voicing = 1.0f;
    const double heldExplicitCents = explicitAuthorityState.desiredCents;
    engine->updateCorrectionState(explicitAuthorityState,
                                  explicitAuthorityQuantizer,
                                  explicitDropout,
                                  explicitAuthorityParameters);
    const double dropoutController = engine->advanceCorrection(explicitAuthorityState);
    success &= check(explicitAuthorityState.trackingState
                         == ModernPitchEngine::TrackingState::stable
                     && std::abs(explicitAuthorityState.desiredCents - heldExplicitCents) < 1.0e-9
                     && std::abs(dropoutController - heldExplicitCents) < 1.0e-9,
                     "transient_f0_hole_keeps_authoritative_wet_correction");


    // A formally valid F0 on a consonant is not allowed to touch transport.
    ModernPitchEngine::Parameters phoneticHoldParameters = explicitAuthorityParameters;
    setBodyEvidence(phoneticHoldParameters);
    phoneticHoldParameters.voiceEventStrength = 0.95f;
    phoneticHoldParameters.voiceHarmonicity = 0.18f;
    auto phoneticOutlier = strongPitch(760.0f);
    phoneticOutlier.audioPresent = true;
    phoneticOutlier.correctionFrequencyHz = 760.0f;
    const double prePhoneticTarget = explicitAuthorityState.targetLog2;
    const double prePhoneticTransport = explicitAuthorityState.transportPeriodHz;
    const double prePhoneticCents = explicitAuthorityState.desiredCents;
    engine->updateCorrectionState(explicitAuthorityState,
                                  explicitAuthorityQuantizer,
                                  phoneticOutlier,
                                  phoneticHoldParameters);
    success &= check(std::abs(explicitAuthorityState.targetLog2 - prePhoneticTarget) < 1.0e-12
                     && std::abs(explicitAuthorityState.transportPeriodHz - prePhoneticTransport) < 1.0e-12
                     && std::abs(explicitAuthorityState.desiredCents - prePhoneticCents) < 1.0e-12,
                     "valid_phonetic_outlier_cannot_touch_transport");

    // A single far detector hop cannot directly revise note identity or source
    // transport. Persistent non-phonetic evidence can still produce a bounded
    // real note change, whose destination remains an exact scale degree.
    std::array<double, 12> authorityChromatic {};
    for (int degree = 0; degree < 12; ++degree)
        authorityChromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    ModernPitchEngine::ScaleQuantizer boundedOutlierQuantizer;
    boundedOutlierQuantizer.reset();
    boundedOutlierQuantizer.setScale(authorityChromatic.data(),
                                     static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState boundedOutlierState;
    auto base440 = strongPitch(440.0f);
    base440.audioPresent = true;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(boundedOutlierState,
                                      boundedOutlierQuantizer,
                                      base440,
                                      explicitAuthorityParameters);
    const double boundedTargetBefore = boundedOutlierState.targetLog2;
    const double boundedTransportBefore = boundedOutlierState.transportPeriodHz;
    const double boundedCentsBefore = boundedOutlierState.desiredCents;
    auto oneHopOutlier = strongPitch(493.8833f);
    oneHopOutlier.audioPresent = true;
    oneHopOutlier.correctionFrequencyHz = 493.8833f;
    engine->updateCorrectionState(boundedOutlierState,
                                  boundedOutlierQuantizer,
                                  oneHopOutlier,
                                  explicitAuthorityParameters);
    success &= check(std::abs(boundedOutlierState.targetLog2 - boundedTargetBefore) < 1.0e-12
                     && std::abs(boundedOutlierState.transportPeriodHz - boundedTransportBefore) < 1.0e-12
                     && std::abs(boundedOutlierState.desiredCents - boundedCentsBefore) < 1.0e-9,
                     "single_far_f0_outlier_has_zero_audible_authority");

    for (int hop = 0; hop < 24; ++hop)
        engine->updateCorrectionState(boundedOutlierState,
                                      boundedOutlierQuantizer,
                                      oneHopOutlier,
                                      explicitAuthorityParameters);
    const double sustainedTargetHz = std::exp2(boundedOutlierState.targetLog2);
    const double sustainedTransportOutputHz = boundedOutlierState.transportPeriodHz
        * std::exp2(boundedOutlierState.desiredCents / 1200.0);
    success &= check(std::abs(sustainedTargetHz - 493.8833) < 0.4
                     && std::abs(1200.0 * std::log2(
                         sustainedTransportOutputHz / sustainedTargetHz)) < 1.0e-6,
                     "sustained_real_change_reaches_exact_scale_degree_without_raw_snap");

    // SCALE_OWNS_VOICE_V2: once a scale destination exists, an explicitly
    // aperiodic/breathy frame may change the detector label but may never undo
    // or attenuate the scale transport.
    ModernPitchEngine::Parameters rigidBreathParameters = explicitAuthorityParameters;
    setBreathEvidence(rigidBreathParameters);
    ModernPitchEngine::CorrectionState rigidBreathState = explicitAuthorityState;
    const double rigidBreathTarget = rigidBreathState.targetLog2;
    const double rigidBreathCents = rigidBreathState.desiredCents;
    ModernPitchEngine::PitchObservation rigidAperiodic;
    rigidAperiodic.valid = false;
    rigidAperiodic.audioPresent = false;
    engine->updateCorrectionState(rigidBreathState,
                                  explicitAuthorityQuantizer,
                                  rigidAperiodic,
                                  rigidBreathParameters);
    success &= check(rigidBreathState.targetValid
                     && std::abs(rigidBreathState.targetLog2 - rigidBreathTarget) < 1.0e-12
                     && std::abs(rigidBreathState.desiredCents - rigidBreathCents) < 1.0e-9
                     && rigidBreathState.trackingState != ModernPitchEngine::TrackingState::release,
                     "rigid_aperiodic_material_never_leaves_scale_target");

    // Even before the body latch, a formally valid pitch coordinate at maximum
    // authority is quantized into the selected scale instead of being discarded
    // as breath and replaced by zero correction.
    ModernPitchEngine::CorrectionState rigidPreBodyBreath;
    auto rigidPreBodyObservation = strongPitch(452.0f);
    rigidPreBodyObservation.audioPresent = false;
    rigidPreBodyObservation.correctionFrequencyHz = 452.0f;
    engine->updateCorrectionState(rigidPreBodyBreath,
                                  explicitAuthorityQuantizer,
                                  rigidPreBodyObservation,
                                  rigidBreathParameters);
    success &= check(rigidPreBodyBreath.targetValid
                     && std::abs(std::exp2(rigidPreBodyBreath.targetLog2) - 440.0) < 0.1
                     && std::abs(rigidPreBodyBreath.desiredCents) > 5.0,
                     "exact_authority_valid_aperiodic_input_enters_scale");

    // Native API semantics: one semitone means 100 cents, with no adapter hack.
    const double unison = 1.0;
    quantizer.setScale(&unison, 1, 440.0);
    ModernPitchEngine::CorrectionState capState;
    auto voiced = strongPitch(static_cast<float>(440.0 * std::exp2(2.0 / 12.0)));
    parameters.maximumCorrectionSemitones = 1.0f;
    parameters.amount = 1.0f;
    parameters.humanize = 0.0f;
    setBodyEvidence(parameters);
    engine->updateCorrectionState(capState, quantizer, voiced, parameters);
    std::cerr << "one_semitone_cap_cents=" << capState.desiredCents << '\n';
    success &= check(std::abs(capState.desiredCents) <= 100.001,
                     "native_semitone_limit_is_100_cents");
    success &= check(std::abs(capState.desiredCents) > 95.0,
                     "native_semitone_limit_is_not_divided_by_twelve");


    // SCALE_OWNS_TRANSPORT_V1: no detector-hole prediction is audible.
    std::array<double, 12> rescueChromatic {};
    for (int degree = 0; degree < 12; ++degree)
        rescueChromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    ModernPitchEngine::ScaleQuantizer rescueQuantizer;
    rescueQuantizer.reset();
    rescueQuantizer.setScale(rescueChromatic.data(),
                             static_cast<int>(rescueChromatic.size()), 440.0);
    ModernPitchEngine::Parameters rescueParameters = explicitAuthorityParameters;
    setBodyEvidence(rescueParameters);
    ModernPitchEngine::CorrectionState noPredictionState;
    auto noPredictionVoice = strongPitch(452.0f);
    noPredictionVoice.audioPresent = true;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(noPredictionState, rescueQuantizer,
                                      noPredictionVoice, rescueParameters);
    const double noPredictionTarget = noPredictionState.targetLog2;
    const double noPredictionTransport = noPredictionState.transportPeriodHz;
    const double noPredictionDesired = noPredictionState.desiredCents;
    ModernPitchEngine::PitchObservation noPredictionHole;
    noPredictionHole.audioPresent = true;
    noPredictionHole.valid = false;
    for (int hop = 0; hop < 120; ++hop)
    {
        engine->updateCorrectionState(noPredictionState, rescueQuantizer,
                                      noPredictionHole, rescueParameters);
        static_cast<void>(engine->advanceCorrection(noPredictionState));
    }
    success &= check(!noPredictionState.rescuePredictionActive
                     && noPredictionState.rescueQualificationHops == 0
                     && std::abs(noPredictionState.targetLog2 - noPredictionTarget) < 1.0e-12
                     && std::abs(noPredictionState.transportPeriodHz - noPredictionTransport) < 1.0e-12
                     && std::abs(noPredictionState.desiredCents - noPredictionDesired) < 1.0e-12,
                     "detector_hole_holds_target_transport_and_correction_exactly");


    // MEASUREMENT_CONTINUUM_V1: weak-but-physical period evidence is usable for
    // sung body when independent voice evidence supports it. It may not remain
    // forever in Acquire merely because detector confidence is low.
    ModernPitchEngine::ScaleQuantizer provisionalQuantizer;
    provisionalQuantizer.reset();
    provisionalQuantizer.setScale(authorityChromatic.data(),
                                   static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState provisionalVoiceState;
    ModernPitchEngine::Parameters provisionalVoiceParameters = explicitAuthorityParameters;
    setBodyEvidence(provisionalVoiceParameters);
    ModernPitchEngine::PitchObservation provisionalVoice;
    provisionalVoice.audioPresent = true;
    provisionalVoice.valid = false;
    provisionalVoice.measurementAvailable = true;
    provisionalVoice.correctionFrequencyHz = 452.0f;
    provisionalVoice.confidence = 0.08f;
    provisionalVoice.periodicity = 0.22f;
    for (int hop = 0; hop < 24; ++hop)
    {
        engine->updateCorrectionState(provisionalVoiceState, provisionalQuantizer,
                                      provisionalVoice, provisionalVoiceParameters);
        for (int sample = 0;
             sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
        {
            static_cast<void>(engine->advanceCorrection(provisionalVoiceState));
        }
    }
    success &= check(provisionalVoiceState.targetValid
                     && provisionalVoiceState.noteBodyLatched
                     && provisionalVoiceState.trackingState != ModernPitchEngine::TrackingState::acquire
                     && std::abs(provisionalVoiceState.desiredCents) > 5.0,
                     "provisional_voice_measurement_cannot_stall_in_acquire");

    // The same provisional period on breath/sibilant evidence is not a note.
    ModernPitchEngine::CorrectionState provisionalSibilantState;
    ModernPitchEngine::Parameters provisionalSibilantParameters = explicitAuthorityParameters;
    setBreathEvidence(provisionalSibilantParameters);
    provisionalSibilantParameters.voiceEventStrength = 0.94f;
    engine->updateCorrectionState(provisionalSibilantState, provisionalQuantizer,
                                  provisionalVoice, provisionalSibilantParameters);
    success &= check(!provisionalSibilantState.targetValid,
                     "provisional_sibilant_measurement_cannot_invent_target");

    // TRANSITION_IS_TRANSPORT_V1: an invalid/phonetic frame cannot turn an
    // already-owned transition into unvoiced/stable or modify its destination.
    ModernPitchEngine::CorrectionState gluedTransition;
    gluedTransition.targetValid = true;
    gluedTransition.targetLog2 = std::log2(493.8833012561241);
    gluedTransition.transportPeriodHz = 500.0;
    gluedTransition.desiredCents = -21.318f;
    gluedTransition.currentCents = -10.0;
    gluedTransition.noteBodyLatched = true;
    gluedTransition.trackingState = ModernPitchEngine::TrackingState::transition;
    const double gluedTarget = gluedTransition.targetLog2;
    const double gluedTransport = gluedTransition.transportPeriodHz;
    const double gluedDesired = gluedTransition.desiredCents;
    ModernPitchEngine::PitchObservation gluedPhonetic;
    gluedPhonetic.audioPresent = true;
    gluedPhonetic.valid = false;
    ModernPitchEngine::Parameters gluedParameters = explicitAuthorityParameters;
    setBodyEvidence(gluedParameters);
    gluedParameters.voiceEventStrength = 0.96f;
    engine->updateCorrectionState(gluedTransition, provisionalQuantizer,
                                  gluedPhonetic, gluedParameters);
    success &= check(gluedTransition.trackingState == ModernPitchEngine::TrackingState::transition
                     && std::abs(gluedTransition.targetLog2 - gluedTarget) < 1.0e-12
                     && std::abs(gluedTransition.transportPeriodHz - gluedTransport) < 1.0e-12
                     && std::abs(gluedTransition.desiredCents - gluedDesired) < 1.0e-12,
                     "phonetic_frame_is_glued_to_owned_transition");

    // LOCAL_TRAJECTORY_V1: correctionFrequencyHz may be faster than the
    // continuity/identity coordinate, but only continuous small innovations may
    // move transport. At zero Vibrato this keeps the physical source estimate
    // within a few cents so the requested output remains nailed to the target.
    ModernPitchEngine::ScaleQuantizer localTrajectoryQuantizer;
    localTrajectoryQuantizer.reset();
    localTrajectoryQuantizer.setScale(authorityChromatic.data(),
                                       static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState localTrajectoryState;
    auto localBase = strongPitch(440.0f);
    localBase.audioPresent = true;
    localBase.correctionFrequencyHz = 440.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(localTrajectoryState, localTrajectoryQuantizer,
                                      localBase, explicitAuthorityParameters);
    double maximumLocalResidual = 0.0;
    bool localTargetChanged = false;
    const double localTargetReference = localTrajectoryState.targetLog2;
    for (int hop = 0; hop < 900; ++hop)
    {
        const double phase = 2.0 * 3.14159265358979323846
            * static_cast<double>(hop) / 150.0;
        const double cents = 70.0 * std::sin(phase);
        const double rawHz = 440.0 * std::exp2(cents / 1200.0);
        auto localObservation = strongPitch(440.0f); // deliberately lagged identity coordinate
        localObservation.audioPresent = true;
        localObservation.correctionFrequencyHz = static_cast<float>(rawHz);
        engine->updateCorrectionState(localTrajectoryState, localTrajectoryQuantizer,
                                      localObservation, explicitAuthorityParameters);
        const double transportResidual = std::abs(1200.0 * std::log2(
            rawHz / localTrajectoryState.transportPeriodHz));
        if (hop > 50)
            maximumLocalResidual = std::max(maximumLocalResidual, transportResidual);
        if (std::abs(localTrajectoryState.targetLog2 - localTargetReference) * 1200.0 > 0.5)
            localTargetChanged = true;
    }
    std::cerr << "local_trajectory_max_residual_cents=" << maximumLocalResidual << '\n';
    success &= check(!localTargetChanged && maximumLocalResidual < 4.0,
                     "zero_vibrato_lock_tracks_continuous_source_without_target_chatter");

    // A discontinuous local measurement still has zero transport authority.
    const double localTransportBeforeOutlier = localTrajectoryState.transportPeriodHz;
    auto localOutlier = strongPitch(440.0f);
    localOutlier.audioPresent = true;
    localOutlier.correctionFrequencyHz = 880.0f;
    engine->updateCorrectionState(localTrajectoryState, localTrajectoryQuantizer,
                                  localOutlier, explicitAuthorityParameters);
    success &= check(std::abs(localTrajectoryState.transportPeriodHz
                              - localTransportBeforeOutlier) < 1.0e-12,
                     "single_octave_innovation_has_zero_transport_authority");

    // LATENT_IDENTITY_NEVER_FREEZES_WET_V1: when a smooth long-note
    // trajectory enters the next cell but has not yet earned identity, target
    // stays put while transport/correction continue. No brief stale/dry notch.
    ModernPitchEngine::ScaleQuantizer pendingIdentityQuantizer;
    pendingIdentityQuantizer.reset();
    pendingIdentityQuantizer.setScale(authorityChromatic.data(),
                                       static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState pendingIdentityState;
    for (int cents = 0; cents <= 70; cents += 5)
    {
        auto ramp = strongPitch(static_cast<float>(
            440.0 * std::exp2(static_cast<double>(cents) / 1200.0)));
        ramp.audioPresent = true;
        ramp.correctionFrequencyHz = ramp.frequencyHz;
        engine->updateCorrectionState(pendingIdentityState,
                                      pendingIdentityQuantizer,
                                      ramp, explicitAuthorityParameters);
    }
    const double pendingTargetBefore = pendingIdentityState.targetLog2;
    const double pendingTransportBefore = pendingIdentityState.transportPeriodHz;
    auto firstDeepPending = strongPitch(static_cast<float>(
        440.0 * std::exp2(75.0 / 1200.0)));
    firstDeepPending.audioPresent = true;
    firstDeepPending.correctionFrequencyHz = firstDeepPending.frequencyHz;
    engine->updateCorrectionState(pendingIdentityState,
                                  pendingIdentityQuantizer,
                                  firstDeepPending, explicitAuthorityParameters);
    success &= check(std::abs(pendingIdentityState.targetLog2 - pendingTargetBefore) < 1.0e-12
                     && pendingIdentityState.transportPeriodHz > pendingTransportBefore,
                     "pending_identity_never_freezes_owned_wet_transport");

    // OCTAVE_AMBIGUITY_V2: a single-family octave challenger must persist for a
    // fixed short window. It is neither immediately hallucinated nor blocked by
    // confidence forever.
    auto octavePersistenceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    octavePersistenceTracker->prepare(48000.0);
    octavePersistenceTracker->trackedPitchHz_ = 440.0f;
    octavePersistenceTracker->trackedConfidence_ = 0.9f;
    octavePersistenceTracker->trackedPeriodicity_ = 0.9f;
    octavePersistenceTracker->trackedConsensus_ = 0.0f;
    octavePersistenceTracker->trackedSupportCount_ = 1;
    octavePersistenceTracker->committedOctaveFrequencyHz_ = 440.0f;
    octavePersistenceTracker->octaveCommitGuardHops_ = 0;
    const auto makeOctaveChallenger = []
    {
        ModernPitchEngine::MultiRatePitchTracker::DecoderDecision d;
        d.valid = true;
        d.candidate.valid = true;
        d.candidate.frequencyHz = 880.0f;
        d.candidate.confidence = 0.01f;
        d.candidate.periodicity = 0.10f;
        d.consensus = 0.0f;
        d.supportCount = 1;
        d.directSupportCount = 1;
        d.freshSupportMask = 0x01;
        return d;
    };
    bool prematureOctaveCommit = false;
    for (int hop = 0; hop < 23; ++hop)
    {
        auto d = makeOctaveChallenger();
        prematureOctaveCommit = octavePersistenceTracker->confirmOctaveTransition(d, false)
            || prematureOctaveCommit;
    }
    auto finalOctave = makeOctaveChallenger();
    const bool finiteOctaveCommit = octavePersistenceTracker->confirmOctaveTransition(
        finalOctave, false);
    success &= check(!prematureOctaveCommit && finiteOctaveCommit,
                     "single_family_octave_requires_strict_finite_persistence");

    // TRANSITION_IS_TRANSPORT_V1: controller motion is strictly monotonic and
    // cannot overshoot/bounce while a transient is being carried to the new
    // destination. This removes one upstream source of long-vowel pumping.
    ModernPitchEngine::CorrectionState monotonicTransition;
    monotonicTransition.targetValid = true;
    monotonicTransition.noteBodyLatched = true;
    monotonicTransition.trackingState = ModernPitchEngine::TrackingState::transition;
    monotonicTransition.currentCents = -80.0;
    monotonicTransition.desiredCents = 25.0;
    monotonicTransition.responseMs = 5.0;
    double previousMonotonic = monotonicTransition.currentCents;
    bool monotonic = true;
    bool overshot = false;
    for (int sample = 0; sample < 400; ++sample)
    {
        const double value = engine->advanceCorrection(monotonicTransition);
        if (value + 1.0e-12 < previousMonotonic)
            monotonic = false;
        if (value > monotonicTransition.desiredCents + 1.0e-9)
            overshot = true;
        previousMonotonic = value;
    }
    success &= check(monotonic && !overshot,
                     "transition_controller_is_monotonic_without_ratio_bounce");


    // LATENT_SCALE_CANDIDATE_V2: a grey-zone measurement may help identify the
    // next note, but until it persists the audible note is exactly the previous
    // owned degree. This is the C -> uncertain C -> stable D contract.
    ModernPitchEngine::ScaleQuantizer latentQuantizer;
    latentQuantizer.reset();
    latentQuantizer.setScale(authorityChromatic.data(),
                             static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState latentState;
    auto latentC = strongPitch(440.0f);
    latentC.audioPresent = true;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(latentState, latentQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double latentCTarget = latentState.targetLog2;
    const double latentCTransport = latentState.transportPeriodHz;

    ModernPitchEngine::PitchObservation greyD;
    greyD.audioPresent = true;
    greyD.valid = false;
    greyD.measurementAvailable = true;
    greyD.correctionFrequencyHz = 493.8833f;
    greyD.confidence = 0.06f;
    greyD.periodicity = 0.20f;
    greyD.detectorSupport = 1;
    for (int hop = 0; hop < 5; ++hop)
    {
        engine->updateCorrectionState(latentState, latentQuantizer,
                                      greyD, explicitAuthorityParameters);
    }
    // A deep provisional degree is analysis-only until its identity commits.
    // The scale target and audible source transport both remain owned by the
    // current degree; there is no 3-hop catch-up toward an uncommitted F0.
    success &= check(std::abs(latentState.targetLog2 - latentCTarget) < 1.0e-12
                     && std::abs(latentState.transportPeriodHz - latentCTransport) < 1.0e-12,
                     "uncertain_new_degree_cannot_move_owned_transport_before_commit");

    engine->updateCorrectionState(latentState, latentQuantizer,
                                  greyD, explicitAuthorityParameters);
    const double latentDHz = std::exp2(latentState.targetLog2);
    success &= check(latentDHz > 492.0 && latentDHz < 496.0,
                     "persistent_grey_voice_can_commit_new_scale_degree");

    // A single provisional octave family can persist indefinitely without
    // stealing the register. It must become trusted/corroborated first.
    ModernPitchEngine::ScaleQuantizer provisionalOctaveQuantizer;
    provisionalOctaveQuantizer.reset();
    provisionalOctaveQuantizer.setScale(authorityChromatic.data(),
                                         static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState provisionalOctaveState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(provisionalOctaveState,
                                      provisionalOctaveQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double provisionalOctaveTarget = provisionalOctaveState.targetLog2;
    const double provisionalOctaveTransport = provisionalOctaveState.transportPeriodHz;
    const double provisionalOctaveDesired = provisionalOctaveState.desiredCents;
    ModernPitchEngine::PitchObservation greyOctave = greyD;
    greyOctave.correctionFrequencyHz = 880.0f;
    greyOctave.detectorSupport = 1;
    for (int hop = 0; hop < 80; ++hop)
        engine->updateCorrectionState(provisionalOctaveState,
                                      provisionalOctaveQuantizer,
                                      greyOctave, explicitAuthorityParameters);
    success &= check(std::abs(provisionalOctaveState.targetLog2
                              - provisionalOctaveTarget) < 1.0e-12
                     && std::abs(provisionalOctaveState.transportPeriodHz
                                 - provisionalOctaveTransport) < 1.0e-12
                     && std::abs(provisionalOctaveState.desiredCents
                                 - provisionalOctaveDesired) < 1.0e-12,
                     "single_family_provisional_octave_never_owns_register");

    // Grey-zone motion inside the already owned scale cell is allowed to refine
    // only the local source coordinate. This removes vibrato residual without
    // giving uncertainty permission to change note identity.
    ModernPitchEngine::ScaleQuantizer greyLocalQuantizer;
    greyLocalQuantizer.reset();
    greyLocalQuantizer.setScale(authorityChromatic.data(),
                                static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState greyLocalState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(greyLocalState, greyLocalQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double greyLocalTarget = greyLocalState.targetLog2;
    const double greyLocalBefore = greyLocalState.transportPeriodHz;
    ModernPitchEngine::PitchObservation greySameCell;
    greySameCell.audioPresent = true;
    greySameCell.valid = false;
    greySameCell.measurementAvailable = true;
    greySameCell.correctionFrequencyHz = static_cast<float>(
        440.0 * std::exp2(18.0 / 1200.0));
    greySameCell.confidence = 0.05f;
    greySameCell.periodicity = 0.18f;
    greySameCell.detectorSupport = 1;
    for (int hop = 0; hop < 8; ++hop)
        engine->updateCorrectionState(greyLocalState, greyLocalQuantizer,
                                      greySameCell, explicitAuthorityParameters);
    success &= check(std::abs(greyLocalState.targetLog2 - greyLocalTarget) < 1.0e-12
                     && std::abs(greyLocalState.transportPeriodHz - greyLocalBefore) > 0.1,
                     "grey_same_cell_measurement_refines_transport_not_identity");

    // TRANSITION_DESTINATION_FROZEN_V2: once D is committed, detector wobble on
    // the way there cannot rewrite source coordinate or desired correction.
    ModernPitchEngine::ScaleQuantizer frozenTransitionQuantizer;
    frozenTransitionQuantizer.reset();
    frozenTransitionQuantizer.setScale(authorityChromatic.data(),
                                        static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState frozenTransitionState;
    auto frozenD = strongPitch(493.8833f);
    frozenD.audioPresent = true;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(frozenTransitionState,
                                      frozenTransitionQuantizer,
                                      frozenD, explicitAuthorityParameters);
    frozenTransitionState.trackingState = ModernPitchEngine::TrackingState::transition;
    frozenTransitionState.responseMs = 8.0;
    frozenTransitionState.currentCents = frozenTransitionState.desiredCents - 40.0;
    const double frozenTarget = frozenTransitionState.targetLog2;
    const double frozenTransport = frozenTransitionState.transportPeriodHz;
    const double frozenDesired = frozenTransitionState.desiredCents;
    auto frozenWobble = frozenD;
    frozenWobble.frequencyHz = 500.0f;
    frozenWobble.correctionFrequencyHz = 500.0f;
    engine->updateCorrectionState(frozenTransitionState,
                                  frozenTransitionQuantizer,
                                  frozenWobble, explicitAuthorityParameters);
    success &= check(std::abs(frozenTransitionState.targetLog2 - frozenTarget) < 1.0e-12
                     && std::abs(frozenTransitionState.transportPeriodHz - frozenTransport) < 1.0e-12
                     && std::abs(frozenTransitionState.desiredCents - frozenDesired) < 1.0e-12,
                     "transition_detector_wobble_cannot_move_destination");

    // VOICE_LABELS_CANNOT_FREEZE_CORRECTION_V1: rich voice labels are
    // allowed to veto note identity, never correction transport for a real F0.
    ModernPitchEngine::Parameters labelTransportParameters = explicitAuthorityParameters;
    labelTransportParameters.amount = 1.0f;
    labelTransportParameters.humanize = 0.0f;
    labelTransportParameters.vibratoPreserve = 0.0f;
    labelTransportParameters.voiceEvidenceValid = true;

    ModernPitchEngine::ScaleQuantizer phoneticTransportQuantizer;
    phoneticTransportQuantizer.reset();
    phoneticTransportQuantizer.setScale(authorityChromatic.data(),
                                         static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState phoneticTransportState;
    auto transportBase = strongPitch(450.0f);
    transportBase.audioPresent = true;
    transportBase.correctionFrequencyHz = 450.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(phoneticTransportState,
                                      phoneticTransportQuantizer,
                                      transportBase,
                                      labelTransportParameters);
    const double phoneticOwnedTarget = phoneticTransportState.targetLog2;

    ModernPitchEngine::Parameters phoneticTransportParameters = labelTransportParameters;
    phoneticTransportParameters.voiceBodyEnergy = 0.66f;
    phoneticTransportParameters.voiceHarmonicity = 0.68f;
    phoneticTransportParameters.voiceSpectralReliability = 0.72f;
    phoneticTransportParameters.voiceBreathiness = 0.24f;
    phoneticTransportParameters.voiceEventStrength = 0.95f;
    auto phoneticMovingF0 = strongPitch(454.0f);
    phoneticMovingF0.audioPresent = true;
    phoneticMovingF0.correctionFrequencyHz = 454.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(phoneticTransportState,
                                      phoneticTransportQuantizer,
                                      phoneticMovingF0,
                                      phoneticTransportParameters);
    const double phoneticTargetHz = std::exp2(phoneticTransportState.targetLog2);
    const double phoneticCorrectedHz = 454.0 * std::exp2(
        phoneticTransportState.desiredCents / 1200.0);
    const double phoneticResidual = std::abs(1200.0 * std::log2(
        phoneticCorrectedHz / phoneticTargetHz));
    success &= check(std::abs(phoneticTransportState.targetLog2
                              - phoneticOwnedTarget) < 1.0e-12
                     && phoneticResidual < 2.0,
                     "phonetic_label_cannot_freeze_owned_correction");

    ModernPitchEngine::ScaleQuantizer absenceTransportQuantizer;
    absenceTransportQuantizer.reset();
    absenceTransportQuantizer.setScale(authorityChromatic.data(),
                                        static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState absenceTransportState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(absenceTransportState,
                                      absenceTransportQuantizer,
                                      transportBase,
                                      labelTransportParameters);
    const double absenceOwnedTarget = absenceTransportState.targetLog2;

    ModernPitchEngine::Parameters absenceTransportParameters = labelTransportParameters;
    absenceTransportParameters.voiceBodyEnergy = 0.10f;
    absenceTransportParameters.voiceHarmonicity = 0.10f;
    absenceTransportParameters.voiceSpectralReliability = 0.10f;
    absenceTransportParameters.voiceBreathiness = 0.88f;
    absenceTransportParameters.voiceEventStrength = 0.10f;
    auto absenceMovingF0 = strongPitch(446.0f);
    // Reproduce the pathological trough directly: the presence bit blinks off,
    // the rich classifier says absence, but the measured F0 is still a small
    // continuous movement of the already-owned source coordinate.
    absenceMovingF0.audioPresent = false;
    absenceMovingF0.correctionFrequencyHz = 446.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(absenceTransportState,
                                      absenceTransportQuantizer,
                                      absenceMovingF0,
                                      absenceTransportParameters);
    const double absenceTargetHz = std::exp2(absenceTransportState.targetLog2);
    const double absenceCorrectedHz = 446.0 * std::exp2(
        absenceTransportState.desiredCents / 1200.0);
    const double absenceResidual = std::abs(1200.0 * std::log2(
        absenceCorrectedHz / absenceTargetHz));
    success &= check(std::abs(absenceTransportState.targetLog2
                              - absenceOwnedTarget) < 1.0e-12
                     && absenceResidual < 2.0,
                     "absence_label_cannot_freeze_owned_correction");
    success &= check(absenceResidual < 2.0,
                     "tremolo_presence_blink_cannot_create_dry_like_escape");

    // UNCOMMITTED_LARGE_INNOVATION_ZERO_TRANSPORT_AUTHORITY_V1: a repeated
    // grey-zone jump may accumulate identity evidence, but before commit it may
    // not drag the audible source coordinate. This directly covers the measured
    // long-vowel failure where 3 repeated bad F0 frames produced 100-300 cent
    // output excursions.
    ModernPitchEngine::ScaleQuantizer largeInnovationQuantizer;
    largeInnovationQuantizer.reset();
    largeInnovationQuantizer.setScale(authorityChromatic.data(),
                                       static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState largeInnovationState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(largeInnovationState,
                                      largeInnovationQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double largeInnovationTarget = largeInnovationState.targetLog2;
    const double largeInnovationTransport = largeInnovationState.transportPeriodHz;
    ModernPitchEngine::PitchObservation greyLargeInnovation = greyD;
    greyLargeInnovation.correctionFrequencyHz = static_cast<float>(
        440.0 * std::exp2(240.0 / 1200.0));
    for (int hop = 0; hop < 5; ++hop)
        engine->updateCorrectionState(largeInnovationState,
                                      largeInnovationQuantizer,
                                      greyLargeInnovation,
                                      explicitAuthorityParameters);
    success &= check(std::abs(largeInnovationState.targetLog2
                              - largeInnovationTarget) < 1.0e-12
                     && std::abs(largeInnovationState.transportPeriodHz
                                 - largeInnovationTransport) < 1.0e-12,
                     "uncommitted_large_innovation_has_zero_transport_authority");

    // DEGREE_RELATIVE_TRANSPORT_GATE_V1: the exact same 12-cent-per-hop source
    // movement is local motion inside a 200-cent diatonic gap, but is already a
    // cross-degree event in 75-EDO (~16 cents/degree). Geometry, not an absolute
    // cents threshold, decides which path is allowed to refine transport.
    std::array<double, 75> edo75Scale {};
    for (int degree = 0; degree < 75; ++degree)
        edo75Scale[static_cast<std::size_t>(degree)] = std::exp2(
            static_cast<double>(degree) / 75.0);
    const std::array<double, 7> diatonicScale {
        1.0,
        std::exp2(2.0 / 12.0),
        std::exp2(4.0 / 12.0),
        std::exp2(5.0 / 12.0),
        std::exp2(7.0 / 12.0),
        std::exp2(9.0 / 12.0),
        std::exp2(11.0 / 12.0)
    };
    ModernPitchEngine::ScaleQuantizer edo75Quantizer;
    ModernPitchEngine::ScaleQuantizer diatonicQuantizer;
    edo75Quantizer.reset();
    diatonicQuantizer.reset();
    edo75Quantizer.setScale(edo75Scale.data(), static_cast<int>(edo75Scale.size()), 440.0);
    diatonicQuantizer.setScale(diatonicScale.data(), static_cast<int>(diatonicScale.size()), 440.0);
    ModernPitchEngine::CorrectionState edo75State;
    ModernPitchEngine::CorrectionState diatonicState;
    for (int hop = 0; hop < 12; ++hop)
    {
        engine->updateCorrectionState(edo75State, edo75Quantizer,
                                      latentC, explicitAuthorityParameters);
        engine->updateCorrectionState(diatonicState, diatonicQuantizer,
                                      latentC, explicitAuthorityParameters);
    }
    const double edo75TransportBefore = edo75State.transportPeriodHz;
    const double diatonicTransportBefore = diatonicState.transportPeriodHz;
    auto twelveCentTrajectory = strongPitch(440.0f);
    twelveCentTrajectory.audioPresent = true;
    twelveCentTrajectory.correctionFrequencyHz = static_cast<float>(
        440.0 * std::exp2(12.0 / 1200.0));
    engine->updateCorrectionState(edo75State, edo75Quantizer,
                                  twelveCentTrajectory, explicitAuthorityParameters);
    engine->updateCorrectionState(diatonicState, diatonicQuantizer,
                                  twelveCentTrajectory, explicitAuthorityParameters);
    success &= check(std::abs(edo75State.transportPeriodHz - edo75TransportBefore) < 1.0e-12
                     && diatonicState.transportPeriodHz > diatonicTransportBefore,
                     "transport_gate_scales_with_actual_adjacent_degree_width");

    // TARGET_REMAINS_DESTINATION_V1: Response changes only convergence speed.
    // It may never change target identity or desired correction depth. Amount is
    // the explicit depth control, and even when softened it remains inside the
    // target-owned local cell rather than restoring dry authority.
    ModernPitchEngine::Parameters fastAuthority = explicitAuthorityParameters;
    ModernPitchEngine::Parameters slowAuthority = explicitAuthorityParameters;
    fastAuthority.amount = 1.0f;
    slowAuthority.amount = 1.0f;
    fastAuthority.humanize = 0.0f;
    slowAuthority.humanize = 0.0f;
    fastAuthority.vibratoPreserve = 0.0f;
    slowAuthority.vibratoPreserve = 0.0f;
    fastAuthority.retuneTimeMs = 0.0f;
    slowAuthority.retuneTimeMs = 250.0f;
    ModernPitchEngine::ScaleQuantizer fastAuthorityQuantizer;
    ModernPitchEngine::ScaleQuantizer slowAuthorityQuantizer;
    fastAuthorityQuantizer.reset();
    slowAuthorityQuantizer.reset();
    fastAuthorityQuantizer.setScale(authorityChromatic.data(),
                                     static_cast<int>(authorityChromatic.size()), 440.0);
    slowAuthorityQuantizer.setScale(authorityChromatic.data(),
                                     static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState fastAuthorityState;
    ModernPitchEngine::CorrectionState slowAuthorityState;
    for (int hop = 0; hop < 12; ++hop)
    {
        engine->updateCorrectionState(fastAuthorityState, fastAuthorityQuantizer,
                                      latentC, fastAuthority);
        engine->updateCorrectionState(slowAuthorityState, slowAuthorityQuantizer,
                                      latentC, slowAuthority);
    }
    auto authorityMove = strongPitch(452.0f);
    authorityMove.audioPresent = true;
    authorityMove.correctionFrequencyHz = 452.0f;
    engine->updateCorrectionState(fastAuthorityState, fastAuthorityQuantizer,
                                  authorityMove, fastAuthority);
    engine->updateCorrectionState(slowAuthorityState, slowAuthorityQuantizer,
                                  authorityMove, slowAuthority);
    success &= check(std::abs(fastAuthorityState.targetLog2
                              - slowAuthorityState.targetLog2) < 1.0e-12
                     && std::abs(fastAuthorityState.desiredCents
                                 - slowAuthorityState.desiredCents) < 1.0e-9
                     && slowAuthorityState.responseMs > fastAuthorityState.responseMs,
                     "response_changes_time_not_target_authority");

    ModernPitchEngine::Parameters softAmountAuthority = fastAuthority;
    softAmountAuthority.amount = 0.0f;
    ModernPitchEngine::ScaleQuantizer softAmountQuantizer;
    softAmountQuantizer.reset();
    softAmountQuantizer.setScale(authorityChromatic.data(),
                                 static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState softAmountState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(softAmountState, softAmountQuantizer,
                                      latentC, softAmountAuthority);
    // Test Amount on a genuinely local source movement. A single 47-cent
    // jump is intentionally a large uncommitted innovation now, so it would
    // test transport veto rather than Amount. About +20 cents remains inside
    // the chromatic local-motion gate and therefore exposes only depth control.
    auto softAmountMove = strongPitch(445.0f);
    softAmountMove.audioPresent = true;
    softAmountMove.correctionFrequencyHz = 445.0f;
    engine->updateCorrectionState(softAmountState, softAmountQuantizer,
                                  softAmountMove, softAmountAuthority);
    const double softAmountTargetHz = std::exp2(softAmountState.targetLog2);
    const double softAmountOutputHz = softAmountState.transportPeriodHz
        * std::exp2(softAmountState.desiredCents / 1200.0);
    const double softAmountResidual = std::abs(1200.0 * std::log2(
        softAmountOutputHz / softAmountTargetHz));
    success &= check(std::abs(softAmountState.targetLog2
                              - fastAuthorityState.targetLog2) < 1.0e-12
                     && std::abs(softAmountState.desiredCents) > 0.1
                     && softAmountResidual < 34.1,
                     "amount_softens_inside_target_cell_never_restores_dry_authority");

    // TERMINAL_TAIL_KEEPS_OWNED_DEGREE_V1: a degrading falling tail is
    // still part of the previously owned note. Its physical F0 may continue to
    // move so correction can cancel that motion, but it may not nominate lower
    // scale degrees until positive structured note evidence returns.
    ModernPitchEngine::Parameters fallingTailParameters = explicitAuthorityParameters;
    fallingTailParameters.voiceEvidenceValid = true;
    fallingTailParameters.voiceBodyEnergy = 0.34f;
    fallingTailParameters.voiceHarmonicity = 0.32f;
    fallingTailParameters.voiceSpectralReliability = 0.34f;
    fallingTailParameters.voiceBreathiness = 0.48f;
    fallingTailParameters.voiceEventStrength = 0.08f;
    ModernPitchEngine::ScaleQuantizer fallingTailQuantizer;
    fallingTailQuantizer.reset();
    fallingTailQuantizer.setScale(authorityChromatic.data(),
                                  static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState fallingTailState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(fallingTailState, fallingTailQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double fallingTailOwnedTarget = fallingTailState.targetLog2;
    const double fallingTailStartTransport = fallingTailState.transportPeriodHz;
    double fallingTailRawHz = 440.0;
    for (int cents = -10; cents >= -110; cents -= 10)
    {
        fallingTailRawHz = 440.0 * std::exp2(static_cast<double>(cents) / 1200.0);
        auto tailHop = strongPitch(static_cast<float>(fallingTailRawHz));
        tailHop.audioPresent = true;
        tailHop.correctionFrequencyHz = tailHop.frequencyHz;
        engine->updateCorrectionState(fallingTailState, fallingTailQuantizer,
                                      tailHop, fallingTailParameters);
    }
    const double fallingTailTargetHz = std::exp2(fallingTailState.targetLog2);
    const double fallingTailOutputHz = fallingTailRawHz * std::exp2(
        fallingTailState.desiredCents / 1200.0);
    const double fallingTailResidual = std::abs(1200.0 * std::log2(
        fallingTailOutputHz / fallingTailTargetHz));
    success &= check(std::abs(fallingTailState.targetLog2
                              - fallingTailOwnedTarget) < 1.0e-12
                     && fallingTailState.transportPeriodHz
                        < fallingTailStartTransport * std::exp2(-45.0 / 1200.0)
                     && fallingTailResidual < 3.0,
                     "falling_terminal_tail_stays_on_previous_degree_and_remains_corrected");

    // The rule is direction symmetric: a weakening rising tail also belongs to
    // the previous degree instead of earning an upward target revision.
    ModernPitchEngine::ScaleQuantizer risingTailQuantizer;
    risingTailQuantizer.reset();
    risingTailQuantizer.setScale(authorityChromatic.data(),
                                 static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState risingTailState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(risingTailState, risingTailQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double risingTailOwnedTarget = risingTailState.targetLog2;
    double risingTailRawHz = 440.0;
    for (int cents = 10; cents <= 110; cents += 10)
    {
        risingTailRawHz = 440.0 * std::exp2(static_cast<double>(cents) / 1200.0);
        auto tailHop = strongPitch(static_cast<float>(risingTailRawHz));
        tailHop.audioPresent = true;
        tailHop.correctionFrequencyHz = tailHop.frequencyHz;
        engine->updateCorrectionState(risingTailState, risingTailQuantizer,
                                      tailHop, fallingTailParameters);
    }
    const double risingTailTargetHz = std::exp2(risingTailState.targetLog2);
    const double risingTailOutputHz = risingTailRawHz * std::exp2(
        risingTailState.desiredCents / 1200.0);
    const double risingTailResidual = std::abs(1200.0 * std::log2(
        risingTailOutputHz / risingTailTargetHz));
    success &= check(std::abs(risingTailState.targetLog2
                              - risingTailOwnedTarget) < 1.0e-12
                     && risingTailResidual < 3.0,
                     "rising_terminal_tail_stays_on_previous_degree_and_remains_corrected");

    // Tail ownership is not a permanent lock. As soon as a genuinely structured
    // new note appears, ordinary scale nomination/commit resumes and the next
    // exact degree may own the output.
    ModernPitchEngine::Parameters recoveredNoteParameters = explicitAuthorityParameters;
    setBodyEvidence(recoveredNoteParameters);
    const double lowerChromaticHz = 440.0 * std::exp2(-100.0 / 1200.0);
    auto recoveredLowerNote = strongPitch(static_cast<float>(lowerChromaticHz));
    recoveredLowerNote.audioPresent = true;
    recoveredLowerNote.correctionFrequencyHz = recoveredLowerNote.frequencyHz;
    for (int hop = 0; hop < 8; ++hop)
        engine->updateCorrectionState(fallingTailState, fallingTailQuantizer,
                                      recoveredLowerNote, recoveredNoteParameters);
    const double recoveredTargetHz = std::exp2(fallingTailState.targetLog2);
    success &= check(recoveredTargetHz > 414.0 && recoveredTargetHz < 417.0,
                     "strong_new_note_after_terminal_tail_can_commit_normally");

    // VOICE_AWARE_F0_FRONTEND_V1: colored breath/noise is signal, but it is
    // not an F0. Presence must never manufacture a pitch measurement.
    auto breathNoiseTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    breathNoiseTracker->prepare(48000.0);
    breathNoiseTracker->setRange(70.0f, 700.0f);
    std::uint32_t breathRng = 0x12345678u;
    float breathFilter = 0.0f;
    int breathValidF0 = 0;
    int breathMeasurements = 0;
    for (int sample = 0; sample < 24000; ++sample)
    {
        breathRng ^= breathRng << 13;
        breathRng ^= breathRng >> 17;
        breathRng ^= breathRng << 5;
        const float white = (static_cast<float>(breathRng & 0xffffu) / 32767.5f) - 1.0f;
        breathFilter = 0.91f * breathFilter + 0.09f * white;
        const float breath = 0.030f * (0.62f * white + 0.38f * breathFilter);
        ModernPitchEngine::PitchObservation observed;
        if (breathNoiseTracker->processSample(breath, observed) && sample > 4096)
        {
            breathValidF0 += observed.valid ? 1 : 0;
            breathMeasurements += observed.measurementAvailable ? 1 : 0;
        }
    }
    std::cerr << "voice_aware_breath_valid_f0=" << breathValidF0
              << " provisional=" << breathMeasurements << '\n';
    success &= check(breathValidF0 == 0 && breathMeasurements <= 2,
                     "colored_breath_is_not_promoted_to_f0");

    // A real low-SNR vocal tone embedded in the same colored noise must remain
    // measurable: the detector rejects noise, not difficult voices.
    auto noisyVoiceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    noisyVoiceTracker->prepare(48000.0);
    noisyVoiceTracker->setRange(70.0f, 700.0f);
    std::uint32_t voiceRng = 0x9e3779b9u;
    float voiceNoiseFilter = 0.0f;
    int nearToneCount = 0;
    int nearHalfToneCount = 0;
    int nearDoubleToneCount = 0;
    int voicedDecisionCount = 0;
    for (int sample = 0; sample < 30000; ++sample)
    {
        voiceRng ^= voiceRng << 13;
        voiceRng ^= voiceRng >> 17;
        voiceRng ^= voiceRng << 5;
        const float white = (static_cast<float>(voiceRng & 0xffffu) / 32767.5f) - 1.0f;
        voiceNoiseFilter = 0.91f * voiceNoiseFilter + 0.09f * white;
        const float noise = 0.024f * (0.60f * white + 0.40f * voiceNoiseFilter);
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * 220.0 * static_cast<double>(sample) / 48000.0);
        // Fundamental + weak second/third harmonic approximates a real source
        // family without making the test unrealistically clean.
        const float voice = 0.050f * std::sin(phase)
                          + 0.017f * std::sin(2.0f * phase + 0.31f)
                          + 0.009f * std::sin(3.0f * phase + 0.73f);
        ModernPitchEngine::PitchObservation observed;
        if (noisyVoiceTracker->processSample(voice + noise, observed)
            && sample > 8000 && observed.valid)
        {
            ++voicedDecisionCount;
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / 220.0f));
            if (cents < 45.0f)
                ++nearToneCount;
            const float halfCents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / 110.0f));
            const float doubleCents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / 440.0f));
            if (halfCents < 45.0f)
                ++nearHalfToneCount;
            if (doubleCents < 45.0f)
                ++nearDoubleToneCount;
        }
    }
    std::cerr << "voice_aware_noisy_voice_decisions=" << voicedDecisionCount
              << " near_220=" << nearToneCount
              << " near_110=" << nearHalfToneCount
              << " near_440=" << nearDoubleToneCount << '\n';
    success &= check(voicedDecisionCount > 20
                     && nearToneCount * 4 >= voicedDecisionCount * 3,
                     "low_snr_vocal_family_isolated_from_colored_noise");

    const auto measureSteadyDetectorHz = [](float sourceHz)
    {
        auto tracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
        tracker->prepare(48000.0);
        tracker->setRange(70.0f, 900.0f);
        double sumHz = 0.0;
        int count = 0;
        for (int sample = 0; sample < 24000; ++sample)
        {
            const double phase = 2.0 * 3.14159265358979323846
                * static_cast<double>(sourceHz) * static_cast<double>(sample) / 48000.0;
            ModernPitchEngine::PitchObservation observed;
            if (tracker->processSample(0.08f * static_cast<float>(std::sin(phase)), observed)
                && sample > 8000 && observed.valid)
            {
                sumHz += observed.correctionFrequencyHz;
                ++count;
            }
        }
        return std::pair<float, int> {
            count > 0 ? static_cast<float>(sumHz / static_cast<double>(count)) : 0.0f,
            count
        };
    };
    const auto [boundary450Hz, boundary450Count] = measureSteadyDetectorHz(450.0f);
    const auto [boundary456Hz, boundary456Count] = measureSteadyDetectorHz(456.0f);
    const float boundary450Cents = boundary450Hz > 0.0f
        ? 1200.0f * std::log2(boundary450Hz / 450.0f) : 9999.0f;
    const float boundary456Cents = boundary456Hz > 0.0f
        ? 1200.0f * std::log2(boundary456Hz / 456.0f) : 9999.0f;
    std::cerr << "voice_aware_boundary_450_hz=" << boundary450Hz
              << " cents=" << boundary450Cents
              << " decisions=" << boundary450Count
              << " boundary_456_hz=" << boundary456Hz
              << " cents=" << boundary456Cents
              << " decisions=" << boundary456Count << '\n';
    success &= check(boundary450Count > 20 && std::abs(boundary450Cents) < 18.0f
                     && boundary456Count > 20 && std::abs(boundary456Cents) < 18.0f,
                     "voice_detector_preserves_scale_boundary_accuracy");

    // PATH_ROLE_SPLIT_V1 regression: the low-rate path is useful for pitch
    // geometry, but the full/half-rate observations carry more information about
    // whether that geometry was extracted from clean tonal voice.
    auto pathRoleTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    pathRoleTracker->prepare(48000.0);
    success &= check(pathRoleTracker->pathCleanlinessAuthority(0, 220.0f)
                         > pathRoleTracker->pathCleanlinessAuthority(3, 220.0f)
                     && pathRoleTracker->pathPitchAuthority(2, 90.0f)
                         > pathRoleTracker->pathPitchAuthority(0, 90.0f),
                     "multirate_paths_split_pitch_and_cleanliness_authority");

    // Resonant/formant-shaped noise: two narrow resonances are deliberately
    // capable of producing autocorrelation peaks, but there is no harmonic vocal
    // family and therefore no trustworthy F0.
    auto formantNoiseTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    formantNoiseTracker->prepare(48000.0);
    formantNoiseTracker->setRange(70.0f, 700.0f);
    std::uint32_t formantRng = 0x9e3779b9u;
    float f1y1 = 0.0f, f1y2 = 0.0f;
    float f2y1 = 0.0f, f2y2 = 0.0f;
    int formantNoiseValid = 0;
    int formantNoiseMeasurements = 0;
    const float r1 = 0.965f;
    const float r2 = 0.955f;
    const float c1 = 2.0f * r1 * std::cos(2.0f * 3.14159265358979323846f * 720.0f / 48000.0f);
    const float c2 = 2.0f * r2 * std::cos(2.0f * 3.14159265358979323846f * 1280.0f / 48000.0f);
    for (int sample = 0; sample < 32000; ++sample)
    {
        formantRng ^= formantRng << 13;
        formantRng ^= formantRng >> 17;
        formantRng ^= formantRng << 5;
        const float noise = (static_cast<float>(formantRng & 0xffffu) / 32767.5f - 1.0f) * 0.006f;
        const float y1 = noise + c1 * f1y1 - r1 * r1 * f1y2;
        const float y2 = noise + c2 * f2y1 - r2 * r2 * f2y2;
        f1y2 = f1y1; f1y1 = y1;
        f2y2 = f2y1; f2y1 = y2;
        const float shaped = std::clamp(0.020f * (y1 + 0.65f * y2), -0.20f, 0.20f);
        ModernPitchEngine::PitchObservation observed;
        if (formantNoiseTracker->processSample(shaped, observed))
        {
            const auto dump = [](const auto& slot)
            {
                const auto& c = slot.candidate;
                return std::array<float, 7> {
                    c.frequencyHz,
                    c.confidence,
                    c.periodicity,
                    c.harmonicFamily,
                    c.tonalCleanliness,
                    static_cast<float>(slot.ageInHops),
                    c.valid ? 1.0f : 0.0f
                };
            };
            if (observed.measurementAvailable)
            {
                ++formantNoiseMeasurements;
                const auto full = dump(formantNoiseTracker->fullRateCandidate_);
                const auto half = dump(formantNoiseTracker->halfRateCandidate_);
                const auto quarter = dump(formantNoiseTracker->quarterRateCandidate_);
                const auto eighth = dump(formantNoiseTracker->eighthRateCandidate_);
                std::cerr << "FORMANT_COORD sample=" << sample
                          << " valid=" << (observed.valid ? 1 : 0)
                          << " out_hz=" << observed.correctionFrequencyHz
                          << " support=" << observed.detectorSupport
                          << " consensus=" << observed.consensus
                          << " confidence=" << observed.confidence
                          << " periodicity=" << observed.periodicity
                          << " tracked=" << formantNoiseTracker->trackedPitchHz_
                          << " invalid_hops=" << formantNoiseTracker->invalidHopCount_
                          << " full=" << full[0] << ',' << full[1] << ',' << full[2]
                          << ',' << full[3] << ',' << full[4] << ',' << full[5] << ',' << full[6]
                          << " half=" << half[0] << ',' << half[1] << ',' << half[2]
                          << ',' << half[3] << ',' << half[4] << ',' << half[5] << ',' << half[6]
                          << " quarter=" << quarter[0] << ',' << quarter[1] << ',' << quarter[2]
                          << ',' << quarter[3] << ',' << quarter[4] << ',' << quarter[5] << ',' << quarter[6]
                          << " eighth=" << eighth[0] << ',' << eighth[1] << ',' << eighth[2]
                          << ',' << eighth[3] << ',' << eighth[4] << ',' << eighth[5] << ',' << eighth[6]
                          << '\n';
            }
            if (observed.valid)
                ++formantNoiseValid;
        }
    }
    const auto formantDumpSlot = [](const auto& slot)
    {
        const auto& c = slot.candidate;
        return std::array<float, 5> { c.frequencyHz, c.confidence, c.periodicity,
                                     c.harmonicFamily, c.tonalCleanliness };
    };
    const auto formantFull = formantDumpSlot(formantNoiseTracker->fullRateCandidate_);
    const auto formantHalf = formantDumpSlot(formantNoiseTracker->halfRateCandidate_);
    const auto formantQuarter = formantDumpSlot(formantNoiseTracker->quarterRateCandidate_);
    const auto formantEighth = formantDumpSlot(formantNoiseTracker->eighthRateCandidate_);
    std::cerr << "voice_aware_formant_noise_valid=" << formantNoiseValid
              << " provisional=" << formantNoiseMeasurements
              << " full=" << formantFull[0] << ',' << formantFull[3] << ',' << formantFull[4]
              << " half=" << formantHalf[0] << ',' << formantHalf[3] << ',' << formantHalf[4]
              << " quarter=" << formantQuarter[0] << ',' << formantQuarter[3] << ',' << formantQuarter[4]
              << " eighth=" << formantEighth[0] << ',' << formantEighth[3] << ',' << formantEighth[4]
              << '\n';
    success &= check(formantNoiseValid == 0 && formantNoiseMeasurements <= 4,
                     "formant_shaped_noise_never_becomes_vocal_f0");

    // RAPID_F0_OBSERVATION_MUST_PRECEDE_HOLD_V1
    // Hold can only act after the detector has actually observed the new physical
    // coordinate. Measure detector latency directly, before any quantizer logic.
    struct RapidF0Metrics
    {
        int firstInitialLockSample = -1;
        int firstNewLockSample = -1;
        float lastInitialHz = 0.0f;
        float firstNewHz = 0.0f;
    };

    const auto measureRapidStep = [](float initialHz, float newHz,
                                     int stepSample, int totalSamples)
    {
        RapidF0Metrics metrics;
        auto tracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
        tracker->prepare(48000.0);
        tracker->setRange(70.0f, 900.0f);
        double phase = 0.0;
        for (int sample = 0; sample < totalSamples; ++sample)
        {
            const float sourceHz = sample < stepSample ? initialHz : newHz;
            phase += 2.0 * 3.14159265358979323846
                   * static_cast<double>(sourceHz) / 48000.0;
            if (phase > 2.0 * 3.14159265358979323846)
                phase -= 2.0 * 3.14159265358979323846;

            ModernPitchEngine::PitchObservation observed;
            if (!tracker->processSample(0.08f * static_cast<float>(std::sin(phase)), observed))
                continue;

            const auto near = [](float measured, float expected, float toleranceCents)
            {
                return measured > 0.0f && expected > 0.0f
                    && std::abs(1200.0f * std::log2(measured / expected)) < toleranceCents;
            };

            if (sample < stepSample && observed.valid
                && near(observed.correctionFrequencyHz, initialHz, 18.0f))
            {
                if (metrics.firstInitialLockSample < 0)
                    metrics.firstInitialLockSample = sample;
                metrics.lastInitialHz = observed.correctionFrequencyHz;
            }
            else if (sample >= stepSample && observed.valid
                     && near(observed.correctionFrequencyHz, newHz, 18.0f))
            {
                if (metrics.firstNewLockSample < 0)
                {
                    metrics.firstNewLockSample = sample;
                    metrics.firstNewHz = observed.correctionFrequencyHz;
                }
            }
        }
        return metrics;
    };

    const auto boundaryRapid = measureRapidStep(450.0f, 456.0f, 12000, 18000);
    const auto noteRapid = measureRapidStep(220.0f, 246.94165f, 12000, 18000);
    const int boundaryInitialLatency = boundaryRapid.firstInitialLockSample;
    const int boundaryStepLatency = boundaryRapid.firstNewLockSample >= 0
        ? boundaryRapid.firstNewLockSample - 12000 : 999999;
    const int noteInitialLatency = noteRapid.firstInitialLockSample;
    const int noteStepLatency = noteRapid.firstNewLockSample >= 0
        ? noteRapid.firstNewLockSample - 12000 : 999999;

    std::cerr << "voice_aware_rapid_boundary_first_lock_samples="
              << boundaryInitialLatency
              << " step_recognition_samples=" << boundaryStepLatency
              << " initial_hz=" << boundaryRapid.lastInitialHz
              << " new_hz=" << boundaryRapid.firstNewHz << '\\n';
    std::cerr << "voice_aware_rapid_note_first_lock_samples="
              << noteInitialLatency
              << " step_recognition_samples=" << noteStepLatency
              << " initial_hz=" << noteRapid.lastInitialHz
              << " new_hz=" << noteRapid.firstNewHz << '\\n';

    success &= check(boundaryInitialLatency >= 0 && boundaryInitialLatency <= 3000,
                     "boundary_f0_acquires_before_hold_needs_it");
    success &= check(boundaryStepLatency <= 1536,
                     "small_boundary_f0_change_is_observed_without_prudence_stall");
    success &= check(noteInitialLatency >= 0 && noteInitialLatency <= 3000,
                     "sung_note_f0_acquires_promptly");
    success &= check(noteStepLatency <= 1024,
                     "rapid_real_note_change_is_observed_before_musical_hold");

    // SOLITARY_VOICE_STRUCTURE_V3 positive control: at 1100 Hz the full-rate
    // detector is the only direct F0 authority. Lower-rate paths may observe
    // exact 1/2 and 1/3 aliases but may not drag a real 1100-Hz source downward.
    auto highVoiceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    highVoiceTracker->prepare(48000.0);
    highVoiceTracker->setRange(45.0f, 1500.0f);
    std::uint32_t highVoiceRng = 0x31415926u;
    float highNoiseLp = 0.0f;
    int highVoiceValid = 0;
    int highVoiceNear = 0;
    int highVoiceNearTwoThirds = 0;
    int highVoiceNearThreeQuarters = 0;
    int highVoiceNearFourThirds = 0;
    int highVoiceTraceCount = 0;
    double highVoiceSumHz = 0.0;
    float highVoiceMinHz = 100000.0f;
    float highVoiceMaxHz = 0.0f;
    constexpr float highVoiceHz = 1100.0f;
    for (int sample = 0; sample < 24000; ++sample)
    {
        highVoiceRng ^= highVoiceRng << 13;
        highVoiceRng ^= highVoiceRng >> 17;
        highVoiceRng ^= highVoiceRng << 5;
        const float white = static_cast<float>(highVoiceRng & 0xffffu) / 32767.5f - 1.0f;
        highNoiseLp = 0.86f * highNoiseLp + 0.14f * white;
        const float noise = 0.008f * (0.72f * white + 0.28f * highNoiseLp);
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * static_cast<double>(highVoiceHz) * static_cast<double>(sample) / 48000.0);
        const float voice = 0.065f * std::sin(phase)
                          + 0.020f * std::sin(2.0f * phase + 0.17f)
                          + 0.010f * std::sin(3.0f * phase + 0.41f);
        ModernPitchEngine::PitchObservation observed;
        if (highVoiceTracker->processSample(voice + noise, observed)
            && sample > 6000 && observed.valid)
        {
            ++highVoiceValid;
            const float outHz = observed.correctionFrequencyHz;
            highVoiceSumHz += outHz;
            highVoiceMinHz = std::min(highVoiceMinHz, outHz);
            highVoiceMaxHz = std::max(highVoiceMaxHz, outHz);
            const auto nearHz = [outHz](float referenceHz)
            {
                return std::abs(1200.0f * std::log2(
                    std::max(1.0f, outHz) / referenceHz)) < 45.0f;
            };
            if (nearHz(highVoiceHz))
                ++highVoiceNear;
            if (nearHz(highVoiceHz * (2.0f / 3.0f)))
                ++highVoiceNearTwoThirds;
            if (nearHz(highVoiceHz * 0.75f))
                ++highVoiceNearThreeQuarters;
            if (nearHz(highVoiceHz * (4.0f / 3.0f)))
                ++highVoiceNearFourThirds;

            if (highVoiceTraceCount < 8)
            {
                ++highVoiceTraceCount;
                const auto& f = highVoiceTracker->fullRateCandidate_.candidate;
                const auto& h = highVoiceTracker->halfRateCandidate_.candidate;
                const auto& q = highVoiceTracker->quarterRateCandidate_.candidate;
                const auto& e = highVoiceTracker->eighthRateCandidate_.candidate;
                std::cerr << "HIGH1100_GEOMETRY sample=" << sample
                          << " out_hz=" << outHz
                          << " support=" << observed.detectorSupport
                          << " consensus=" << observed.consensus
                          << " full=" << f.frequencyHz << ',' << f.confidence << ','
                          << f.periodicity << ',' << f.harmonicFamily << ','
                          << f.tonalCleanliness << ',' << (f.valid ? 1 : 0)
                          << " half=" << h.frequencyHz << ',' << h.confidence << ','
                          << h.periodicity << ',' << h.harmonicFamily << ','
                          << h.tonalCleanliness << ',' << (h.valid ? 1 : 0)
                          << " quarter=" << q.frequencyHz << ',' << q.confidence << ','
                          << q.periodicity << ',' << q.harmonicFamily << ','
                          << q.tonalCleanliness << ',' << (q.valid ? 1 : 0)
                          << " eighth=" << e.frequencyHz << ',' << e.confidence << ','
                          << e.periodicity << ',' << e.harmonicFamily << ','
                          << e.tonalCleanliness << ',' << (e.valid ? 1 : 0)
                          << '\n';
            }
        }
    }
    std::cerr << "solitary_high_voice_valid=" << highVoiceValid
              << " near_1100=" << highVoiceNear
              << " near_733=" << highVoiceNearTwoThirds
              << " near_825=" << highVoiceNearThreeQuarters
              << " near_1467=" << highVoiceNearFourThirds
              << " mean_hz=" << (highVoiceValid > 0
                    ? highVoiceSumHz / static_cast<double>(highVoiceValid) : 0.0)
              << " minmax_hz=" << (highVoiceValid > 0 ? highVoiceMinHz : 0.0f)
              << ',' << highVoiceMaxHz << '\n';
    success &= check(highVoiceValid > 20
                     && highVoiceNear * 4 >= highVoiceValid * 3,
                     "solitary_full_rate_real_voice_survives_structure_veto");

    // DIRECT_HIGH_PATH_OWNS_RATIONAL_ALIAS_V2 negative control: the resolver
    // must not double a genuine 550-Hz source merely because 1100 Hz is a strong
    // harmonic. A real 550-Hz first YIN basin remains at 550 and is unaffected.
    auto midHighVoiceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    midHighVoiceTracker->prepare(48000.0);
    midHighVoiceTracker->setRange(45.0f, 1500.0f);
    std::uint32_t midHighRng = 0x27182818u;
    float midHighNoiseLp = 0.0f;
    int midHighValid = 0;
    int midHighNear = 0;
    constexpr float midHighHz = 550.0f;
    for (int sample = 0; sample < 24000; ++sample)
    {
        midHighRng ^= midHighRng << 13;
        midHighRng ^= midHighRng >> 17;
        midHighRng ^= midHighRng << 5;
        const float white = static_cast<float>(midHighRng & 0xffffu) / 32767.5f - 1.0f;
        midHighNoiseLp = 0.86f * midHighNoiseLp + 0.14f * white;
        const float noise = 0.008f * (0.72f * white + 0.28f * midHighNoiseLp);
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * static_cast<double>(midHighHz) * static_cast<double>(sample) / 48000.0);
        const float voice = 0.060f * std::sin(phase)
                          + 0.026f * std::sin(2.0f * phase + 0.23f)
                          + 0.011f * std::sin(3.0f * phase + 0.37f);
        ModernPitchEngine::PitchObservation observed;
        if (midHighVoiceTracker->processSample(voice + noise, observed)
            && sample > 6000 && observed.valid)
        {
            ++midHighValid;
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / midHighHz));
            if (cents < 45.0f)
                ++midHighNear;
        }
    }
    std::cerr << "mid_high_voice_valid=" << midHighValid
              << " near_550=" << midHighNear << '\n';
    success &= check(midHighValid > 20
                     && midHighNear * 4 >= midHighValid * 3,
                     "high_path_octave_resolver_does_not_double_real_550_hz_voice");

    // LOW_RATE_RESONANCE_VETO_V1 positive control: true low F0 remains
    // measurable even though quarter/eighth paths use a stricter cleanliness
    // requirement than full/half rate.
    auto lowVoiceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    lowVoiceTracker->prepare(48000.0);
    lowVoiceTracker->setRange(45.0f, 320.0f);
    constexpr float lowVoiceHz = 60.0f;
    int lowVoiceValid = 0;
    int lowVoiceNearFundamental = 0;
    float lowVoiceQuarterMinClean = 2.0f;
    float lowVoiceQuarterMaxClean = -1.0f;
    float lowVoiceEighthMinClean = 2.0f;
    float lowVoiceEighthMaxClean = -1.0f;
    for (int sample = 0; sample < 36000; ++sample)
    {
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * static_cast<double>(lowVoiceHz) * static_cast<double>(sample) / 48000.0);
        const float voice = 0.075f * std::sin(phase)
                          + 0.024f * std::sin(2.0f * phase + 0.19f)
                          + 0.012f * std::sin(3.0f * phase + 0.43f)
                          + 0.006f * std::sin(4.0f * phase + 0.71f);
        ModernPitchEngine::PitchObservation observed;
        if (lowVoiceTracker->processSample(voice, observed)
            && sample > 12000 && observed.valid)
        {
            ++lowVoiceValid;
            const auto& q = lowVoiceTracker->quarterRateCandidate_.candidate;
            const auto& e = lowVoiceTracker->eighthRateCandidate_.candidate;
            if (q.valid && q.tonalCleanliness >= 0.0f)
            {
                lowVoiceQuarterMinClean = std::min(lowVoiceQuarterMinClean, q.tonalCleanliness);
                lowVoiceQuarterMaxClean = std::max(lowVoiceQuarterMaxClean, q.tonalCleanliness);
            }
            if (e.valid && e.tonalCleanliness >= 0.0f)
            {
                lowVoiceEighthMinClean = std::min(lowVoiceEighthMinClean, e.tonalCleanliness);
                lowVoiceEighthMaxClean = std::max(lowVoiceEighthMaxClean, e.tonalCleanliness);
            }
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / lowVoiceHz));
            if (cents < 45.0f)
                ++lowVoiceNearFundamental;
        }
    }
    std::cerr << "low_rate_voice_valid=" << lowVoiceValid
              << " near_60=" << lowVoiceNearFundamental
              << " quarter_clean_minmax=" << lowVoiceQuarterMinClean << ',' << lowVoiceQuarterMaxClean
              << " eighth_clean_minmax=" << lowVoiceEighthMinClean << ',' << lowVoiceEighthMaxClean
              << '\n';
    success &= check(lowVoiceValid > 20
                     && lowVoiceNearFundamental * 4 >= lowVoiceValid * 3,
                     "low_rate_real_voice_survives_resonance_veto");

    // OBSERVATION_MEMORY_IS_FALSIFIABLE_V1: emulate a stale wrong register
    // continuously supplied as a rescue anchor. Strong current vocal evidence
    // must recover autonomously; changing preset/reset is never required.
    auto staleAnchorTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    staleAnchorTracker->prepare(48000.0);
    staleAnchorTracker->setRange(70.0f, 700.0f);
    staleAnchorTracker->setRescueMode(true);
    staleAnchorTracker->setTransitionWake(true);
    staleAnchorTracker->trackedPitchHz_ = 110.0f;
    staleAnchorTracker->trackedConfidence_ = 0.88f;
    staleAnchorTracker->trackedPeriodicity_ = 0.88f;
    staleAnchorTracker->trackedConsensus_ = 0.70f;
    staleAnchorTracker->decoderBeam_[0].valid = true;
    staleAnchorTracker->decoderBeam_[0].logFrequency = std::log2(110.0);
    staleAnchorTracker->decoderBeam_[0].score = 1.0f;
    int recoveredAtSample = -1;
    int staleNear110 = 0;
    int staleNear220 = 0;
    int staleOther = 0;
    for (int sample = 0; sample < 12000; ++sample)
    {
        staleAnchorTracker->setReacquisitionAnchor(110.0f);
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * 220.0 * static_cast<double>(sample) / 48000.0);
        const float voice = 0.070f * std::sin(phase)
                          + 0.020f * std::sin(2.0f * phase + 0.2f)
                          + 0.010f * std::sin(3.0f * phase + 0.5f);
        ModernPitchEngine::PitchObservation observed;
        if (staleAnchorTracker->processSample(voice, observed) && observed.valid)
        {
            const float hz = std::max(1.0f, observed.correctionFrequencyHz);
            const float cents220 = std::abs(1200.0f * std::log2(hz / 220.0f));
            const float cents110 = std::abs(1200.0f * std::log2(hz / 110.0f));
            if (cents220 < 45.0f)
                ++staleNear220;
            else if (cents110 < 45.0f)
                ++staleNear110;
            else
                ++staleOther;
            if (cents220 < 45.0f)
            {
                recoveredAtSample = sample;
                break;
            }
        }
    }
    const auto staleDumpSlot = [](const auto& slot)
    {
        const auto& c = slot.candidate;
        return std::array<float, 5> { c.frequencyHz, c.confidence, c.periodicity,
                                     c.harmonicFamily, c.tonalCleanliness };
    };
    const auto staleFull = staleDumpSlot(staleAnchorTracker->fullRateCandidate_);
    const auto staleHalf = staleDumpSlot(staleAnchorTracker->halfRateCandidate_);
    const auto staleQuarter = staleDumpSlot(staleAnchorTracker->quarterRateCandidate_);
    const auto staleEighth = staleDumpSlot(staleAnchorTracker->eighthRateCandidate_);
    std::cerr << "stale_recovery_at=" << recoveredAtSample
              << " near110=" << staleNear110
              << " near220=" << staleNear220
              << " other=" << staleOther
              << " tracked=" << staleAnchorTracker->trackedPitchHz_
              << " pending_count=" << staleAnchorTracker->pendingOctaveCount_
              << " pending_hz=" << staleAnchorTracker->pendingOctaveFrequencyHz_
              << " full=" << staleFull[0] << ',' << staleFull[3] << ',' << staleFull[4]
              << " half=" << staleHalf[0] << ',' << staleHalf[3] << ',' << staleHalf[4]
              << " quarter=" << staleQuarter[0] << ',' << staleQuarter[3] << ',' << staleQuarter[4]
              << " eighth=" << staleEighth[0] << ',' << staleEighth[3] << ',' << staleEighth[4]
              << '\n';
    success &= check(recoveredAtSample >= 0 && recoveredAtSample < 6000,
                     "stale_wrong_f0_memory_is_falsified_without_preset_reset");

    // BOUNDARY_PATH_BIAS_DIAGNOSTIC_V1
    // Reproduce exactly the stronger linked input channel used by the frozen
    // hysteresis regression: 450 Hz -> 456 Hz, with the same two harmonics.
    // Measure only fresh path candidates so slower multirate paths are not
    // over-counted through age reuse. Diagnostic only; no production behavior.
    struct BoundaryPathStats
    {
        double sumHz = 0.0;
        float minimumHz = 1.0e9f;
        float maximumHz = 0.0f;
        int freshValid = 0;
        int near456 = 0;
        int aboveHoldBoundary = 0;
    };

    auto boundaryPathTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    boundaryPathTracker->prepare(48000.0);
    boundaryPathTracker->setRange(70.0f, 1200.0f);
    boundaryPathTracker->setSensitivity(0.85f);

    BoundaryPathStats boundaryFull;
    BoundaryPathStats boundaryHalf;
    BoundaryPathStats boundaryQuarter;
    BoundaryPathStats boundaryEighth;
    BoundaryPathStats boundaryOutput;
    const float holdBoundaryHz = static_cast<float>(440.0 * std::exp2(65.0 / 1200.0));
    const auto addBoundaryPath = [holdBoundaryHz](BoundaryPathStats& stats,
                                                   const auto& slot) noexcept
    {
        const auto& candidate = slot.candidate;
        if (slot.ageInHops != 0 || !candidate.valid
            || !std::isfinite(candidate.frequencyHz) || candidate.frequencyHz <= 0.0f)
        {
            return;
        }
        ++stats.freshValid;
        const float cents456 = std::abs(1200.0f * std::log2(candidate.frequencyHz / 456.0f));
        if (cents456 > 80.0f)
            return;
        ++stats.near456;
        stats.sumHz += candidate.frequencyHz;
        stats.minimumHz = std::min(stats.minimumHz, candidate.frequencyHz);
        stats.maximumHz = std::max(stats.maximumHz, candidate.frequencyHz);
        if (candidate.frequencyHz > holdBoundaryHz)
            ++stats.aboveHoldBoundary;
    };
    const auto addBoundaryOutput = [holdBoundaryHz](BoundaryPathStats& stats,
                                                     const ModernPitchEngine::PitchObservation& observed) noexcept
    {
        if (!observed.valid || !std::isfinite(observed.correctionFrequencyHz)
            || observed.correctionFrequencyHz <= 0.0f)
        {
            return;
        }
        ++stats.freshValid;
        const float cents456 = std::abs(1200.0f * std::log2(
            observed.correctionFrequencyHz / 456.0f));
        if (cents456 > 80.0f)
            return;
        ++stats.near456;
        stats.sumHz += observed.correctionFrequencyHz;
        stats.minimumHz = std::min(stats.minimumHz, observed.correctionFrequencyHz);
        stats.maximumHz = std::max(stats.maximumHz, observed.correctionFrequencyHz);
        if (observed.correctionFrequencyHz > holdBoundaryHz)
            ++stats.aboveHoldBoundary;
    };

    constexpr int boundaryWarmSamples = 24000;
    constexpr int boundaryMeasureSamples = 36000;
    double boundaryPhase = 0.0;
    for (int sample = 0; sample < boundaryWarmSamples + boundaryMeasureSamples; ++sample)
    {
        const double frequencyHz = sample < boundaryWarmSamples ? 450.0 : 456.0;
        boundaryPhase += 2.0 * 3.14159265358979323846 * frequencyHz / 48000.0;
        if (boundaryPhase >= 2.0 * 3.14159265358979323846)
            boundaryPhase -= 2.0 * 3.14159265358979323846;
        const float voice = static_cast<float>(
            0.53 * std::sin(boundaryPhase) + 0.12 * std::sin(2.0 * boundaryPhase));
        ModernPitchEngine::PitchObservation observed;
        if (!boundaryPathTracker->processSample(voice, observed)
            || sample < boundaryWarmSamples)
        {
            continue;
        }
        addBoundaryPath(boundaryFull, boundaryPathTracker->fullRateCandidate_);
        addBoundaryPath(boundaryHalf, boundaryPathTracker->halfRateCandidate_);
        addBoundaryPath(boundaryQuarter, boundaryPathTracker->quarterRateCandidate_);
        addBoundaryPath(boundaryEighth, boundaryPathTracker->eighthRateCandidate_);
        addBoundaryOutput(boundaryOutput, observed);
    }

    const auto printBoundaryStats = [holdBoundaryHz](const char* name,
                                                      const BoundaryPathStats& stats)
    {
        const double meanHz = stats.near456 > 0
            ? stats.sumHz / static_cast<double>(stats.near456) : 0.0;
        const double meanCents = meanHz > 0.0
            ? 1200.0 * std::log2(meanHz / 456.0) : 0.0;
        const double minimumHz = stats.near456 > 0 ? stats.minimumHz : 0.0;
        const double maximumHz = stats.near456 > 0 ? stats.maximumHz : 0.0;
        std::cerr << "BOUNDARY_PATH_BIAS path=" << name
                  << " fresh_valid=" << stats.freshValid
                  << " near456=" << stats.near456
                  << " mean_hz=" << meanHz
                  << " mean_cents=" << meanCents
                  << " minmax_hz=" << minimumHz << ',' << maximumHz
                  << " above_hold_boundary=" << stats.aboveHoldBoundary
                  << " hold_boundary_hz=" << holdBoundaryHz << '\n';
    };
    printBoundaryStats("full", boundaryFull);
    printBoundaryStats("half", boundaryHalf);
    printBoundaryStats("quarter", boundaryQuarter);
    printBoundaryStats("eighth", boundaryEighth);
    printBoundaryStats("output", boundaryOutput);

    // OBSERVATION_MEMORY_SEPARATION_V1: exact digital absence clears detector
    // history immediately, but downstream target-hold invariants remain separate.
    auto zeroGapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    zeroGapTracker->prepare(48000.0);
    zeroGapTracker->setRange(70.0f, 700.0f);
    bool acquiredBeforeZero = false;
    for (int sample = 0; sample < 12000; ++sample)
    {
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * 220.0 * static_cast<double>(sample) / 48000.0);
        ModernPitchEngine::PitchObservation observed;
        if (zeroGapTracker->processSample(0.075f * std::sin(phase), observed)
            && observed.valid)
        {
            acquiredBeforeZero = true;
        }
    }
    zeroGapTracker->setRescueMode(true);
    zeroGapTracker->setReacquisitionAnchor(220.0f);
    ModernPitchEngine::PitchObservation zeroObserved;
    for (int sample = 0; sample < 512; ++sample)
        zeroGapTracker->processSample(0.0f, zeroObserved);
    const bool beamClearedByZero = std::none_of(
        zeroGapTracker->decoderBeam_.begin(), zeroGapTracker->decoderBeam_.end(),
        [](const auto& state) { return state.valid; });
    success &= check(acquiredBeforeZero
                     && zeroGapTracker->trackedPitchHz_ == 0.0f
                     && zeroGapTracker->reacquisitionAnchorHz_ == 0.0f
                     && beamClearedByZero
                     && zeroGapTracker->observationContinuityBroken_,
                     "exact_zero_clears_detector_observation_memory");
    zeroGapTracker->setReacquisitionAnchor(110.0f);
    success &= check(zeroGapTracker->reacquisitionAnchorHz_ == 0.0f,
                     "musical_anchor_cannot_reenter_detector_after_zero");

    int postZeroReacquiredAt = -1;
    constexpr float postZeroHz = 246.94165f;
    for (int sample = 0; sample < 12000; ++sample)
    {
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * static_cast<double>(postZeroHz) * static_cast<double>(sample) / 48000.0);
        ModernPitchEngine::PitchObservation observed;
        if (zeroGapTracker->processSample(0.075f * std::sin(phase), observed)
            && observed.valid)
        {
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / postZeroHz));
            if (cents < 45.0f)
            {
                postZeroReacquiredAt = sample;
                break;
            }
        }
    }
    success &= check(postZeroReacquiredAt >= 0 && postZeroReacquiredAt < 6000,
                     "post_zero_tone_is_fresh_detector_acquisition");

    auto watchdogNoiseTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    watchdogNoiseTracker->prepare(48000.0);
    watchdogNoiseTracker->setRange(70.0f, 700.0f);
    watchdogNoiseTracker->trackedPitchHz_ = 110.0f;
    watchdogNoiseTracker->trackedConfidence_ = 0.90f;
    watchdogNoiseTracker->trackedPeriodicity_ = 0.90f;
    watchdogNoiseTracker->setReacquisitionAnchor(110.0f);
    watchdogNoiseTracker->setTransitionWake(true);
    std::uint32_t watchdogRng = 0x4f1bbcdcu;
    float watchdogColored = 0.0f;
    int watchdogNoiseValid = 0;
    for (int sample = 0; sample < 18000; ++sample)
    {
        watchdogRng ^= watchdogRng << 13;
        watchdogRng ^= watchdogRng >> 17;
        watchdogRng ^= watchdogRng << 5;
        const float white = (static_cast<float>(watchdogRng & 0xffffu) / 32767.5f) - 1.0f;
        watchdogColored = 0.94f * watchdogColored + 0.06f * white;
        ModernPitchEngine::PitchObservation observed;
        if (watchdogNoiseTracker->processSample(
                0.03f * (0.45f * white + 0.55f * watchdogColored), observed)
            && sample > 4096 && observed.valid)
        {
            ++watchdogNoiseValid;
        }
    }
    success &= check(watchdogNoiseValid == 0,
                     "transition_watchdog_never_invents_f0_on_noise");

    ModernPitchEngine::CorrectionState zeroResponseTransition;
    zeroResponseTransition.targetValid = true;
    zeroResponseTransition.noteBodyLatched = true;
    zeroResponseTransition.trackingState = ModernPitchEngine::TrackingState::transition;
    zeroResponseTransition.currentCents = -90.0;
    zeroResponseTransition.desiredCents = 35.0;
    zeroResponseTransition.responseMs = 0.0;
    const double zeroResponseValue = engine->advanceCorrection(zeroResponseTransition);
    success &= check(std::abs(zeroResponseValue - 35.0) < 1.0e-12
                     && zeroResponseTransition.trackingState
                        == ModernPitchEngine::TrackingState::stable,
                     "zero_response_has_no_hidden_transition_window");

    return success ? 0 : 1;
}
