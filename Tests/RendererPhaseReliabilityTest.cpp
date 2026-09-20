#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iostream>
#include <vector>

#define private public
#include "../Source/SingleWetSpectralRenderer.h"
#undef private

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double sampleRate = 48000.0;

bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}

double tonePower(const std::vector<float>& signal,
                 double frequencyHz,
                 int startSample,
                 int sampleCount)
{
    const int start = std::clamp(startSample, 0, static_cast<int>(signal.size()));
    const int end = std::clamp(start + sampleCount, start,
                               static_cast<int>(signal.size()));
    double real = 0.0;
    double imaginary = 0.0;
    for (int sample = start; sample < end; ++sample)
    {
        const double phase = 2.0 * pi * frequencyHz
            * static_cast<double>(sample) / sampleRate;
        const double value = signal[static_cast<std::size_t>(sample)];
        real += value * std::cos(phase);
        imaginary -= value * std::sin(phase);
    }
    return real * real + imaginary * imaginary;
}

double maximumStep(const std::vector<float>& signal,
                   int startSample,
                   int sampleCount)
{
    const int start = std::clamp(startSample + 1, 1,
                                 static_cast<int>(signal.size()));
    const int end = std::clamp(startSample + sampleCount, start,
                               static_cast<int>(signal.size()));
    double maximum = 0.0;
    for (int sample = start; sample < end; ++sample)
    {
        maximum = std::max(maximum,
            std::abs(static_cast<double>(signal[static_cast<std::size_t>(sample)])
                   - static_cast<double>(signal[static_cast<std::size_t>(sample - 1)])));
    }
    return maximum;
}
}

int main()
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 512);
    constexpr int positiveBins = 256;

    // Weak but coherent leakage ridge: every neighbouring bin estimates the
    // same physical instantaneous frequency. Reliability must remain 1 even
    // though the ridge is far below the frame peak.
    std::fill(renderer.magnitudes_.begin(), renderer.magnitudes_.end(), 0.0f);
    std::fill(renderer.previousMagnitudes_.begin(),
              renderer.previousMagnitudes_.end(), 0.0f);
    for (int bin = 0; bin <= positiveBins; ++bin)
        renderer.rawTrueSourceBins_[static_cast<std::size_t>(bin)] =
            static_cast<double>(bin);

    for (int bin = 8; bin <= 12; ++bin)
    {
        renderer.magnitudes_[static_cast<std::size_t>(bin)] = 0.002f;
        renderer.previousMagnitudes_[static_cast<std::size_t>(bin)] = 0.002f;
        renderer.rawTrueSourceBins_[static_cast<std::size_t>(bin)] = 10.35;
    }

    renderer.stabiliseTrueSourceBins(positiveBins, false, 1.0f);
    if (!near(renderer.trueSourceBins_[10], 10.35, 0.02))
    {
        std::cerr << "PHASE_RELIABILITY_COHERENT_RIDGE=FAIL"
                  << " true_bin=" << renderer.trueSourceBins_[10] << "\n";
        return 2;
    }

    // Weak isolated phase excursion: neighbours disagree strongly, so this
    // single-bin velocity must lose authority and collapse toward nominal.
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        renderer.magnitudes_[static_cast<std::size_t>(bin)] = 0.002f;
        renderer.previousMagnitudes_[static_cast<std::size_t>(bin)] = 0.002f;
        renderer.rawTrueSourceBins_[static_cast<std::size_t>(bin)] =
            static_cast<double>(bin);
    }
    renderer.rawTrueSourceBins_[10] = 8.0;

    renderer.stabiliseTrueSourceBins(positiveBins, false, 1.0f);
    if (!near(renderer.trueSourceBins_[10], 10.0, 0.20))
    {
        std::cerr << "PHASE_RELIABILITY_WEAK_ISOLATED=FAIL"
                  << " true_bin=" << renderer.trueSourceBins_[10] << "\n";
        return 3;
    }

    // A strong component is authoritative even if neighbours disagree.
    renderer.magnitudes_[10] = 0.020f;
    renderer.previousMagnitudes_[10] = 0.020f;
    renderer.rawTrueSourceBins_[10] = 8.0;
    renderer.stabiliseTrueSourceBins(positiveBins, false, 1.0f);
    if (!near(renderer.trueSourceBins_[10], 8.0, 0.02))
    {
        std::cerr << "PHASE_RELIABILITY_STRONG_COMPONENT=FAIL"
                  << " true_bin=" << renderer.trueSourceBins_[10] << "\n";
        return 4;
    }

    std::cout << "PHASE_RELIABILITY_COHERENT_RIDGE=PASS\n"
              << "PHASE_RELIABILITY_WEAK_ISOLATED=PASS\n"
              << "PHASE_RELIABILITY_STRONG_COMPONENT=PASS\n";

    // Audio guard: a physically coherent tone may change amplitude abruptly.
    // The new reliability law must not turn that amplitude transient into a
    // pitch/subharmonic transient.
    SingleWetSpectralRenderer audioRenderer;
    audioRenderer.prepare(sampleRate, 512);
    constexpr double sourceHz = 277.0;
    constexpr double targetHz = 440.0;
    const double correctionCents =
        1200.0 * std::log2(targetHz / sourceHz);
    constexpr int totalSamples = 48000;
    constexpr int stepSample = 24000;
    std::vector<float> output(static_cast<std::size_t>(totalSamples));

    for (int sample = 0; sample < totalSamples; ++sample)
    {
        const double amplitude = sample < stepSample ? 0.012 : 0.22;
        const float input = static_cast<float>(amplitude * std::sin(
            2.0 * pi * sourceHz * static_cast<double>(sample) / sampleRate));
        output[static_cast<std::size_t>(sample)] =
            audioRenderer.processSample(input, correctionCents, 0.90f);
    }

    const int transientStart = stepSample + 512;
    const int transientLength = 4096;
    const double targetPower = tonePower(
        output, targetHz, transientStart, transientLength);
    const double halfTargetPower = tonePower(
        output, 0.5 * targetHz, transientStart, transientLength);
    const double sourcePower = tonePower(
        output, sourceHz, transientStart, transientLength);
    const double step = maximumStep(
        output, transientStart - 512, transientLength + 1024);

    if (!(targetPower > 40.0 * halfTargetPower
          && targetPower > 100.0 * sourcePower))
    {
        std::cerr << "PHASE_RELIABILITY_AMPLITUDE_TRANSIENT=FAIL"
                  << " target_half_ratio="
                  << targetPower / std::max(1.0e-20, halfTargetPower)
                  << " target_source_ratio="
                  << targetPower / std::max(1.0e-20, sourcePower) << "\n";
        return 5;
    }

    if (!(step < 0.20))
    {
        std::cerr << "PHASE_RELIABILITY_AMPLITUDE_TRANSIENT=FAIL"
                  << " reason=click max_step=" << step << "\n";
        return 6;
    }

    std::cout << "PHASE_RELIABILITY_AMPLITUDE_TRANSIENT=PASS"
              << " target_half_ratio="
              << targetPower / std::max(1.0e-20, halfTargetPower)
              << " target_source_ratio="
              << targetPower / std::max(1.0e-20, sourcePower)
              << " max_step=" << step << "\n";
    return 0;
}
