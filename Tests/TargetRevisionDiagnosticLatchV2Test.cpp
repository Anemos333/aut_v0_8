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

ModernPitchEngine::PitchObservation pitch(float hz)
{
    ModernPitchEngine::PitchObservation o;
    o.valid = true;
    o.measurementAvailable = true;
    o.audioPresent = true;
    o.frequencyHz = hz;
    o.correctionFrequencyHz = hz;
    o.confidence = 0.95f;
    o.periodicity = 0.95f;
    o.voicing = 0.95f;
    o.consensus = 0.90f;
    o.detectorSupport = 3;
    o.onset = false;
    o.onsetStrength = 0.0f;
    return o;
}

void bodyEvidence(ModernPitchEngine::Parameters& p)
{
    p.voiceEvidenceValid = true;
    p.voiceBodyEnergy = 0.95f;
    p.voiceHarmonicity = 0.94f;
    p.voiceSpectralReliability = 0.92f;
    p.voiceBreathiness = 0.02f;
    p.voiceEventStrength = 0.0f;
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

    ModernPitchEngine::Parameters p;
    bodyEvidence(p);
    p.scaleLock = true;
    p.amount = 1.0f;
    p.humanize = 0.0f;
    p.vibratoPreserve = 0.0f;
    p.preserveVibrato = 0.0f;
    p.retuneTimeMs = 0.0f;
    p.maximumCorrectionSemitones = 24.0f;
    p.hardLockActive = false;
    p.lockStrictness = 0.0f;
    p.lockHysteresis = 0.0f;

    ModernPitchEngine::CorrectionState state;
    const auto a4 = pitch(440.0f);
    for (int hop = 0; hop < 20; ++hop)
    {
        engine->updateCorrectionState(state, quantizer, a4, p);
        for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
            static_cast<void>(engine->advanceCorrection(state));
    }

    const auto serialBefore = engine->targetRevisionDiagnosticSerial_;
    const auto lower = pitch(static_cast<float>(440.0 * std::exp2(-1.0 / 12.0)));

    bool committed = false;
    for (int hop = 0; hop < 16 && !committed; ++hop)
    {
        engine->updateCorrectionState(state, quantizer, lower, p);
        committed = engine->targetRevisionDiagnosticSerial_ != serialBefore;
        if (!committed)
        {
            for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
                static_cast<void>(engine->advanceCorrection(state));
        }
    }

    success &= check(committed, "diagnostic_v2_legato_commits_revision");

    const bool routeCaptured =
        engine->targetRevisionDetectorScaleCommit_
        || engine->targetRevisionDeepCentreExit_
        || engine->targetRevisionPersistentBoundaryExit_;
    success &= check(routeCaptured, "diagnostic_v2_records_identity_route");

    success &= check(std::abs(engine->targetRevisionVoiceBodyEnergy_ - p.voiceBodyEnergy) < 1.0e-6f
                     && std::abs(engine->targetRevisionVoiceHarmonicity_ - p.voiceHarmonicity) < 1.0e-6f
                     && std::abs(engine->targetRevisionVoiceSpectralReliability_ - p.voiceSpectralReliability) < 1.0e-6f
                     && std::abs(engine->targetRevisionVoiceBreathiness_ - p.voiceBreathiness) < 1.0e-6f
                     && std::abs(engine->targetRevisionVoiceEventStrength_ - p.voiceEventStrength) < 1.0e-6f,
                     "diagnostic_v2_records_voice_sensor_values");

    success &= check(std::isfinite(engine->targetRevisionCorrectionBeforeCents_)
                     && std::isfinite(engine->targetRevisionCorrectionAfterCents_)
                     && std::isfinite(engine->targetRevisionCorrectionDeltaCents_),
                     "diagnostic_v2_records_correction_consequence");

    success &= check(std::abs((engine->targetRevisionCorrectionAfterCents_
                              - engine->targetRevisionCorrectionBeforeCents_)
                             - engine->targetRevisionCorrectionDeltaCents_) < 1.0e-4f,
                     "diagnostic_v2_correction_delta_is_consistent");

    std::cerr << "TARGET_REVISION_DIAGNOSTIC_LATCH_V2="
              << (success ? "PASS" : "FAIL") << '\n';
    return success ? 0 : 1;
}
