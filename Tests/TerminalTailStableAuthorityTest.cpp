#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace
{
bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

ModernPitchEngine::PitchObservation strongPitch(float frequency)
{
    ModernPitchEngine::PitchObservation observation;
    observation.valid = true;
    observation.audioPresent = true;
    observation.frequencyHz = frequency;
    observation.correctionFrequencyHz = frequency;
    observation.confidence = 0.95f;
    observation.periodicity = 0.95f;
    observation.consensus = 0.88f;
    observation.voicing = 0.95f;
    observation.detectorSupport = 2;
    return observation;
}

void setBodyEvidence(ModernPitchEngine::Parameters& p)
{
    p.voiceEvidenceValid = true;
    p.voiceBodyEnergy = 0.92f;
    p.voiceHarmonicity = 0.90f;
    p.voiceSpectralReliability = 0.88f;
    p.voiceBreathiness = 0.04f;
    p.voiceEventStrength = 0.0f;
}

void setTailEvidence(ModernPitchEngine::Parameters& p)
{
    p.voiceEvidenceValid = true;
    p.voiceBodyEnergy = 0.34f;
    p.voiceHarmonicity = 0.32f;
    p.voiceSpectralReliability = 0.34f;
    p.voiceBreathiness = 0.48f;
    p.voiceEventStrength = 0.08f;
}
}

int main()
{
    bool success = true;

    auto engine = std::make_unique<ModernPitchEngine>();
    engine->prepare(48000.0, 256, 1, ModernPitchEngine::LatencyMode::live);

    std::array<double, 12> chromatic {};
    for (int degree = 0; degree < 12; ++degree)
        chromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);

    ModernPitchEngine::ScaleQuantizer quantizer;
    quantizer.reset();
    quantizer.setScale(chromatic.data(), static_cast<int>(chromatic.size()), 440.0);

    ModernPitchEngine::Parameters body;
    setBodyEvidence(body);
    body.scaleLock = true;
    body.hardLockActive = false;
    body.lockStrictness = 0.0f;
    body.lockHysteresis = 0.0f;
    body.amount = 1.0f;
    body.humanize = 0.0f;
    body.vibratoPreserve = 0.0f;
    body.preserveVibrato = 0.0f;
    body.retuneTimeMs = 180.0f;
    body.maximumCorrectionSemitones = 24.0f;

    ModernPitchEngine::CorrectionState falling;
    auto centre = strongPitch(440.0f);
    for (int hop = 0; hop < 24; ++hop)
    {
        engine->updateCorrectionState(falling, quantizer, centre, body);
        for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
            static_cast<void>(engine->advanceCorrection(falling));
    }

    const double ownedTarget = falling.targetLog2;
    const double targetHz = std::exp2(ownedTarget);
    ModernPitchEngine::Parameters tail = body;
    setTailEvidence(tail);

    double maxFallingResidual = 0.0;
    bool fallingStayedStable = true;
    for (int cents = -8; cents >= -120; cents -= 8)
    {
        const double rawHz = 440.0 * std::exp2(static_cast<double>(cents) / 1200.0);
        auto obs = strongPitch(static_cast<float>(rawHz));
        engine->updateCorrectionState(falling, quantizer, obs, tail);
        fallingStayedStable &= falling.trackingState == ModernPitchEngine::TrackingState::stable;
        for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
        {
            const double correction = engine->advanceCorrection(falling);
            const double outputHz = rawHz * std::exp2(correction / 1200.0);
            maxFallingResidual = std::max(maxFallingResidual,
                std::abs(1200.0 * std::log2(outputHz / targetHz)));
        }
    }

    std::cerr << "terminal_tail_falling_max_residual_cents="
              << maxFallingResidual << '\n';
    success &= check(fallingStayedStable
                     && std::abs(falling.targetLog2 - ownedTarget) < 1.0e-12,
                     "terminal_tail_falling_remains_same_stable_identity");
    success &= check(maxFallingResidual < 5.0,
                     "terminal_tail_stable_current_correction_follows_owned_center");

    ModernPitchEngine::ScaleQuantizer risingQuantizer;
    risingQuantizer.reset();
    risingQuantizer.setScale(chromatic.data(), static_cast<int>(chromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState rising;
    for (int hop = 0; hop < 24; ++hop)
    {
        engine->updateCorrectionState(rising, risingQuantizer, centre, body);
        for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
            static_cast<void>(engine->advanceCorrection(rising));
    }

    double maxRisingResidual = 0.0;
    bool risingStayedStable = true;
    for (int cents = 8; cents <= 120; cents += 8)
    {
        const double rawHz = 440.0 * std::exp2(static_cast<double>(cents) / 1200.0);
        auto obs = strongPitch(static_cast<float>(rawHz));
        engine->updateCorrectionState(rising, risingQuantizer, obs, tail);
        risingStayedStable &= rising.trackingState == ModernPitchEngine::TrackingState::stable;
        for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
        {
            const double correction = engine->advanceCorrection(rising);
            const double outputHz = rawHz * std::exp2(correction / 1200.0);
            maxRisingResidual = std::max(maxRisingResidual,
                std::abs(1200.0 * std::log2(outputHz / targetHz)));
        }
    }

    std::cerr << "terminal_tail_rising_max_residual_cents="
              << maxRisingResidual << '\n';
    success &= check(risingStayedStable
                     && std::abs(rising.targetLog2 - ownedTarget) < 1.0e-12,
                     "terminal_tail_rising_remains_same_stable_identity");
    success &= check(maxRisingResidual < 5.0,
                     "terminal_tail_rising_current_correction_follows_owned_center");

    std::cerr << "TERMINAL_TAIL_STABLE_AUTHORITY="
              << (success ? "PASS" : "FAIL") << '\n';
    return success ? 0 : 1;
}
