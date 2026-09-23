#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

#include "F0WholeNoteLocalDoubleLagProbe.cpp"

namespace
{
struct CvPeriodScore
{
    bool valid = false;
    double normalizedError = 1.0;
    int minimumRepeats = 0;
};

CvPeriodScore crossValidatedTemplateError(
    const std::array<double, progressiveMaxSamples>& x,
    int sampleCount,
    int lag)
{
    if (lag <= 0 || sampleCount < 3 * lag)
        return {};

    double residual = 0.0;
    double energy = 0.0;
    int used = 0;
    int minRepeats = std::numeric_limits<int>::max();

    for (int phase = 0; phase < lag; ++phase)
    {
        double sum = 0.0;
        int count = 0;
        for (int n = phase; n < sampleCount; n += lag)
        {
            sum += x[static_cast<std::size_t>(n)];
            ++count;
        }
        if (count < 3)
            continue;
        minRepeats = std::min(minRepeats, count);

        for (int n = phase; n < sampleCount; n += lag)
        {
            const double s = x[static_cast<std::size_t>(n)];
            const double prediction = (sum - s) / static_cast<double>(count - 1);
            const double d = s - prediction;
            residual += d * d;
            energy += s * s;
            ++used;
        }
    }

    if (used < sampleCount / 2 || !(energy > 1.0e-20))
        return {};
    return {
        true,
        residual / energy,
        minRepeats == std::numeric_limits<int>::max() ? 0 : minRepeats
    };
}

struct CvWitness
{
    bool observable = false;
    double ratio = 1.0;
    double shortError = 1.0;
    double longError = 1.0;
    int longLag = 0;
};

CvWitness measureCvPrimitive(
    const std::array<double, progressiveMaxSamples>& x,
    const ProgressiveEstimate& e)
{
    if (!e.valid || e.lag <= 0)
        return {};
    const auto local = measureLocalDoubleLag(x, 1024, e.lag, 0.05);
    if (!local.observable)
        return {};
    const auto shortCv = crossValidatedTemplateError(x, 1024, e.lag);
    const auto longCv = crossValidatedTemplateError(x, 1024, local.bestLongLag);
    if (!shortCv.valid || !longCv.valid)
        return {};
    return {
        true,
        longCv.normalizedError / std::max(1.0e-12, shortCv.normalizedError),
        shortCv.normalizedError,
        longCv.normalizedError,
        local.bestLongLag
    };
}

struct Counts { int correct = 0; int high = 0; };
constexpr std::array<double, 10> cvThresholds {
    0.50, 0.60, 0.65, 0.70, 0.75,
    0.80, 0.85, 0.90, 0.95, 1.00
};
}

int main()
{
    std::array<Counts, cvThresholds.size()> counts {};
    int eligibleCorrect = 0, eligibleHigh = 0, observableCorrect = 0, observableHigh = 0;

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

                        const auto w = measureCvPrimitive(x, e);
                        if (!w.observable) continue;
                        if (correct) ++observableCorrect;
                        if (high) ++observableHigh;
                        for (std::size_t i = 0; i < cvThresholds.size(); ++i)
                            if (w.ratio <= cvThresholds[i])
                            {
                                if (correct) ++counts[i].correct;
                                if (high) ++counts[i].high;
                            }
                    }

    std::cout << "CV_PERIOD_SUMMARY"
              << " eligible_correct=" << eligibleCorrect
              << " eligible_high=" << eligibleHigh
              << " observable_correct=" << observableCorrect
              << " observable_high=" << observableHigh;
    for (std::size_t i = 0; i < cvThresholds.size(); ++i)
        std::cout << " t" << static_cast<int>(cvThresholds[i] * 100.0 + 0.5)
                  << "_correct=" << counts[i].correct
                  << " t" << static_cast<int>(cvThresholds[i] * 100.0 + 0.5)
                  << "_high=" << counts[i].high;
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
            const auto w = measureCvPrimitive(x, e);
            std::cout << std::fixed << std::setprecision(6)
                      << "CV_PERIOD_EIGHT seed=" << seed
                      << " observable=" << (w.observable ? 1 : 0)
                      << " short_hz=" << e.hz
                      << " short_lag=" << e.lag
                      << " long_lag=" << w.longLag
                      << " ratio=" << w.ratio
                      << " short_error=" << w.shortError
                      << " long_error=" << w.longError
                      << '\n';
        }
    return 0;
}
