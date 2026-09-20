#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <iostream>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kTargetHz = 440.0;

std::array<double, 12> makeTwelveTet()
{
    std::array<double, 12> ratios {};
    for (int i = 0; i < 12; ++i)
        ratios[static_cast<std::size_t>(i)] =
            std::exp2(static_cast<double>(i) / 12.0);
    return ratios;
}

ModernPitchEngine::CorrectionState makeStableState()
{
    ModernPitchEngine::CorrectionState state;
    const double targetLog2 = std::log2(kTargetHz);
    state.targetValid = true;
    state.pitchCentreValid = true;
    state.targetLog2 = targetLog2;
    state.pitchCentreLog2 = targetLog2;
    state.desiredCents = -12.0;
    state.currentCents = -12.0;
    state.velocityCentsPerSecond = 0.0;
    state.responseMs = 35.0;
    state.stableObservations = 12;
    state.stableBodyObservations = 12;
    state.stateAgeSamples = static_cast<int>(0.25 * kSampleRate);
    state.noteBodyLatched = true;
    state.noteBodyConfidence = 1.0f;
    state.transportPeriodHz = kTargetHz;
    state.transportVelocityCentsPerHop = 0.0;
    state.trackingState = ModernPitchEngine::TrackingState::stable;
    return state;
}

ModernPitchEngine::PitchObservation makeObservation(double cents)
{
    ModernPitchEngine::PitchObservation observation;
    const float hz = static_cast<float>(
        kTargetHz * std::exp2(cents / 1200.0));
    observation.frequencyHz = hz;
    observation.correctionFrequencyHz = hz;
    observation.confidence = 0.92f;
    observation.periodicity = 0.90f;
    observation.voicing = 0.92f;
    observation.consensus = 0.82f;
    observation.detectorSupport = 3;
    observation.valid = true;
    observation.measurementAvailable = true;
    observation.audioPresent = true;
    observation.onset = false;
    observation.onsetStrength = 0.0f;
    return observation;
}

ModernPitchEngine::Parameters makeTailParameters()
{
    ModernPitchEngine::Parameters parameters;
    parameters.amount = 1.0f;
    parameters.humanize = 0.0f;
    parameters.vibratoPreserve = 0.0f;
    parameters.retuneTimeMs = 35.0f;
    parameters.transitionTimeMs = 35.0f;
    parameters.maximumCorrectionSemitones = 12.0f;
    parameters.lockHysteresis = 24.0f;
    parameters.voiceEvidenceValid = true;
    parameters.voiceBodyEnergy = 0.30f;
    parameters.voiceHarmonicity = 0.30f;
    parameters.voiceSpectralReliability = 0.30f;
    parameters.voiceBreathiness = 0.60f;
    parameters.voiceEventStrength = 0.10f;
    parameters.voiceFormantStability = 0.40f;
    return parameters;
}

ModernPitchEngine::Parameters makeStableParameters()
{
    auto parameters = makeTailParameters();
    parameters.voiceBodyEnergy = 0.92f;
    parameters.voiceHarmonicity = 0.90f;
    parameters.voiceSpectralReliability = 0.90f;
    parameters.voiceBreathiness = 0.04f;
    parameters.voiceEventStrength = 0.0f;
    parameters.voiceFormantStability = 0.92f;
    return parameters;
}

bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}
}

