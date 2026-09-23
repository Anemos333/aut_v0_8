#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

#include "F0TransitionAdaptiveStateProbe.cpp"

namespace
{
struct CorrConfig
{
    double drop;
    double energyRatio;
};

constexpr std::array<CorrConfig, 15> configs {{
    {0.10, 0.15}, {0.10, 0.20}, {0.10, 0.25},
    {0.15, 0.15}, {0.15, 0.20}, {0.15, 0.25},
    {0.20, 0.15}, {0.20, 0.20}, {0.20, 0.25},
    {0.25, 0.15}, {0.25, 0.20}, {0.25, 0.25},
    {0.30, 0.15}, {0.30, 0.20}, {0.30, 0.25}
}};

struct CorrEnergy
{
    bool valid = false;
    double corr = 0.0;
    double rms = 0.0;
};

CorrEnergy stableLagStats(const Sequence& x, int endExclusive, double stableHz)
{
    if (!(stableHz > 0.0)) return {};
    const int lag = std::max(2, static_cast<int>(std::lround(sr / stableHz)));
    const int begin = endExclusive - analysisSamples;
    if (begin - lag < 0 || endExclusive > sequenceSamples) return {};

    double ab = 0.0, aa = 0.0, bb = 0.0, energy = 0.0;
    for (int n = begin; n < endExclusive; ++n)
    {
        const double a = x[static_cast<std::size_t>(n)];
        const double b = x[static_cast<std::size_t>(n - lag)];
        ab += a * b;
        aa += a * a;
        bb += b * b;
        energy += a * a;
    }
    const double den = std::sqrt(std::max(1.0e-30, aa * bb));
    return { true,
             den > 0.0 ? ab / den : 0.0,
             std::sqrt(energy / static_cast<double>(analysisSamples)) };
}

struct Baseline
{
    double corr = 0.0;
    double rms = 0.0;
};

void updateBaseline(Baseline& b, const CorrEnergy& now)
{
    // Slow stable-state reference. Frozen while transition is latched.
    constexpr double alpha = 0.02;
    b.corr = (1.0 - alpha) * b.corr + alpha * now.corr;
    b.rms  = (1.0 - alpha) * b.rms  + alpha * now.rms;
}

bool breaks(const CorrEnergy& now, const Baseline& base, const CorrConfig& cfg)
{
    if (!now.valid) return true;
    const bool corrBreak = (base.corr - now.corr) >= cfg.drop;
    const bool energyBreak = now.rms <= cfg.energyRatio * std::max(1.0e-9, base.rms);
    return corrBreak || energyBreak;
}
}

