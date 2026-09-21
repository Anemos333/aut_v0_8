#include <JuceHeader.h>
#include "../Source/Tempo.h"
#include "../Source/SingleWetSpectralRenderer.h"
#include "../Source/VoiceEvidenceAnalyzer.h"

#define private public
#include "../Source/ModernPitchEngine.h"
#include "../Source/LivePitchProcessor.h"
#undef private

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.1415926535897932384626433832795;

struct SignalGenerator
{
    float next() noexcept
    {
        // Deterministic voice-like harmonic complex with a small nonstationary body.
        const double t = static_cast<double>(index) / kSampleRate;
        const double vibrato = 1.0 + 0.0035 * std::sin(2.0 * kPi * 5.1 * t);
        const double phase = 2.0 * kPi * 220.0 * vibrato * t;
        std::uint32_t x = noiseState;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        noiseState = x;
        const float noise = static_cast<float>((static_cast<int>(x & 0xffffu) - 32768) / 32768.0) * 0.004f;
        ++index;
        return static_cast<float>(0.18 * std::sin(phase)
                                + 0.11 * std::sin(2.0 * phase + 0.18)
                                + 0.065 * std::sin(3.0 * phase - 0.23)
                                + 0.035 * std::sin(4.0 * phase + 0.51)) + noise;
    }
    std::uint64_t index = 0;
    std::uint32_t noiseState = 0x51f15e5du;
};

std::array<double, 12> makeScale()
{
    std::array<double, 12> ratios {};
    for (int i = 0; i < 12; ++i)
        ratios[static_cast<std::size_t>(i)] = std::pow(2.0, static_cast<double>(i) / 12.0);
    return ratios;
}

template <typename Fn>
double elapsedMs(Fn&& fn)
{
    const auto begin = std::chrono::steady_clock::now();
    fn();
    const auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

double benchmarkTracker(double seconds)
{
    ModernPitchEngine::MultiRatePitchTracker tracker;
    tracker.prepare(kSampleRate);
    tracker.setRange(45.0f, 1600.0f);
    tracker.setSensitivity(0.70f);
    tracker.setVoiceAuthorityContext(true, 0.88f, 0.10f, 0.90f, 0.88f, 0.08f, 0.90f, 0.06f);

    ModernPitchEngine::PitchObservation observation;
    SignalGenerator generator;
    const int warmup = static_cast<int>(kSampleRate * 0.5);
    for (int i = 0; i < warmup; ++i)
        tracker.processSample(generator.next(), observation);

    volatile float sink = 0.0f;
    const int samples = static_cast<int>(kSampleRate * seconds);
    const double ms = elapsedMs([&]
    {
        for (int i = 0; i < samples; ++i)
        {
            if (tracker.processSample(generator.next(), observation))
                sink += observation.frequencyHz * 1.0e-9f;
        }
    });
    static_cast<void>(sink);
    return ms;
}

double benchmarkModernEngine(int blockSize, double seconds)
{
    ModernPitchEngine engine;
    engine.prepare(kSampleRate, blockSize, 1, ModernPitchEngine::LatencyMode::live);
    auto scale = makeScale();
    ModernPitchEngine::Parameters parameters;
    parameters.amount = 1.0f;
    parameters.minimumPitchHz = 45.0f;
    parameters.maximumPitchHz = 1600.0f;
    parameters.detectorSensitivity = 0.70f;
    parameters.scaleLock = true;
    parameters.voiceEvidenceValid = true;
    parameters.voiceHarmonicity = 0.88f;
    parameters.voiceBreathiness = 0.10f;
    parameters.voiceBodyEnergy = 0.90f;
    parameters.voiceSpectralReliability = 0.88f;
    parameters.voiceFormantStability = 0.90f;
    parameters.voiceLowerFamilyEvidence = 0.06f;

    juce::AudioBuffer<float> buffer(1, blockSize);
    SignalGenerator generator;
    auto fill = [&]
    {
        float* out = buffer.getWritePointer(0);
        for (int i = 0; i < blockSize; ++i)
            out[i] = generator.next();
    };

    const int warmBlocks = static_cast<int>((0.5 * kSampleRate) / blockSize);
    for (int block = 0; block < warmBlocks; ++block)
    {
        fill();
        engine.process(buffer, scale.data(), static_cast<int>(scale.size()), 440.0, parameters);
    }

    const int blocks = static_cast<int>((seconds * kSampleRate) / blockSize);
    return elapsedMs([&]
    {
        for (int block = 0; block < blocks; ++block)
        {
            fill();
            engine.process(buffer, scale.data(), static_cast<int>(scale.size()), 440.0, parameters);
        }
    });
}

double benchmarkLiveProcessor(int blockSize, double seconds)
{
    LivePitchProcessor processor;
    processor.prepare(kSampleRate, blockSize, 1, ModernPitchEngine::LatencyMode::live);
    processor.setAdvancedParameters(35.0f, 0.20f, 0.90f, 0.70f,
                                    12.0f, 45.0f, 1600.0f,
                                    ModernPitchEngine::StereoMode::linkedMidSide);
    processor.setScaleLockParameters(true, 24.0f, 0.0f);
    auto scale = makeScale();

    juce::AudioBuffer<float> buffer(1, blockSize);
    SignalGenerator generator;
    auto fill = [&]
    {
        float* out = buffer.getWritePointer(0);
        for (int i = 0; i < blockSize; ++i)
            out[i] = generator.next();
    };

    const int warmBlocks = static_cast<int>((0.5 * kSampleRate) / blockSize);
    for (int block = 0; block < warmBlocks; ++block)
    {
        fill();
        processor.process(buffer, scale.data(), static_cast<int>(scale.size()), 440.0, 8.0f, 1.0f);
    }

    const int blocks = static_cast<int>((seconds * kSampleRate) / blockSize);
    return elapsedMs([&]
    {
        for (int block = 0; block < blocks; ++block)
        {
            fill();
            processor.process(buffer, scale.data(), static_cast<int>(scale.size()), 440.0, 8.0f, 1.0f);
        }
    });
}

void printRow(int blockSize, double seconds, double trackerMs, double engineMs, double liveMs)
{
    const double audioMs = seconds * 1000.0;
    std::cout << "CPU_PROFILE"
              << " block=" << blockSize
              << " audio_ms=" << audioMs
              << " tracker_ms=" << trackerMs
              << " engine_ms=" << engineMs
              << " live_ms=" << liveMs
              << " tracker_rt=" << trackerMs / audioMs
              << " engine_rt=" << engineMs / audioMs
              << " live_rt=" << liveMs / audioMs
              << " engine_minus_tracker_ms=" << (engineMs - trackerMs)
              << " live_minus_engine_ms=" << (liveMs - engineMs)
              << '\n';
}
}

int main()
{
    constexpr double seconds = 3.0;
    const double trackerMs = benchmarkTracker(seconds);
    for (const int blockSize : { 64, 128, 256 })
    {
        const double engineMs = benchmarkModernEngine(blockSize, seconds);
        const double liveMs = benchmarkLiveProcessor(blockSize, seconds);
        printRow(blockSize, seconds, trackerMs, engineMs, liveMs);
    }
    return 0;
}
