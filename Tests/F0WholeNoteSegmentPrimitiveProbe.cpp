#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0WholeNoteLocalDoubleLagProbe.cpp"

namespace
{
double segmentMismatch(const std::array<double, progressiveMaxSamples>& x,
                       int start,
                       int length,
                       int lag)
{
    if (lag <= 0 || length <= lag + 8)
        return 1.0;
    double mean = 0.0;
    for (int n = 0; n < length; ++n)
        mean += x[static_cast<std::size_t>(start + n)];
    mean /= static_cast<double>(length);

    double diff = 0.0, energy = 0.0;
    const int overlap = length - lag;
    for (int n = 0; n < overlap; ++n)
    {
        const double a = x[static_cast<std::size_t>(start + n)] - mean;
        const double b = x[static_cast<std::size_t>(start + n + lag)] - mean;
        const double d = a - b;
        diff += d * d;
        energy += a * a + b * b;
    }
    return diff / std::max(1.0e-30, energy);
}

struct SegmentWitness
{
    bool observable = false;
    double edgeMaxRatio = 1.0;
    double threeMaxRatio = 1.0;
    double edgeMeanRatio = 1.0;
};

SegmentWitness measureSegmentConsistency(
    const std::array<double, progressiveMaxSamples>& x,
    int shortLag,
    int longLag)
{
    if (shortLag <= 0 || longLag <= shortLag)
        return {};

    constexpr int window = 512;
    constexpr std::array<int, 3> starts { 0, 256, 512 };
    std::array<double, 3> ratios {};
    for (std::size_t i = 0; i < starts.size(); ++i)
    {
        const double sm = segmentMismatch(x, starts[i], window, shortLag);
        const double lm = segmentMismatch(x, starts[i], window, longLag);
        ratios[i] = lm / std::max(1.0e-12, sm);
    }
    return {
        true,
        std::max(ratios[0], ratios[2]),
        std::max({ratios[0], ratios[1], ratios[2]}),
        0.5 * (ratios[0] + ratios[2])
    };
}

struct Counts { int correct = 0; int high = 0; };
constexpr std::array<double, 7> segmentThresholds {
    0.70, 0.75, 0.80, 0.85, 0.90, 0.95, 1.00
};
}

int main()
{
    std::array<Counts, segmentThresholds.size()> edgeCounts {};
    std::array<Counts, segmentThresholds.size()> threeCounts {};
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

                        const auto local = measureLocalDoubleLag(x, 1024, e.lag, 0.05);
                        if (!local.observable) continue;
                        const auto seg = measureSegmentConsistency(
                            x, e.lag, local.bestLongLag);
                        if (!seg.observable) continue;

                        for (std::size_t i = 0; i < segmentThresholds.size(); ++i)
                        {
                            if (seg.edgeMaxRatio <= segmentThresholds[i])
                            {
                                if (correct) ++edgeCounts[i].correct;
                                if (high) ++edgeCounts[i].high;
                            }
                            if (seg.threeMaxRatio <= segmentThresholds[i])
                            {
                                if (correct) ++threeCounts[i].correct;
                                if (high) ++threeCounts[i].high;
                            }
                        }
                    }

    std::cout << "SEGMENT_PRIMITIVE_SUMMARY"
              << " eligible_correct=" << eligibleCorrect
              << " eligible_high=" << eligibleHigh;
    for (std::size_t i = 0; i < segmentThresholds.size(); ++i)
        std::cout << " t" << static_cast<int>(segmentThresholds[i] * 100.0 + 0.5)
                  << "_edge_correct=" << edgeCounts[i].correct
                  << " t" << static_cast<int>(segmentThresholds[i] * 100.0 + 0.5)
                  << "_edge_high=" << edgeCounts[i].high
                  << " t" << static_cast<int>(segmentThresholds[i] * 100.0 + 0.5)
                  << "_three_correct=" << threeCounts[i].correct
                  << " t" << static_cast<int>(segmentThresholds[i] * 100.0 + 0.5)
                  << "_three_high=" << threeCounts[i].high;
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
            const auto local = measureLocalDoubleLag(x, 1024, e.lag, 0.05);
            const auto seg = measureSegmentConsistency(x, e.lag, local.bestLongLag);
            std::cout << std::fixed << std::setprecision(6)
                      << "SEGMENT_PRIMITIVE_EIGHT seed=" << seed
                      << " local_ratio=" << local.ratio
                      << " long_lag=" << local.bestLongLag
                      << " edge_max=" << seg.edgeMaxRatio
                      << " three_max=" << seg.threeMaxRatio
                      << " edge_mean=" << seg.edgeMeanRatio
                      << '\n';
        }
    return 0;
}
