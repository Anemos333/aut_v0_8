#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.1415926535897932384626433832795;

struct SignalGenerator
{
    float next() noexcept
    {
        const double t = static_cast<double>(index) / kSampleRate;
        const double vibrato = 1.0 + 0.0035 * std::sin(2.0 * kPi * 5.1 * t);
        const double phase = 2.0 * kPi * 220.0 * vibrato * t;

        std::uint32_t x = noiseState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        noiseState = x;

        const float noise =
            static_cast<float>((static_cast<int>(x & 0xffffu) - 32768) / 32768.0)
            * 0.004f;

        ++index;
        return static_cast<float>(0.18 * std::sin(phase)
                                + 0.11 * std::sin(2.0 * phase + 0.18)
                                + 0.065 * std::sin(3.0 * phase - 0.23)
                                + 0.035 * std::sin(4.0 * phase + 0.51))
             + noise;
    }

    std::uint64_t index = 0;
    std::uint32_t noiseState = 0x51f15e5du;
};

double envDouble(const char* name, double fallback) noexcept
{
    if (const char* value = std::getenv(name))
    {
        const double parsed = std::atof(value);
        if (std::isfinite(parsed) && parsed > 0.0)
            return parsed;
    }
    return fallback;
}

int envInt(const char* name, int fallback) noexcept
{
    if (const char* value = std::getenv(name))
    {
        const int parsed = std::atoi(value);
        if (parsed > 0)
            return parsed;
    }
    return fallback;
}

struct Distribution
{
    void add(double microseconds)
    {
        values.push_back(microseconds);
    }

    [[nodiscard]] double percentile(double q) const
    {
        if (values.empty())
            return 0.0;
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const double position = std::clamp(q, 0.0, 1.0)
                              * static_cast<double>(sorted.size() - 1);
        const auto lo = static_cast<std::size_t>(std::floor(position));
        const auto hi = static_cast<std::size_t>(std::ceil(position));
        const double fraction = position - static_cast<double>(lo);
        return sorted[lo] + (sorted[hi] - sorted[lo]) * fraction;
    }

    [[nodiscard]] double maximum() const
    {
        return values.empty() ? 0.0
                              : *std::max_element(values.begin(), values.end());
    }

    std::vector<double> values;
};

void printDistribution(const char* prefix,
                       const char* name,
                       const Distribution& distribution,
                       double budgetUs = 0.0)
{
    const double p50 = distribution.percentile(0.50);
    const double p95 = distribution.percentile(0.95);
    const double p99 = distribution.percentile(0.99);
    const double max = distribution.maximum();

    std::cout << prefix
              << " name=" << name
              << " count=" << distribution.values.size()
              << " p50_us=" << p50
              << " p95_us=" << p95
              << " p99_us=" << p99
              << " max_us=" << max;
    if (budgetUs > 0.0)
    {
        std::cout << " p50_budget_pct=" << (100.0 * p50 / budgetUs)
                  << " p99_budget_pct=" << (100.0 * p99 / budgetUs)
                  << " max_budget_pct=" << (100.0 * max / budgetUs);
    }
    std::cout << '\n';
}

void configure(ModernPitchEngine::MultiRatePitchTracker& tracker) noexcept
{
    tracker.prepare(kSampleRate);
    tracker.setRange(45.0f, 1600.0f);
    tracker.setSensitivity(0.70f);
    tracker.setVoiceAuthorityContext(true, 0.88f, 0.10f, 0.90f,
                                     0.88f, 0.08f, 0.90f, 0.06f);
}

int hopClass(int hopIndex) noexcept
{
    if ((hopIndex & 7) == 0)
        return 3;
    if ((hopIndex & 3) == 0)
        return 2;
    if ((hopIndex & 1) == 0)
        return 1;
    return 0;
}

const char* hopClassName(int index) noexcept
{
    constexpr std::array<const char*, 4> names {
        "full_only",
        "full_half",
        "full_half_quarter",
        "super_full_half_quarter_eighth"
    };
    return names[static_cast<std::size_t>(std::clamp(index, 0, 3))];
}

template <typename Ring>
Distribution profileAnalysePath(ModernPitchEngine::MultiRatePitchTracker& tracker,
                                const Ring& ring,
                                int writePosition,
                                int availableSamples,
                                double effectiveSampleRate,
                                float minimumFrequency,
                                float maximumFrequency,
                                int analysisLength,
                                int repetitions)
{
    Distribution distribution;
    distribution.values.reserve(static_cast<std::size_t>(repetitions));

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace workspace {};
    volatile float sink = 0.0f;

    for (int repetition = 0; repetition < repetitions; ++repetition)
    {
        const auto begin = std::chrono::steady_clock::now();
        const auto candidate = tracker.analyse(ring,
                                               writePosition,
                                               availableSamples,
                                               effectiveSampleRate,
                                               minimumFrequency,
                                               maximumFrequency,
                                               analysisLength,
                                               workspace);
        const auto end = std::chrono::steady_clock::now();

        sink += candidate.frequencyHz * 1.0e-9f;
        distribution.add(std::chrono::duration<double, std::micro>(end - begin).count());
    }

    static_cast<void>(sink);
    return distribution;
}

} // namespace

