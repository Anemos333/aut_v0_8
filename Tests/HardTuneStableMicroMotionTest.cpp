#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <cmath>
#include <iostream>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kTargetHz = 440.0;

ModernPitchEngine::CorrectionState makeStableState()
{
    ModernPitchEngine::CorrectionState state;
    const double targetLog2 = std::log2(kTargetHz);

    state.targetValid = true;
    state.pitchCentreValid = true;
    state.targetLog2 = targetLog2;
    state.pitchCentreLog2 = targetLog2;
    state.desiredCents = 0.0;
    state.currentCents = 0.0;
    state.velocityCentsPerSecond = 0.0;
    state.responseMs = 80.0;
    state.stableObservations = 12;
    state.invalidObservations = 0;
    state.stableBodyObservations = 12;
    state.stateAgeSamples = static_cast<int>(0.25 * kSampleRate);
    state.pitchStaleSamples = 0;
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
    observation.confidence = 1.0f;
    observation.periodicity = 1.0f;
    observation.voicing = 1.0f;
    observation.consensus = 1.0f;
    observation.detectorSupport = 3;
    observation.valid = true;
    observation.measurementAvailable = true;
    observation.audioPresent = true;
    observation.onset = false;
    observation.onsetStrength = 0.0f;
    return observation;
}

ModernPitchEngine::Parameters makeParameters()
{
    ModernPitchEngine::Parameters parameters;
    parameters.amount = 1.0f;
    parameters.humanize = 0.0f;
    parameters.vibratoPreserve = 0.0f;
    parameters.retuneTimeMs = 80.0f;
    parameters.transitionTimeMs = 35.0f;
    parameters.maximumCorrectionSemitones = 12.0f;
    parameters.lockHysteresis = 24.0f;
    parameters.scaleLock = false;
    parameters.hardLockActive = false;
    parameters.voiceEvidenceValid = false;
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

    ModernPitchEngine::ScaleQuantizer hardQuantizer;
    hardQuantizer.setScale(nullptr, 0, kTargetHz);

    auto hardState = makeStableState();
    auto hardParameters = makeParameters();
    const auto observation = makeObservation(10.0);

    engine.updateCorrectionState(
        hardState, hardQuantizer, observation, hardParameters);

    const double hardControllerError =
        hardState.desiredCents - hardState.currentCents;
    const double hardTransportCents =
        1200.0 * std::log2(hardState.transportPeriodHz / kTargetHz);

    if (std::abs(hardControllerError) > 2.5)
    {
        std::cerr << "HARD_VIBRATO_COMPENSATION=FAIL"
                  << " reason=hard_compensation_too_weak"
                  << " desired=" << hardState.desiredCents
                  << " current=" << hardState.currentCents
                  << " transport_cents=" << hardTransportCents
                  << "\n";
        return 2;
    }

    if (hardState.trackingState != ModernPitchEngine::TrackingState::stable
        || !hardState.targetValid
        || std::abs(hardState.targetLog2 - std::log2(kTargetHz)) * 1200.0 > 0.01)
    {
        std::cerr << "HARD_VIBRATO_COMPENSATION=FAIL"
                  << " reason=identity_or_state_changed\n";
        return 3;
    }

    ModernPitchEngine::ScaleQuantizer normalQuantizer;
    normalQuantizer.setScale(nullptr, 0, kTargetHz);

    auto normalState = makeStableState();
    auto normalParameters = makeParameters();
    normalParameters.humanize = 0.20f;

    engine.updateCorrectionState(
        normalState, normalQuantizer, observation, normalParameters);

    const double normalControllerError =
        normalState.desiredCents - normalState.currentCents;

    if (std::abs(normalControllerError) < 2.0)
    {
        std::cerr << "HARD_VIBRATO_COMPENSATION=FAIL"
                  << " reason=ordinary_humanize_was_precompensated"
                  << " desired=" << normalState.desiredCents
                  << " current=" << normalState.currentCents
                  << "\n";
        return 4;
    }

    if (std::abs(hardControllerError)
        >= 0.40 * std::abs(normalControllerError))
    {
        std::cerr << "HARD_VIBRATO_COMPENSATION=FAIL"
                  << " reason=hard_compensation_did_not_beat_response"
                  << " hard_error=" << hardControllerError
                  << " normal_error=" << normalControllerError
                  << "\n";
        return 5;
    }

    std::cout << "HARD_VIBRATO_COMPENSATION=PASS"
              << " hard_transport_cents=" << hardTransportCents
              << " hard_desired_cents=" << hardState.desiredCents
              << " hard_current_cents=" << hardState.currentCents
              << " hard_controller_error_cents=" << hardControllerError
              << " normal_controller_error_cents=" << normalControllerError
              << "\n";
    return 0;
}
