#include <JuceHeader.h>
#include "../Source/LivePitchProcessor.h"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<double> makeScale(int count)
{
    std::vector<double> ratios(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        ratios[static_cast<std::size_t>(i)] =
            std::exp2(static_cast<double>(i) / static_cast<double>(count));
    return ratios;
}

double runCase(int blockSize, int scaleSize, double seconds)
{
    LivePitchProcessor processor;
    processor.prepare(kSampleRate, blockSize, 1, ModernPitchEngine::LatencyMode::live);
    processor.setAdvancedParameters(
        35.0f, 0.2f, 0.25f, 0.90f, 0.85f, 0.70f,
        12.0f, 45.0f, 1600.0f, ModernPitchEngine::StereoMode::linkedMidSide);
    processor.setScaleLockParameters(true, 24.0f, 0.2f);

    const auto scale = makeScale(scaleSize);
    juce::AudioBuffer<float> buffer(1, blockSize);
    const int blocks = static_cast<int>(seconds * kSampleRate / blockSize);
    std::int64_t sampleCounter = 0;
    double checksum = 0.0;

    // Warmup.
    for (int block = 0; block < 20; ++block)
    {
        float* data = buffer.getWritePointer(0);
        for (int i = 0; i < blockSize; ++i, ++sampleCounter)
        {
            const double t = static_cast<double>(sampleCounter) / kSampleRate;
            data[i] = static_cast<float>(
                0.16 * std::sin(2.0 * kPi * 220.0 * t)
              + 0.04 * std::sin(2.0 * kPi * 441.0 * t));
        }
        processor.process(buffer, scale.data(), static_cast<int>(scale.size()),
                          440.0, 8.0f, 1.0f);
    }

    const auto start = std::chrono::steady_clock::now();
    for (int block = 0; block < blocks; ++block)
    {
        float* data = buffer.getWritePointer(0);
        for (int i = 0; i < blockSize; ++i, ++sampleCounter)
        {
            const double t = static_cast<double>(sampleCounter) / kSampleRate;
            data[i] = static_cast<float>(
                0.16 * std::sin(2.0 * kPi * (220.0 + 7.0 * std::sin(2.0 * kPi * 0.7 * t)) * t)
              + 0.04 * std::sin(2.0 * kPi * 441.0 * t));
        }
        processor.process(buffer, scale.data(), static_cast<int>(scale.size()),
                          440.0, 8.0f, 1.0f);
        checksum += buffer.getSample(0, blockSize - 1);
    }
    const auto stop = std::chrono::steady_clock::now();

    const double ms = std::chrono::duration<double, std::milli>(stop - start).count();
    std::cout << "SCALE_CONTEXT_CPU"
              << " block=" << blockSize
              << " scale=" << scaleSize
              << " ms=" << ms
              << " checksum=" << checksum
              << "\n";
    return checksum;
}
}

int main()
{
    volatile double sink = 0.0;
    for (int block : {64, 256})
        for (int scale : {12, 120})
            sink += runCase(block, scale, 3.0);
    return std::isfinite(static_cast<double>(sink)) ? 0 : 2;
}
