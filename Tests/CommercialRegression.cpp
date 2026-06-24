#include "../Source/ModernPitchEngine.h"
#include "../Source/LivePitchProcessor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <vector>

namespace AllocationAudit
{
std::atomic<bool> enabled { false };
std::atomic<std::uint64_t> count { 0 };
}

#ifndef AUTOTUNE_SANITIZER_BUILD
void* operator new(std::size_t size)
{
    if (AllocationAudit::enabled.load(std::memory_order_relaxed))
        AllocationAudit::count.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
#endif

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
#ifdef AUTOTUNE_SANITIZER_BUILD
constexpr int parameterFuzzIterations = 5000;
constexpr int modeChangeIterations = 300;
#else
constexpr int parameterFuzzIterations = 100000;
constexpr int modeChangeIterations = 1000;
#endif

struct Tests
{
    int failures = 0;
    std::ofstream csv;

    explicit Tests(const char* csvPath)
    {
        if (csvPath != nullptr)
        {
            csv.open(csvPath);
            csv << "test,mode,frequency,inputRms,outputRms,deltaDb,detectedHz,finite,detail\n";
        }
    }

    void expect(bool condition, const std::string& label)
    {
        std::cout << (condition ? "[PASS] " : "[FAIL] ") << label << '\n';
        if (!condition)
            ++failures;
    }
};

std::array<double, 12> chromaticScale()
{
    std::array<double, 12> scale {};
    for (int i = 0; i < 12; ++i)
        scale[static_cast<std::size_t>(i)] = std::exp2(static_cast<double>(i) / 12.0);
    return scale;
}

std::vector<float> sine(double frequency, double seconds, double sampleRate,
                        float amplitude = 0.25f, double phase = 0.0)
{
    const int samples = static_cast<int>(std::llround(seconds * sampleRate));
    std::vector<float> result(static_cast<std::size_t>(samples));
    const double increment = 2.0 * pi * frequency / sampleRate;
    for (float& sample : result)
    {
        sample = amplitude * static_cast<float>(std::sin(phase));
        phase += increment;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;
    }
    return result;
}

std::vector<float> harmonicVoice(double frequency, double seconds, double sampleRate,
                                 bool missingFundamental, float noiseAmount)
{
    const int samples = static_cast<int>(std::llround(seconds * sampleRate));
    std::vector<float> result(static_cast<std::size_t>(samples));
    std::mt19937 random(0x4a21u + static_cast<unsigned>(frequency));
    std::normal_distribution<float> noise(0.0f, noiseAmount);
    for (int i = 0; i < samples; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        double value = missingFundamental ? 0.0 : 0.08 * std::sin(2.0 * pi * frequency * t);
        value += 0.48 * std::sin(2.0 * pi * frequency * 2.0 * t + 0.17);
        value += 0.27 * std::sin(2.0 * pi * frequency * 3.0 * t + 0.31);
        value += 0.14 * std::sin(2.0 * pi * frequency * 4.0 * t + 0.47);
        result[static_cast<std::size_t>(i)] = static_cast<float>(value) + noise(random);
    }
    return result;
}

double rms(const std::vector<float>& values, int start)
{
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = static_cast<std::size_t>(std::max(0, start)); i < values.size(); ++i)
    {
        if (std::isfinite(values[i]))
        {
            sum += static_cast<double>(values[i]) * values[i];
            ++count;
        }
    }
    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

bool allFinite(const std::vector<float>& values)
{
    return std::all_of(values.begin(), values.end(), [](float x) { return std::isfinite(x); });
}

float maxDifference(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return std::numeric_limits<float>::infinity();
    float maximum = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        maximum = std::max(maximum, std::abs(a[i] - b[i]));
    return maximum;
}

double correlation(const std::vector<float>& a, const std::vector<float>& b, int start)
{
    double aa = 0.0, bb = 0.0, ab = 0.0;
    const int samples = static_cast<int>(std::min(a.size(), b.size()));
    for (int i = std::max(0, start); i < samples; ++i)
    {
        aa += static_cast<double>(a[static_cast<std::size_t>(i)]) * a[static_cast<std::size_t>(i)];
        bb += static_cast<double>(b[static_cast<std::size_t>(i)]) * b[static_cast<std::size_t>(i)];
        ab += static_cast<double>(a[static_cast<std::size_t>(i)]) * b[static_cast<std::size_t>(i)];
    }
    return ab / std::sqrt(std::max(1.0e-30, aa * bb));
}

struct RenderResult
{
    std::vector<std::vector<float>> audio;
    ModernPitchEngine::Metering meter;
};

RenderResult render(const std::vector<std::vector<float>>& input,
                    double sampleRate,
                    int blockSize,
                    ModernPitchEngine::LatencyMode mode,
                    ModernPitchEngine::Parameters parameters)
{
    RenderResult result { input, {} };
    const int channels = static_cast<int>(input.size());
    const int samples = channels > 0 ? static_cast<int>(input[0].size()) : 0;
    ModernPitchEngine engine;
    engine.prepare(sampleRate, blockSize, channels, mode);
    const auto scale = chromaticScale();
    std::array<float*, ModernPitchEngine::maxSupportedChannels> pointers {};
    for (int offset = 0; offset < samples; offset += blockSize)
    {
        const int count = std::min(blockSize, samples - offset);
        for (int channel = 0; channel < channels; ++channel)
            pointers[static_cast<std::size_t>(channel)] =
                result.audio[static_cast<std::size_t>(channel)].data() + offset;
        juce::AudioBuffer<float> view(pointers.data(), channels, count);
        engine.process(view, scale.data(), static_cast<int>(scale.size()),
                       261.625565, parameters);
    }
    result.meter = engine.getMetering();
    return result;
}

void phaseAndLevelTests(Tests& tests)
{
    auto parameters = ModernPitchEngine::Parameters {};
    parameters.amount = 1.0f;
    parameters.retuneTimeMs = 8.0f;
    parameters.minimumPitchHz = 35.0f;
    const std::array<double, 5> frequencies { 98.0, 164.814, 196.0, 230.0, 277.0 };

    for (int mode = 0; mode < 3; ++mode)
    {
        for (double frequency : frequencies)
        {
            const auto input = sine(frequency, 3.0, 48000.0);
            const auto output = render({ input }, 48000.0, 73,
                static_cast<ModernPitchEngine::LatencyMode>(mode), parameters);
            const int latency = mode == 0 ? 128 : mode == 1 ? 256 : 512;
            const double inRms = rms(input, 4096);
            const double outRms = rms(output.audio[0], 4096 + latency);
            const double deltaDb = 20.0 * std::log10(std::max(1.0e-12, outRms / inRms));
            const bool pass = allFinite(output.audio[0]) && deltaDb > -0.75 && deltaDb < 1.25;
            tests.expect(pass, "level/phase mode=" + std::to_string(mode)
                + " f=" + std::to_string(frequency)
                + " delta=" + std::to_string(deltaDb) + " dB");
            if (tests.csv)
                tests.csv << "phase_level," << mode << ',' << frequency << ','
                          << inRms << ',' << outRms << ',' << deltaDb << ','
                          << output.meter.detectedPitchHz << ',' << allFinite(output.audio[0])
                          << ",steady sine\n";
        }
    }
}

void lowRegisterTests(Tests& tests)
{
    auto parameters = ModernPitchEngine::Parameters {};
    parameters.amount = 1.0f;
    parameters.minimumPitchHz = 35.0f;
    parameters.maximumPitchHz = 1000.0f;
    const std::array<double, 8> notes { 45.0, 55.0, 65.406, 73.416, 82.407, 98.0, 110.0, 130.813 };

    for (int mode = 0; mode < 3; ++mode)
    {
        for (double note : notes)
        {
            for (bool missing : { false, true })
            {
                const auto input = harmonicVoice(note, 1.6, 48000.0, missing, 0.012f);
                const auto output = render({ input }, 48000.0, 64,
                    static_cast<ModernPitchEngine::LatencyMode>(mode), parameters);
                const float detected = output.meter.detectedPitchHz;
                const float cents = detected > 0.0f
                    ? std::abs(1200.0f * std::log2(detected / static_cast<float>(note)))
                    : 9999.0f;
                tests.expect(cents < 28.0f && output.meter.octaveState >= -4
                                          && output.meter.octaveState <= 4,
                    "low register mode=" + std::to_string(mode) + " "
                    + std::to_string(note)
                    + (missing ? " missing-fundamental" : " harmonic")
                    + " cents=" + std::to_string(cents));
            }
        }
    }
}

void subharmonicSafetyTests(Tests& tests)
{
    auto parameters = ModernPitchEngine::Parameters {};
    parameters.amount = 1.0f;
    parameters.minimumPitchHz = 35.0f;
    parameters.maximumPitchHz = 1000.0f;
    const std::array<double, 4> notes { 55.0, 73.416, 82.407, 98.0 };

    for (int mode = 0; mode < 3; ++mode)
    {
        for (double note : notes)
        {
            auto input = harmonicVoice(note, 1.6, 48000.0, false, 0.010f);
            for (std::size_t i = 0; i < input.size(); ++i)
            {
                const double time = static_cast<double>(i) / 48000.0;
                input[i] += static_cast<float>(0.18 * std::sin(pi * note * time));
            }

            const auto output = render({ input }, 48000.0, 64,
                static_cast<ModernPitchEngine::LatencyMode>(mode), parameters);
            const float detected = output.meter.detectedPitchHz;
            const float cents = detected > 0.0f
                ? std::abs(1200.0f * std::log2(detected / static_cast<float>(note)))
                : 9999.0f;
            const bool tracksFundamental = cents < 35.0f;
            const bool safelyRejects = output.meter.confidence < 0.20f
                                    && output.meter.wetMix < 0.10f;
            tests.expect(tracksFundamental || safelyRejects,
                "subharmonic safety mode=" + std::to_string(mode)
                + " f=" + std::to_string(note)
                + " cents=" + std::to_string(cents)
                + " confidence=" + std::to_string(output.meter.confidence)
                + " wet=" + std::to_string(output.meter.wetMix));
        }
    }
}

void amountZeroStereoTest(Tests& tests)
{
    auto left = sine(220.0, 2.0, 48000.0, 0.28f, 0.0);
    auto right = sine(220.0, 2.0, 48000.0, 0.28f, 0.72);
    auto parameters = ModernPitchEngine::Parameters {};
    parameters.amount = 0.0f;

    for (int mode = 0; mode < 3; ++mode)
    {
        const int latency = mode == 0 ? 128 : mode == 1 ? 256 : 512;
        const auto output = render({ left, right }, 48000.0, 127,
            static_cast<ModernPitchEngine::LatencyMode>(mode), parameters);
        std::vector<float> delayedLeft(left.size(), 0.0f);
        std::vector<float> delayedRight(right.size(), 0.0f);
        for (std::size_t i = static_cast<std::size_t>(latency); i < left.size(); ++i)
        {
            delayedLeft[i] = left[i - static_cast<std::size_t>(latency)];
            delayedRight[i] = right[i - static_cast<std::size_t>(latency)];
        }
        const float error = std::max(maxDifference(output.audio[0], delayedLeft),
                                     maxDifference(output.audio[1], delayedRight));
        const double corrIn = correlation(left, right, 0);
        const double corrOut = correlation(output.audio[0], output.audio[1], latency + 1024);
        tests.expect(error < 4.0e-5f && std::abs(corrIn - corrOut) < 2.0e-4,
            "Amount 0 stereo mode=" + std::to_string(mode)
            + " maxError=" + std::to_string(error));
    }
}

void blockAndRateTests(Tests& tests)
{
    auto input = harmonicVoice(82.407, 1.4, 48000.0, false, 0.008f);
    auto parameters = ModernPitchEngine::Parameters {};
    parameters.minimumPitchHz = 35.0f;
    const auto reference = render({ input }, 48000.0, 32,
        ModernPitchEngine::LatencyMode::live, parameters).audio[0];
    for (int block : { 1, 2, 3, 7, 31, 64, 127, 257, 512, 1024, 4096 })
    {
        const auto candidate = render({ input }, 48000.0, block,
            ModernPitchEngine::LatencyMode::live, parameters).audio[0];
        tests.expect(maxDifference(reference, candidate) < 3.0e-5f,
                     "block invariance size=" + std::to_string(block));
    }

    for (double rate : { 22050.0, 32000.0, 44100.0, 48000.0, 88200.0,
                         96000.0, 176400.0, 192000.0 })
    {
        auto signal = harmonicVoice(65.406, 0.8, rate, false, 0.005f);
        const auto output = render({ signal }, rate, 113,
            ModernPitchEngine::LatencyMode::quality, parameters);
        tests.expect(allFinite(output.audio[0]), "finite sample-rate=" + std::to_string(rate));
    }
}

void nonFiniteAndImpulseTests(Tests& tests)
{
    auto parameters = ModernPitchEngine::Parameters {};
    parameters.amount = std::numeric_limits<float>::quiet_NaN();
    parameters.retuneTimeMs = std::numeric_limits<float>::infinity();
    std::vector<float> input(32768, 0.0f);
    input[0] = 1.0f;
    input[13] = std::numeric_limits<float>::quiet_NaN();
    input[27] = std::numeric_limits<float>::infinity();
    input[39] = -std::numeric_limits<float>::infinity();
    input[51] = std::numeric_limits<float>::denorm_min();
    input[1024] = 32.0f;
    for (int mode = 0; mode < 3; ++mode)
    {
        const auto output = render({ input }, 48000.0, 19,
            static_cast<ModernPitchEngine::LatencyMode>(mode), parameters);
        float peak = 0.0f;
        for (float x : output.audio[0])
            peak = std::max(peak, std::abs(x));
        tests.expect(allFinite(output.audio[0]) && peak <= 32.001f,
            "non-finite/full-scale recovery mode=" + std::to_string(mode));
    }
}

void deterministicResetTest(Tests& tests)
{
    auto signal = harmonicVoice(110.0, 1.0, 48000.0, false, 0.003f);
    auto parameters = ModernPitchEngine::Parameters {};
    auto scale = chromaticScale();
    ModernPitchEngine engine;
    engine.prepare(48000.0, 64, 1, ModernPitchEngine::LatencyMode::live);
    auto run = [&]()
    {
        auto output = signal;
        for (int offset = 0; offset < static_cast<int>(output.size()); offset += 64)
        {
            const int count = std::min(64, static_cast<int>(output.size()) - offset);
            float* pointer = output.data() + offset;
            juce::AudioBuffer<float> view(&pointer, 1, count);
            engine.process(view, scale.data(), static_cast<int>(scale.size()),
                           261.625565, parameters);
        }
        return output;
    };
    const auto first = run();
    engine.reset();
    const auto second = run();
    tests.expect(maxDifference(first, second) == 0.0f, "deterministic reset");
}

void allocationAndFuzzTests(Tests& tests)
{
    auto scale = chromaticScale();
    ModernPitchEngine engine;
    engine.prepare(48000.0, 4096, 2, ModernPitchEngine::LatencyMode::live);
    std::array<float, 64> left {};
    std::array<float, 64> right {};
    std::array<float*, 2> pointers { left.data(), right.data() };
    juce::AudioBuffer<float> block(pointers.data(), 2, static_cast<int>(left.size()));
    juce::AudioBuffer<float> one(pointers.data(), 2, 1);
    ModernPitchEngine::Parameters parameters;
    std::mt19937 random(0x9b72u);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> bipolar(-1.0f, 1.0f);

    AllocationAudit::count.store(0, std::memory_order_relaxed);
    AllocationAudit::enabled.store(true, std::memory_order_release);
    for (int iteration = 0; iteration < parameterFuzzIterations; ++iteration)
    {
        parameters.amount = unit(random);
        parameters.retuneTimeMs = 500.0f * unit(random);
        parameters.transitionTimeMs = 300.0f * unit(random);
        parameters.preserveVibrato = unit(random);
        parameters.humanize = unit(random);
        parameters.formantPreservation = unit(random);
        parameters.transientProtection = unit(random);
        parameters.detectorSensitivity = unit(random);
        parameters.maximumCorrectionSemitones = 24.0f * unit(random);
        parameters.minimumPitchHz = 35.0f + 180.0f * unit(random);
        parameters.maximumPitchHz = parameters.minimumPitchHz + 50.0f + 2000.0f * unit(random);
        parameters.breathReduction = unit(random);
        parameters.stereoMode = (iteration & 1) != 0
            ? ModernPitchEngine::StereoMode::linkedMidSide
            : ModernPitchEngine::StereoMode::dualMono;
        left[0] = bipolar(random);
        right[0] = bipolar(random);
        engine.process(one, scale.data(), static_cast<int>(scale.size()),
                       261.625565, parameters);
    }
    AllocationAudit::enabled.store(false, std::memory_order_release);
    tests.expect(AllocationAudit::count.load(std::memory_order_relaxed) == 0,
                 std::to_string(parameterFuzzIterations)
                     + " parameter combinations: zero audio-thread allocations");
    tests.expect(std::isfinite(left[0]) && std::isfinite(right[0]),
                 std::to_string(parameterFuzzIterations)
                     + " parameter combinations: finite output");

    LivePitchProcessor live;
    live.prepare(48000.0, 4096, 2, ModernPitchEngine::LatencyMode::live);
    AllocationAudit::count.store(0, std::memory_order_relaxed);
    AllocationAudit::enabled.store(true, std::memory_order_release);
    for (int iteration = 0; iteration < modeChangeIterations; ++iteration)
    {
        const auto mode = static_cast<ModernPitchEngine::LatencyMode>(iteration % 3);
        live.setLatencyModeNonRealtime(mode);
        left[0] = 0.1f;
        right[0] = -0.1f;
        live.process(one, scale.data(), static_cast<int>(scale.size()),
                     261.625565, 8.0f, 1.0f);
    }
    AllocationAudit::enabled.store(false, std::memory_order_release);
    tests.expect(AllocationAudit::count.load(std::memory_order_relaxed) == 0,
                 std::to_string(modeChangeIterations)
                     + " mode changes: zero audio-thread allocations");
    tests.expect(std::isfinite(left[0]) && std::isfinite(right[0]),
                 std::to_string(modeChangeIterations)
                     + " mode changes: finite output");
}

} // namespace

int main(int argc, char** argv)
{
    Tests tests(argc > 1 ? argv[1] : nullptr);
    phaseAndLevelTests(tests);
    lowRegisterTests(tests);
    subharmonicSafetyTests(tests);
    amountZeroStereoTest(tests);
    blockAndRateTests(tests);
    nonFiniteAndImpulseTests(tests);
    deterministicResetTest(tests);
    allocationAndFuzzTests(tests);

    if (tests.failures != 0)
    {
        std::cerr << tests.failures << " commercial regression test(s) failed\n";
        return 1;
    }
    std::cout << "All commercial regression tests passed.\n";
    return 0;
}
