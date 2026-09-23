#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0TransitionAdaptiveStateProbe.cpp"

namespace
{
struct Config { int window; double jump; };
constexpr std::array<Config, 16> configs {{
    {128, 2.0}, {128, 2.5}, {128, 3.0}, {128, 4.0},
    {192, 2.0}, {192, 2.5}, {192, 3.0}, {192, 4.0},
    {256, 2.0}, {256, 2.5}, {256, 3.0}, {256, 4.0},
    {448, 1.75}, {448, 2.0}, {448, 2.5}, {448, 3.0}
}};

double tailResidual(const Sequence& x, int endExclusive, double stableHz, int window)
{
    const int lag = std::max(2, static_cast<int>(std::lround(sr / stableHz)));
    const int begin = endExclusive - window;
    if (begin - lag < 0) return std::numeric_limits<double>::infinity();
    double diff = 0.0, energy = 0.0;
    for (int n = begin; n < endExclusive; ++n)
    {
        const double a = x[static_cast<std::size_t>(n)];
        const double b = x[static_cast<std::size_t>(n - lag)];
        const double d = a - b;
        diff += d * d;
        energy += a * a + b * b;
    }
    return diff / std::max(1.0e-30, energy);
}
}

int main()
{
    constexpr std::array<double, 6> frequencies {
        123.4708, 164.8138, 220.0, 293.6648, 440.0, 659.2551
    };
    constexpr std::array<double, 2> snrs { 18.0, 9.0 };
    constexpr std::array<std::uint32_t, 6> normalSeeds {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu,
        0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u
    };

    std::array<int, configs.size()> normalFrames {};
    std::array<int, configs.size()> normalFalse {};
    std::array<double, configs.size()> worstRatio {};

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : normalSeeds)
                {
                    const auto x = makeContinuousVoice(profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 61.0));
                    std::array<double, configs.size()> baseline {};
                    for (std::size_t ci = 0; ci < configs.size(); ++ci)
                        baseline[ci] = tailResidual(x, 2400, f0, configs[ci].window);

                    for (int end = 2528; end <= 12000; end += 128)
                        for (std::size_t ci = 0; ci < configs.size(); ++ci)
                        {
                            ++normalFrames[ci];
                            const double r = tailResidual(x, end, f0, configs[ci].window);
                            const double ratio = r / std::max(1.0e-6, baseline[ci]);
                            worstRatio[ci] = std::max(worstRatio[ci], ratio);
                            if (ratio >= configs[ci].jump)
                                ++normalFalse[ci];
                            else
                                baseline[ci] = r;
                        }
                }

    constexpr std::array<EventKind, 6> kinds {
        EventKind::click, EventKind::dropout, EventKind::whiteBurst,
        EventKind::hissBurst, EventKind::breathBurst, EventKind::phaseBreak
    };
    constexpr std::array<double, 3> eventFreqs { 164.8138, 220.0, 329.6276 };
    constexpr std::array<double, 2> eventSnrs { 18.0, 9.0 };
    constexpr std::array<std::uint32_t, 4> eventSeeds {
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u
    };

    std::array<int, configs.size()> core {};
    std::array<int, configs.size()> latchedCaught {};
    std::array<int, configs.size()> sequences {};
    std::array<int, configs.size()> entered {};

    for (EventKind kind : kinds)
        for (const auto& profile : profiles)
            for (double f0 : eventFreqs)
                for (double snr : eventSnrs)
                    for (auto seed : eventSeeds)
                    {
                        auto x = makeContinuousVoice(profile, f0, snr,
                            seed ^ static_cast<std::uint32_t>(f0 * 71.0));
                        injectEvent(x, kind, seed ^ 0x3c6ef372u);

                        std::array<double, configs.size()> baseline {};
                        std::array<bool, configs.size()> latched {};
                        std::array<bool, configs.size()> everEntered {};
                        for (std::size_t ci = 0; ci < configs.size(); ++ci)
                        {
                            baseline[ci] = tailResidual(
                                x, eventStart - 512, f0, configs[ci].window);
                            ++sequences[ci];
                        }

                        for (int end = eventStart;
                             end <= eventEnd + analysisSamples;
                             end += hopSamples)
                        {
                            for (std::size_t ci = 0; ci < configs.size(); ++ci)
                            {
                                const double r = tailResidual(x, end, f0, configs[ci].window);
                                const double ratio = r / std::max(1.0e-6, baseline[ci]);
                                const bool broken = ratio >= configs[ci].jump;
                                if (broken && !latched[ci])
                                {
                                    latched[ci] = true;
                                    if (!everEntered[ci])
                                    {
                                        everEntered[ci] = true;
                                        ++entered[ci];
                                    }
                                }
                                if (coreOverlapsEvent(end, kind))
                                {
                                    ++core[ci];
                                    if (latched[ci]) ++latchedCaught[ci];
                                }
                                if (!latched[ci] && !broken)
                                    baseline[ci] = r;
                            }
                        }
                    }

    std::cout << std::fixed << std::setprecision(3);
    for (std::size_t ci = 0; ci < configs.size(); ++ci)
        std::cout << "TAIL_RESIDUAL window=" << configs[ci].window
                  << " jump=" << configs[ci].jump
                  << " normal=" << normalFrames[ci]
                  << " false=" << normalFalse[ci]
                  << " worst_ratio=" << worstRatio[ci]
                  << " sequences=" << sequences[ci]
                  << " entered=" << entered[ci]
                  << " core=" << core[ci]
                  << " caught=" << latchedCaught[ci]
                  << " missed=" << (core[ci] - latchedCaught[ci])
                  << '\n';
    return 0;
}
