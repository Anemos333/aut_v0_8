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

ModernPitchEngine::PitchObservation pitch(float hz, bool onset = false)
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
    o.onset = onset;
    o.onsetStrength = onset ? 1.0f : 0.0f;
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

    success &= check(state.targetValid
                     && state.trackingState == ModernPitchEngine::TrackingState::stable,
                     "diagnostic_test_starts_stable");

    const auto serialBefore = engine->targetRevisionDiagnosticSerial_;
    const float nextHz = static_cast<float>(440.0 * std::exp2(1.0 / 12.0));
    const auto next = pitch(nextHz, true);

    engine->updateCorrectionState(state, quantizer, next, p);

    const bool enteredTransition =
        state.trackingState == ModernPitchEngine::TrackingState::transition;
    success &= check(enteredTransition,
                     "target_revision_enters_transition_before_response_zero");

    static_cast<void>(engine->advanceCorrection(state));

    success &= check(state.trackingState == ModernPitchEngine::TrackingState::stable,
                     "response_zero_hides_transition_by_next_audio_sample");
    success &= check(engine->targetRevisionDiagnosticSerial_ == serialBefore + 1,
                     "target_revision_diagnostic_serial_latches_hidden_transition");
    success &= check(engine->targetRevisionFromStable_,
                     "target_revision_diagnostic_records_from_stable");
    success &= check(engine->targetRevisionMusicalOnset_,
                     "target_revision_diagnostic_records_onset");
    success &= check(engine->targetRevisionVoiceEvidenceValid_,
                     "target_revision_diagnostic_records_voice_evidence");
    success &= check(std::abs(engine->targetRevisionBeforeHz_ - 440.0f) < 0.5f
                     && std::abs(engine->targetRevisionAfterHz_ - nextHz) < 0.5f,
                     "target_revision_diagnostic_records_target_before_after");
    success &= check(std::abs(engine->targetRevisionJumpCents_ - 100.0f) < 1.0f,
                     "target_revision_diagnostic_records_jump_cents");

    std::cerr << "TARGET_REVISION_DIAGNOSTIC_LATCH="
              << (success ? "PASS" : "FAIL") << '\n';
    return success ? 0 : 1;
}
