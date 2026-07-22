#include "ModernPitchEngine.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <new>
#include <string>
#include <vector>

#ifndef NEUMATON_OUTPUT_V3_SHADOW_LEDGER
#define NEUMATON_OUTPUT_V3_SHADOW_LEDGER 0
#endif

namespace
{
std::atomic<std::uint64_t> guardedAllocations { 0 };
thread_local bool allocationGuardActive = false;

constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;

struct Guard final
{
    Guard() noexcept { allocationGuardActive = true; }
    ~Guard() { allocationGuardActive = false; }
};

void countAllocation() noexcept
{
    if (allocationGuardActive)
        guardedAllocations.fetch_add(1u, std::memory_order_relaxed);
}

float deterministicNoise(std::uint32_t& state) noexcept
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return static_cast<float>(state) / static_cast<float>(UINT32_MAX) * 2.0f - 1.0f;
}

std::vector<double> equalTemperament()
{
    std::vector<double> ratios(12);
    for (int index = 0; index < 12; ++index)
        ratios[static_cast<std::size_t>(index)] = std::exp2(static_cast<double>(index) / 12.0);
    return ratios;
}

float sourceSample(std::int64_t sample,
                   double sampleRate,
                   int channel,
                   ModernPitchEngine::StereoMode stereoMode,
                   std::uint32_t& noiseState) noexcept
{
    const double time = static_cast<double>(sample) / sampleRate;
    const double segment = std::fmod(time, 1.2);
    double centreHz = 220.0;
    if (segment >= 0.40 && segment < 0.78)
    {
        const double alpha = (segment - 0.40) / 0.38;
        centreHz = 220.0 * std::exp2(alpha * 2.0 / 12.0);
    }
    else if (segment >= 0.78)
    {
        centreHz = 196.0;
    }

    const double vibratoCents = 16.0 * std::sin(twoPi * 5.1 * time);
    const double frequencyHz = centreHz * std::exp2(vibratoCents / 1200.0);
    const double phase = twoPi * frequencyHz * time;
    const double envelope = std::min(1.0, std::max(0.0, time * 8.0));

    double signal = 0.54 * std::sin(phase)
                  + 0.23 * std::sin(2.0 * phase + 0.18)
                  + 0.11 * std::sin(3.0 * phase - 0.31)
                  + 0.055 * std::sin(5.0 * phase + 0.47);

    const double consonant = segment < 0.025
        ? std::exp(-segment * 150.0) * static_cast<double>(deterministicNoise(noiseState))
        : 0.0;
    signal = envelope * (signal + 0.10 * consonant
        + 0.008 * static_cast<double>(deterministicNoise(noiseState)));

    if (channel == 1)
    {
        if (stereoMode == ModernPitchEngine::StereoMode::linkedMidSide)
            signal = 0.965 * signal + 0.008 * std::sin(twoPi * 330.0 * time);
        else
            signal = 0.91 * signal + 0.055 * std::sin(twoPi * 277.18 * time + 0.23);
    }

    return static_cast<float>(0.45 * signal);
}

