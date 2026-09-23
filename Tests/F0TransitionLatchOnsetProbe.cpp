#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0TransitionAdaptiveStateProbe.cpp"

namespace
{
constexpr double latchJump = 1.90;

bool breakNow(const Sequence& x, int endExclusive, const State& state)
{
    const double r = residual(x, endExclusive, state.stableHz);
    const double rms = tailRms(x, endExclusive);
    const bool residualBreak = r >= latchJump * std::max(1.0e-6, state.baselineResidual);
    const bool energyBreak = rms <= energyCollapse * std::max(1.0e-9, state.baselineRms);
    return !gateAllowsAdaptive(x, endExclusive) || residualBreak || energyBreak;
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

    int normalFrames = 0, normalFalse = 0;
    double worstRatio = 0.0;
    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : normalSeeds)
                {
                    const auto x = makeContinuousVoice(profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 61.0));
                    State state { f0, residual(x, 2400, f0), tailRms(x, 2400) };
                    if (!std::isfinite(state.baselineResidual)) continue;
                    for (int end = 2528; end <= 12000; end += 128)
                    {
                        ++normalFrames;
                        const double r = residual(x, end, state.stableHz);
                        worstRatio = std::max(worstRatio,
                            r / std::max(1.0e-6, state.baselineResidual));
                        if (breakNow(x, end, state))
                            ++normalFalse;
                        else
                        {
                            state.baselineResidual = r;
                            state.baselineRms = tailRms(x, end);
                        }
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

    std::array<int, kinds.size()> sequences {};
    std::array<int, kinds.size()> entered {};
    std::array<int, kinds.size()> rawCore {};
    std::array<int, kinds.size()> rawCaught {};
    std::array<int, kinds.size()> latchedCore {};
    std::array<int, kinds.size()> latchedCaught {};
    std::array<int, kinds.size()> coreBeforeEntry {};
    std::array<double, kinds.size()> worstEntryMs {};

    for (std::size_t ki = 0; ki < kinds.size(); ++ki)
        for (const auto& profile : profiles)
            for (double f0 : eventFreqs)
                for (double snr : eventSnrs)
                    for (auto seed : eventSeeds)
                    {
                        ++sequences[ki];
                        auto x = makeContinuousVoice(profile, f0, snr,
                            seed ^ static_cast<std::uint32_t>(f0 * 71.0));
                        State state { f0, residual(x, eventStart - 512, f0),
                                          tailRms(x, eventStart - 512) };
                        if (!std::isfinite(state.baselineResidual)) continue;
                        injectEvent(x, kinds[ki], seed ^ 0x3c6ef372u);

                        bool latched = false;
                        bool everEntered = false;
                        int firstEntryEnd = -1;

                        for (int end = eventStart;
                             end <= eventEnd + analysisSamples;
                             end += hopSamples)
                        {
                            const bool raw = breakNow(x, end, state);
                            if (raw && !latched)
                            {
                                latched = true;
                                if (!everEntered)
                                {
                                    everEntered = true;
                                    firstEntryEnd = end;
                                }
                            }

                            if (coreOverlapsEvent(end, kinds[ki]))
                            {
                                ++rawCore[ki];
                                ++latchedCore[ki];
                                if (raw) ++rawCaught[ki];
                                if (latched) ++latchedCaught[ki];
                                else ++coreBeforeEntry[ki];
                            }

                            // Diagnostic latch intentionally stays set through
                            // the contaminated interval. Exit is evaluated in
                            // the separate recovery/no-lingering test.
                            if (!latched && !raw)
                            {
                                state.baselineResidual = residual(x, end, state.stableHz);
                                state.baselineRms = tailRms(x, end);
                            }
                        }

                        if (everEntered)
                        {
                            ++entered[ki];
                            const double ms = 1000.0 * static_cast<double>(
                                std::max(0, firstEntryEnd - eventStart)) / sr;
                            worstEntryMs[ki] = std::max(worstEntryMs[ki], ms);
                        }
                    }

    std::cout << std::fixed << std::setprecision(6)
              << "LATCH_NORMAL frames=" << normalFrames
              << " false_transition=" << normalFalse
              << " worst_ratio=" << worstRatio << '\n';

    int totalCore = 0, totalLatched = 0, totalBefore = 0;
    for (std::size_t ki = 0; ki < kinds.size(); ++ki)
    {
        totalCore += latchedCore[ki];
        totalLatched += latchedCaught[ki];
        totalBefore += coreBeforeEntry[ki];
        std::cout << "LATCH_EVENT kind=" << ki
                  << " sequences=" << sequences[ki]
                  << " entered=" << entered[ki]
                  << " raw_core=" << rawCore[ki]
                  << " raw_caught=" << rawCaught[ki]
                  << " latched_core=" << latchedCore[ki]
                  << " latched_caught=" << latchedCaught[ki]
                  << " before_entry=" << coreBeforeEntry[ki]
                  << " worst_entry_ms=" << worstEntryMs[ki] << '\n';
    }

    std::cout << "LATCH_SUMMARY core=" << totalCore
              << " caught=" << totalLatched
              << " missed_before_entry=" << totalBefore << '\n';
    std::cout << "F0_LATCH_NORMAL_SAFE=" << (normalFalse == 0 ? "PASS" : "FAIL") << '\n';
    return 0;
}
