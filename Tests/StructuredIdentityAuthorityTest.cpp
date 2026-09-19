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

ModernPitchEngine::PitchObservation strongPitch(float hz)
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

void strongBody(ModernPitchEngine::Parameters& p)
{
    p.voiceEvidenceValid = true;
    p.voiceBodyEnergy = 0.92f;
    p.voiceHarmonicity = 0.90f;
    p.voiceSpectralReliability = 0.88f;
    p.voiceBreathiness = 0.04f;
    p.voiceEventStrength = 0.0f;
}

void borderlineTailBody(ModernPitchEngine::Parameters& p)
{
    // Still audible and formally body-present, but not positive structured
    // evidence for a new note. Importantly, this does NOT satisfy the existing
    // terminalStructure vote, reproducing the gap seen in the DAW diagnostics.
    p.voiceEvidenceValid = true;
    p.voiceBodyEnergy = 0.55f;
    p.voiceHarmonicity = 0.50f;
    p.voiceSpectralReliability = 0.45f;
    p.voiceBreathiness = 0.38f;
    p.voiceEventStrength = 0.20f;
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
    strongBody(p);
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
    const auto a4 = strongPitch(440.0f);
    for (int hop = 0; hop < 20; ++hop)
    {
        engine->updateCorrectionState(state, quantizer, a4, p);
        for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
            static_cast<void>(engine->advanceCorrection(state));
    }

    const double ownedTarget = state.targetLog2;
    const auto revisionBeforeTail = state.revision;

    ModernPitchEngine::Parameters tail = p;
    borderlineTailBody(tail);
    const float lowerSemitoneHz = static_cast<float>(
        440.0 * std::exp2(-1.0 / 12.0));
    const auto lowerCandidate = strongPitch(lowerSemitoneHz);

    bool targetMovedDuringBorderlineTail = false;
    for (int hop = 0; hop < 12; ++hop)
    {
        engine->updateCorrectionState(state, quantizer, lowerCandidate, tail);
        targetMovedDuringBorderlineTail |=
            std::abs((state.targetLog2 - ownedTarget) * 1200.0) > 0.5;
        for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
            static_cast<void>(engine->advanceCorrection(state));
    }

    success &= check(!targetMovedDuringBorderlineTail
                     && std::abs((state.targetLog2 - ownedTarget) * 1200.0) < 0.5
                     && state.revision == revisionBeforeTail,
                     "audible_borderline_tail_cannot_gain_new_identity_authority");

    // The physical coordinate must remain alive: this is identity veto only,
    // never a freeze/dry fallback.
    success &= check(state.transportPeriodHz > 0.0
                     && std::isfinite(state.transportPeriodHz),
                     "borderline_tail_keeps_live_transport");

    // Positive structured body evidence returns on the exact same challenger.
    // It must be allowed to commit promptly; we are not adding generic Hold.
    ModernPitchEngine::Parameters legato = p;
    strongBody(legato);
    bool committedStrongLegato = false;
    int commitHop = -1;
    for (int hop = 0; hop < 4; ++hop)
    {
        engine->updateCorrectionState(state, quantizer, lowerCandidate, legato);
        if (std::abs((state.targetLog2 - ownedTarget) * 1200.0) > 50.0)
        {
            committedStrongLegato = true;
            commitHop = hop;
            break;
        }
        for (int sample = 0; sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
            static_cast<void>(engine->advanceCorrection(state));
    }

    const double committedHz = std::exp2(state.targetLog2);
    success &= check(committedStrongLegato
                     && commitHop <= 2
                     && committedHz > 414.0
                     && committedHz < 417.0,
                     "strong_structured_legato_commits_promptly_after_tail_veto");

    std::cerr << "STRUCTURED_IDENTITY_AUTHORITY="
              << (success ? "PASS" : "FAIL") << '\n';
    return success ? 0 : 1;
}
