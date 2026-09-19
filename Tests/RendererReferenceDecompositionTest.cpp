#include "SingleWetSpectralRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double sampleRate = 48000.0;

struct VowelProfile
{
    const char* name;
    std::array<double, 3> formantHz;
    std::array<double, 3> bandwidthHz;
};

double vowelEnvelope(double frequencyHz, const VowelProfile& vowel)
{
    double envelope = 0.18;
    for (std::size_t index = 0; index < vowel.formantHz.size(); ++index)
    {
        const double distance = (frequencyHz - vowel.formantHz[index])
            / std::max(1.0, vowel.bandwidthHz[index]);
        envelope += 0.82 * std::exp(-0.5 * distance * distance);
    }
    return envelope;
}

std::vector<double> harmonicAmplitudes(double sourceF0, const VowelProfile& vowel)
{
    const int maximumHarmonic = std::min(
        24, static_cast<int>(std::floor(8500.0 / sourceF0)));
    std::vector<double> amplitudes(static_cast<std::size_t>(maximumHarmonic + 1), 0.0);
    for (int harmonic = 1; harmonic <= maximumHarmonic; ++harmonic)
    {
        const double frequency = sourceF0 * static_cast<double>(harmonic);
        const double rolloff = 1.0 / std::pow(static_cast<double>(harmonic), 1.06);
        amplitudes[static_cast<std::size_t>(harmonic)] =
            0.075 * rolloff * vowelEnvelope(frequency, vowel);
    }
    return amplitudes;
}

double harmonicPhase(int harmonic)
{
    return 0.19 * static_cast<double>(harmonic)
        + 0.009 * static_cast<double>(harmonic * harmonic);
}

std::vector<float> makeHarmonicSignal(double fundamentalHz,
                                      const std::vector<double>& amplitudes,
                                      int totalSamples)
{
    std::vector<float> signal(static_cast<std::size_t>(totalSamples));
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        const double t = static_cast<double>(sample) / sampleRate;
        double value = 0.0;
        for (int harmonic = 1; harmonic < static_cast<int>(amplitudes.size()); ++harmonic)
        {
            value += amplitudes[static_cast<std::size_t>(harmonic)]
                * std::sin(2.0 * pi * fundamentalHz
                    * static_cast<double>(harmonic) * t
                    + harmonicPhase(harmonic));
        }
        signal[static_cast<std::size_t>(sample)] = static_cast<float>(value);
    }
    return signal;
}

std::vector<float> render(const std::vector<float>& input,
                          int frameSize,
                          double correctionCents)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);
    std::vector<float> output(input.size());
    for (int sample = 0; sample < static_cast<int>(input.size()); ++sample)
    {
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            input[static_cast<std::size_t>(sample)],
            correctionCents,
            0.0f);
    }
    return output;
}

double tonePowerRange(const std::vector<float>& signal,
                      double frequencyHz,
                      int startSample,
                      int sampleCount)
{
    const int start = std::clamp(startSample, 0, static_cast<int>(signal.size()));
    const int end = std::clamp(start + sampleCount, start, static_cast<int>(signal.size()));
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

double estimateNear(const std::vector<float>& signal,
                    double expectedHz,
                    int startSample,
                    int sampleCount)
{
    double bestCents = 0.0;
    double bestPower = -1.0;
    for (double cents = -45.0; cents <= 45.0001; cents += 0.75)
    {
        const double frequency = expectedHz * std::exp2(cents / 1200.0);
        const double power = tonePowerRange(signal, frequency, startSample, sampleCount);
        if (power > bestPower)
        {
            bestPower = power;
            bestCents = cents;
        }
    }

    double left = bestCents - 1.0;
    double right = bestCents + 1.0;
    for (int iteration = 0; iteration < 15; ++iteration)
    {
        const double third = (right - left) / 3.0;
        const double a = left + third;
        const double b = right - third;
        const double pa = tonePowerRange(
            signal, expectedHz * std::exp2(a / 1200.0), startSample, sampleCount);
        const double pb = tonePowerRange(
            signal, expectedHz * std::exp2(b / 1200.0), startSample, sampleCount);
        if (pa < pb)
            left = a;
        else
            right = b;
    }
    return expectedHz * std::exp2(0.5 * (left + right) / 1200.0);
}

double centsError(double measuredHz, double expectedHz)
{
    return 1200.0 * std::log2(measuredHz / expectedHz);
}

struct NullStats
{
    double gain = 0.0;
    double normalisedRmse = 0.0;
    double correlation = 0.0;
};

NullStats unityNull(const std::vector<float>& input,
                    const std::vector<float>& output,
                    int latency,
                    int startInput,
                    int sampleCount)
{
    const int count = std::min({
        sampleCount,
        static_cast<int>(input.size()) - startInput,
        static_cast<int>(output.size()) - (startInput + latency)
    });

    double xx = 0.0;
    double yy = 0.0;
    double xy = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double x = input[static_cast<std::size_t>(startInput + i)];
        const double y = output[static_cast<std::size_t>(startInput + latency + i)];
        xx += x * x;
        yy += y * y;
        xy += x * y;
    }

    NullStats stats;
    stats.gain = xy / std::max(1.0e-20, xx);
    stats.correlation = xy / std::sqrt(std::max(1.0e-20, xx * yy));

    double residual = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const double x = input[static_cast<std::size_t>(startInput + i)];
        const double y = output[static_cast<std::size_t>(startInput + latency + i)];
        const double d = y - stats.gain * x;
        residual += d * d;
    }
    stats.normalisedRmse = std::sqrt(
        residual / std::max(1.0e-20, stats.gain * stats.gain * xx));
    return stats;
}

} // namespace

