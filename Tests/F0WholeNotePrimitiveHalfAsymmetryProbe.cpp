#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0WholeNoteLocalDoubleLagProbe.cpp"

namespace
{
struct HalfAsymmetry
{
    bool observable = false;
    double significance = 0.0;
    double normalizedMeanDifference = 0.0;
    int cycles = 0;
    int longLag = 0;
};

HalfAsymmetry measurePrimitiveHalfAsymmetry(
    const std::array<double, progressiveMaxSamples>& x,
    int sampleCount,
    int shortLag)
{
    const auto local = measureLocalDoubleLag(x, sampleCount, shortLag, 0.05);
    if (!local.observable || local.bestLongLag <= 0)
        return {};

    const int period = local.bestLongLag;
    const int half = static_cast<int>(std::lround(0.5 * period));
    const int cycles = sampleCount / period;
    if (half < 2 || cycles < 3)
        return {};

    double systematic = 0.0;
    double expectedNoise = 0.0;
    double templateEnergy = 0.0;
    int phases = 0;

    for (int p = 0; p < half && p + half < period; ++p)
    {
        double sumDiff = 0.0;
        double sumDiff2 = 0.0;
        double sumLevel2 = 0.0;
        int count = 0;

        for (int k = 0; k < cycles; ++k)
        {
            const int base = k * period;
            if (base + p + half >= sampleCount)
                break;
            const double a = x[static_cast<std::size_t>(base + p)];
            const double b = x[static_cast<std::size_t>(base + p + half)];
            const double d = a - b;
            sumDiff += d;
            sumDiff2 += d * d;
            sumLevel2 += 0.5 * (a * a + b * b);
            ++count;
        }

        if (count < 3)
            continue;
        const double mean = sumDiff / static_cast<double>(count);
        const double variance = std::max(0.0,
            (sumDiff2 - static_cast<double>(count) * mean * mean)
            / static_cast<double>(count - 1));

        systematic += mean * mean;
        expectedNoise += variance / static_cast<double>(count);
        templateEnergy += sumLevel2 / static_cast<double>(count);
        ++phases;
    }

    if (phases < half / 2 || !(templateEnergy > 1.0e-20))
        return {};

    return {
        true,
        systematic / std::max(1.0e-12, expectedNoise),
        systematic / templateEnergy,
        cycles,
        period
    };
}

struct Counts { int correct = 0; int high = 0; };
constexpr std::array<double, 10> significanceThresholds {
    1.0, 1.25, 1.5, 2.0, 2.5, 3.0, 4.0, 5.0, 7.5, 10.0
};
constexpr std::array<double, 8> normalizedThresholds {
    0.01, 0.02, 0.03, 0.05, 0.08, 0.10, 0.15, 0.20
};
}

int main()
{
    std::array<Counts, significanceThresholds.size()> sigCounts {};
    std::array<Counts, normalizedThresholds.size()> normCounts {};
    int eligibleCorrect = 0, eligibleHigh = 0;

    for (const auto& seedSet : seedSets)
        for (const auto& profile : profiles)
            for (double f0 : frequencies)
                for (double snr : snrs)
                    for (auto seed : seedSet)
                    {
                        const auto x = makeProgressiveVoiceLike(
                            profile, f0, snr,
                            seed ^ static_cast<std::uint32_t>(f0 * 97.0));
                        const auto e = estimateProgressive(x, 1024);
                        if (!e.valid || e.loweredPrimitive
                            || 0.5 * e.hz < minimumF0)
                            continue;
                        const double ae = std::abs(cents(e.hz, f0));
                        const bool correct = ae <= 100.0;
                        const bool high = e.hz > 1.5 * f0;
                        if (!correct && !high) continue;
                        if (correct) ++eligibleCorrect;
                        if (high) ++eligibleHigh;

                        const auto a = measurePrimitiveHalfAsymmetry(x, 1024, e.lag);
                        if (!a.observable) continue;
                        for (std::size_t i = 0; i < significanceThresholds.size(); ++i)
                            if (a.significance >= significanceThresholds[i])
                            {
                                if (correct) ++sigCounts[i].correct;
                                if (high) ++sigCounts[i].high;
                            }
                        for (std::size_t i = 0; i < normalizedThresholds.size(); ++i)
                            if (a.normalizedMeanDifference >= normalizedThresholds[i])
                            {
                                if (correct) ++normCounts[i].correct;
                                if (high) ++normCounts[i].high;
                            }
                    }

    std::cout << "HALF_ASYMMETRY_SUMMARY"
              << " eligible_correct=" << eligibleCorrect
              << " eligible_high=" << eligibleHigh;
    for (std::size_t i = 0; i < significanceThresholds.size(); ++i)
        std::cout << " s" << static_cast<int>(significanceThresholds[i] * 100.0 + 0.5)
                  << "_correct=" << sigCounts[i].correct
                  << " s" << static_cast<int>(significanceThresholds[i] * 100.0 + 0.5)
                  << "_high=" << sigCounts[i].high;
    for (std::size_t i = 0; i < normalizedThresholds.size(); ++i)
        std::cout << " n" << static_cast<int>(normalizedThresholds[i] * 100.0 + 0.5)
                  << "_correct=" << normCounts[i].correct
                  << " n" << static_cast<int>(normalizedThresholds[i] * 100.0 + 0.5)
                  << "_high=" << normCounts[i].high;
    std::cout << '\n';

    constexpr std::array<std::uint32_t, 8> unresolvedSeeds {
        1821285621u, 1518500249u, 2600822924u, 1249150122u,
        1065670069u, 2614888103u, 2438529370u, 2240740374u
    };
    const VoiceProfile* strong = nullptr;
    for (const auto& profile : profiles)
        if (std::string(profile.name) == "strong_second") strong = &profile;
    if (strong != nullptr)
        for (auto seed : unresolvedSeeds)
        {
            constexpr double f0 = 246.9417;
            const auto x = makeProgressiveVoiceLike(
                *strong, f0, 3.0,
                seed ^ static_cast<std::uint32_t>(f0 * 97.0));
            const auto e = estimateProgressive(x, 1024);
            const auto a = measurePrimitiveHalfAsymmetry(x, 1024, e.lag);
            std::cout << std::fixed << std::setprecision(6)
                      << "HALF_ASYMMETRY_EIGHT seed=" << seed
                      << " observable=" << (a.observable ? 1 : 0)
                      << " short_hz=" << e.hz
                      << " short_lag=" << e.lag
                      << " long_lag=" << a.longLag
                      << " cycles=" << a.cycles
                      << " significance=" << a.significance
                      << " normalized=" << a.normalizedMeanDifference
                      << '\n';
        }
    return 0;
}
