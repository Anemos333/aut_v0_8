#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr double pi = 3.1415926535897932384626433832795;

bool check(bool condition, const std::string& name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

bool lpcIsSchurStable(
    const std::array<float, ModernPitchEngine::maximumLpcOrder>& lpc)
{
    constexpr int order = ModernPitchEngine::maximumLpcOrder;
    std::array<double, order + 1> coefficients {};
    coefficients[0] = 1.0;
    for (int i = 0; i < order; ++i)
        coefficients[static_cast<std::size_t>(i + 1)] = lpc[static_cast<std::size_t>(i)];

    for (int currentOrder = order; currentOrder >= 1; --currentOrder)
    {
        const double reflection = coefficients[static_cast<std::size_t>(currentOrder)];
        if (!std::isfinite(reflection) || std::abs(reflection) >= 0.999999)
            return false;
        const double denominator = 1.0 - reflection * reflection;
        const auto previous = coefficients;
        for (int i = 1; i < currentOrder; ++i)
        {
            coefficients[static_cast<std::size_t>(i)] =
                (previous[static_cast<std::size_t>(i)]
                 + reflection * previous[static_cast<std::size_t>(currentOrder - i)])
                / denominator;
        }
    }
    return true;
}

double effectiveDelay(const ModernPitchEngine::TransportPlan& plan)
{
    return static_cast<double>(plan.gainA) * plan.delayA
         + static_cast<double>(plan.gainB) * plan.delayB;
}

std::vector<float> makeVoice(int samples)
{
    std::vector<float> result(static_cast<std::size_t>(samples), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        const double f0 = i < samples / 2 ? 218.0 : 247.0;
        phase += 2.0 * pi * f0 / sampleRate;
        phase -= std::floor(phase / (2.0 * pi)) * 2.0 * pi;
        const double attack = std::min(1.0, static_cast<double>(i) / 1200.0);
        result[static_cast<std::size_t>(i)] = static_cast<float>(
            attack * (0.23 * std::sin(phase)
                    + 0.10 * std::sin(2.0 * phase + 0.2)
                    + 0.055 * std::sin(3.0 * phase + 0.45)
                    + 0.030 * std::sin(5.0 * phase)));
    }
    return result;
}

std::vector<float> renderWithBlockSize(const std::vector<float>& source,
                                       int blockSize)
{
    auto engine = std::make_unique<ModernPitchEngine>();
    engine->prepare(sampleRate, blockSize, 1, ModernPitchEngine::LatencyMode::live);
    ModernPitchEngine::Parameters parameters;
    parameters.amount = 1.0f;
    parameters.retuneTimeMs = 8.0f;
    parameters.transitionTimeMs = 35.0f;
    parameters.formantPreservation = 0.95f;
    parameters.transientProtection = 0.90f;
    parameters.detectorSensitivity = 0.80f;
    parameters.breathReduction = 0.40f;

    std::vector<double> chromatic;
    for (int degree = 0; degree < 12; ++degree)
        chromatic.push_back(std::exp2(static_cast<double>(degree) / 12.0));

    std::vector<float> output(source.size(), 0.0f);
    for (int offset = 0; offset < static_cast<int>(source.size()); offset += blockSize)
    {
        const int count = std::min(blockSize,
            static_cast<int>(source.size()) - offset);
        juce::AudioBuffer<float> block(1, count);
        for (int i = 0; i < count; ++i)
            block.setSample(0, i, source[static_cast<std::size_t>(offset + i)]);
        engine->process(block, chromatic, 440.0, parameters);
        for (int i = 0; i < count; ++i)
            output[static_cast<std::size_t>(offset + i)] = block.getSample(0, i);
    }
    return output;
}
} // namespace

