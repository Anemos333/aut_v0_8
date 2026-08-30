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
    for (int sample = startSample; sample < static_cast<int>(signal.size()); ++sample)
    {
        const double phase = 2.0 * pi * frequencyHz
                           * static_cast<double>(sample) / sampleRate;
        real += static_cast<double>(signal[static_cast<std::size_t>(sample)])
              * std::cos(phase);
        imaginary -= static_cast<double>(signal[static_cast<std::size_t>(sample)])
                   * std::sin(phase);
    }
    return real * real + imaginary * imaginary;
}

std::vector<float> renderTone(int frameSize,
                              double correctionCents,
                              double inputFrequencyHz = 220.0)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);

    std::vector<float> output(48000);
    for (int sample = 0; sample < static_cast<int>(output.size()); ++sample)
    {
        const float input = 0.22f * static_cast<float>(std::sin(
            2.0 * pi * inputFrequencyHz * static_cast<double>(sample) / sampleRate));
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            input, correctionCents, 0.9f);
    }
    return output;
}

std::vector<float> renderInharmonicOctaveShift()
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 512);

    constexpr std::array<double, 4> frequencies { 277.0, 401.0, 593.0, 877.0 };
    constexpr std::array<double, 4> phases { 0.17, 0.73, 1.31, 2.03 };
    std::vector<float> output(72000);
    for (int sample = 0; sample < static_cast<int>(output.size()); ++sample)
    {
        double input = 0.0;
        for (std::size_t index = 0; index < frequencies.size(); ++index)
        {
            input += 0.055 * std::sin(
                2.0 * pi * frequencies[index] * static_cast<double>(sample) / sampleRate
                + phases[index]);
        }
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            static_cast<float>(input), 1200.0, 0.0f);
    }
    return output;
}
} // namespace

int main()
{
    bool success = true;
    const double semitoneTargetHz = 220.0 * std::exp2(100.0 / 1200.0);

    // The same transport law must work at all three production frame sizes.
    const std::array<int, 3> frameSizes { 512, 256, 128 };
    for (const int frameSize : frameSizes)
    {
        const auto output = renderTone(frameSize, 100.0);
        const double targetPower = tonePower(output, semitoneTargetHz, 12000);
        const double sourcePower = tonePower(output, 220.0, 12000);
        const double ratio = targetPower / std::max(1.0e-20, sourcePower);
        std::cerr << "frame_" << frameSize << "_target_source_ratio=" << ratio << '\n';
        success &= check(targetPower > 1.5 * sourcePower,
                         frameSize == 512 ? "quality_obeys_exact_transport"
                         : frameSize == 256 ? "live_obeys_exact_transport"
                                            : "experimental_obeys_exact_transport");
    }

    const auto unity = renderTone(512, 0.0);
    const double unitySourcePower = tonePower(unity, 220.0, 12000);
    const double unityShiftedPower = tonePower(unity, semitoneTargetHz, 12000);
    success &= check(unitySourcePower > 4.0 * unityShiftedPower,
                     "zero_correction_preserves_source_pitch");

    // A full octave catches both historical octave wrapping and any hidden
    // original-position contribution.
    const auto octave = renderTone(512, 1200.0);
    const double octaveTargetPower = tonePower(octave, 440.0, 12000);
    const double octaveSourcePower = tonePower(octave, 220.0, 12000);
    std::cerr << "octave_target_source_ratio="
              << octaveTargetPower / std::max(1.0e-20, octaveSourcePower) << '\n';
    success &= check(octaveTargetPower > 4.0 * octaveSourcePower,
                     "octave_correction_is_not_wrapped_or_split");

    // An inharmonic signal has no special branch: every component must obey the
    // same requested transport.
    const auto inharmonic = renderInharmonicOctaveShift();
    constexpr std::array<double, 4> sourceFrequencies { 277.0, 401.0, 593.0, 877.0 };
    double inharmonicSourcePower = 0.0;
    double inharmonicTargetPower = 0.0;
    for (const double frequency : sourceFrequencies)
    {
        inharmonicSourcePower += tonePower(inharmonic, frequency, 24000);
        inharmonicTargetPower += tonePower(inharmonic, 2.0 * frequency, 24000);
    }
    const double inharmonicRatio = inharmonicTargetPower
        / std::max(1.0e-20, inharmonicSourcePower);
    std::cerr << "inharmonic_full_transport_ratio=" << inharmonicRatio << '\n';
    success &= check(inharmonicTargetPower > 2.0 * inharmonicSourcePower,
                     "inharmonic_signal_uses_same_transport");

    return success ? 0 : 1;
}