int main()
{
    const double warmupSeconds = envDouble("NEUMATON_WARMUP_SECONDS", 0.50);
    const double profileSeconds = envDouble("NEUMATON_PROFILE_SECONDS", 4.0);
    const int pathRepetitions = envInt("NEUMATON_PATH_REPS", 80);

    ModernPitchEngine::MultiRatePitchTracker tracker;
    configure(tracker);

    SignalGenerator generator;
    ModernPitchEngine::PitchObservation observation;

    const int warmupSamples = static_cast<int>(std::ceil(kSampleRate * warmupSeconds));
    for (int sample = 0; sample < warmupSamples; ++sample)
        tracker.processSample(generator.next(), observation);

    if (tracker.eighthRateAvailableSamples_
        < ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize)
    {
        std::cerr << "PROFILE_ERROR eighth-rate ring not warm enough: "
                  << tracker.eighthRateAvailableSamples_ << '\n';
        return 2;
    }

    std::array<Distribution, 4> hopDistributions;
    for (auto& distribution : hopDistributions)
        distribution.values.reserve(static_cast<std::size_t>(
            profileSeconds * kSampleRate
            / ModernPitchEngine::MultiRatePitchTracker::detectorHop / 4.0 + 64.0));

    const int profileSamples = static_cast<int>(std::ceil(kSampleRate * profileSeconds));
    volatile float sink = 0.0f;

    for (int sample = 0; sample < profileSamples; ++sample)
    {
        const bool hopDue =
            tracker.hopCounter_
            == ModernPitchEngine::MultiRatePitchTracker::detectorHop - 1;

        if (!hopDue)
        {
            tracker.processSample(generator.next(), observation);
            continue;
        }

        const int nextHopIndex = tracker.analysisHopCounter_ + 1;
        const int classIndex = hopClass(nextHopIndex);

        const auto begin = std::chrono::steady_clock::now();
        const bool emitted = tracker.processSample(generator.next(), observation);
        const auto end = std::chrono::steady_clock::now();

        if (!emitted)
        {
            std::cerr << "PROFILE_ERROR expected detector hop did not emit\n";
            return 3;
        }

        sink += observation.frequencyHz * 1.0e-9f;
        hopDistributions[static_cast<std::size_t>(classIndex)].add(
            std::chrono::duration<double, std::micro>(end - begin).count());
    }

    static_cast<void>(sink);

    const double detectorHopBudgetUs =
        1.0e6
        * static_cast<double>(ModernPitchEngine::MultiRatePitchTracker::detectorHop)
        / kSampleRate;

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "PROFILE_CONFIG"
              << " warmup_seconds=" << warmupSeconds
              << " profile_seconds=" << profileSeconds
              << " path_repetitions=" << pathRepetitions
              << " detector_hop_budget_us=" << detectorHopBudgetUs
              << '\n';

    for (int index = 0; index < 4; ++index)
        printDistribution("HOP_PROFILE",
                          hopClassName(index),
                          hopDistributions[static_cast<std::size_t>(index)],
                          detectorHopBudgetUs);

    const double fullP50 = hopDistributions[0].percentile(0.50);
    const double fullP99 = hopDistributions[0].percentile(0.99);
    const double superP50 = hopDistributions[3].percentile(0.50);
    const double superP99 = hopDistributions[3].percentile(0.99);
    std::cout << "SUPERHOP_RATIO"
              << " p50_vs_full=" << (superP50 / std::max(1.0e-9, fullP50))
              << " p99_vs_full=" << (superP99 / std::max(1.0e-9, fullP99))
              << '\n';

    const auto fullPath = profileAnalysePath(
        tracker,
        tracker.fullRateRing_,
        tracker.fullRateWritePosition_,
        tracker.fullRateAvailableSamples_,
        kSampleRate,
        std::max(160.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 2600.0f),
        ModernPitchEngine::MultiRatePitchTracker::standardAnalysisSize,
        pathRepetitions);

    const auto halfPath = profileAnalysePath(
        tracker,
        tracker.halfRateRing_,
        tracker.halfRateWritePosition_,
        tracker.halfRateAvailableSamples_,
        kSampleRate * 0.5,
        std::max(78.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 900.0f),
        ModernPitchEngine::MultiRatePitchTracker::standardAnalysisSize,
        pathRepetitions);

    const auto quarterPath = profileAnalysePath(
        tracker,
        tracker.quarterRateRing_,
        tracker.quarterRateWritePosition_,
        tracker.quarterRateAvailableSamples_,
        kSampleRate * 0.25,
        std::max(35.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 460.0f),
        384,
        pathRepetitions);

    const auto eighthPath = profileAnalysePath(
        tracker,
        tracker.eighthRateRing_,
        tracker.eighthRateWritePosition_,
        tracker.eighthRateAvailableSamples_,
        kSampleRate * 0.125,
        std::max(25.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 230.0f),
        ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize,
        pathRepetitions);

    printDistribution("PATH_PROFILE", "full", fullPath);
    printDistribution("PATH_PROFILE", "half", halfPath);
    printDistribution("PATH_PROFILE", "quarter", quarterPath);
    printDistribution("PATH_PROFILE", "eighth", eighthPath);

    return 0;
}
