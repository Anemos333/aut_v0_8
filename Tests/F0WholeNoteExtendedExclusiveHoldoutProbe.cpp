#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

#include "F0WholeNoteExtendedEightProbe.cpp"

namespace
{
struct ExtendedExclusiveEvidence
{
    bool observable = false;
    double lowHz = 0.0;
    double localRatio = 1.0;
    int witnesses8 = 0;
    int witnesses12 = 0;
};

double extendedProjectionPower(const ExtendedSignal& x,
                               int sampleCount,
                               double hz) noexcept
{
    if (!(hz > 0.0) || hz >= 0.48 * sr || sampleCount < 8)
        return 0.0;

    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < sampleCount; ++n)
    {
        const double w = 0.5 - 0.5 * std::cos(
            2.0 * pi * static_cast<double>(n)
            / static_cast<double>(sampleCount - 1));
        const double p = 2.0 * pi * hz * static_cast<double>(n) / sr;
        const double s = x[static_cast<std::size_t>(n)] * w;
        re += s * std::cos(p);
        im -= s * std::sin(p);
    }
    return re * re + im * im;
}

ExtendedExclusiveEvidence measureExtendedExclusiveLowerFamily(
    const ExtendedSignal& x,
    int sampleCount,
    const ProgressiveEstimate& e) noexcept
{
    if (!e.valid || e.lag <= 0 || 0.5 * e.hz < minimumF0)
        return {};

    const int centre = 2 * e.lag;
    const int radius = std::max(2,
        static_cast<int>(std::ceil(0.05 * static_cast<double>(centre))));
    const int lo = std::max(e.lag + 2, centre - radius);
    const int hi = std::min(sampleCount - 40, centre + radius);
    if (lo > hi)
        return {};

    const int commonOverlap = sampleCount - hi;
    if (commonOverlap < 40)
        return {};

    double mean = 0.0;
    for (int n = 0; n < sampleCount; ++n)
        mean += x[static_cast<std::size_t>(n)];
    mean /= static_cast<double>(sampleCount);

    auto mismatch = [&](int lag) noexcept
    {
        double diff = 0.0;
        double energy = 0.0;
        for (int n = 0; n < commonOverlap; ++n)
        {
            const double a = x[static_cast<std::size_t>(n)] - mean;
            const double b = x[static_cast<std::size_t>(n + lag)] - mean;
            const double d = a - b;
            diff += d * d;
            energy += a * a + b * b;
        }
        return diff / std::max(1.0e-30, energy);
    };

    const double shortMismatch = mismatch(e.lag);
    double bestLongMismatch = std::numeric_limits<double>::infinity();
    int bestLongLag = 0;
    for (int lag = lo; lag <= hi; ++lag)
    {
        const double m = mismatch(lag);
        if (m < bestLongMismatch)
        {
            bestLongMismatch = m;
            bestLongLag = lag;
        }
    }
    if (bestLongLag <= 0)
        return {};

    const double lowHz = sr / static_cast<double>(bestLongLag);
    if (!(lowHz >= minimumF0) || !(lowHz < e.hz))
        return {};

    int witnesses8 = 0;
    int witnesses12 = 0;
    int measured = 0;
    for (int k : { 1, 3, 5, 7 })
    {
        const double hz = lowHz * static_cast<double>(k);
        if (hz >= 0.44 * sr)
            continue;

        const double centrePower = extendedProjectionPower(x, sampleCount, hz);
        std::array<double, 4> side {
            extendedProjectionPower(x, sampleCount, hz - 0.40 * lowHz),
            extendedProjectionPower(x, sampleCount, hz - 0.30 * lowHz),
            extendedProjectionPower(x, sampleCount, hz + 0.30 * lowHz),
            extendedProjectionPower(x, sampleCount, hz + 0.40 * lowHz)
        };
        std::sort(side.begin(), side.end());
        const double localFloor = 0.5 * (side[1] + side[2]);
        const double ratio = centrePower / std::max(1.0e-20, localFloor);
        if (ratio >= 8.0) ++witnesses8;
        if (ratio >= 12.0) ++witnesses12;
        ++measured;
    }

    if (measured < 3)
        return {};

    return {
        true,
        lowHz,
        bestLongMismatch / std::max(1.0e-12, shortMismatch),
        witnesses8,
        witnesses12
    };
}

struct CheckpointStats
{
    int cases = 0;
    int eligibleCorrect = 0;
    int eligibleHigh = 0;
    int otherWrong = 0;
    int strictCorrectFlagged = 0;
    int strictHighFlagged = 0;
    int strictHighCorrected = 0;
    int strictHighWrongLower = 0;
    int strictArtificialLow = 0;
};