int main()
{
    bool success = true;

    std::array<float, ModernPitchEngine::maximumLpcOrder> reflectionA {
        0.78f, -0.56f, 0.43f, -0.31f, 0.24f, -0.18f,
        0.14f, -0.11f, 0.08f, -0.06f, 0.04f, -0.025f
    };
    std::array<float, ModernPitchEngine::maximumLpcOrder> reflectionB {
        -0.72f, 0.61f, -0.48f, 0.36f, -0.27f, 0.20f,
        -0.15f, 0.11f, -0.08f, 0.055f, -0.035f, 0.020f
    };

    bool interpolationStable = true;
    for (int step = 0; step <= 200; ++step)
    {
        const float t = static_cast<float>(step) / 200.0f;
        std::array<float, ModernPitchEngine::maximumLpcOrder> reflection {};
        for (int i = 0; i < ModernPitchEngine::maximumLpcOrder; ++i)
            reflection[static_cast<std::size_t>(i)] =
                reflectionA[static_cast<std::size_t>(i)]
                + t * (reflectionB[static_cast<std::size_t>(i)]
                     - reflectionA[static_cast<std::size_t>(i)]);
        const auto lpc = ModernPitchEngine::ChannelPath::reflectionToLpc(reflection);
        interpolationStable &= lpcIsSchurStable(lpc);
    }
    success &= check(interpolationStable,
                     "parcor_morph_is_stable_at_every_intermediate_state");

    ModernPitchEngine::ChannelPath path;
    path.prepare(sampleRate, 256);
    path.setVoiceModel(reflectionA, 1.0f, 0.0f);
    ModernPitchEngine::TransportPlan fixedPlan;
    fixedPlan.delayA = 64.0;
    fixedPlan.delayB = 64.0;
    fixedPlan.gainA = 1.0f;
    fixedPlan.gainB = 0.0f;
    bool finite = true;
    double maximumOutput = 0.0;
    for (int sample = 0; sample < 36000; ++sample)
    {
        if (sample == 12000)
            path.setVoiceModel(reflectionB, 1.0f, 0.0f);
        if (sample == 24000)
            path.setVoiceModel(reflectionA, 1.0f, 0.0f);
        const double phase = 2.0 * pi * 220.0
                           * static_cast<double>(sample) / sampleRate;
        const float input = static_cast<float>(
            0.10 * std::sin(phase)
          + 0.045 * std::sin(2.0 * phase + 0.3)
          + 0.025 * std::sin(3.0 * phase + 0.5));
        const float output = path.process(input, fixedPlan);
        finite &= std::isfinite(output);
        maximumOutput = std::max(maximumOutput, std::abs(static_cast<double>(output)));
    }
    std::cerr << "maximum_stable_lpc_output=" << maximumOutput << '\n';
    success &= check(finite, "stable_lpc_path_remains_finite_during_model_changes");
    success &= check(maximumOutput < 3.0,
                     "stable_lpc_path_has_no_recursive_burst");

    ModernPitchEngine::TransportClock plain;
    ModernPitchEngine::TransportClock synced;
    plain.prepare(sampleRate, 256);
    synced.prepare(sampleRate, 256);
    const double sourcePeriod = sampleRate / 220.0;
    const double ratio = std::exp2(180.0 / 1200.0);
    double plainMismatch = 0.0;
    double syncedMismatch = 0.0;
    double weightSum = 0.0;
    double maximumMeanDifference = 0.0;
    for (int sample = 0; sample < 18000; ++sample)
    {
        const auto a = plain.next(ratio, sourcePeriod, 0.0f);
        const auto b = synced.next(ratio, sourcePeriod, 1.0f);
        if (sample < 3000)
            continue;
        const double overlap = 2.0 * std::min(
            static_cast<double>(b.gainA), static_cast<double>(b.gainB));
        const auto mismatch = [sourcePeriod](const ModernPitchEngine::TransportPlan& p)
        {
            return std::abs(std::sin(
                pi * (p.delayB - p.delayA) / sourcePeriod));
        };
        plainMismatch += overlap * mismatch(a);
        syncedMismatch += overlap * mismatch(b);
        weightSum += overlap;
        maximumMeanDifference = std::max(
            maximumMeanDifference,
            std::abs(effectiveDelay(a) - effectiveDelay(b)));
    }
    plainMismatch /= std::max(1.0e-12, weightSum);
    syncedMismatch /= std::max(1.0e-12, weightSum);
    std::cerr << "plain_period_phase_mismatch=" << plainMismatch << '\n';
    std::cerr << "synced_period_phase_mismatch=" << syncedMismatch << '\n';
    std::cerr << "maximum_period_sync_mean_delay_difference="
              << maximumMeanDifference << '\n';
    success &= check(syncedMismatch < 0.70 * plainMismatch,
                     "period_guidance_reduces_two_head_phase_mismatch");
    success &= check(maximumMeanDifference < 1.0e-6,
                     "period_guidance_preserves_weighted_transport_delay");

    ModernPitchEngine::TransportClock legacyWindow;
    ModernPitchEngine::TransportClock narrowWindow;
    legacyWindow.prepare(sampleRate, 256);
    narrowWindow.prepare(sampleRate, 256);
    double legacyOverlap = 0.0;
    double narrowOverlap = 0.0;
    for (int sample = 0; sample < 12000; ++sample)
    {
        const auto legacy = legacyWindow.next(ratio);
        const auto narrow = narrowWindow.next(ratio, sourcePeriod, 0.0f);
        legacyOverlap += std::min(
            static_cast<double>(legacy.gainA), static_cast<double>(legacy.gainB));
        narrowOverlap += std::min(
            static_cast<double>(narrow.gainA), static_cast<double>(narrow.gainB));
    }
    legacyOverlap /= 12000.0;
    narrowOverlap /= 12000.0;
    std::cerr << "legacy_average_dual_head_overlap=" << legacyOverlap << '\n';
    std::cerr << "production_average_dual_head_overlap=" << narrowOverlap << '\n';
    success &= check(narrowOverlap < 0.82 * legacyOverlap,
                     "production_window_reduces_simultaneous_two_head_energy");

    const auto source = makeVoice(36000);
    const auto block64 = renderWithBlockSize(source, 64);
    const auto block257 = renderWithBlockSize(source, 257);
    double maximumBlockDifference = 0.0;
    double rmsDifference = 0.0;
    for (std::size_t i = 0; i < source.size(); ++i)
    {
        const double difference = static_cast<double>(block64[i])
                                - static_cast<double>(block257[i]);
        maximumBlockDifference = std::max(maximumBlockDifference,
                                          std::abs(difference));
        rmsDifference += difference * difference;
    }
    rmsDifference = std::sqrt(rmsDifference / static_cast<double>(source.size()));
    std::cerr << "streaming_lpc_max_block_difference=" << maximumBlockDifference << '\n';
    std::cerr << "streaming_lpc_rms_block_difference=" << rmsDifference << '\n';
    success &= check(maximumBlockDifference < 2.0e-5,
                     "voice_path_is_independent_of_host_block_size");

    return success ? 0 : 1;
}
