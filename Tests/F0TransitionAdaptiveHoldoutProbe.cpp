#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0TransitionAdaptiveStateProbe.cpp"

namespace
{
constexpr double holdoutJump = 1.75;

bool transitionHoldout(const Sequence& x, int endExclusive, const State& state)
{
    const double r = residual(x, endExclusive, state.stableHz);
    const double rms = tailRms(x, endExclusive);
    const bool residualBreak = r >= holdoutJump * std::max(1.0e-6, state.baselineResidual);
    const bool energyBreak = rms <= energyCollapse * std::max(1.0e-9, state.baselineRms);
    return !gateAllowsAdaptive(x, endExclusive) || residualBreak || energyBreak;
}
}

int main()
{
    constexpr std::array<double, 6> frequencies {
        123.4708, 164.8138, 220.0, 293.6648, 440.0, 659.2551
    };
    constexpr std::array<double, 3> snrs { 18.0, 9.0, 3.0 };
    constexpr std::array<std::uint32_t, 6> seeds {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu,
        0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u
    };

    std::array<int, snrs.size()> normalFrames {};
    std::array<int, snrs.size()> normalFalse {};
    std::array<double, snrs.size()> worstRatio {};

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (std::size_t si = 0; si < snrs.size(); ++si)
                for (auto seed : seeds)
                {
                    const auto x = makeContinuousVoice(profile, f0, snrs[si],
                        seed ^ static_cast<std::uint32_t>(f0 * 61.0));
                    State state { f0, residual(x, 2400, f0), tailRms(x, 2400) };
                    if (!std::isfinite(state.baselineResidual)) continue;

                    for (int end = 2528; end <= 12000; end += 128)
                    {
                        ++normalFrames[si];
                        const double r = residual(x, end, state.stableHz);
                        const double ratio = r / std::max(1.0e-6, state.baselineResidual);
                        worstRatio[si] = std::max(worstRatio[si], ratio);
                        const bool trans = transitionHoldout(x, end, state);
                        if (trans)
                            ++normalFalse[si];
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
    constexpr std::array<double, 3> eventFrequencies { 164.8138, 220.0, 329.6276 };
    constexpr std::array<double, 2> eventSnrs { 18.0, 9.0 };
    constexpr std::array<std::uint32_t, 4> eventSeeds {
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u
    };

    std::array<int, kinds.size()> core {};
    std::array<int, kinds.size()> caught {};
    std::array<int, kinds.size()> lateFalse {};
    std::array<int, kinds.size()> lateFrames {};
    double worstRecoveryMs = 0.0;

    for (std::size_t ki = 0; ki < kinds.size(); ++ki)
        for (const auto& profile : profiles)
            for (double f0 : eventFrequencies)
                for (double snr : eventSnrs)
                    for (auto seed : eventSeeds)
                    {
                        auto x = makeContinuousVoice(profile, f0, snr,
                            seed ^ static_cast<std::uint32_t>(f0 * 71.0));
                        State state { f0, residual(x, eventStart - 512, f0),
                                          tailRms(x, eventStart - 512) };
                        if (!std::isfinite(state.baselineResidual)) continue;
                        injectEvent(x, kinds[ki], seed ^ 0x3c6ef372u);

                        bool recovered = false;
                        int recoveryEnd = -1;
                        for (int end = eventStart;
                             end <= eventEnd + analysisSamples + 960;
                             end += hopSamples)
                        {
                            const bool trans = transitionHoldout(x, end, state);
                            if (coreOverlapsEvent(end, kinds[ki]))
                            {
                                ++core[ki];
                                if (trans) ++caught[ki];
                            }

                            const int onePeriod = static_cast<int>(std::ceil(sr / f0));
                            if (end - analysisSamples >= eventEnd + onePeriod)
                            {
                                ++lateFrames[ki];
                                if (trans) ++lateFalse[ki];
                            }

                            if (!trans)
                            {
                                if (!recovered && end >= eventEnd)
                                {
                                    recovered = true;
                                    recoveryEnd = end;
                                }
                                state.baselineResidual = residual(x, end, state.stableHz);
                                state.baselineRms = tailRms(x, end);
                            }
                        }

                        if (!recovered)
                            worstRecoveryMs = std::numeric_limits<double>::infinity();
                        else if (std::isfinite(worstRecoveryMs))
                            worstRecoveryMs = std::max(worstRecoveryMs,
                                1000.0 * static_cast<double>(std::max(0, recoveryEnd - eventEnd)) / sr);
                    }

    std::cout << std::fixed << std::setprecision(6);
    for (std::size_t si = 0; si < snrs.size(); ++si)
        std::cout << "TRANSITION_HOLDOUT_NORMAL snr=" << snrs[si]
                  << " frames=" << normalFrames[si]
                  << " false_transition=" << normalFalse[si]
                  << " worst_ratio=" << worstRatio[si] << '\n';

    int totalCore = 0, totalCaught = 0, totalLate = 0, totalLateFalse = 0;
    for (std::size_t ki = 0; ki < kinds.size(); ++ki)
    {
        totalCore += core[ki];
        totalCaught += caught[ki];
        totalLate += lateFrames[ki];
        totalLateFalse += lateFalse[ki];
        std::cout << "TRANSITION_HOLDOUT_EVENT kind=" << ki
                  << " core=" << core[ki]
                  << " caught=" << caught[ki]
                  << " missed=" << (core[ki] - caught[ki])
                  << " late_frames=" << lateFrames[ki]
                  << " late_false=" << lateFalse[ki] << '\n';
    }

    std::cout << "TRANSITION_HOLDOUT_SUMMARY core=" << totalCore
              << " caught=" << totalCaught
              << " missed=" << (totalCore - totalCaught)
              << " late_frames=" << totalLate
              << " late_false=" << totalLateFalse
              << " worst_recovery_ms=" << worstRecoveryMs << '\n';

    const bool commonNormalSafe = normalFalse[0] == 0 && normalFalse[1] == 0;
    const bool noLingering = totalLateFalse == 0;
    std::cout << "F0_TRANSITION_HOLDOUT_COMMON_NORMAL_SAFE="
              << (commonNormalSafe ? "PASS" : "FAIL") << '\n';
    std::cout << "F0_TRANSITION_HOLDOUT_NO_LINGERING="
              << (noLingering ? "PASS" : "FAIL") << '\n';
    return 0;
}
