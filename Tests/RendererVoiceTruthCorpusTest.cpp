#include "SingleWetSpectralRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double sampleRate = 48000.0;

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

double centsError(double measuredHz, double expectedHz)
{
    if (!(measuredHz > 0.0) || !(expectedHz > 0.0))
        return 1.0e9;
    return 1200.0 * std::log2(measuredHz / expectedHz);
}

double estimateFundamentalNearExpected(const std::vector<float>& signal,
                                       double expectedHz,
                                       int startSample,
                                       int sampleCount)
{
    double bestCents = 0.0;
    double bestPower = -1.0;
    for (double cents = -40.0; cents <= 40.0001; cents += 1.0)
    {
        const double frequency = expectedHz * std::exp2(cents / 1200.0);
        const double power = tonePowerRange(signal, frequency, startSample, sampleCount);
        if (power > bestPower)
        {
            bestPower = power;
            bestCents = cents;
        }
    }

    double left = bestCents - 1.25;
    double right = bestCents + 1.25;
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        const double third = (right - left) / 3.0;
        const double aCents = left + third;
        const double bCents = right - third;
        const double aHz = expectedHz * std::exp2(aCents / 1200.0);
        const double bHz = expectedHz * std::exp2(bCents / 1200.0);
        if (tonePowerRange(signal, aHz, startSample, sampleCount)
            < tonePowerRange(signal, bHz, startSample, sampleCount))
        {
            left = aCents;
        }
        else
        {
            right = bCents;
        }
    }
    return expectedHz * std::exp2(0.5 * (left + right) / 1200.0);
}

double octaveEvidenceScore(const std::vector<float>& signal,
                           double hypothesisF0,
                           int startSample,
                           int sampleCount)
{
    // Keep odd harmonics visible so the true fundamental has evidence that a
    // doubled-octave hypothesis cannot reproduce merely by matching evens.
    constexpr std::array<int, 6> harmonics { 1, 2, 3, 5, 7, 9 };
    constexpr std::array<double, 6> weights { 1.7, 1.0, 1.25, 0.90, 0.65, 0.50 };
    double score = 0.0;
    double weightSum = 0.0;
    for (std::size_t i = 0; i < harmonics.size(); ++i)
    {
        const double frequency = hypothesisF0 * static_cast<double>(harmonics[i]);
        if (frequency >= 0.46 * sampleRate)
            continue;
        score += weights[i] * tonePowerRange(signal, frequency, startSample, sampleCount);
        weightSum += weights[i];
    }
    return score / std::max(1.0e-20, weightSum);
}

struct VowelProfile
{
    const char* name;
    std::array<double, 3> formantHz;
    std::array<double, 3> bandwidthHz;
};

double vowelEnvelope(double frequencyHz, const VowelProfile& vowel)
{
    // A glottal floor preserves a measurable fundamental while three resonant
    // regions create a strongly multi-harmonic, vowel-like spectrum.
    double envelope = 0.18;
    for (std::size_t index = 0; index < vowel.formantHz.size(); ++index)
    {
        const double distance = (frequencyHz - vowel.formantHz[index])
            / std::max(1.0, vowel.bandwidthHz[index]);
        envelope += 0.82 * std::exp(-0.5 * distance * distance);
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

    constexpr int totalSamples = 60000;
    std::vector<float> output(static_cast<std::size_t>(totalSamples));
    const int maximumHarmonic = std::min(
        24, static_cast<int>(std::floor(8500.0 / sourceF0)));

    for (int sample = 0; sample < totalSamples; ++sample)
    {
        const double t = static_cast<double>(sample) / sampleRate;
        double input = 0.0;
        for (int harmonic = 1; harmonic <= maximumHarmonic; ++harmonic)
        {
            const double frequency = sourceF0 * static_cast<double>(harmonic);
            const double rolloff = 1.0 / std::pow(static_cast<double>(harmonic), 1.06);
            const double phase = 0.19 * static_cast<double>(harmonic)
                + 0.009 * static_cast<double>(harmonic * harmonic);
            input += rolloff * vowelEnvelope(frequency, vowel)
                * std::sin(2.0 * pi * frequency * t + phase);
        }
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            static_cast<float>(0.075 * input), correctionCents, formantPreservation);
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

    constexpr int totalSamples = 72000;
    std::vector<float> output(static_cast<std::size_t>(totalSamples));
    double fundamentalPhase = 0.0;

    for (int sample = 0; sample < totalSamples; ++sample)
    {
        const double t = static_cast<double>(sample) / sampleRate;
        const double vibratoCents =
            18.0 * std::sin(2.0 * pi * 5.2 * t)
            + 4.0 * std::sin(2.0 * pi * 1.15 * t + 0.7);
        const double sourceF0 = sourceCentreHz * std::exp2(vibratoCents / 1200.0);

        fundamentalPhase += 2.0 * pi * sourceF0 / sampleRate;
        fundamentalPhase -= 2.0 * pi * std::floor(fundamentalPhase / (2.0 * pi));

        double input = 0.0;
        for (int harmonic = 1; harmonic <= 22; ++harmonic)
        {
            const double frequency = sourceF0 * static_cast<double>(harmonic);
            if (frequency >= 8500.0)
                break;
            const double rolloff = 1.0 / std::pow(static_cast<double>(harmonic), 1.04);
            const double slowEnvelopeMotion = 1.0
                + 0.08 * std::sin(2.0 * pi * 0.63 * t
                    + 0.31 * static_cast<double>(harmonic));
            input += rolloff * vowelEnvelope(frequency, vowel) * slowEnvelopeMotion
                * std::sin(static_cast<double>(harmonic) * fundamentalPhase
                    + 0.13 * static_cast<double>(harmonic));
        }

        const double correctionCents = 1200.0 * std::log2(targetHz / sourceF0);
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            static_cast<float>(0.07 * input), correctionCents, formantPreservation);
    }
    return output;
}

