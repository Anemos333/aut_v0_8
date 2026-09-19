#include "SingleWetSpectralRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double sampleRate = 48000.0;

double centsError(double measuredHz, double expectedHz)
{
    if (!(measuredHz > 0.0) || !(expectedHz > 0.0))
        return 1.0e9;
    return 1200.0 * std::log2(measuredHz / expectedHz);
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

double harmonicScore(const std::vector<float>& signal,
                     double fundamentalHz,
                     int startSample,
                     int sampleCount)
{
    if (!(fundamentalHz > 0.0))
        return 0.0;

    double score = 0.0;
    double weightSum = 0.0;
    for (int harmonic = 1; harmonic <= 10; ++harmonic)
    {
        const double frequency = fundamentalHz * static_cast<double>(harmonic);
        if (frequency >= 0.46 * sampleRate)
            break;

        // Weight odd and low-order harmonics slightly more strongly so an
        // octave hypothesis cannot win merely by matching the even subset.
        const double weight = (1.0 / static_cast<double>(harmonic))
            * ((harmonic & 1) ? 1.35 : 1.0);
        score += weight * tonePowerRange(signal, frequency, startSample, sampleCount);
        weightSum += weight;
    }
    return score / std::max(1.0e-20, weightSum);
}

double estimateHarmonicFundamental(const std::vector<float>& signal,
                                   double expectedHz,
                                   int startSample,
                                   int sampleCount)
{
    double bestHz = expectedHz;
    double bestScore = -1.0;

    // Search +/- 80 cents around the commanded target. This estimator is not
    // allowed to "solve" octave errors by wrapping the answer.
    for (double cents = -80.0; cents <= 80.0001; cents += 0.5)
    {
        const double candidate = expectedHz * std::exp2(cents / 1200.0);
        const double score = harmonicScore(signal, candidate, startSample, sampleCount);
        if (score > bestScore)
        {
            bestScore = score;
            bestHz = candidate;
        }
    }

    double leftCents = 1200.0 * std::log2(bestHz / expectedHz) - 0.8;
    double rightCents = leftCents + 1.6;
    for (int iteration = 0; iteration < 18; ++iteration)
    {
        const double third = (rightCents - leftCents) / 3.0;
        const double aCents = leftCents + third;
        const double bCents = rightCents - third;
        const double aHz = expectedHz * std::exp2(aCents / 1200.0);
        const double bHz = expectedHz * std::exp2(bCents / 1200.0);
        if (harmonicScore(signal, aHz, startSample, sampleCount)
            < harmonicScore(signal, bHz, startSample, sampleCount))
        {
            leftCents = aCents;
        }
        else
        {
            rightCents = bCents;
        }
    }
    return expectedHz * std::exp2(0.5 * (leftCents + rightCents) / 1200.0);
}

struct VowelProfile
{
    const char* name;
    std::array<double, 3> formantHz;
    std::array<double, 3> bandwidthHz;
};

double vowelHarmonicGain(double frequencyHz, const VowelProfile& vowel)
{
    double envelope = 0.018;
    for (std::size_t index = 0; index < vowel.formantHz.size(); ++index)
    {
        const double distance = (frequencyHz - vowel.formantHz[index])
            / std::max(1.0, vowel.bandwidthHz[index]);
        envelope += std::exp(-0.5 * distance * distance);
    }
    return envelope;
}

std::vector<float> renderStaticVowel(int frameSize,
                                     double sourceF0,
                                     double correctionCents,
                                     const VowelProfile& vowel,
                                     float formantPreservation)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);

    constexpr int sampleCount = 120000;
    std::vector<float> output(static_cast<std::size_t>(sampleCount));
    const int maximumHarmonic = std::min(
        28, static_cast<int>(std::floor(9000.0 / sourceF0)));

    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const double t = static_cast<double>(sample) / sampleRate;
        double input = 0.0;
        for (int harmonic = 1; harmonic <= maximumHarmonic; ++harmonic)
        {
            const double frequency = sourceF0 * static_cast<double>(harmonic);
            const double rolloff = 1.0 / std::pow(static_cast<double>(harmonic), 1.08);
            const double envelope = vowelHarmonicGain(frequency, vowel);
            const double phase = 0.17 * static_cast<double>(harmonic)
                + 0.013 * static_cast<double>(harmonic * harmonic);
            input += rolloff * envelope
                * std::sin(2.0 * pi * frequency * t + phase);
        }

        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            static_cast<float>(0.11 * input), correctionCents, formantPreservation);
    }
    return output;
}

