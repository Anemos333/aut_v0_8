#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int n = 480;

struct Rng
{
    std::uint32_t s;
    float next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xffffu) / 32767.5f - 1.0f;
    }
};

float voice(double p) noexcept
{
    return static_cast<float>(
          0.72 * std::sin(p)
        + 0.34 * std::sin(2.0 * p + 0.17)
        + 0.21 * std::sin(3.0 * p - 0.31)
        + 0.13 * std::sin(4.0 * p + 0.49)
        + 0.08 * std::sin(5.0 * p - 0.63));
}

double shifted(double hz, double cents) noexcept
{
    return hz * std::exp2(cents / 1200.0);
}

double centsError(double measured, double target) noexcept
{
    return 1200.0 * std::log2(measured / target);
}

double refineParabolicCmndf(
    const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& d,
    double proposal,
    double radiusCents) noexcept
{
    const double lowerHz = shifted(proposal, -radiusCents);
    const double upperHz = shifted(proposal,  radiusCents);

    int tauMin = static_cast<int>(std::floor(sr / upperHz));
    int tauMax = static_cast<int>(std::ceil (sr / lowerHz));
    tauMin = std::clamp(tauMin, 2, n - 10);
    tauMax = std::clamp(tauMax, tauMin, n - 10);

    int bestTau = tauMin;
    float bestValue = d[static_cast<std::size_t>(bestTau)];
    for (int tau = tauMin + 1; tau <= tauMax; ++tau)
    {
        const float value = d[static_cast<std::size_t>(tau)];
        if (value < bestValue)
        {
            bestValue = value;
            bestTau = tau;
        }
    }

    double refinedTau = static_cast<double>(bestTau);
    if (bestTau > 2 && bestTau < n - 10)
    {
        const double left = d[static_cast<std::size_t>(bestTau - 1)];
        const double centre = d[static_cast<std::size_t>(bestTau)];
        const double right = d[static_cast<std::size_t>(bestTau + 1)];
        const double denominator = left - 2.0 * centre + right;
        if (std::abs(denominator) > 1.0e-12)
        {
            refinedTau += std::clamp(
                0.5 * (left - right) / denominator, -0.75, 0.75);
        }
    }

    return sr / refinedTau;
}

void run(double targetHz, double snrDb, std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker tracker;
    tracker.prepare(sr);
    tracker.setRange(55.0f, 1600.0f);

    Rng rng { seed };
    double phase = 0.0;
    const double amp = std::pow(10.0, -24.0 / 20.0);
    const double noiseAmp = amp / std::pow(10.0, snrDb / 20.0);
    ModernPitchEngine::PitchObservation observation;

    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * targetHz / sr;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;
        const float x = static_cast<float>(amp) * voice(phase)
                      + static_cast<float>(noiseAmp) * rng.next();
        tracker.processSample(x, observation);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace workspace {};
    static_cast<void>(tracker.measureCoordinate(
        tracker.fullRateRing_,
        tracker.fullRateWritePosition_,
        tracker.fullRateAvailableSamples_,
        sr, 55.0f, 1600.0f, n, workspace));

    constexpr std::array<double, 8> proposalErrors {
        -20.0, -10.0, -5.0, -2.0, 2.0, 5.0, 10.0, 20.0
    };
    constexpr std::array<double, 4> radii { 2.0, 5.0, 10.0, 20.0 };

    for (double radius : radii)
    {
        for (double proposalError : proposalErrors)
        {
            if (std::abs(proposalError) > radius + 1.0e-9)
                continue;

            const double proposal = shifted(targetHz, proposalError);
            constexpr int repeats = 100;

            const auto begin = std::chrono::steady_clock::now();
            double refined = proposal;
            for (int rep = 0; rep < repeats; ++rep)
                refined = refineParabolicCmndf(workspace.difference, proposal, radius);
            const auto end = std::chrono::steady_clock::now();

            const double ns =
                std::chrono::duration<double, std::nano>(end - begin).count()
                / static_cast<double>(repeats);
            const double error = centsError(refined, targetHz);

            std::cout << std::fixed << std::setprecision(6)
                      << "PARABOLIC_CMNDF_SECOND"
                      << " hz=" << targetHz
                      << " snr=" << snrDb
                      << " radius=" << radius
                      << " proposal_error=" << proposalError
                      << " refined_error=" << error
                      << " improves=" << (std::abs(error) < std::abs(proposalError) ? 1 : 0)
                      << " pass_1p5=" << (std::abs(error) <= 1.5 ? 1 : 0)
                      << " ns=" << ns
                      << '\n';
        }
    }
}
}

int main()
{
    constexpr std::array<double, 6> frequencies {
        82.4069, 110.0, 220.0, 440.0, 660.0, 880.0
    };
    constexpr std::array<double, 3> snrs { 12.0, 6.0, 3.0 };
    constexpr std::array<std::uint32_t, 3> seeds {
        0x1234567u, 0x51f15e5du, 0x9e3779b9u
    };

    for (double hz : frequencies)
        for (double snr : snrs)
            for (auto seed : seeds)
                run(hz, snr, seed);
}