struct WindowStats
{
    double medianErrorCents = 0.0;
    double maxAbsErrorCents = 0.0;
    double errorSpanCents = 0.0;
    double minimumTrueToHalfOctave = std::numeric_limits<double>::infinity();
    double minimumTrueToDoubleOctave = std::numeric_limits<double>::infinity();
};

WindowStats analyseWindows(const std::vector<float>& output,
                           double expectedF0,
                           int startSample)
{
    constexpr int windowSamples = 4096;
    constexpr int strideSamples = 6144;

    std::vector<double> errors;
    WindowStats stats;

    for (int start = startSample;
         start + windowSamples <= static_cast<int>(output.size());
         start += strideSamples)
    {
        const double measured = estimateFundamentalNearExpected(
            output, expectedF0, start, windowSamples);
        errors.push_back(centsError(measured, expectedF0));

        const double trueScore = octaveEvidenceScore(
            output, expectedF0, start, windowSamples);
        const double halfScore = octaveEvidenceScore(
            output, expectedF0 * 0.5, start, windowSamples);
        const double doubleScore = octaveEvidenceScore(
            output, expectedF0 * 2.0, start, windowSamples);
        stats.minimumTrueToHalfOctave = std::min(
            stats.minimumTrueToHalfOctave,
            trueScore / std::max(1.0e-20, halfScore));
        stats.minimumTrueToDoubleOctave = std::min(
            stats.minimumTrueToDoubleOctave,
            trueScore / std::max(1.0e-20, doubleScore));
    }

    if (errors.empty())
        return stats;

    auto sorted = errors;
    std::sort(sorted.begin(), sorted.end());
    stats.medianErrorCents = sorted[sorted.size() / 2];

    const auto [minimum, maximum] = std::minmax_element(errors.begin(), errors.end());
    stats.errorSpanCents = *maximum - *minimum;
    for (const double error : errors)
        stats.maxAbsErrorCents = std::max(stats.maxAbsErrorCents, std::abs(error));
    return stats;
}

void printStats(const char* type,
                int frameSize,
                const char* vowelName,
                double formantPreservation,
                double sourceF0,
                double correctionCents,
                double expectedF0,
                const WindowStats& stats)
{
    std::cerr << "RENDERER_VOICE_TRUTH"
              << " type=" << type
              << " frame=" << frameSize
              << " vowel=" << vowelName
              << " formant=" << formantPreservation
              << " source_f0=" << sourceF0
              << " correction_cents=" << correctionCents
              << " expected_f0=" << expectedF0
              << " median_error_cents=" << stats.medianErrorCents
              << " max_abs_error_cents=" << stats.maxAbsErrorCents
              << " error_span_cents=" << stats.errorSpanCents
              << " true_half_octave_ratio=" << stats.minimumTrueToHalfOctave
              << " true_double_octave_ratio=" << stats.minimumTrueToDoubleOctave
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
        double correctionCents;
    };
    // Focused decomposition census:
    //  - 0 cents: reconstruction truth without pitch transport.
    //  - +37 cents at low F0: strongest observed cents bias.
    //  - +700 cents: strongest octave-evidence ambiguity.
    //  - -34 cents at 277 Hz: known-good control.
    const std::array<StaticCase, 4> staticCases {{
        { 118.0,   0.0 },
        { 118.0,  37.0 },
        { 196.0, 700.0 },
        { 277.0, -34.0 }
    }};
    const std::array<float, 2> formantAmounts {{ 0.0f, 0.92f }};

    bool broadContractPass = true;

    for (const int frameSize : { 512, 256 })
    {
        for (const auto& vowel : vowels)
        {
            for (const float formantAmount : formantAmounts)
            {
                for (const auto& testCase : staticCases)
                {
                    const double expectedF0 = testCase.sourceF0
                        * std::exp2(testCase.correctionCents / 1200.0);
                    const auto output = renderStaticVowel(
                        frameSize,
                        testCase.sourceF0,
                        testCase.correctionCents,
                        vowel,
                        formantAmount);
                    const auto stats = analyseWindows(output, expectedF0, 12000);

                    printStats("static",
                               frameSize,
                               vowel.name,
                               formantAmount,
                               testCase.sourceF0,
                               testCase.correctionCents,
                               expectedF0,
                               stats);

                    broadContractPass &= std::abs(stats.medianErrorCents) < 5.0;
                    broadContractPass &= stats.maxAbsErrorCents < 14.0;
                    broadContractPass &= stats.errorSpanCents < 18.0;
                }

                const double sourceCentre = 196.0;
                const double target = 220.0;
                const double nominalCorrection =
                    1200.0 * std::log2(target / sourceCentre);
                const auto moving = renderCompensatedVibratoVowel(
                    frameSize, sourceCentre, target, vowel, formantAmount);
                const auto movingStats = analyseWindows(moving, target, 12000);

                printStats("compensated_vibrato",
                           frameSize,
                           vowel.name,
                           formantAmount,
                           sourceCentre,
                           nominalCorrection,
                           target,
                           movingStats);

                broadContractPass &= std::abs(movingStats.medianErrorCents) < 7.0;
                broadContractPass &= movingStats.maxAbsErrorCents < 22.0;
                broadContractPass &= movingStats.errorSpanCents < 28.0;
            }
        }
    }

    std::cerr << "RENDERER_VOICE_TRUTH_CORPUS="
              << (broadContractPass ? "PASS" : "FAIL") << '\n';
    return broadContractPass ? 0 : 1;
}
