#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <iostream>

namespace
{
ModernPitchEngine::PitchObservation pitch(double cents)
{
    ModernPitchEngine::PitchObservation o;
    const float hz = static_cast<float>(440.0 * std::exp2(cents / 1200.0));
    o.frequencyHz = hz;
    o.correctionFrequencyHz = hz;
    o.confidence = 0.95f;
    o.periodicity = 0.94f;
    o.voicing = 0.94f;
    o.consensus = 0.90f;
    o.detectorSupport = 3;
    o.valid = true;
    o.measurementAvailable = true;
    o.audioPresent = true;
    return o;
}

ModernPitchEngine::Parameters params(float hold)
{
    ModernPitchEngine::Parameters p;
    p.amount = 1.0f;
    p.humanize = 0.0f;
    p.vibratoPreserve = 0.0f;
    p.preserveVibrato = 0.0f;
    p.retuneTimeMs = 20.0f;
    p.transitionTimeMs = 20.0f;
    p.lockHysteresis = hold;
    p.scaleLock = true;
    p.hardLockActive = true;
    p.maximumCorrectionSemitones = 12.0f;
    p.voiceEvidenceValid = true;
    p.voiceBodyEnergy = 0.95f;
    p.voiceHarmonicity = 0.94f;
    p.voiceSpectralReliability = 0.94f;
    p.voiceBreathiness = 0.03f;
    p.voiceEventStrength = 0.0f;
    p.voiceFormantStability = 0.94f;
    return p;
}

ModernPitchEngine::CorrectionState stable()
{
    ModernPitchEngine::CorrectionState s;
    const double root = std::log2(440.0);
    s.targetValid = true;
    s.pitchCentreValid = true;
    s.targetLog2 = root;
    s.pitchCentreLog2 = root;
    s.desiredCents = 0.0;
    s.currentCents = 0.0;
    s.responseMs = 20.0;
    s.stableObservations = 20;
    s.stableBodyObservations = 20;
    s.noteBodyLatched = true;
    s.noteBodyConfidence = 1.0f;
    s.transportPeriodHz = 440.0;
    s.trackingState = ModernPitchEngine::TrackingState::stable;
    return s;
}
}

int main()
{
    ModernPitchEngine engine;
    engine.sampleRate_ = 48000.0;

    const std::array<double, 2> scale {
        1.0, std::exp2(1.0 / 12.0)
    };
    ModernPitchEngine::ScaleQuantizer lowQ, highQ;
    lowQ.setScale(scale.data(), 2, 440.0);
    highQ.setScale(scale.data(), 2, 440.0);

    auto low = stable();
    auto high = stable();
    const auto o = pitch(75.0);
    const auto lowP = params(0.0f);
    const auto highP = params(80.0f);

    for (int hop = 0; hop < 4; ++hop)
    {
        engine.updateCorrectionState(low, lowQ, o, lowP);
        engine.updateCorrectionState(high, highQ, o, highP);
    }

    const double lowMove = (low.targetLog2 - std::log2(440.0)) * 1200.0;
    const double highMove = (high.targetLog2 - std::log2(440.0)) * 1200.0;

    if (!(lowMove > 90.0 && std::abs(highMove) < 0.5))
    {
        std::cerr << "HOLD_OWNERSHIP_BOUNDARY=FAIL"
                  << " low_move=" << lowMove
                  << " high_move=" << highMove << "\n";
        return 2;
    }

    std::cout << "HOLD_OWNERSHIP_BOUNDARY=PASS"
              << " low_move=" << lowMove
              << " high_move=" << highMove << "\n";
    return 0;
}
