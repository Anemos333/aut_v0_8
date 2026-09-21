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

double refineCmndf(
    const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& difference,
    double proposal,
    double radiusCents) noexcept
{
    constexpr double stepCents = 0.125;
    double bestHz = proposal;
    float best = std::numeric_limits<float>::infinity();

    for (double offset = -radiusCents;
         offset <= radiusCents + 1.0e-9;
         offset += stepCents)
    {
        const double hz = shifted(proposal, offset);
        const double tau = sr / hz;
        if (tau < 2.0 || tau >= static_cast<double>(n - 9))
            continue;

        const int left = std::clamp(static_cast<int>(std::floor(tau)), 2, n - 10);
        const double frac = tau - static_cast<double>(left);
        const float a = difference[static_cast<std::size_t>(left)];
        const float b = difference[static_cast<std::size_t>(left + 1)];
        const float value = static_cast<float>((1.0 - frac) * a + frac * b);

        if (value < best)
        {
            best = value;
            bestHz = hz;
        }
    }
    return bestHz;
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
                refined = refineCmndf(workspace.difference, proposal, radius);
            const auto end = std::chrono::steady_clock::now();

            const double ns =
                std::chrono::duration<double, std::nano>(end - begin).count()
                / static_cast<double>(repeats);
            const double error = centsError(refined, targetHz);

            std::cout << std::fixed << std::setprecision(6)
                      << "LOCAL_CMNDF_SECOND"
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
