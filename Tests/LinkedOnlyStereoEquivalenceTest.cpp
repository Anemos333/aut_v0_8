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
    constexpr int blocks = 900;

    ModernPitchEngine engine;
    engine.prepare(kSampleRate, blockSize, channels, ModernPitchEngine::LatencyMode::live);

    auto twelve = equalDivision<12>();
    auto seven = equalDivision<7>();
    auto five = equalDivision<5>();

    ModernPitchEngine::Parameters p;
    p.amount = 1.0f;
    p.retuneTimeMs = 8.0f;
    p.transitionTimeMs = 35.0f;
    p.humanize = 0.12f;
    p.formantPreservation = 0.90f;
    p.detectorSensitivity = 0.72f;
    p.maximumCorrectionSemitones = 12.0f;
    p.minimumPitchHz = 45.0f;
    p.maximumPitchHz = 1600.0f;
    p.scaleLock = true;
    p.lockHysteresis = 24.0f;
    p.vibratoPreserve = 0.0f;
    p.voiceEvidenceValid = true;
    p.voiceHarmonicity = 0.93f;
    p.voiceBreathiness = 0.04f;
    p.voiceBodyEnergy = 0.94f;
    p.voiceSpectralReliability = 0.92f;
    p.voiceEventStrength = 0.02f;
    p.voiceFormantStability = 0.91f;
    p.voiceLowerFamilyEvidence = 0.42f;

    juce::AudioBuffer<float> buffer(channels, blockSize);
    Hash hash;
    double phase = 0.0;

    for (int block = 0; block < blocks; ++block)
    {
        const double* scale = twelve.data();
        int scaleSize = static_cast<int>(twelve.size());
        if (block >= 300 && block < 600)
        {
            scale = seven.data();
            scaleSize = static_cast<int>(seven.size());
        }
        else if (block >= 600)
        {
            scale = five.data();
            scaleSize = static_cast<int>(five.size());
        }

        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);
        for (int i = 0; i < blockSize; ++i)
        {
            const std::int64_t n = static_cast<std::int64_t>(block) * blockSize + i;
            const double t = static_cast<double>(n) / kSampleRate;
            const double f = 226.0 + 16.0 * std::sin(2.0 * kPi * 0.31 * t);
            phase += 2.0 * kPi * f / kSampleRate;
            if (phase > 2.0 * kPi)
                phase -= 2.0 * kPi;

            const float voice = static_cast<float>(
                0.16 * std::sin(phase) + 0.042 * std::sin(2.0 * phase));

            // Exercise linked-channel authority: alternate anti-phase and
            // side-heavy blocks so strongest-channel selection is observable.
            if ((block / 75) % 2 == 0)
            {
                left[i] = voice;
                right[i] = -0.82f * voice;
            }
            else
            {
                left[i] = 0.38f * voice;
                right[i] = voice;
            }
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

    std::cout << "LINKED_ONLY_STEREO_HASH="
              << std::hex << std::setw(16) << std::setfill('0')
              << hash.value << std::dec << "\n";
    std::cout << "MODERN_ENGINE_SIZE=" << sizeof(ModernPitchEngine) << "\n";
    return 0;
}
