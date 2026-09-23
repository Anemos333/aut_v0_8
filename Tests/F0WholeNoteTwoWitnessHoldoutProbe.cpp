#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0WholeNoteExclusiveConjunctionHoldoutProbe.cpp"

namespace
{
constexpr double frozenRatioThreshold = 0.85;
constexpr int frozenWitnessThreshold = 2;

constexpr std::array<std::uint32_t, 8> knownSeedsA {
    0x0d95748fu, 0x728eb658u, 0x718bcd58u, 0x82154aeeu,
    0x7b54a41du, 0xc25a59b5u, 0x9c30d539u, 0x2af26013u
};
constexpr std::array<std::uint32_t, 8> knownSeedsB {
    0xa4093822u, 0x299f31d0u, 0x082efa98u, 0xec4e6c89u,
    0x452821e6u, 0x38d01377u, 0xbe5466cfu, 0x34e90c6cu
};
// Independent holdout: fixed before observing any result from this rule.
constexpr std::array<std::uint32_t, 8> independentSeeds {
    0x9e3779b9u, 0x7f4a7c15u, 0xf39cc060u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u
};

struct Stats
{
    int cases = 0;
    int rawCorrect = 0;
    int rawHigh = 0;
    int rawLow = 0;
    int rawOther = 0;
    int correctFlagged = 0;
    int highFlagged = 0;
    int highCorrected = 0;
    int highWrongLower = 0;
    int artificialLow = 0;
};

template <std::size_t N>
void runSet(const char* name,
            const std::array<std::uint32_t, N>& seeds)
{
    std::array<Stats, checkpoints.size()> stats {};

    for (const auto& profile : profiles)
        for (double f0 : holdoutFrequencies)
            for (double snr : holdoutSnrs)
                for (auto seed : seeds)
                {
                    const auto mixedSeed = seed
                        ^ static_cast<std::uint32_t>(f0 * 97.0);
                    const auto x = makePrefixStableExtendedVoiceLike(
                        profile, f0, snr, mixedSeed);

                    for (std::size_t i = 0; i < checkpoints.size(); ++i)
                    {
                        const int sampleCount = checkpoints[i];
                        auto e = estimateExtended(x, sampleCount);
                        if (sampleCount >= 896)
                            e = applyExtendedLongConsensus(x, sampleCount, e);

                        auto& s = stats[i];
                        ++s.cases;
                        if (!e.valid)
                            continue;

                        const double ae = std::abs(cents(e.hz, f0));
                        const bool correct = ae <= 100.0;
                        const bool high = e.hz > 1.5 * f0;
                        const bool low = e.hz < 0.75 * f0;
                        if (correct) ++s.rawCorrect;
                        else if (high) ++s.rawHigh;
                        else if (low) ++s.rawLow;
                        else ++s.rawOther;

                        if (e.loweredPrimitive || 0.5 * e.hz < minimumF0)
                            continue;

                        const auto d = measureExtendedExclusiveLowerFamily(
                            x, sampleCount, e);
                        if (!d.observable)
                            continue;

                        const bool flag = d.witnesses8 >= frozenWitnessThreshold
                                       && d.localRatio <= frozenRatioThreshold;
                        if (!flag)
                            continue;

                        if (correct)
                        {
                            ++s.correctFlagged;
                            continue;
                        }
                        if (!high)
                            continue;

                        ++s.highFlagged;
                        const double lowerError = std::abs(cents(d.lowHz, f0));
                        if (lowerError <= 100.0) ++s.highCorrected;
                        else ++s.highWrongLower;
                        if (d.lowHz < 0.75 * f0) ++s.artificialLow;
                    }
                }

    bool safe = true;
    for (std::size_t i = 0; i < checkpoints.size(); ++i)
    {
        const auto& s = stats[i];
        std::cout << "TWO_WITNESS_HOLDOUT"
                  << " set=" << name
                  << " samples=" << checkpoints[i]
                  << " ms=" << std::fixed << std::setprecision(4)
                  << (1000.0 * static_cast<double>(checkpoints[i]) / sr)
                  << " cases=" << s.cases
                  << " raw_correct=" << s.rawCorrect
                  << " raw_high=" << s.rawHigh
                  << " raw_low=" << s.rawLow
                  << " raw_other=" << s.rawOther
                  << " correct_flagged=" << s.correctFlagged
                  << " high_flagged=" << s.highFlagged
                  << " high_corrected=" << s.highCorrected
                  << " high_wrong_lower=" << s.highWrongLower
                  << " artificial_low=" << s.artificialLow
                  << '\n';

        if (s.correctFlagged != 0
            || s.highWrongLower != 0
            || s.artificialLow != 0)
            safe = false;
    }

    std::cout << "TWO_WITNESS_HOLDOUT_SAFE set=" << name
              << " value=" << (safe ? "PASS" : "FAIL") << '\n';
}
}

int main()
{
    runSet("known_a", knownSeedsA);
    runSet("known_b", knownSeedsB);
    runSet("independent", independentSeeds);
    return 0;
}
