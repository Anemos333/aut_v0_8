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

bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

double tonePower(const std::vector<float>& signal,
                 double frequencyHz,
                 int startSample)
{
    double real = 0.0;
    double imaginary = 0.0;
    for (int n = startSample; n < static_cast<int>(signal.size()); ++n)
    {
        const double phase = 2.0 * pi * frequencyHz
                           * static_cast<double>(n) / sampleRate;
        const double x = static_cast<double>(signal[static_cast<std::size_t>(n)]);
        real += x * std::cos(phase);
        imaginary -= x * std::sin(phase);
    }
    return real * real + imaginary * imaginary;
}

std::vector<float> renderTone(double correctionCents,
                              double formant,
                              bool flipF0)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 128);
    std::vector<float> out(72000);
    for (int n = 0; n < static_cast<int>(out.size()); ++n)
    {
        const float x = 0.22f * static_cast<float>(
            std::sin(2.0 * pi * 220.0 * static_cast<double>(n) / sampleRate));
        const double suppliedF0 = flipF0 && n >= 36000 ? 110.0 : 220.0;
        out[static_cast<std::size_t>(n)] = renderer.processSample(
            x, correctionCents, static_cast<float>(formant), suppliedF0);
    }
    return out;
}

std::vector<float> renderHarmonic(bool flipF0, double formant)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 128);
    constexpr double fundamental = 173.70;
    std::vector<float> out(72000);
    for (int n = 0; n < static_cast<int>(out.size()); ++n)
    {
        double x = 0.0;
        for (int h = 1; h <= 8; ++h)
        {
            x += (0.12 / static_cast<double>(h)) * std::sin(
                2.0 * pi * fundamental * static_cast<double>(h)
                * static_cast<double>(n) / sampleRate
                + 0.17 * static_cast<double>(h));
        }
        const double suppliedF0 = flipF0 && n >= 36000
            ? fundamental * 0.5 : fundamental;
        out[static_cast<std::size_t>(n)] = renderer.processSample(
            static_cast<float>(x), 137.60, static_cast<float>(formant), suppliedF0);
    }
    return out;
}

std::vector<float> renderInharmonic()
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 128);
    constexpr std::array<double, 4> frequencies {277.0, 401.0, 593.0, 877.0};
    constexpr std::array<double, 4> phases {0.17, 0.73, 1.31, 2.03};
    std::vector<float> out(72000);
    for (int n = 0; n < static_cast<int>(out.size()); ++n)
    {
        double x = 0.0;
        for (std::size_t i = 0; i < frequencies.size(); ++i)
            x += 0.055 * std::sin(2.0 * pi * frequencies[i]
                * static_cast<double>(n) / sampleRate + phases[i]);
        out[static_cast<std::size_t>(n)] = renderer.processSample(
            static_cast<float>(x), 500.0, 0.9f, n < 36000 ? 220.0 : 110.0);
    }
    return out;
}

double maxDifference(const std::vector<float>& a,
                     const std::vector<float>& b,
                     int startSample)
{
    double maximum = 0.0;
    for (int n = startSample; n < static_cast<int>(a.size()); ++n)
        maximum = std::max(maximum, std::abs(
            static_cast<double>(a[static_cast<std::size_t>(n)])
          - static_cast<double>(b[static_cast<std::size_t>(n)])));
    return maximum;
}
}

int main()
{
    bool success = true;

    // F0 must have zero renderer authority in Experimental.
    const auto harmonicReference = renderHarmonic(false, 0.9);
    const auto harmonicF0Flip = renderHarmonic(true, 0.9);
    const double f0Difference = maxDifference(harmonicReference, harmonicF0Flip, 24000);
    std::cerr << "experimental_f0_flip_max_difference=" << f0Difference << '\n';
    success &= check(f0Difference < 1.0e-7,
                     "experimental_renderer_has_no_f0_reconstruction_authority");

    // Formant preservation is intentionally removed from the pure Experimental
    // renderer. This proves there is no hidden spectral-envelope reconstruction.
    const auto harmonicNoFormant = renderHarmonic(false, 0.0);
    const double formantDifference = maxDifference(
        harmonicReference, harmonicNoFormant, 24000);
    std::cerr << "experimental_formant_max_difference=" << formantDifference << '\n';
    success &= check(formantDifference < 1.0e-7,
                     "experimental_has_no_formant_reconstruction");

    // Isolated pitch transport must leave essentially no original family.
    const auto shiftedTone = renderTone(100.0, 0.9, true);
    const double ratio = std::exp2(100.0 / 1200.0);
    const double targetHz = 220.0 * ratio;
    const double targetPower = tonePower(shiftedTone, targetHz, 24000);
    const double sourcePower = tonePower(shiftedTone, 220.0, 24000);
    const double sourceRatio = targetPower / std::max(1.0e-20, sourcePower);
    std::cerr << "experimental_target_source_ratio=" << sourceRatio << '\n';
    success &= check(sourceRatio > 1000.0,
                     "experimental_has_no_audible_source_copy");

    // An octave error in a supplied F0 must not create a sub-octave reconstruction.
    constexpr double harmonicFundamental = 173.70;
    constexpr double harmonicShiftCents = 137.60;
    const double harmonicTarget = harmonicFundamental
        * std::exp2(harmonicShiftCents / 1200.0);
    const double fundamentalPower = tonePower(harmonicF0Flip, harmonicTarget, 24000);
    const double subOctavePower = tonePower(harmonicF0Flip, harmonicTarget * 0.5, 24000);
    const double subOctaveRatio = fundamentalPower / std::max(1.0e-20, subOctavePower);
    std::cerr << "experimental_target_suboctave_ratio=" << subOctaveRatio << '\n';
    success &= check(subOctaveRatio > 40.0,
                     "experimental_does_not_build_suboctave_family");

    // Inharmonic material has no classification branch: every measured component
    // is transported by the same ratio and the original coordinates are suppressed.
    const auto inharmonic = renderInharmonic();
    constexpr std::array<double, 4> sourceFrequencies {277.0, 401.0, 593.0, 877.0};
    const double inharmonicRatio = std::exp2(500.0 / 1200.0);
    double shiftedPower = 0.0;
    double originalPower = 0.0;
    for (double f : sourceFrequencies)
    {
        shiftedPower += tonePower(inharmonic, f * inharmonicRatio, 24000);
        originalPower += tonePower(inharmonic, f, 24000);
    }
    const double inharmonicTransportRatio = shiftedPower
        / std::max(1.0e-20, originalPower);
    std::cerr << "experimental_inharmonic_shift_source_ratio="
              << inharmonicTransportRatio << '\n';
    success &= check(inharmonicTransportRatio > 20.0,
                     "experimental_inharmonic_signal_uses_one_transport");

    return success ? 0 : 1;
}
