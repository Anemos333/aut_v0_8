#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0WholeNoteLocalDoubleLagProbe.cpp"

namespace
{
struct ComboCount { int correct = 0; int high = 0; };
constexpr std::array<double, 5> comboRatios { 0.70, 0.75, 0.80, 0.85, 0.90 };
constexpr std::array<int, 4> minOffsets { 1, 2, 3, 4 };
}

int main()
{
    std::array<std::array<ComboCount, minOffsets.size()>, comboRatios.size()> counts {};
    int eligibleCorrect = 0;
    int eligibleHigh = 0;

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

                        const auto w = measureLocalDoubleLag(x, 1024, e.lag, 0.05);
                        if (!w.observable) continue;
                        const int offset = std::abs(w.bestLongLag - w.centreLag);

                        for (std::size_t r = 0; r < comboRatios.size(); ++r)
                            for (std::size_t o = 0; o < minOffsets.size(); ++o)
                                if (w.ratio <= comboRatios[r]
                                    && offset >= minOffsets[o])
                                {
                                    if (correct) ++counts[r][o].correct;
                                    if (high) ++counts[r][o].high;
                                }
                    }

    std::cout << "LOCAL_DOUBLE_COMBO_SUMMARY"
              << " eligible_correct=" << eligibleCorrect
              << " eligible_high=" << eligibleHigh;
    for (std::size_t r = 0; r < comboRatios.size(); ++r)
        for (std::size_t o = 0; o < minOffsets.size(); ++o)
            std::cout << " r" << static_cast<int>(comboRatios[r] * 100.0 + 0.5)
                      << "o" << minOffsets[o]
                      << "_correct=" << counts[r][o].correct
                      << " r" << static_cast<int>(comboRatios[r] * 100.0 + 0.5)
                      << "o" << minOffsets[o]
                      << "_high=" << counts[r][o].high;
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
            const auto w = measureLocalDoubleLag(x, 1024, e.lag, 0.05);
            std::cout << std::fixed << std::setprecision(6)
                      << "LOCAL_DOUBLE_COMBO_EIGHT seed=" << seed
                      << " ratio=" << w.ratio
                      << " short_lag=" << e.lag
                      << " centre=" << w.centreLag
                      << " best_long=" << w.bestLongLag
                      << " offset=" << std::abs(w.bestLongLag - w.centreLag)
                      << " inferred_low_hz="
                      << (w.bestLongLag > 0 ? sr / w.bestLongLag : 0.0)
                      << '\n';
        }
    return 0;
}
