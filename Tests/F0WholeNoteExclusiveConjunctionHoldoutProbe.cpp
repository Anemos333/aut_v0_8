#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0WholeNoteExtendedExclusiveHoldoutProbe.cpp"

namespace
{
constexpr std::array<std::uint32_t, 8> conjunctionHoldoutSeeds {
    0xa4093822u, 0x299f31d0u, 0x082efa98u, 0xec4e6c89u,
    0x452821e6u, 0x38d01377u, 0xbe5466cfu, 0x34e90c6cu
};

struct ConjunctionStats
{
    int cases = 0;
    int correct = 0;
    int high = 0;
    int otherWrong = 0;
    int correctFlagged = 0;
    int highFlagged = 0;
    int highCorrected = 0;
    int highWrongLower = 0;
    int artificialLow = 0;
};
}

int main()
{
    constexpr double maxLongToShortMismatchRatio = 1.20;
    std::array<ConjunctionStats, checkpoints.size()> stats {};

    for (const auto& profile : profiles)
        for (double f0 : holdoutFrequencies)
            for (double snr : holdoutSnrs)
                for (auto seed : conjunctionHoldoutSeeds)
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
                        if (correct) ++s.correct;
                        else if (high) ++s.high;
                        else { ++s.otherWrong; continue; }

                        if (e.loweredPrimitive || 0.5 * e.hz < minimumF0)
                            continue;

                        const auto d = measureExtendedExclusiveLowerFamily(
                            x, sampleCount, e);
                        if (!d.observable)
                            continue;

                        // Frozen before this holdout.  Spectral lower-family
                        // evidence is allowed to speak only when the whole-wave
                        // doubled-period hypothesis is not materially worse than
                        // the primary short period.
                        const bool flag = d.witnesses8 >= 3
                                       && d.localRatio <= maxLongToShortMismatchRatio;
                        if (!flag)
                            continue;

                        if (correct)
                        {
                            ++s.correctFlagged;
                            continue;
                        }

                        ++s.highFlagged;
                        const double lowerError = std::abs(cents(d.lowHz, f0));
                        if (lowerError <= 100.0) ++s.highCorrected;
                        else ++s.highWrongLower;
                        if (d.lowHz < 0.75 * f0) ++s.artificialLow;
                    }
                }

    bool pass = true;
    for (std::size_t i = 0; i < checkpoints.size(); ++i)
    {
        const auto& s = stats[i];
        std::cout << "EXCLUSIVE_CONJUNCTION_HOLDOUT"
                  << " samples=" << checkpoints[i]
                  << " ms=" << std::fixed << std::setprecision(4)
                  << (1000.0 * static_cast<double>(checkpoints[i]) / sr)
                  << " cases=" << s.cases
                  << " correct=" << s.correct
                  << " high=" << s.high
                  << " other_wrong=" << s.otherWrong
                  << " correct_flagged=" << s.correctFlagged
                  << " high_flagged=" << s.highFlagged
                  << " high_corrected=" << s.highCorrected
                  << " high_wrong_lower=" << s.highWrongLower
                  << " artificial_low=" << s.artificialLow
                  << '\n';

        if (s.correctFlagged != 0
            || s.highWrongLower != 0
            || s.artificialLow != 0)
            pass = false;
    }

    std::cout << "EXCLUSIVE_CONJUNCTION_HOLDOUT_SAFE="
              << (pass ? "PASS" : "FAIL") << '\n';
    return 0;
}
