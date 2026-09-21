#include <JuceHeader.h>
#include "../Source/ModernPitchEngine.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace
{
struct Hash
{
    std::uint64_t value = 1469598103934665603ull;
    void byte(std::uint8_t b) noexcept
    {
        value ^= static_cast<std::uint64_t>(b);
        value *= 1099511628211ull;
    }
    template <typename T>
    void pod(const T& v) noexcept
    {
        std::array<std::uint8_t, sizeof(T)> bytes {};
        std::memcpy(bytes.data(), &v, sizeof(T));
        for (auto b : bytes)
            byte(b);
    }
};

constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

template <std::size_t N>
std::array<double, N> equalDivision()
{
    std::array<double, N> out {};
    for (std::size_t i = 0; i < N; ++i)
        out[i] = std::exp2(static_cast<double>(i) / static_cast<double>(N));
    return out;
}
}

int main()
{
    constexpr int blockSize = 64;
    constexpr int channels = 2;
    constexpr int blocks = 840;

    ModernPitchEngine engine;
    engine.prepare(kSampleRate, blockSize, channels, ModernPitchEngine::LatencyMode::live);

    auto chromatic = equalDivision<12>();
    auto seven = equalDivision<7>();
    auto five = equalDivision<5>();

    ModernPitchEngine::Parameters p;
    p.amount = 1.0f;
    p.retuneTimeMs = 8.0f;
    p.transitionTimeMs = 35.0f;
    p.humanize = 0.15f;
    p.formantPreservation = 0.90f;
    p.detectorSensitivity = 0.72f;
    p.maximumCorrectionSemitones = 12.0f;
    p.minimumPitchHz = 45.0f;
    p.maximumPitchHz = 1600.0f;
    p.scaleLock = true;
    p.lockHysteresis = 24.0f;
    p.vibratoPreserve = 0.0f;
    p.voiceEvidenceValid = true;
    p.voiceHarmonicity = 0.92f;
    p.voiceBreathiness = 0.05f;
    p.voiceBodyEnergy = 0.93f;
    p.voiceSpectralReliability = 0.91f;
    p.voiceEventStrength = 0.03f;
    p.voiceFormantStability = 0.90f;
    p.voiceLowerFamilyEvidence = 0.45f;

    juce::AudioBuffer<float> buffer(channels, blockSize);
    Hash hash;
    double phaseL = 0.0;
    double phaseR = 0.0;

    for (int block = 0; block < blocks; ++block)
    {
        const double* scale = chromatic.data();
        int scaleSize = static_cast<int>(chromatic.size());

        if (block < 180)
        {
            p.stereoMode = ModernPitchEngine::StereoMode::linkedMidSide;
        }
        else if (block < 320)
        {
            p.stereoMode = ModernPitchEngine::StereoMode::dualMono;
        }
        else if (block < 500)
        {
            p.stereoMode = ModernPitchEngine::StereoMode::linkedMidSide;
            scale = seven.data();
            scaleSize = static_cast<int>(seven.size());
        }
        else if (block < 680)
        {
            p.stereoMode = ModernPitchEngine::StereoMode::dualMono;
            scale = five.data();
            scaleSize = static_cast<int>(five.size());
        }
        else
        {
            p.stereoMode = ModernPitchEngine::StereoMode::linkedMidSide;
            scale = five.data();
            scaleSize = static_cast<int>(five.size());
        }

        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);
        for (int i = 0; i < blockSize; ++i)
        {
            const std::int64_t n = static_cast<std::int64_t>(block) * blockSize + i;
            const double t = static_cast<double>(n) / kSampleRate;
            const double fL = 219.0 + 14.0 * std::sin(2.0 * kPi * 0.37 * t);
            const double fR = 329.0 + 18.0 * std::sin(2.0 * kPi * 0.23 * t);
            phaseL += 2.0 * kPi * fL / kSampleRate;
            phaseR += 2.0 * kPi * fR / kSampleRate;
            if (phaseL > 2.0 * kPi) phaseL -= 2.0 * kPi;
            if (phaseR > 2.0 * kPi) phaseR -= 2.0 * kPi;

            left[i] = static_cast<float>(
                0.17 * std::sin(phaseL) + 0.045 * std::sin(2.0 * phaseL));
            right[i] = static_cast<float>(
                0.11 * std::sin(phaseR) + 0.025 * std::sin(2.0 * phaseR));
        }

        engine.process(buffer, scale, scaleSize, 440.0, p);

        for (int ch = 0; ch < channels; ++ch)
        {
            const float* data = buffer.getReadPointer(ch);
            for (int i = 0; i < blockSize; ++i)
                hash.pod(data[i]);
        }

        const auto m = engine.getMetering();
        hash.pod(m.detectedPitchHz);
        hash.pod(m.targetPitchHz);
        hash.pod(m.correctionCents);
        hash.pod(m.confidence);
        hash.pod(m.consensus);
        hash.pod(m.detectorSupport);
        hash.pod(m.octaveState);
        hash.pod(m.state);
    }

    std::cout << "ACTIVE_PATH_SETUP_HASH="
              << std::hex << std::setw(16) << std::setfill('0')
              << hash.value << std::dec << "\n";
    return 0;
}
