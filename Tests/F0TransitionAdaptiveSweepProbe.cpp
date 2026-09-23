#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0TransitionAdaptiveStateProbe.cpp"

namespace
{
constexpr std::array<double, 6> jumpThresholds { 1.75, 1.80, 1.85, 1.90, 1.95, 2.00 };

struct SweepStats
{
    int normalFrames = 0;
    std::array<int, jumpThresholds.size()> normalFalse {};
    std::array<int, jumpThresholds.size()> eventCore {};
    std::array<int, jumpThresholds.size()> eventCaught {};
    std::array<int, jumpThresholds.size()> lateClean {};
    std::array<int, jumpThresholds.size()> lateFalse {};
};

bool transitionAt(const Sequence& x,
                  int endExclusive,
                  const State& state,
                  double jump)
{
    const double r = residual(x, endExclusive, state.stableHz);
    const double rms = tailRms(x, endExclusive);
    const bool residualBreak = r >= jump * std::max(1.0e-6, state.baselineResidual);
    const bool energyBreak = rms <= energyCollapse * std::max(1.0e-9, state.baselineRms);
    return !gateAllowsAdaptive(x, endExclusive) || residualBreak || energyBreak;
}
}

int main()
{
    SweepStats stats {};

    constexpr std::array<double, 8> frequencies {
        110.0, 146.8324, 196.0, 220.0, 246.9417, 329.6276, 440.0, 659.2551
    };
    constexpr std::array<double, 2> normalSnrs { 18.0, 9.0 };
    constexpr std::array<std::uint32_t, 8> normalSeeds {
        0x5a827999u, 0x6ed9eba1u, 0x8f1bbcdcu, 0xca62c1d6u,
        0x3f84d5b5u, 0xb5470917u, 0x9216d5d9u, 0x8979fb1bu
    };

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : normalSnrs)
                for (auto seed : normalSeeds)
                {
                    const auto x = makeContinuousVoice(profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 53.0));
                    State baseline { f0, residual(x, 2400, f0), tailRms(x, 2400) };
                    if (!std::isfinite(baseline.baselineResidual)) continue;

                    std::array<State, jumpThresholds.size()> states {};
                    states.fill(baseline);
                    for (int end = 2528; end <= 12000; end += 128)
                    {
                        ++stats.normalFrames;
                        for (std::size_t ti = 0; ti < jumpThresholds.size(); ++ti)
                        {
                            const bool trans = transitionAt(x, end, states[ti], jumpThresholds[ti]);
                            if (trans)
                                ++stats.normalFalse[ti];
                            else
                            {
                                states[ti].baselineResidual = residual(x, end, states[ti].stableHz);
                                states[ti].baselineRms = tailRms(x, end);
                            }
                        }
                    }
                }

    constexpr std::array<EventKind, 6> kinds {
        EventKind::click, EventKind::dropout, EventKind::whiteBurst,
        EventKind::hissBurst, EventKind::breathBurst, EventKind::phaseBreak
    };
    constexpr std::array<std::uint32_t, 8> eventSeeds {
        0xd1310ba6u, 0x98dfb5acu, 0x2ffd72dbu, 0xd01adfb7u,
        0xb8e1afedu, 0x6a267e96u, 0xba7c9045u, 0xf12c7f99u
    };

    for (EventKind kind : kinds)
        for (auto seed : eventSeeds)
        {
            constexpr double f0 = 220.0;
            auto x = makeContinuousVoice(profiles[0], f0, 9.0, seed);
            injectEvent(x, kind, seed ^ 0xa5a5a5a5u);

            State baseline { f0, residual(x, eventStart - 512, f0),
                                  tailRms(x, eventStart - 512) };
            std::array<State, jumpThresholds.size()> states {};
            states.fill(baseline);

            for (int end = eventStart; end <= eventEnd + analysisSamples + 960; end += hopSamples)
            {
                for (std::size_t ti = 0; ti < jumpThresholds.size(); ++ti)
                {
                    const bool trans = transitionAt(x, end, states[ti], jumpThresholds[ti]);
                    if (coreOverlapsEvent(end, kind))
                    {
                        ++stats.eventCore[ti];
                        if (trans) ++stats.eventCaught[ti];
                    }

                    const int onePeriod = static_cast<int>(std::ceil(sr / f0));
                    if (end - analysisSamples >= eventEnd + onePeriod)
                    {
                        ++stats.lateClean[ti];
                        if (trans) ++stats.lateFalse[ti];
                    }

                    if (!trans)
                    {
                        states[ti].baselineResidual = residual(x, end, states[ti].stableHz);
                        states[ti].baselineRms = tailRms(x, end);
                    }
                }
            }
        }

    std::cout << std::fixed << std::setprecision(2);
    for (std::size_t ti = 0; ti < jumpThresholds.size(); ++ti)
    {
        std::cout << "ADAPTIVE_SWEEP jump=" << jumpThresholds[ti]
                  << " normal_frames=" << stats.normalFrames
                  << " normal_false=" << stats.normalFalse[ti]
                  << " core=" << stats.eventCore[ti]
                  << " caught=" << stats.eventCaught[ti]
                  << " missed=" << (stats.eventCore[ti] - stats.eventCaught[ti])
                  << " late_clean=" << stats.lateClean[ti]
                  << " late_false=" << stats.lateFalse[ti]
                  << '\n';
    }
    return 0;
}
