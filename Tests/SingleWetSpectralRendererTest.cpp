#include "SingleWetSpectralRenderer.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <tuple>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;

bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

double tonePower(const std::vector<float>& signal,
                 double sampleRate,
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

std::vector<float> render(float detectorConfidence,
                          float noteBodyConfidence,
                          double correctionCents)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(48000.0, 512);

    SingleWetSpectralRenderer::Context context;
    context.detectedPitchHz = 220.0f;
    context.confidence = detectorConfidence;
    context.voicing = detectorConfidence;
    context.consensus = detectorConfidence;
    context.noteBodyLatched = true;
    context.noteBodyConfidence = noteBodyConfidence;
    context.stableMusicalBody = true;

    std::vector<float> output(48000);
    for (int sample = 0; sample < static_cast<int>(output.size()); ++sample)
    {
        const float input = 0.22f * static_cast<float>(std::sin(
            2.0 * pi * 220.0 * static_cast<double>(sample) / 48000.0));
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            input, correctionCents, 0.9f, context);
    }
    return output;
}
} // namespace

int main()
{
    bool success = true;
    const double targetHz = 220.0 * std::exp2(100.0 / 1200.0);

    const std::vector<std::tuple<float, float, const char*>> cases {
        { 0.95f, 0.95f, "strong_body" },
        { 0.05f, 0.92f, "latched_low_confidence" }
    };

    for (const auto& [confidence, bodyConfidence, name] : cases)
    {
        const auto output = render(confidence, bodyConfidence, 100.0);
        const double targetPower = tonePower(output, 48000.0, targetHz, 12000);
        const double sourcePower = tonePower(output, 48000.0, 220.0, 12000);
        const double ratio = targetPower / std::max(1.0e-20, sourcePower);
        std::cerr << name << "_target_source_ratio=" << ratio << '\n';
        success &= check(targetPower > 4.0 * sourcePower, name);
    }

    const auto unity = render(0.95f, 0.95f, 0.0);
    const double sourcePower = tonePower(unity, 48000.0, 220.0, 12000);
    const double shiftedPower = tonePower(unity, 48000.0, targetHz, 12000);
    success &= check(sourcePower > 4.0 * shiftedPower,
                     "zero_correction_preserves_source_pitch");

    return success ? 0 : 1;
}