std::vector<float> renderCompensatedVibratoVowel(int frameSize,
                                                  double sourceCentreHz,
                                                  double targetHz,
                                                  const VowelProfile& vowel,
                                                  float formantPreservation)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);

    constexpr int sampleCount = 144000;
    std::vector<float> output(static_cast<std::size_t>(sampleCount));

    std::array<double, 24> phases {};
    for (std::size_t harmonic = 1; harmonic < phases.size(); ++harmonic)
        phases[harmonic] = 0.11 * static_cast<double>(harmonic);

    double fundamentalPhase = 0.0;
    for (int sample = 0; sample < sampleCount; ++sample)
    {
        const double t = static_cast<double>(sample) / sampleRate;

        // A realistic, deliberately modest sung-vowel motion. The renderer gets
        // the exact inverse command sample-by-sample, so the acoustic target is
        // mathematically constant even though the source coordinate moves.
        const double vibratoCents =
            18.0 * std::sin(2.0 * pi * 5.2 * t)
            + 4.0 * std::sin(2.0 * pi * 1.15 * t + 0.7);
        const double sourceF0 = sourceCentreHz * std::exp2(vibratoCents / 1200.0);
        fundamentalPhase += 2.0 * pi * sourceF0 / sampleRate;
        fundamentalPhase -= 2.0 * pi * std::floor(fundamentalPhase / (2.0 * pi));

        double input = 0.0;
        for (int harmonic = 1; harmonic < static_cast<int>(phases.size()); ++harmonic)
        {
            const double frequency = sourceF0 * static_cast<double>(harmonic);
            if (frequency > 9000.0)
                break;
            const double rolloff = 1.0 / std::pow(static_cast<double>(harmonic), 1.05);
            const double envelope = vowelHarmonicGain(frequency, vowel);
            const double slowEnvelopeMotion = 1.0
                + 0.10 * std::sin(2.0 * pi * 0.63 * t
                    + 0.29 * static_cast<double>(harmonic));
            input += rolloff * envelope * slowEnvelopeMotion
                * std::sin(static_cast<double>(harmonic) * fundamentalPhase
                    + phases[static_cast<std::size_t>(harmonic)]);
        }

        const double correctionCents = 1200.0 * std::log2(targetHz / sourceF0);
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            static_cast<float>(0.10 * input), correctionCents, formantPreservation);
    }
    return output;
}

struct WindowStats
{
    double medianErrorCents = 0.0;
    double maxAbsErrorCents = 0.0;
    double errorSpanCents = 0.0;
    double minimumTrueToHalfOctaveScore = std::numeric_limits<double>::infinity();
    double minimumTrueToDoubleOctaveScore = std::numeric_limits<double>::infinity();
};

WindowStats analyseWindows(const std::vector<float>& output,
                           double expectedF0,
                           int startSample,
                           int windowSamples,
                           int strideSamples)
{
    std::vector<double> errors;
    double minimumHalfRatio = std::numeric_limits<double>::infinity();
    double minimumDoubleRatio = std::numeric_limits<double>::infinity();

    for (int start = startSample;
         start + windowSamples <= static_cast<int>(output.size());
         start += strideSamples)
    {
        const double measured = estimateHarmonicFundamental(
            output, expectedF0, start, windowSamples);
        errors.push_back(centsError(measured, expectedF0));

        const double trueScore = harmonicScore(output, expectedF0, start, windowSamples);
        const double halfScore = harmonicScore(output, 0.5 * expectedF0, start, windowSamples);
        const double doubleScore = harmonicScore(output, 2.0 * expectedF0, start, windowSamples);
        minimumHalfRatio = std::min(minimumHalfRatio,
            trueScore / std::max(1.0e-20, halfScore));
        minimumDoubleRatio = std::min(minimumDoubleRatio,
            trueScore / std::max(1.0e-20, doubleScore));
    }

    WindowStats result;
    if (errors.empty())
        return result;

    auto sorted = errors;
    std::sort(sorted.begin(), sorted.end());
    result.medianErrorCents = sorted[sorted.size() / 2];
    const auto [minimum, maximum] = std::minmax_element(errors.begin(), errors.end());
    result.errorSpanCents = *maximum - *minimum;
    for (const double error : errors)
        result.maxAbsErrorCents = std::max(result.maxAbsErrorCents, std::abs(error));
    result.minimumTrueToHalfOctaveScore = minimumHalfRatio;
    result.minimumTrueToDoubleOctaveScore = minimumDoubleRatio;
    return result;
}