int main()
{
    constexpr std::array<double, 6> frequencies {
        123.4708, 164.8138, 220.0, 293.6648, 440.0, 659.2551
    };
    constexpr std::array<double, 3> snrs { 18.0, 9.0, 3.0 };
    constexpr std::array<std::uint32_t, 8> normalSeeds {
        0x6d2b79f5u, 0x1b873593u, 0x85ebca6bu, 0xc2b2ae35u,
        0x27d4eb2fu, 0x165667b1u, 0xd3a2646cu, 0xfd7046c5u
    };

    std::array<int, configs.size()> normalFrames {};
    std::array<int, configs.size()> normalFalse {};
    std::array<double, configs.size()> worstDrop {};
    std::array<double, configs.size()> minEnergyRatio {};
    minEnergyRatio.fill(1.0);

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : normalSeeds)
                {
                    const auto x = makeContinuousVoice(
                        profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 83.0));
                    const auto initial = stableLagStats(x, 2400, f0);
                    if (!initial.valid) continue;
                    std::array<Baseline, configs.size()> baseline {};
                    for (auto& b : baseline) b = { initial.corr, initial.rms };

                    for (int end = 2528; end <= 12000; end += 128)
                    {
                        const auto now = stableLagStats(x, end, f0);
                        if (!now.valid) continue;
                        for (std::size_t ci = 0; ci < configs.size(); ++ci)
                        {
                            ++normalFrames[ci];
                            worstDrop[ci] = std::max(
                                worstDrop[ci], baseline[ci].corr - now.corr);
                            minEnergyRatio[ci] = std::min(
                                minEnergyRatio[ci],
                                now.rms / std::max(1.0e-9, baseline[ci].rms));
                            if (breaks(now, baseline[ci], configs[ci]))
                                ++normalFalse[ci];
                            else
                                updateBaseline(baseline[ci], now);
                        }
                    }
                }

    constexpr std::array<EventKind, 6> kinds {
        EventKind::click, EventKind::dropout, EventKind::whiteBurst,
        EventKind::hissBurst, EventKind::breathBurst, EventKind::phaseBreak
    };
    constexpr std::array<double, 3> eventFreqs { 164.8138, 220.0, 329.6276 };
    constexpr std::array<double, 2> eventSnrs { 18.0, 9.0 };
    constexpr std::array<std::uint32_t, 5> eventSeeds {
        0x94d049bbu, 0x369dea0fu, 0xdb4f0b91u, 0xbbf13aefu, 0x5f356495u
    };

    std::array<int, configs.size()> sequences {};
    std::array<int, configs.size()> entered {};
    std::array<int, configs.size()> core {};
    std::array<int, configs.size()> caught {};
    std::array<double, configs.size()> worstEntryMs {};

    for (EventKind kind : kinds)
        for (const auto& profile : profiles)
            for (double f0 : eventFreqs)
                for (double snr : eventSnrs)
                    for (auto seed : eventSeeds)
                    {
                        auto x = makeContinuousVoice(
                            profile, f0, snr,
                            seed ^ static_cast<std::uint32_t>(f0 * 89.0));
                        const auto initial = stableLagStats(x, eventStart - 512, f0);
                        if (!initial.valid) continue;
                        injectEvent(x, kind, seed ^ 0x9e3779b9u);

                        std::array<Baseline, configs.size()> baseline {};
                        std::array<bool, configs.size()> latched {};
                        std::array<bool, configs.size()> everEntered {};
                        for (std::size_t ci = 0; ci < configs.size(); ++ci)
                        {
                            baseline[ci] = { initial.corr, initial.rms };
                            ++sequences[ci];
                        }

                        for (int end = eventStart;
                             end <= eventEnd + analysisSamples;
                             end += hopSamples)
                        {
                            const auto now = stableLagStats(x, end, f0);
                            for (std::size_t ci = 0; ci < configs.size(); ++ci)
                            {
                                const bool broken = breaks(now, baseline[ci], configs[ci]);
                                if (broken && !latched[ci])
                                {
                                    latched[ci] = true;
                                    if (!everEntered[ci])
                                    {
                                        everEntered[ci] = true;
                                        ++entered[ci];
                                        const double ms = 1000.0 * static_cast<double>(
                                            std::max(0, end - eventStart)) / sr;
                                        worstEntryMs[ci] = std::max(worstEntryMs[ci], ms);
                                    }
                                }

                                if (coreOverlapsEvent(end, kind))
                                {
                                    ++core[ci];
                                    if (latched[ci]) ++caught[ci];
                                }

                                if (!latched[ci] && !broken)
                                    updateBaseline(baseline[ci], now);
                            }
                        }
                    }

    std::cout << std::fixed << std::setprecision(3);
    for (std::size_t ci = 0; ci < configs.size(); ++ci)
    {
        std::cout << "CORR_SWEEP drop=" << configs[ci].drop
                  << " energy=" << configs[ci].energyRatio
                  << " normal=" << normalFrames[ci]
                  << " false=" << normalFalse[ci]
                  << " worst_drop=" << worstDrop[ci]
                  << " min_energy_ratio=" << minEnergyRatio[ci]
                  << " sequences=" << sequences[ci]
                  << " entered=" << entered[ci]
                  << " core=" << core[ci]
                  << " caught=" << caught[ci]
                  << " missed=" << (core[ci] - caught[ci])
                  << " worst_entry_ms=" << worstEntryMs[ci]
                  << '\n';
    }
    return 0;
}
