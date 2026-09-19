#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr int kFull = 1 << 0;
constexpr int kHalf = 1 << 1;
constexpr int kQuarter = 1 << 2;
constexpr int kEighth = 1 << 3;
constexpr int kSuperMask = kFull | kHalf | kQuarter | kEighth;
constexpr int kQuarterHopMask = kFull | kHalf | kQuarter;

struct SignalGenerator
{
    float next() noexcept
    {
        const double t = static_cast<double>(index) / kSampleRate;
        const double vibrato = 1.0 + 0.0035 * std::sin(2.0 * kPi * 5.1 * t);
        const double phase = 2.0 * kPi * 220.0 * vibrato * t;
        std::uint32_t x = noiseState;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
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

std::uint32_t bits(float value) noexcept
{
    std::uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

using Tracker = ModernPitchEngine::MultiRatePitchTracker;
using Candidate = Tracker::PitchCandidate;

bool sameCandidate(const Candidate& a, const Candidate& b) noexcept
{
    return bits(a.frequencyHz) == bits(b.frequencyHz)
        && bits(a.confidence) == bits(b.confidence)
        && bits(a.periodicity) == bits(b.periodicity)
        && bits(a.harmonicFamily) == bits(b.harmonicFamily)
        && bits(a.aperiodicity) == bits(b.aperiodicity)
        && bits(a.tonalCleanliness) == bits(b.tonalCleanliness)
        && a.pathIndex == b.pathIndex
        && a.ageInHops == b.ageInHops
        && a.valid == b.valid;
}

struct CandidateSet
{
    std::array<Candidate, 4> candidate {};
};

bool sameSet(const CandidateSet& a, const CandidateSet& b, int mask) noexcept
{
    for (int path = 0; path < 4; ++path)
    {
        if ((mask & (1 << path)) != 0
            && !sameCandidate(a.candidate[static_cast<std::size_t>(path)],
                              b.candidate[static_cast<std::size_t>(path)]))
            return false;
    }
    return true;
}

struct Distribution
{
    std::vector<double> values;
    void add(double us) { values.push_back(us); }

    double percentile(double q) const
    {
        if (values.empty()) return 0.0;
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const double p = std::clamp(q, 0.0, 1.0)
                       * static_cast<double>(sorted.size() - 1);
        const auto lo = static_cast<std::size_t>(std::floor(p));
        const auto hi = static_cast<std::size_t>(std::ceil(p));
        const double f = p - static_cast<double>(lo);
        return sorted[lo] + (sorted[hi] - sorted[lo]) * f;
    }

    double maximum() const
    {
        return values.empty() ? 0.0
                              : *std::max_element(values.begin(), values.end());
    }
};

Candidate analysePath(Tracker& tracker,
                      int path,
                      Tracker::AnalysisWorkspace& workspace)
{
    Candidate candidate;
    switch (path)
    {
        case 0:
            candidate = tracker.analyse(
                tracker.fullRateRing_,
                tracker.fullRateWritePosition_,
                tracker.fullRateAvailableSamples_,
                kSampleRate,
                std::max(160.0f, tracker.minimumPitchHz_),
                std::min(tracker.maximumPitchHz_, 2600.0f),
                Tracker::standardAnalysisSize,
                workspace);
            break;
        case 1:
            candidate = tracker.analyse(
                tracker.halfRateRing_,
                tracker.halfRateWritePosition_,
                tracker.halfRateAvailableSamples_,
                kSampleRate * 0.5,
                std::max(78.0f, tracker.minimumPitchHz_),
                std::min(tracker.maximumPitchHz_, 900.0f),
                Tracker::standardAnalysisSize,
                workspace);
            break;
        case 2:
            candidate = tracker.analyse(
                tracker.quarterRateRing_,
                tracker.quarterRateWritePosition_,
                tracker.quarterRateAvailableSamples_,
                kSampleRate * 0.25,
                std::max(35.0f, tracker.minimumPitchHz_),
                std::min(tracker.maximumPitchHz_, 460.0f),
                384,
                workspace);
            break;
        default:
            candidate = tracker.analyse(
                tracker.eighthRateRing_,
                tracker.eighthRateWritePosition_,
                tracker.eighthRateAvailableSamples_,
                kSampleRate * 0.125,
                std::max(25.0f, tracker.minimumPitchHz_),
                std::min(tracker.maximumPitchHz_, 230.0f),
                Tracker::maxAnalysisSize,
                workspace);
            break;
    }
    candidate.pathIndex = path;
    candidate.ageInHops = 0;
    return candidate;
}

void runMask(Tracker& tracker,
             int mask,
             Tracker::AnalysisWorkspace& workspace,
             CandidateSet& output)
{
    for (int path = 0; path < 4; ++path)
        if ((mask & (1 << path)) != 0)
            output.candidate[static_cast<std::size_t>(path)]
                = analysePath(tracker, path, workspace);
}

class Worker
{
public:
    explicit Worker(Tracker& tracker)
        : tracker_(tracker), thread_([this] { run(); })
    {
    }

    ~Worker()
    {
        stop_.store(true, std::memory_order_release);
        request_.fetch_add(1, std::memory_order_release);
        thread_.join();
    }

    std::uint64_t submit(int mask) noexcept
    {
        mask_ = mask;
        const auto ticket = request_.load(std::memory_order_relaxed) + 1u;
        request_.store(ticket, std::memory_order_release);
        return ticket;
    }

    void wait(std::uint64_t ticket) noexcept
    {
        while (completed_.load(std::memory_order_acquire) < ticket)
            std::this_thread::yield();
    }

    CandidateSet result() const noexcept { return result_; }

private:
    void run()
    {
        std::uint64_t seen = 0;
        while (!stop_.load(std::memory_order_acquire))
        {
            const auto request = request_.load(std::memory_order_acquire);
            if (request == seen)
            {
                std::this_thread::yield();
                continue;
            }
            seen = request;
            if (stop_.load(std::memory_order_acquire))
                break;

            CandidateSet local {};
            runMask(tracker_, mask_, workspace_, local);
            result_ = local;
            completed_.store(seen, std::memory_order_release);
        }
    }

    Tracker& tracker_;
    Tracker::AnalysisWorkspace workspace_ {};
    std::thread thread_;
    std::atomic<bool> stop_ { false };
    std::atomic<std::uint64_t> request_ { 0 };
    std::atomic<std::uint64_t> completed_ { 0 };
    int mask_ = 0;
    CandidateSet result_ {};
};

CandidateSet runSerial(Tracker& tracker,
                       int dueMask,
                       Tracker::AnalysisWorkspace& workspace)
{
    CandidateSet output {};
    runMask(tracker, dueMask, workspace, output);
    return output;
}

CandidateSet runParallel(Tracker& tracker,
                         Worker& worker,
                         int dueMask,
                         int workerMask,
                         Tracker::AnalysisWorkspace& mainWorkspace)
{
    CandidateSet output {};
    const int effectiveWorkerMask = workerMask & dueMask;
    const int mainMask = dueMask & ~effectiveWorkerMask;
    const auto ticket = worker.submit(effectiveWorkerMask);
    runMask(tracker, mainMask, mainWorkspace, output);
    worker.wait(ticket);
    const auto workerOutput = worker.result();
    for (int path = 0; path < 4; ++path)
        if ((effectiveWorkerMask & (1 << path)) != 0)
            output.candidate[static_cast<std::size_t>(path)]
                = workerOutput.candidate[static_cast<std::size_t>(path)];
    return output;
}

struct Strategy
{
    const char* name = "";
    int dueMask = 0;
    int workerMask = 0;
    Distribution timings;
};

void printStrategy(const Strategy& strategy,
                   const Distribution& serial)
{
    const double s50 = serial.percentile(0.50);
    const double s95 = serial.percentile(0.95);
    const double s99 = serial.percentile(0.99);
    const double p50 = strategy.timings.percentile(0.50);
    const double p95 = strategy.timings.percentile(0.95);
    const double p99 = strategy.timings.percentile(0.99);
    const double pmax = strategy.timings.maximum();

    std::cout << "PARTITION_CENSUS"
              << " name=" << strategy.name
              << " serial_p50_us=" << s50
              << " parallel_p50_us=" << p50
              << " p50_reduction_pct=" << (100.0 * (1.0 - p50 / std::max(1.0e-9, s50)))
              << " serial_p95_us=" << s95
              << " parallel_p95_us=" << p95
              << " p95_reduction_pct=" << (100.0 * (1.0 - p95 / std::max(1.0e-9, s95)))
              << " serial_p99_us=" << s99
              << " parallel_p99_us=" << p99
              << " p99_reduction_pct=" << (100.0 * (1.0 - p99 / std::max(1.0e-9, s99)))
              << " parallel_max_us=" << pmax
              << '\n';
}

} // namespace

int main()
{
    Tracker tracker;
    tracker.prepare(kSampleRate);
    tracker.setRange(45.0f, 1600.0f);
    tracker.setSensitivity(0.70f);
    tracker.setVoiceAuthorityContext(true, 0.88f, 0.10f, 0.90f,
                                     0.88f, 0.08f, 0.90f, 0.06f);

    SignalGenerator generator;
    ModernPitchEngine::PitchObservation observation;
    for (int sample = 0; sample < static_cast<int>(kSampleRate); ++sample)
        tracker.processSample(generator.next(), observation);

    if (tracker.eighthRateAvailableSamples_ < Tracker::maxAnalysisSize)
    {
        std::cerr << "PARTITION_CENSUS=FAIL reason=insufficient_warmup\n";
        return 2;
    }

    Worker worker(tracker);
    Tracker::AnalysisWorkspace serialWorkspace {};
    Tracker::AnalysisWorkspace mainWorkspace {};

    std::array<Strategy, 4> strategies {{
        { "quarter_main_full_half_worker_quarter", kQuarterHopMask, kQuarter, {} },
        { "super_main_full_half_worker_quarter_eighth", kSuperMask, kQuarter | kEighth, {} },
        { "super_main_full_quarter_worker_half_eighth", kSuperMask, kHalf | kEighth, {} },
        { "super_main_full_eighth_worker_half_quarter", kSuperMask, kHalf | kQuarter, {} }
    }};

    for (auto& strategy : strategies)
        strategy.timings.values.reserve(300);

    Distribution quarterSerial;
    Distribution superSerial;
    quarterSerial.values.reserve(300);
    superSerial.values.reserve(300);

    // Functional warmup and bit-equivalence.
    for (int i = 0; i < 30; ++i)
    {
        for (auto& strategy : strategies)
        {
            const auto serial = runSerial(tracker, strategy.dueMask, serialWorkspace);
            const auto parallel = runParallel(tracker, worker, strategy.dueMask,
                                              strategy.workerMask, mainWorkspace);
            if (!sameSet(serial, parallel, strategy.dueMask))
            {
                std::cerr << "PARTITION_CENSUS=FAIL reason=warmup_bit_mismatch"
                          << " strategy=" << strategy.name
                          << " iteration=" << i << '\n';
                return 3;
            }
        }
    }

    constexpr int repetitions = 300;
    for (int i = 0; i < repetitions; ++i)
    {
        {
            const auto begin = std::chrono::steady_clock::now();
            const auto result = runSerial(tracker, kQuarterHopMask, serialWorkspace);
            const auto end = std::chrono::steady_clock::now();
            quarterSerial.add(std::chrono::duration<double, std::micro>(end - begin).count());
            (void) result;
        }
        {
            const auto begin = std::chrono::steady_clock::now();
            const auto result = runSerial(tracker, kSuperMask, serialWorkspace);
            const auto end = std::chrono::steady_clock::now();
            superSerial.add(std::chrono::duration<double, std::micro>(end - begin).count());
            (void) result;
        }

        const int rotation = i % static_cast<int>(strategies.size());
        for (int offset = 0; offset < static_cast<int>(strategies.size()); ++offset)
        {
            auto& strategy = strategies[static_cast<std::size_t>(
                (rotation + offset) % static_cast<int>(strategies.size()))];

            const auto reference = runSerial(tracker, strategy.dueMask, serialWorkspace);
            const auto begin = std::chrono::steady_clock::now();
            const auto parallel = runParallel(tracker, worker, strategy.dueMask,
                                              strategy.workerMask, mainWorkspace);
            const auto end = std::chrono::steady_clock::now();

            strategy.timings.add(
                std::chrono::duration<double, std::micro>(end - begin).count());

            if (!sameSet(reference, parallel, strategy.dueMask))
            {
                std::cerr << "PARTITION_CENSUS=FAIL reason=bit_mismatch"
                          << " strategy=" << strategy.name
                          << " iteration=" << i << '\n';
                return 4;
            }
        }
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "PARTITION_CENSUS_BIT_EQUIVALENCE=PASS repetitions="
              << repetitions << '\n';

    for (const auto& strategy : strategies)
        printStrategy(strategy,
                      strategy.dueMask == kQuarterHopMask ? quarterSerial : superSerial);

    return 0;
}