int main()
{
    ModernPitchEngine engine;
    engine.sampleRate_ = kSampleRate;

    const auto ratios = makeTwelveTet();
    ModernPitchEngine::ScaleQuantizer quantizer;
    quantizer.setScale(ratios.data(), static_cast<int>(ratios.size()), kTargetHz);

    auto state = makeStableState();

    // Seed the last actually-heard stable renderer command.
    state.rendererAcceptCurrentHop = true;
    const double initialAudible =
        engine.selectRendererCorrection(state, -12.0);
    if (!near(initialAudible, -12.0, 1.0e-9))
    {
        std::cerr << "RENDERER_INCOHERENT_HOP_HOLD=FAIL reason=seed\n";
        return 2;
    }

    // +45 cents is still inside the same 12-TET cell, but it is outside the
    // stable tail core and larger than the local transport innovation gate.
    // The supervisor must retain it as evidence while denying renderer authority.
    const auto tailObservation = makeObservation(45.0);
    auto tailParameters = makeTailParameters();
    engine.updateCorrectionState(state, quantizer, tailObservation, tailParameters);

    if (state.rendererAcceptCurrentHop)
    {
        std::cerr << "RENDERER_INCOHERENT_HOP_HOLD=FAIL reason=tail_not_vetoed\n";
        return 3;
    }

    const double controllerDuringTail = state.desiredCents;
    const double tailAudible =
        engine.selectRendererCorrection(state, controllerDuringTail);

    if (!near(tailAudible, -12.0, 1.0e-9))
    {
        std::cerr << "RENDERER_INCOHERENT_HOP_HOLD=FAIL"
                  << " reason=tail_reached_renderer"
                  << " controller=" << controllerDuringTail
                  << " audible=" << tailAudible << "\n";
        return 4;
    }

    // The incoherent hop must still exist upstream: desired/controller state is
    // allowed to change. Only its renderer authority is revoked.
    if (near(controllerDuringTail, -12.0, 0.01))
    {
        std::cerr << "RENDERER_INCOHERENT_HOP_HOLD=FAIL"
                  << " reason=tail_was_erased_upstream\n";
        return 5;
    }

    // A coherent same-target hop restores renderer authority immediately.
    const auto stableObservation = makeObservation(2.0);
    auto stableParameters = makeStableParameters();
    engine.updateCorrectionState(state, quantizer, stableObservation, stableParameters);

    if (!state.rendererAcceptCurrentHop)
    {
        std::cerr << "RENDERER_INCOHERENT_HOP_HOLD=FAIL reason=stable_not_restored\n";
        return 6;
    }

    const double resumedController = state.desiredCents;
    const double resumedAudible =
        engine.selectRendererCorrection(state, resumedController);

    if (!near(resumedAudible, resumedController, 1.0e-9))
    {
        std::cerr << "RENDERER_INCOHERENT_HOP_HOLD=FAIL"
                  << " reason=stable_command_not_resumed\n";
        return 7;
    }

    // A genuinely persistent adjacent-note move must not be hidden forever.
    // Early uncommitted hops may be held, but the exact hop that commits the new
    // target must immediately restore renderer authority so Response can travel
    // toward the new stable note.
    ModernPitchEngine::ScaleQuantizer transitionQuantizer;
    transitionQuantizer.setScale(
        ratios.data(), static_cast<int>(ratios.size()), kTargetHz);
    auto transitionState = makeStableState();
    transitionState.rendererAcceptCurrentHop = true;
    static_cast<void>(engine.selectRendererCorrection(
        transitionState, transitionState.currentCents));

    const auto newNoteObservation = makeObservation(100.0);
    auto transitionParameters = makeStableParameters();
    transitionParameters.lockHysteresis = 0.0f;

    bool sawHeldChallenger = false;
    bool committed = false;
    bool commitRestoredAuthority = false;
    double committedTargetHz = kTargetHz;

    for (int hop = 0; hop < 20 && !committed; ++hop)
    {
        const double beforeTarget = transitionState.targetLog2;
        engine.updateCorrectionState(
            transitionState,
            transitionQuantizer,
            newNoteObservation,
            transitionParameters);

        const bool targetMoved =
            std::abs(transitionState.targetLog2 - beforeTarget) * 1200.0 > 0.5;

        if (!targetMoved && !transitionState.rendererAcceptCurrentHop)
            sawHeldChallenger = true;

        if (targetMoved)
        {
            committed = true;
            commitRestoredAuthority =
                transitionState.rendererAcceptCurrentHop;
            committedTargetHz = std::exp2(transitionState.targetLog2);
        }
    }

    if (!sawHeldChallenger)
    {
        std::cerr << "RENDERER_PERSISTENT_CHANGE_RELEASE=FAIL"
                  << " reason=no_uncommitted_hold_observed\n";
        return 8;
    }

    if (!committed)
    {
        std::cerr << "RENDERER_PERSISTENT_CHANGE_RELEASE=FAIL"
                  << " reason=real_change_never_committed\n";
        return 9;
    }

    if (!commitRestoredAuthority)
    {
        std::cerr << "RENDERER_PERSISTENT_CHANGE_RELEASE=FAIL"
                  << " reason=commit_remained_hidden\n";
        return 10;
    }

    const double committedController = transitionState.desiredCents;
    const double committedAudible = engine.selectRendererCorrection(
        transitionState, committedController);
    if (!near(committedAudible, committedController, 1.0e-9))
    {
        std::cerr << "RENDERER_PERSISTENT_CHANGE_RELEASE=FAIL"
                  << " reason=committed_command_not_released\n";
        return 11;
    }

    std::cout << "RENDERER_INCOHERENT_HOP_HOLD=PASS"
              << " initial_audible=" << initialAudible
              << " tail_controller=" << controllerDuringTail
              << " tail_audible=" << tailAudible
              << " resumed_controller=" << resumedController
              << " resumed_audible=" << resumedAudible
              << "\n";
    std::cout << "RENDERER_PERSISTENT_CHANGE_RELEASE=PASS"
              << " committed_target_hz=" << committedTargetHz
              << " committed_controller=" << committedController
              << " committed_audible=" << committedAudible
              << "\n";
    return 0;
}
