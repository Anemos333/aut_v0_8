#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#define main transition_discipline_reference_main
#include "F0TransitionDisciplineProbe.cpp"
#undef main

namespace
{
double stablePeriodBreakScore(const Sequence& x, int endExclusive, double stableHz)
{
    if (!(stableHz > 0.0)) return std::numeric_limits<double>::infinity();
    const int lag = std::max(2, static_cast<int>(std::lround(sr / stableHz)));
    const int begin = endExclusive - analysisSamples;
    if (begin - lag < 0) return std::numeric_limits<double>::infinity();

    double diff = 0.0;
    double energy = 0.0;
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

double percentile(std::vector<double> v, double p)
{
    if (v.empty()) return -1.0;
    std::sort(v.begin(), v.end());
    const std::size_t i = static_cast<std::size_t>(
        std::floor(p * static_cast<double>(v.size() - 1)));
    return v[i];
}
}

int main()
{
    constexpr std::array<double, 7> frequencies {
        110.0, 146.8324, 196.0, 246.9417, 329.6276, 440.0, 659.2551
    };
    constexpr std::array<std::uint32_t, 8> seeds {
        0x10203040u, 0x55667788u, 0x13572468u, 0x89abcdefu,
        0xcafebabeu, 0x0badf00du, 0x31415926u, 0x27182818u
    };
    constexpr std::array<double, 10> thresholds {
        0.03, 0.05, 0.08, 0.10, 0.12, 0.15, 0.20, 0.30, 0.50, 0.80
    };

    std::vector<double> normalScores;
    std::array<int, thresholds.size()> normalFlagged {};

    for (double f0 : frequencies)
        for (auto seed : seeds)
        {
            const auto x = makeStableSequence(f0, seed ^ static_cast<std::uint32_t>(f0 * 31.0));
            const auto pre = decide(extractFrame(x, 2200), f0);
            if (!pre.stable || !pre.familyCorrect) continue;
            for (int end = 2600; end <= 12000; end += 128)
            {
                const double score = stablePeriodBreakScore(x, end, pre.hz);
                if (!std::isfinite(score)) continue;
                normalScores.push_back(score);
                for (std::size_t i = 0; i < thresholds.size(); ++i)
                    if (score >= thresholds[i]) ++normalFlagged[i];
            }
        }

    constexpr std::array<EventKind, 6> kinds {
        EventKind::click, EventKind::dropout, EventKind::whiteBurst,
        EventKind::hissBurst, EventKind::breathBurst, EventKind::phaseBreak
    };
    constexpr std::array<std::uint32_t, 8> transientSeeds {
        0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u,
        0x9999aaaau, 0xbbbbccccu, 0xddddeeeeu, 0x12344321u
    };

    std::array<int, 6> transientFrames {};
    std::array<std::array<int, thresholds.size()>, 6> transientFlagged {};
    std::array<std::vector<double>, 6> transientScores;

    for (std::size_t k = 0; k < kinds.size(); ++k)
        for (auto seed : transientSeeds)
        {
            constexpr double f0 = 220.0;
            auto x = makeStableSequence(f0, seed);
            const auto pre = decide(extractFrame(x, eventStart - 512), f0);
            if (!pre.stable || !pre.familyCorrect) continue;
            injectEvent(x, kinds[k], seed ^ 0xa5a5a5a5u);

            for (int end = eventStart; end <= eventEnd + analysisSamples; end += hopSamples)
            {
                if (!coreOverlapsEvent(end, kinds[k])) continue;
                const double score = stablePeriodBreakScore(x, end, pre.hz);
                if (!std::isfinite(score)) continue;
                ++transientFrames[k];
                transientScores[k].push_back(score);
                for (std::size_t i = 0; i < thresholds.size(); ++i)
                    if (score >= thresholds[i]) ++transientFlagged[k][i];
            }
        }

    std::cout << std::fixed << std::setprecision(6)
              << "TRANSITION_NOVELTY_NORMAL count=" << normalScores.size()
              << " p50=" << percentile(normalScores, 0.50)
              << " p90=" << percentile(normalScores, 0.90)
              << " p95=" << percentile(normalScores, 0.95)
              << " p99=" << percentile(normalScores, 0.99)
              << " max=" << percentile(normalScores, 1.00);
    for (std::size_t i = 0; i < thresholds.size(); ++i)
        std::cout << " t" << thresholds[i] << "=" << normalFlagged[i];
    std::cout << '\n';

    for (std::size_t k = 0; k < kinds.size(); ++k)
    {
        std::cout << "TRANSITION_NOVELTY_EVENT kind=" << static_cast<int>(k)
                  << " frames=" << transientFrames[k]
                  << " p10=" << percentile(transientScores[k], 0.10)
                  << " p50=" << percentile(transientScores[k], 0.50)
                  << " p90=" << percentile(transientScores[k], 0.90);
        for (std::size_t i = 0; i < thresholds.size(); ++i)
            std::cout << " t" << thresholds[i] << "=" << transientFlagged[k][i];
        std::cout << '\n';
    }

    return 0;
}
