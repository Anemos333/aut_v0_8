#include "ModernPitchEngine.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;

[[nodiscard]] double estimateFrequency(const std::vector<float>& signal,
                                       double sampleRate,
                                       double minimumHz,
                                       double maximumHz)
{
    if (signal.size() < 256)
        return 0.0;

    const int minimumLag = std::max(
        2, static_cast<int>(std::floor(sampleRate / maximumHz)));
    const int maximumLag = std::min(
        static_cast<int>(signal.size() / 3),
        static_cast<int>(std::ceil(sampleRate / minimumHz)));

    double bestCorrelation = -1.0;
    int bestLag = minimumLag;
    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        double xy = 0.0;
        double xx = 0.0;
        double yy = 0.0;
        for (std::size_t index = static_cast<std::size_t>(lag);
             index < signal.size();
             ++index)
        {
            const double a = signal[index];
            const double b = signal[index - static_cast<std::size_t>(lag)];
            xy += a * b;
            xx += a * a;
            yy += b * b;
        }

        if (xx <= 1.0e-16 || yy <= 1.0e-16)
            continue;
        const double correlation = xy / std::sqrt(xx * yy);
        if (correlation > bestCorrelation)
        {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }

    return bestLag > 0 ? sampleRate / static_cast<double>(bestLag) : 0.0;
}

[[nodiscard]] bool finiteBuffer(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        const float* data = buffer.getReadPointer(channel);
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            if (!std::isfinite(data[sample]))
                return false;
        }
    }
    return true;
}
} // namespace

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int channelCount = 2;
    constexpr double sourceFrequency = 452.0;
    constexpr double expectedTarget = 440.0;
    constexpr int totalSamples = static_cast<int>(6.0 * sampleRate);
    constexpr int captureSamples = static_cast<int>(1.5 * sampleRate);

    ModernPitchEngine engine;
    engine.prepare(sampleRate,
                   blockSize,
                   channelCount,
                   ModernPitchEngine::LatencyMode::live);

    ModernPitchEngine::Parameters parameters;
    parameters.amount = 1.0f;
    parameters.retuneTimeMs = 0.0f;
    parameters.humanize = 0.35f;
    parameters.formantPreservation = 0.0f;
    parameters.detectorSensitivity = 0.85f;
    parameters.minimumPitchHz = 70.0f;
    parameters.maximumPitchHz = 1200.0f;
    parameters.maximumCorrectionSemitones = 12.0f;
    parameters.stereoMode = ModernPitchEngine::StereoMode::linkedMidSide;
    parameters.minScaleStepCents = 100.0f;

    const double scale[] {
        1.0,
        9.0 / 8.0,
        6.0 / 5.0,
        4.0 / 3.0,
        3.0 / 2.0,
        8.0 / 5.0,
        9.0 / 5.0
    };

    juce::AudioBuffer<float> block(channelCount, blockSize);
    std::vector<float> leftCapture;
    std::vector<float> rightCapture;
    leftCapture.reserve(captureSamples);
    rightCapture.reserve(captureSamples);

    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    int produced = 0;

    while (produced < totalSamples)
    {
        const int samplesThisBlock = std::min(blockSize, totalSamples - produced);
        block.setSize(channelCount,
                      samplesThisBlock,
                      false,
                      false,
                      true);

        float* left = block.getWritePointer(0);
        float* right = block.getWritePointer(1);
        for (int sample = 0; sample < samplesThisBlock; ++sample)
        {
            const double time = static_cast<double>(produced + sample)
                              / sampleRate;
            const double phase = 2.0 * pi * sourceFrequency * time;
            left[sample] = static_cast<float>(0.55 * std::sin(phase)
                                             + 0.12 * std::sin(2.0 * phase));
            right[sample] = static_cast<float>(
                0.22 * std::sin(phase + 0.37)
                + 0.62 * std::sin(2.0 * phase + 0.81));
        }

        engine.process(block,
                       scale,
                       static_cast<int>(std::size(scale)),
                       expectedTarget,
                       parameters);

        if (!finiteBuffer(block))
        {
            std::cerr << "non_finite_output=1\n";
            return EXIT_FAILURE;
        }

        const int captureStart = totalSamples - captureSamples;
        for (int sample = 0; sample < samplesThisBlock; ++sample)
        {
            const int absolute = produced + sample;
            const float l = block.getSample(0, sample);
            const float r = block.getSample(1, sample);
            leftEnergy += static_cast<double>(l) * l;
            rightEnergy += static_cast<double>(r) * r;
            if (absolute >= captureStart)
            {
                leftCapture.push_back(l);
                rightCapture.push_back(r);
            }
        }

        produced += samplesThisBlock;
    }

    const auto meter = engine.getMetering();
    const double leftFrequency = estimateFrequency(leftCapture,
                                                   sampleRate,
                                                   380.0,
                                                   500.0);
    const double rightFrequency = estimateFrequency(rightCapture,
                                                    sampleRate,
                                                    380.0,
                                                    500.0);

    const bool targetCorrect = std::abs(
        static_cast<double>(meter.targetPitchHz) - expectedTarget) < 1.0;
    const bool leftCorrect = std::abs(leftFrequency - expectedTarget) < 6.0;
    const bool rightCorrect = std::abs(rightFrequency - expectedTarget) < 6.0;
    const bool bothChannelsAlive = leftEnergy > 1.0
        && rightEnergy > 1.0;
    const bool latencyCorrect = engine.getLatencySamples() == 256;

    std::cout
        << "target_hz=" << meter.targetPitchHz << '\n'
        << "detected_hz=" << meter.detectedPitchHz << '\n'
        << "left_output_hz=" << leftFrequency << '\n'
        << "right_output_hz=" << rightFrequency << '\n'
        << "left_energy=" << leftEnergy << '\n'
        << "right_energy=" << rightEnergy << '\n'
        << "target_correct=" << targetCorrect << '\n'
        << "left_correct=" << leftCorrect << '\n'
        << "right_correct=" << rightCorrect << '\n'
        << "both_channels_alive=" << bothChannelsAlive << '\n'
        << "latency_correct=" << latencyCorrect << '\n';

    return targetCorrect
        && leftCorrect
        && rightCorrect
        && bothChannelsAlive
        && latencyCorrect
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