constexpr std::array<double, 12> holdoutFrequencies {
    110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
    246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
};
constexpr std::array<double, 3> holdoutSnrs { 18.0, 9.0, 3.0 };
constexpr std::array<std::uint32_t, 8> freshSeeds {
    0x0d95748fu, 0x728eb658u, 0x718bcd58u, 0x82154aeeu,
    0x7b54a41du, 0xc25a59b5u, 0x9c30d539u, 0x2af26013u
};
constexpr std::array<int, 3> checkpoints { 1024, 1280, 1536 };
}

int main()
{
    std::array<CheckpointStats, checkpoints.size()> stats {};

    for (const auto& profile : profiles)
        for (double f0 : holdoutFrequencies)
            for (double snr : holdoutSnrs)
                for (auto seed : freshSeeds)
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
                        if (correct) ++s.eligibleCorrect;
                        else if (high) ++s.eligibleHigh;
                        else { ++s.otherWrong; continue; }

                        if (e.loweredPrimitive || 0.5 * e.hz < minimumF0)
                            continue;

                        const auto d = measureExtendedExclusiveLowerFamily(
                            x, sampleCount, e);
                        if (!d.observable)
                            continue;

                        // Threshold fixed before this holdout: three odd,
                        // lower-family-exclusive lines, each >= 8x its local floor.
                        const bool strict = d.witnesses8 >= 3;
                        if (!strict)
                            continue;

                        if (correct)
                        {
                            ++s.strictCorrectFlagged;
                            continue;
                        }

                        if (high)
                        {
                            ++s.strictHighFlagged;
                            const double lowerError = std::abs(cents(d.lowHz, f0));
                            if (lowerError <= 100.0)
                                ++s.strictHighCorrected;
                            else
                                ++s.strictHighWrongLower;
                            if (d.lowHz < 0.75 * f0)
                                ++s.strictArtificialLow;
                        }
                    }
                }

    bool pass = true;
    for (std::size_t i = 0; i < checkpoints.size(); ++i)
    {
        const auto& s = stats[i];
        std::cout << "EXTENDED_EXCLUSIVE_HOLDOUT"
                  << " samples=" << checkpoints[i]
                  << " ms=" << std::fixed << std::setprecision(4)
                  << (1000.0 * static_cast<double>(checkpoints[i]) / sr)
                  << " cases=" << s.cases
                  << " correct=" << s.eligibleCorrect
                  << " high=" << s.eligibleHigh
                  << " other_wrong=" << s.otherWrong
                  << " correct_flagged=" << s.strictCorrectFlagged
                  << " high_flagged=" << s.strictHighFlagged
                  << " high_corrected=" << s.strictHighCorrected
                  << " high_wrong_lower=" << s.strictHighWrongLower
                  << " artificial_low=" << s.strictArtificialLow
                  << '\n';

        if (s.strictCorrectFlagged != 0
            || s.strictHighWrongLower != 0
            || s.strictArtificialLow != 0)
            pass = false;
    }

    // Development-only visibility of the original eight after the holdout rule
    // has already been fixed. These lines do not influence PASS/FAIL.
    const auto& strong = strongSecondProfile();
    constexpr double unresolvedTruth = 246.9417;
    for (const auto& item : unresolved)
    {
        const auto mixedSeed = item.seed
            ^ static_cast<std::uint32_t>(unresolvedTruth * 97.0);
        const auto x = makePrefixStableExtendedVoiceLike(
            strong, unresolvedTruth, 3.0, mixedSeed);
        for (int sampleCount : checkpoints)
        {
            auto e = estimateExtended(x, sampleCount);
            if (sampleCount >= 896)
                e = applyExtendedLongConsensus(x, sampleCount, e);
            const auto d = measureExtendedExclusiveLowerFamily(x, sampleCount, e);
            std::cout << std::fixed << std::setprecision(4)
                      << "EXTENDED_EXCLUSIVE_EIGHT seed=" << item.seed
                      << " samples=" << sampleCount
                      << " raw_hz=" << e.hz
                      << " low_hz=" << d.lowHz
                      << " w8=" << d.witnesses8
                      << " w12=" << d.witnesses12
                      << " strict=" << ((d.observable && d.witnesses8 >= 3) ? 1 : 0)
                      << '\n';
        }
    }

    std::cout << "EXTENDED_EXCLUSIVE_HOLDOUT_SAFE="
              << (pass ? "PASS" : "FAIL") << '\n';
    return 0;
}