bool processConfiguration(std::ofstream& audio,
                          std::ofstream& stats,
                          ModernPitchEngine::LatencyMode latencyMode,
                          ModernPitchEngine::StereoMode stereoMode,
                          std::uint64_t& totalObservations,
                          std::uint64_t& totalValidFrames,
                          std::uint64_t& maximumActive)
{
    constexpr double sampleRate = 48000.0;
    constexpr int channels = 2;
    constexpr int maximumBlock = 257;
    constexpr int samplesToRender = 57600;

    ModernPitchEngine engine;
    engine.prepare(sampleRate, maximumBlock, channels, latencyMode);

    ModernPitchEngine::Parameters parameters;
    parameters.amount = 1.0f;
    parameters.retuneTimeMs = 8.0f;
    parameters.transitionTimeMs = 42.0f;
    parameters.preserveVibrato = 0.68f;
    parameters.humanize = 0.22f;
    parameters.formantPreservation = 0.90f;
    parameters.transientProtection = 0.85f;
    parameters.detectorSensitivity = 0.82f;
    parameters.breathReduction = 0.45f;
    parameters.stereoMode = stereoMode;
    parameters.scaleLock = true;
    parameters.lockStrictness = 0.72f;
    parameters.hardLockActive = true;
    parameters.latencyMode = static_cast<int>(latencyMode);

    const auto ratios = equalTemperament();

    {
        juce::AudioBuffer<float> warmup(channels, 1024);
        warmup.clear();
        for (int sample = 0; sample < warmup.getNumSamples(); ++sample)
        {
            const float value = 0.25f * std::sin(static_cast<float>(twoPi * 220.0
                * static_cast<double>(sample) / sampleRate));
            warmup.setSample(0, sample, value);
            warmup.setSample(1, sample, value);
        }
        engine.process(warmup, ratios, 440.0, parameters);
        engine.reset();
    }

    const int blockPattern[] { 17, 64, 113, 257, 31, 128, 89, 211 };
    int patternIndex = 0;
    std::int64_t rendered = 0;
    std::uint32_t leftNoise = 0x12345678u;
    std::uint32_t rightNoise = 0x9abcdef0u;

    while (rendered < samplesToRender)
    {
        const int requested = blockPattern[patternIndex
            % static_cast<int>(sizeof(blockPattern) / sizeof(blockPattern[0]))];
        ++patternIndex;
        const int blockSize = std::min(requested,
            samplesToRender - static_cast<int>(rendered));
        juce::AudioBuffer<float> block(channels, blockSize);

        for (int sample = 0; sample < blockSize; ++sample)
        {
            const std::int64_t absolute = rendered + sample;
            block.setSample(0, sample, sourceSample(absolute, sampleRate, 0,
                                                   stereoMode, leftNoise));
            block.setSample(1, sample, sourceSample(absolute, sampleRate, 1,
                                                   stereoMode, rightNoise));
        }

        {
            Guard guard;
            engine.process(block, ratios, 440.0, parameters);
        }

        for (int sample = 0; sample < blockSize; ++sample)
        {
            const float left = block.getSample(0, sample);
            const float right = block.getSample(1, sample);
            audio.write(reinterpret_cast<const char*>(&left), sizeof(left));
            audio.write(reinterpret_cast<const char*>(&right), sizeof(right));
        }

        const auto meter = engine.getMetering();
        totalObservations += static_cast<std::uint64_t>(
            std::max(0, meter.shadowRidgeObservationCount));
        totalValidFrames += meter.shadowRidgeValid ? 1u : 0u;
        maximumActive = std::max(maximumActive,
            static_cast<std::uint64_t>(std::max(0, meter.shadowRidgeActiveCount)));
        rendered += blockSize;
    }

    stats << "mode=" << static_cast<int>(latencyMode)
          << ",stereo=" << static_cast<int>(stereoMode)
          << ",observations=" << totalObservations
          << ",valid_blocks=" << totalValidFrames
          << ",max_active=" << maximumActive << '\n';
    return audio.good() && stats.good();
}
} // namespace

void* operator new(std::size_t size)
{
    countAllocation();
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
    countAllocation();
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: shadow_identity <audio.raw> <stats.txt>\n";
        return 2;
    }

    std::ofstream audio(argv[1], std::ios::binary | std::ios::trunc);
    std::ofstream stats(argv[2], std::ios::trunc);
    if (!audio || !stats)
        return 3;

    std::uint64_t totalObservations = 0;
    std::uint64_t totalValidFrames = 0;
    std::uint64_t maximumActive = 0;

    for (int mode = 0; mode <= 2; ++mode)
    {
        for (int stereo = 0; stereo <= 1; ++stereo)
        {
            if (!processConfiguration(audio,
                                      stats,
                                      static_cast<ModernPitchEngine::LatencyMode>(mode),
                                      static_cast<ModernPitchEngine::StereoMode>(stereo),
                                      totalObservations,
                                      totalValidFrames,
                                      maximumActive))
            {
                return 4;
            }
        }
    }

    const std::uint64_t allocations = guardedAllocations.load(std::memory_order_relaxed);
    stats << "guarded_allocations=" << allocations << '\n'
          << "total_observations=" << totalObservations << '\n'
          << "total_valid_blocks=" << totalValidFrames << '\n'
          << "maximum_active=" << maximumActive << '\n';

    if (allocations != 0u)
    {
        std::cerr << "allocation detected inside ModernPitchEngine::process: "
                  << allocations << '\n';
        return 5;
    }

#if NEUMATON_OUTPUT_V3_SHADOW_LEDGER
    if (totalObservations == 0u || totalValidFrames == 0u)
    {
        std::cerr << "shadow ledger did not observe valid frames\n";
        return 6;
    }
#endif

    return 0;
}
