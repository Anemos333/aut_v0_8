#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

#define main transition_discipline_reference_main
#include "F0TransitionDisciplineProbe.cpp"
#undef main

namespace
{
constexpr double transitionBreakThreshold = 0.03;

double breakScore(const Sequence& x, int endExclusive, double stableHz)
{
    if (!(stableHz > 0.0)) return std::numeric_limits<double>::infinity();
    const int lag = std::max(2, static_cast<int>(std::lround(sr / stableHz)));
    const int begin = endExclusive - analysisSamples;
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

bool gateAllows(const Sequence& x, int endExclusive)
{
    F0PeriodicGateV1 gate;
    gate.prepare(sr);
    bool measure = true;
    const int begin = endExclusive - analysisSamples;
    for (int n = begin; n < endExclusive; ++n)
        measure = gate.processSample(static_cast<float>(x[static_cast<std::size_t>(n)]));
    return measure;
}

bool transitionRequired(const Sequence& x, int endExclusive, double stableHz)
{
    return !gateAllows(x, endExclusive)
        || breakScore(x, endExclusive, stableHz) >= transitionBreakThreshold;
}
}

int main()
{
    constexpr std::array<double, 7> frequencies {
        110.0, 146.8324, 196.0, 246.9417, 329.6276, 440.0, 659.2551
    };
    constexpr std::array<std::uint32_t, 10> normalSeeds {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu,
        0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u, 0xcbbb9d5du, 0x629a292au
    };

    int normalFrames = 0;
    int normalFalseTransition = 0;
    double normalWorstScore = 0.0;
    for (double f0 : frequencies)
        for (auto seed : normalSeeds)
        {
            const auto x = makeStableSequence(f0, seed ^ static_cast<std::uint32_t>(f0 * 43.0));
            const auto prior = decide(extractFrame(x, 2200), f0);
            if (!prior.stable || !prior.familyCorrect) continue;
            for (int end = 2600; end <= 12000; end += 96)
            {
                ++normalFrames;
                const double s = breakScore(x, end, prior.hz);
                normalWorstScore = std::max(normalWorstScore, s);
                if (transitionRequired(x, end, prior.hz)) ++normalFalseTransition;
            }
        }

    constexpr std::array<EventKind, 6> kinds {
        EventKind::click, EventKind::dropout, EventKind::whiteBurst,
        EventKind::hissBurst, EventKind::breathBurst, EventKind::phaseBreak
    };
    constexpr std::array<std::uint32_t, 10> transientSeeds {
        0x243f6a88u, 0x85a308d3u, 0x13198a2eu, 0x03707344u, 0xa4093822u,
        0x299f31d0u, 0x082efa98u, 0xec4e6c89u, 0x452821e6u, 0x38d01377u
    };

    std::array<int, 6> coreFrames {};
    std::array<int, 6> transitionFrames {};
    std::array<int, 6> gateFrames {};
    std::array<int, 6> noveltyFrames {};
    int postFrames = 0;
    int postFalseTransition = 0;
    double worstRecoveryMs = 0.0;

    for (std::size_t k = 0; k < kinds.size(); ++k)
        for (auto seed : transientSeeds)
        {
            constexpr double f0 = 220.0;
            auto x = makeStableSequence(f0, seed);
            const auto prior = decide(extractFrame(x, eventStart - 512), f0);
            if (!prior.stable || !prior.familyCorrect) continue;
            injectEvent(x, kinds[k], seed ^ 0x9e3779b9u);

            bool recovered = false;
            int recoveryEnd = -1;
            for (int end = eventStart; end <= eventEnd + analysisSamples + 512; end += hopSamples)
            {
                const bool gateBlocked = !gateAllows(x, end);
                const bool novelty = breakScore(x, end, prior.hz) >= transitionBreakThreshold;
                const bool trans = gateBlocked || novelty;

                if (coreOverlapsEvent(end, kinds[k]))
                {
                    ++coreFrames[k];
                    if (trans) ++transitionFrames[k];
                    if (gateBlocked) ++gateFrames[k];
                    if (novelty) ++noveltyFrames[k];
                }

                if (cleanAfterEvent(end))
                {
                    ++postFrames;
                    if (trans) ++postFalseTransition;
                    if (!recovered && !trans)
                    {
                        recovered = true;
                        recoveryEnd = end;
                    }
                }
            }

            if (!recovered)
                worstRecoveryMs = std::numeric_limits<double>::infinity();
            else if (std::isfinite(worstRecoveryMs))
            {
                const double ms = 1000.0 * static_cast<double>(
                    std::max(0, recoveryEnd - (eventEnd + analysisSamples))) / sr;
                worstRecoveryMs = std::max(worstRecoveryMs, ms);
            }
        }

    std::cout << std::fixed << std::setprecision(6)
              << "STATEFUL_TRANSITION_NORMAL frames=" << normalFrames
              << " false_transition=" << normalFalseTransition
              << " worst_score=" << normalWorstScore << '\n';

    int totalCore = 0, totalTransition = 0;
    for (std::size_t k = 0; k < kinds.size(); ++k)
    {
        totalCore += coreFrames[k];
        totalTransition += transitionFrames[k];
        std::cout << "STATEFUL_TRANSITION_EVENT kind=" << k
                  << " core=" << coreFrames[k]
                  << " transition=" << transitionFrames[k]
                  << " gate=" << gateFrames[k]
                  << " novelty=" << noveltyFrames[k] << '\n';
    }

    std::cout << "STATEFUL_TRANSITION_SUMMARY core=" << totalCore
              << " transition=" << totalTransition
              << " missed=" << (totalCore - totalTransition)
              << " post_frames=" << postFrames
              << " post_false_transition=" << postFalseTransition
              << " worst_recovery_ms=" << worstRecoveryMs << '\n';

    const bool ready = normalFalseTransition == 0
                    && totalCore == totalTransition
                    && postFalseTransition == 0;
    std::cout << "F0_STATEFUL_TRANSITION_READY=" << (ready ? "PASS" : "FAIL") << '\n';
    return 0;
}
