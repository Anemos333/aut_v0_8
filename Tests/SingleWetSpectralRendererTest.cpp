#include "SingleWetSpectralRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <tuple>
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
                              float detectorConfidence,
                              float noteBodyConfidence,
                              bool noteBodyLatched,
                              double correctionCents,
                              double inputFrequencyHz = 220.0)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);

    SingleWetSpectralRenderer::Context context;
    context.detectedPitchHz = noteBodyLatched ? static_cast<float>(inputFrequencyHz) : 0.0f;
    context.pitchAnchorFresh = noteBodyLatched;
    context.confidence = detectorConfidence;
    context.voicing = detectorConfidence;
    context.consensus = detectorConfidence;
    context.noteBodyLatched = noteBodyLatched;
    context.noteBodyConfidence = noteBodyConfidence;
    context.stableMusicalBody = noteBodyLatched;

    std::vector<float> output(48000);
    for (int sample = 0; sample < static_cast<int>(output.size()); ++sample)
    {
        const float input = 0.22f * static_cast<float>(std::sin(
            2.0 * pi * inputFrequencyHz * static_cast<double>(sample) / sampleRate));
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            input, correctionCents, 0.9f, context);
    }
    return output;
}

std::vector<float> renderInharmonicOctaveShift()
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 512);

    SingleWetSpectralRenderer::Context context;
    context.detectedPitchHz = 0.0f;
    context.pitchAnchorFresh = false;
    context.confidence = 0.0f;
    context.voicing = 0.0f;
    context.consensus = 0.0f;
    context.noteBodyLatched = false;
    context.noteBodyConfidence = 0.0f;

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
            static_cast<float>(input), 1200.0, 0.0f, context);
    }
    return output;
}
} // namespace

int main()
{
    bool success = true;
    const double semitoneTargetHz = 220.0 * std::exp2(100.0 / 1200.0);

    // Quality, Live and Experimental may differ in frame latency, but the
    // correction law is identical: every mode must move energy toward the same
    // requested target instead of exposing an unshifted residual in short modes.
    const std::array<int, 3> frameSizes { 512, 256, 128 };
    for (const int frameSize : frameSizes)
    {
        const auto output = renderTone(frameSize, 0.95f, 0.95f, true, 100.0);
        const double targetPower = tonePower(output, semitoneTargetHz, 12000);
        const double sourcePower = tonePower(output, 220.0, 12000);
        const double ratio = targetPower / std::max(1.0e-20, sourcePower);
        std::cerr << "frame_" << frameSize << "_target_source_ratio=" << ratio << '\n';
        success &= check(targetPower > 1.5 * sourcePower,
                         frameSize == 512 ? "quality_obeys_exact_transport"
                         : frameSize == 256 ? "live_obeys_exact_transport"
                                            : "experimental_obeys_exact_transport");
    }

    // Sensor confidence cannot authorize part of the spectrum to remain at the
    // source pitch while a correction request exists.
    const auto lowConfidence = renderTone(512, 0.05f, 0.92f, true, 100.0);
    const double lowTargetPower = tonePower(lowConfidence, semitoneTargetHz, 12000);
    const double lowSourcePower = tonePower(lowConfidence, 220.0, 12000);
    success &= check(lowTargetPower > 4.0 * lowSourcePower,
                     "latched_low_confidence_still_obeys_full_transport");

    const auto unity = renderTone(512, 0.95f, 0.95f, true, 0.0);
    const double unitySourcePower = tonePower(unity, 220.0, 12000);
    const double unityShiftedPower = tonePower(unity, semitoneTargetHz, 12000);
    success &= check(unitySourcePower > 4.0 * unityShiftedPower,
                     "zero_correction_preserves_source_pitch");

    // A full octave catches both historical octave wrapping and any hidden
    // original-position contribution.
    const auto octave = renderTone(512, 0.95f, 0.95f, true, 1200.0);
    const double octaveTargetPower = tonePower(octave, 440.0, 12000);
    const double octaveSourcePower = tonePower(octave, 220.0, 12000);
    std::cerr << "octave_target_source_ratio="
              << octaveTargetPower / std::max(1.0e-20, octaveSourcePower) << '\n';
    success &= check(octaveTargetPower > 4.0 * octaveSourcePower,
                     "octave_correction_is_not_wrapped_or_split");

    // Deliberately remove voiced/F0 guidance and feed an inharmonic signal.
    // The classifier is now allowed only to choose reconstruction technique;
    // it cannot decide that a residual fraction stays at source coordinates.
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
                     "aperiodic_evidence_cannot_create_hidden_dry_spectrum");

    return success ? 0 : 1;
}
