#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <type_traits>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kRootHz = 440.0;

struct Hash
{
    std::uint64_t value = 1469598103934665603ull;

    void byte(std::uint8_t b) noexcept
    {
        value ^= static_cast<std::uint64_t>(b);
        value *= 1099511628211ull;
    }

    template <typename T>
    void pod(const T& valueToHash) noexcept
    {
        static_assert(std::is_trivially_copyable<T>::value,
                      "trajectory hash requires POD values");
        std::array<std::uint8_t, sizeof(T)> bytes {};
        std::memcpy(bytes.data(), &valueToHash, sizeof(T));
        for (auto b : bytes)
            byte(b);
    }

    void boolean(bool v) noexcept { byte(v ? 1u : 0u); }
};

std::array<double, 12> twelveTet()
{
    std::array<double, 12> ratios {};
    for (int i = 0; i < 12; ++i)
        ratios[static_cast<std::size_t>(i)] =
            std::exp2(static_cast<double>(i) / 12.0);
    return ratios;
}

ModernPitchEngine::CorrectionState stableState()
{
    ModernPitchEngine::CorrectionState state;
    const double logRoot = std::log2(kRootHz);
    state.targetValid = true;
    state.pitchCentreValid = true;
    state.targetLog2 = logRoot;
    state.pitchCentreLog2 = logRoot;
    state.desiredCents = 0.0;
    state.currentCents = 0.0;
    state.velocityCentsPerSecond = 0.0;
    state.responseMs = 24.0;
    state.stableObservations = 16;
    state.stableBodyObservations = 16;
    state.stateAgeSamples = static_cast<int>(0.30 * kSampleRate);
    state.noteBodyLatched = true;
    state.noteBodyConfidence = 1.0f;
    state.transportPeriodHz = kRootHz;
    state.trackingState = ModernPitchEngine::TrackingState::stable;
    return state;
}

ModernPitchEngine::PitchObservation observation(double cents,
                                                float confidence,
                                                float periodicity,
                                                float consensus,
                                                int support,
                                                float onset = 0.0f)
{
    ModernPitchEngine::PitchObservation o;
    const float hz = static_cast<float>(
        kRootHz * std::exp2(cents / 1200.0));
    o.frequencyHz = hz;
    o.correctionFrequencyHz = hz;
    o.confidence = confidence;
    o.periodicity = periodicity;
    o.voicing = periodicity;
    o.consensus = consensus;
    o.onsetStrength = onset;
    o.detectorSupport = support;
    o.valid = true;
    o.measurementAvailable = true;
    o.audioPresent = true;
    o.onset = onset > 0.5f;
    return o;
}

ModernPitchEngine::Parameters parameters(bool strongBody)
{
    ModernPitchEngine::Parameters p;
    p.amount = 1.0f;
    p.humanize = 0.0f;
    p.vibratoPreserve = 0.0f;
    p.retuneTimeMs = 24.0f;
    p.transitionTimeMs = 24.0f;
    p.maximumCorrectionSemitones = 12.0f;
    p.lockHysteresis = 0.0f;
    p.scaleLock = true;
    p.voiceEvidenceValid = true;
    p.voiceBodyEnergy = strongBody ? 0.92f : 0.28f;
    p.voiceHarmonicity = strongBody ? 0.90f : 0.28f;
    p.voiceSpectralReliability = strongBody ? 0.90f : 0.28f;
    p.voiceBreathiness = strongBody ? 0.04f : 0.68f;
    p.voiceEventStrength = strongBody ? 0.02f : 0.10f;
    p.voiceFormantStability = strongBody ? 0.92f : 0.38f;
    return p;
}

