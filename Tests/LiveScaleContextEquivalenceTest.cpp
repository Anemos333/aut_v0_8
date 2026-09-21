#include <JuceHeader.h>
#include "../Source/LivePitchProcessor.h"

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
        for (auto b : bytes) byte(b);
    }
};
}

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;
    constexpr int blocks = 1200;

    LivePitchProcessor processor;
    processor.prepare(sampleRate, blockSize, 1, ModernPitchEngine::LatencyMode::live);
    processor.setAdvancedParameters(
        35.0f, 0.25f, 0.35f, 0.90f, 0.85f, 0.72f,
        12.0f, 45.0f, 1600.0f, ModernPitchEngine::StereoMode::linkedMidSide);
    processor.setScaleLockParameters(true, 24.0f, 0.25f);

    std::array<double, 120> scale {};
    for (int i = 0; i < static_cast<int>(scale.size()); ++i)
        scale[static_cast<std::size_t>(i)] = std::exp2(static_cast<double>(i) / 120.0);

    juce::AudioBuffer<float> buffer(1, blockSize);
    Hash hash;
    double phase = 0.0;

    for (int block = 0; block < blocks; ++block)
    {
        float* data = buffer.getWritePointer(0);
        for (int i = 0; i < blockSize; ++i)
        {
            const double t = static_cast<double>(block * blockSize + i) / sampleRate;
            const double f = 221.0 + 23.0 * std::sin(2.0 * 3.14159265358979323846 * 0.43 * t);
            phase += 2.0 * 3.14159265358979323846 * f / sampleRate;
            if (phase > 2.0 * 3.14159265358979323846)
                phase -= 2.0 * 3.14159265358979323846;
            data[i] = static_cast<float>(
                0.16 * std::sin(phase)
              + 0.035 * std::sin(2.0 * phase)
              + 0.012 * std::sin(3.0 * phase));
        }

        processor.process(buffer, scale.data(), static_cast<int>(scale.size()),
                          440.0, 8.0f, 1.0f);

        const float* out = buffer.getReadPointer(0);
        for (int i = 0; i < blockSize; ++i)
            hash.pod(out[i]);

        const auto meter = processor.getMetering();
        hash.pod(meter.detectedPitchHz);
        hash.pod(meter.targetPitchHz);
        hash.pod(meter.correctionCents);
        hash.pod(meter.confidence);
        hash.pod(meter.consensus);
    }

    std::cout << "LIVE_SCALE_CONTEXT_HASH="
              << std::hex << std::setw(16) << std::setfill('0')
              << hash.value << std::dec << "\n";
    return 0;
}
