#include "SingleWetSpectralRenderer.h"

#include <cmath>
#include <iostream>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double sampleRate = 48000.0;

bool bounded01(float value)
{
    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}
}

int main()
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 512);

    // Discard startup diagnostics, then inspect an established periodic segment.
    for (int sample = 0; sample < 12000; ++sample)
    {
        const double t = static_cast<double>(sample) / sampleRate;
        const float input = static_cast<float>(
            0.16 * std::sin(2.0 * pi * 277.0 * t)
          + 0.08 * std::sin(2.0 * pi * 554.0 * t + 0.4)
          + 0.04 * std::sin(2.0 * pi * 831.0 * t + 1.1));
        static_cast<void>(renderer.processSample(input, 430.0, 0.90f));
    }
    static_cast<void>(renderer.consumeDiagnostics());

    for (int sample = 12000; sample < 24000; ++sample)
    {
        const double t = static_cast<double>(sample) / sampleRate;
        const float input = static_cast<float>(
            0.16 * std::sin(2.0 * pi * 277.0 * t)
          + 0.08 * std::sin(2.0 * pi * 554.0 * t + 0.4)
          + 0.04 * std::sin(2.0 * pi * 831.0 * t + 1.1));
        static_cast<void>(renderer.processSample(input, 430.0, 0.90f));
    }

    const auto d = renderer.consumeDiagnostics();
    if (!d.valid || d.frameCount <= 0)
    {
        std::cerr << "RENDERER_PHASE_DIAGNOSTICS=FAIL reason=no_frames\n";
        return 2;
    }

    if (!bounded01(d.minimumPreIfftCoherence)
        || !bounded01(d.minimumStrongBinCoherence)
        || !std::isfinite(d.maximumDominantRidgeSpreadBins)
        || d.maximumDominantRidgeSpreadBins < 0.0f)
    {
        std::cerr << "RENDERER_PHASE_DIAGNOSTICS=FAIL reason=invalid_metrics\n";
        return 3;
    }

    if (d.minimumPreIfftCoherence < 0.20f)
    {
        std::cerr << "RENDERER_PHASE_DIAGNOSTICS=FAIL"
                  << " reason=periodic_reference_implausibly_incoherent"
                  << " pre_ifft=" << d.minimumPreIfftCoherence << "\n";
        return 4;
    }

    if (!d.olaValid)
    {
        std::cerr << "RENDERER_OLA_DIAGNOSTICS=FAIL reason=no_ola_samples\n";
        return 5;
    }

    if (!std::isfinite(d.minimumOlaEnergyRatio)
        || d.minimumOlaEnergyRatio < 0.0f
        || !std::isfinite(d.minimumOlaCoverageRatio)
        || d.minimumOlaCoverageRatio < 0.0f
        || !d.frameOverlapCorrelationValid
        || !std::isfinite(d.minimumFrameOverlapCorrelation)
        || d.minimumFrameOverlapCorrelation < -1.0f
        || d.minimumFrameOverlapCorrelation > 1.0f)
    {
        std::cerr << "RENDERER_OLA_DIAGNOSTICS=FAIL reason=invalid_metrics\n";
        return 6;
    }

    // With sqrt-Hann and N/4 hop, the established OLA stream must always have
    // the full four-frame window coverage. This is a geometry contract, not a
    // judgement about how much phase cancellation is perceptually acceptable.
    if (d.minimumOlaCoverageRatio < 0.995f
        || d.minimumOlaContributionCount < 4)
    {
        std::cerr << "RENDERER_OLA_DIAGNOSTICS=FAIL"
                  << " reason=coverage_or_frame_count"
                  << " coverage=" << d.minimumOlaCoverageRatio
                  << " count=" << d.minimumOlaContributionCount << "\n";
        return 7;
    }

    std::cout << "RENDERER_PHASE_DIAGNOSTICS=PASS"
              << " frames=" << d.frameCount
              << " pre_ifft=" << d.minimumPreIfftCoherence
              << " strong_bin=" << d.minimumStrongBinCoherence
              << " ridge_spread_bins=" << d.maximumDominantRidgeSpreadBins
              << " worst_frame_end_sample=" << d.worstFrameEndSample
              << "\n";

    std::cout << "RENDERER_OLA_DIAGNOSTICS=PASS"
              << " energy_ratio=" << d.minimumOlaEnergyRatio
              << " frame_correlation=" << d.minimumFrameOverlapCorrelation
              << " coverage=" << d.minimumOlaCoverageRatio
              << " count=" << d.minimumOlaContributionCount
              << " worst_hop_end_sample=" << d.worstOlaHopEndSample
              << " worst_frame_end_sample="
              << d.worstFrameCorrelationEndSample
              << "\n";
    return 0;
}