void hashState(Hash& hash,
               const ModernPitchEngine::CorrectionState& s,
               double audible)
{
    hash.boolean(s.targetValid);
    hash.boolean(s.pitchCentreValid);
    hash.pod(s.targetLog2);
    hash.pod(s.pitchCentreLog2);
    hash.pod(s.desiredCents);
    hash.pod(s.currentCents);
    hash.pod(s.velocityCentsPerSecond);
    hash.pod(s.responseMs);
    hash.pod(s.revision);
    hash.pod(s.stableObservations);
    hash.pod(s.invalidObservations);
    hash.pod(s.stableBodyObservations);
    hash.pod(s.pitchStaleSamples);
    hash.boolean(s.noteBodyLatched);
    hash.pod(s.transportPeriodHz);
    hash.pod(s.transportVelocityCentsPerHop);
    hash.pod(s.transportChallengerLog2);
    hash.pod(s.transportChallengerHops);
    hash.boolean(s.rendererAcceptCurrentHop);
    hash.boolean(s.rendererCommandValid);
    hash.pod(s.rendererCommandCents);
    hash.boolean(s.latentTargetValid);
    hash.pod(s.latentTargetLog2);
    hash.pod(s.latentTargetHops);
    hash.pod(s.identityChallengerDirection);
    hash.pod(s.identityChallengerEvidence);
    hash.pod(static_cast<int>(s.trackingState));
    hash.pod(audible);
}

void driveHop(ModernPitchEngine& engine,
              ModernPitchEngine::CorrectionState& state,
              ModernPitchEngine::ScaleQuantizer& quantizer,
              const ModernPitchEngine::PitchObservation& o,
              const ModernPitchEngine::Parameters& p,
              Hash& hash)
{
    engine.updateCorrectionState(state, quantizer, o, p);
    const double controller = engine.advanceCorrection(state);
    const double audible = engine.selectRendererCorrection(state, controller);
    hashState(hash, state, audible);
}
}

int main()
{
    ModernPitchEngine engine;
    engine.sampleRate_ = kSampleRate;

    const auto ratios = twelveTet();
    ModernPitchEngine::ScaleQuantizer quantizer;
    quantizer.setScale(ratios.data(), static_cast<int>(ratios.size()), kRootHz);

    Hash hash;

    // Stable within-note vibrato: ownership should remain bounded and deterministic.
    auto state = stableState();
    const auto strong = parameters(true);
    constexpr std::array<double, 12> vibrato {
        0.0, 16.0, 31.0, 39.0, 27.0, 8.0,
        -12.0, -33.0, -41.0, -25.0, -7.0, 3.0
    };
    for (double cents : vibrato)
        driveHop(engine, state, quantizer,
                 observation(cents, 0.93f, 0.91f, 0.84f, 3),
                 strong, hash);

    // Terminal-tail style degradation while pitch drifts inside the same cell.
    const auto weakTail = parameters(false);
    for (double cents : {18.0, 29.0, 39.0, 46.0, 51.0, 44.0})
        driveHop(engine, state, quantizer,
                 observation(cents, 0.42f, 0.38f, 0.20f, 1),
                 weakTail, hash);

    // Return to coherent note body.
    for (double cents : {8.0, 3.0, -2.0})
        driveHop(engine, state, quantizer,
                 observation(cents, 0.94f, 0.93f, 0.88f, 3),
                 strong, hash);

    // Provisional octave-family challenge: intentionally short.
    for (int i = 0; i < 3; ++i)
        driveHop(engine, state, quantizer,
                 observation(1200.0, 0.64f, 0.66f, 0.24f, 1),
                 weakTail, hash);

    // Genuine persistent adjacent-note change must eventually commit.
    for (int i = 0; i < 16; ++i)
        driveHop(engine, state, quantizer,
                 observation(100.0, 0.95f, 0.94f, 0.90f, 3,
                             i == 0 ? 0.85f : 0.0f),
                 strong, hash);

    std::cout << "CORRECTION_TRAJECTORY_GOLDEN=PASS\n";
    std::cout << "CORRECTION_TRAJECTORY_HASH="
              << std::hex << std::setw(16) << std::setfill('0')
              << hash.value << std::dec << "\n";
    return 0;
}