int main()
{
    const std::array<VowelProfile, 3> vowels {{
        { "a", { 730.0, 1090.0, 2440.0 }, { 95.0, 125.0, 180.0 } },
        { "i", { 300.0, 2290.0, 3010.0 }, { 75.0, 150.0, 210.0 } },
        { "u", { 330.0, 870.0, 2240.0 }, { 80.0, 110.0, 190.0 } }
    }};

    struct ShiftCase
    {
        double sourceF0;
        double correctionCents;
    };
    const std::array<ShiftCase, 3> shifts {{
        { 118.0,  37.0 },
        { 196.0, 700.0 },
        { 277.0, -34.0 }
    }};

    constexpr int totalSamples = 72000;
    constexpr int analysisStart = 16000;
    constexpr int analysisCount = 32768;

    bool pass = true;

    for (const int frameSize : { 512, 256 })
    {
        for (const auto& vowel : vowels)
        {
            // Unity/null truth first.
            const auto amplitudes = harmonicAmplitudes(118.0, vowel);
            const auto input = makeHarmonicSignal(118.0, amplitudes, totalSamples);
            const auto unity = render(input, frameSize, 0.0);
            const auto nullStats = unityNull(
                input, unity, frameSize, 10000, 36000);

            std::cerr << "RENDERER_REFERENCE_NULL"
                      << " frame=" << frameSize
                      << " vowel=" << vowel.name
                      << " gain=" << nullStats.gain
                      << " correlation=" << nullStats.correlation
                      << " normalised_rmse=" << nullStats.normalisedRmse
                      << '\n';

            // Loose census guards: exact conclusions come from the printed
            // values. A gross failure means unity reconstruction itself is bad.
            pass &= nullStats.correlation > 0.95;
            pass &= nullStats.normalisedRmse < 0.35;

            for (const auto& testCase : shifts)
            {
                const auto sourceAmplitudes =
                    harmonicAmplitudes(testCase.sourceF0, vowel);
                const auto source = makeHarmonicSignal(
                    testCase.sourceF0, sourceAmplitudes, totalSamples);
                const auto output = render(
                    source, frameSize, testCase.correctionCents);

                const double expectedF0 = testCase.sourceF0
                    * std::exp2(testCase.correctionCents / 1200.0);
                const auto ideal = makeHarmonicSignal(
                    expectedF0, sourceAmplitudes, totalSamples);

                const double rendererMeasured = estimateNear(
                    output, expectedF0, analysisStart, analysisCount);
                const double idealMeasured = estimateNear(
                    ideal, expectedF0, analysisStart, analysisCount);
                const double rendererError = centsError(
                    rendererMeasured, expectedF0);
                const double idealEstimatorError = centsError(
                    idealMeasured, expectedF0);
                const double excessError =
                    rendererError - idealEstimatorError;

                const double truePowerOut = tonePowerRange(
                    output, expectedF0, analysisStart, analysisCount);
                const double doublePowerOut = tonePowerRange(
                    output, 2.0 * expectedF0, analysisStart, analysisCount);
                const double truePowerIdeal = tonePowerRange(
                    ideal, expectedF0, analysisStart, analysisCount);
                const double doublePowerIdeal = tonePowerRange(
                    ideal, 2.0 * expectedF0, analysisStart, analysisCount);

                std::cerr << "RENDERER_REFERENCE_SHIFT"
                          << " frame=" << frameSize
                          << " vowel=" << vowel.name
                          << " source_f0=" << testCase.sourceF0
                          << " correction_cents=" << testCase.correctionCents
                          << " expected_f0=" << expectedF0
                          << " renderer_error_cents=" << rendererError
                          << " ideal_estimator_error_cents=" << idealEstimatorError
                          << " excess_error_cents=" << excessError
                          << " out_f0_to_2f0="
                          << truePowerOut / std::max(1.0e-20, doublePowerOut)
                          << " ideal_f0_to_2f0="
                          << truePowerIdeal / std::max(1.0e-20, doublePowerIdeal)
                          << '\n';

                pass &= std::abs(excessError) < 35.0;
            }
        }
    }

    std::cerr << "RENDERER_REFERENCE_DECOMPOSITION="
              << (pass ? "PASS" : "FAIL") << '\n';
    return pass ? 0 : 1;
}
