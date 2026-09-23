#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

#include "F0WholeNoteProgressiveProbe.cpp"

namespace
{
struct LocalDoubleWitness
{
    bool observable = false;
    double ratio = 1.0;
    int bestLongLag = 0;
    int centreLag = 0;
    int radius = 0;
};

LocalDoubleWitness measureLocalDoubleLag(
    const std::array<double, progressiveMaxSamples>& x,
    int sampleCount,
    int shortLag,
    double radiusFraction)
{
    if (shortLag <= 0)
        return {};

    const int centre = 2 * shortLag;
    const int radius = std::max(2,
        static_cast<int>(std::ceil(radiusFraction * centre)));
    const int lo = std::max(shortLag + 2, centre - radius);
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

    auto mismatch = [&](int lag)
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

    const double shortMismatch = mismatch(shortLag);
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

    return {
        true,
        bestLongMismatch / std::max(1.0e-12, shortMismatch),
        bestLongLag,
        centre,
        radius
    };
}

struct RuleCounts
{
    int correctFlagged = 0;
    int highFlagged = 0;
};

constexpr std::array<double, 4> radii { 0.02, 0.03, 0.04, 0.05 };
constexpr std::array<double, 7> thresholds {
    0.65, 0.70, 0.75, 0.80, 0.85, 0.90, 0.95
};
constexpr std::array<double, 12> frequencies {
    110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
    246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
};
constexpr std::array<double, 3> snrs { 18.0, 9.0, 3.0 };
constexpr std::array<std::array<std::uint32_t, 6>, 6> seedSets {{
    {{ 0x6c8e9cf5u, 0x5a827999u, 0x3c6ef372u, 0xbb67ae85u, 0xa54ff53au, 0x510e527fu }},
    {{ 0x8f1bbcdcu, 0xca62c1d6u, 0x9b05688cu, 0x1f83d9abu, 0x4a7484aau, 0x3f84d5b5u }},
    {{ 0x5be0cd19u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u }},
    {{ 0xcbbb9d5du, 0x629a292au, 0x9159015au, 0x152fecd8u, 0x67332667u, 0x8eb44a87u }},
    {{ 0xd1310ba6u, 0x98dfb5acu, 0x2ffd72dbu, 0xd01adfb7u, 0xb8e1afedu, 0x6a267e96u }},
    {{ 0xba7c9045u, 0xf12c7f99u, 0x24a19947u, 0xb3916cf7u, 0x0801f2e2u, 0x858efc16u }}
}};
}

int main()
{
    std::array<std::array<RuleCounts, thresholds.size()>, radii.size()> counts {};
    int eligibleCorrect = 0;
    int eligibleHigh = 0;
    int otherWrong = 0;

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
                        if (correct) ++eligibleCorrect;
                        else if (high) ++eligibleHigh;
                        else { ++otherWrong; continue; }

                        for (std::size_t r = 0; r < radii.size(); ++r)
                        {
                            const auto w = measureLocalDoubleLag(
                                x, 1024, e.lag, radii[r]);
                            if (!w.observable)
                                continue;
                            for (std::size_t t = 0; t < thresholds.size(); ++t)
                                if (w.ratio <= thresholds[t])
                                {
                                    if (correct) ++counts[r][t].correctFlagged;
                                    if (high) ++counts[r][t].highFlagged;
                                }
                        }
                    }

    std::cout << "LOCAL_DOUBLE_SUMMARY"
              << " eligible_correct=" << eligibleCorrect
              << " eligible_high=" << eligibleHigh
              << " other_wrong=" << otherWrong;
    for (std::size_t r = 0; r < radii.size(); ++r)
        for (std::size_t t = 0; t < thresholds.size(); ++t)
            std::cout << " r" << static_cast<int>(radii[r] * 100.0 + 0.5)
                      << "t" << static_cast<int>(thresholds[t] * 100.0 + 0.5)
                      << "_correct=" << counts[r][t].correctFlagged
                      << " r" << static_cast<int>(radii[r] * 100.0 + 0.5)
                      << "t" << static_cast<int>(thresholds[t] * 100.0 + 0.5)
                      << "_high=" << counts[r][t].highFlagged;
    std::cout << '\n';

    // Print the exact eight unresolved cases at the most useful radii.
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
            const double f0 = 246.9417;
            const auto x = makeProgressiveVoiceLike(
                *strong, f0, 3.0,
                seed ^ static_cast<std::uint32_t>(f0 * 97.0));
            const auto e = estimateProgressive(x, 1024);
            std::cout << std::fixed << std::setprecision(6)
                      << "LOCAL_DOUBLE_EIGHT seed=" << seed
                      << " raw_hz=" << e.hz
                      << " raw_cents=" << cents(e.hz, f0)
                      << " lag=" << e.lag;
            for (double radius : radii)
            {
                const auto w = measureLocalDoubleLag(x, 1024, e.lag, radius);
                std::cout << " r" << static_cast<int>(radius * 100.0 + 0.5)
                          << "_ratio=" << w.ratio
                          << " r" << static_cast<int>(radius * 100.0 + 0.5)
                          << "_bestlag=" << w.bestLongLag;
            }
            std::cout << '\n';
        }
    return 0;
}