void printStats(const char* type,
                int frameSize,
                const char* vowel,
                double sourceF0,
                double correctionCents,
                double expectedF0,
                const WindowStats& stats)
{
    std::cerr << "RENDERER_VOICE_TRUTH"
              << " type=" << type
              << " frame=" << frameSize
              << " vowel=" << vowel
              << " source_f0=" << sourceF0
              << " correction_cents=" << correctionCents
              << " expected_f0=" << expectedF0
              << " median_error_cents=" << stats.medianErrorCents
              << " max_abs_error_cents=" << stats.maxAbsErrorCents
              << " error_span_cents=" << stats.errorSpanCents
              << " true_half_octave_ratio=" << stats.minimumTrueToHalfOctaveScore
              << " true_double_octave_ratio=" << stats.minimumTrueToDoubleOctaveScore
              << '\n';
}

} // namespace

int main()
{
    const std::array<VowelProfile, 3> vowels {{
        { "a", { 730.0, 1090.0, 2440.0 }, { 95.0, 125.0, 180.0 } },
        { "i", { 300.0, 2290.0, 3010.0 }, { 75.0, 150.0, 210.0 } },
        { "u", { 330.0, 870.0, 2240.0 }, { 80.0, 110.0, 190.0 } }
    }};

    struct StaticCase
    {
        double sourceF0;
        double cents;
    };
    const std::array<StaticCase, 6> staticCases {{
        { 118.0,  37.0 },
        { 173.0, -63.0 },
        { 219.0,  91.0 },
        { 277.0, -34.0 },
        { 196.0, 700.0 },
        { 247.0, -500.0 }
    }};

    bool broadContractPass = true;

    for (const int frameSize : { 512, 256 })
    {
        for (const auto& vowel : vowels)
        {
            for (const auto& testCase : staticCases)
            {
                const double expectedF0 = testCase.sourceF0
                    * std::exp2(testCase.cents / 1200.0);
                const auto output = renderStaticVowel(
                    frameSize, testCase.sourceF0, testCase.cents, vowel, 0.92f);
                const auto stats = analyseWindows(
                    output, expectedF0, 24000, 8192, 4096);
                printStats("static", frameSize, vowel.name,
                           testCase.sourceF0, testCase.cents, expectedF0, stats);

                // Deliberately broad census guard: failures here indicate a
                // structural reconstruction fault, not a tuning preference.
                broadContractPass &= std::abs(stats.medianErrorCents) < 6.0;
                broadContractPass &= stats.maxAbsErrorCents < 18.0;
                broadContractPass &= stats.errorSpanCents < 22.0;
            }

            const double sourceCentre = 196.0;
            const double target = 220.0;
            const auto moving = renderCompensatedVibratoVowel(
                frameSize, sourceCentre, target, vowel, 0.92f);
            const auto movingStats = analyseWindows(
                moving, target, 24000, 8192, 4096);
            const double nominalCorrection = 1200.0 * std::log2(target / sourceCentre);
            printStats("compensated_vibrato", frameSize, vowel.name,
                       sourceCentre, nominalCorrection, target, movingStats);

            broadContractPass &= std::abs(movingStats.medianErrorCents) < 8.0;
            broadContractPass &= movingStats.maxAbsErrorCents < 28.0;
            broadContractPass &= movingStats.errorSpanCents < 34.0;
        }
    }

    std::cerr << "RENDERER_VOICE_TRUTH_CORPUS="
              << (broadContractPass ? "PASS" : "FAIL") << '\n';
    return broadContractPass ? 0 : 1;
}
