#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <semaphore.h>
#include <stdexcept>
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
constexpr int kMainMask = kFull | kQuarter;
constexpr int kWorkerMask = kHalf | kEighth;
constexpr std::chrono::microseconds kSuperHopCadence { 5333 };

using Tracker = ModernPitchEngine::MultiRatePitchTracker;
using Candidate = Tracker::PitchCandidate;

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

    void add(double microseconds)
    {
        values.push_back(microseconds);
    }

    double percentile(double q) const
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
    {
        if ((mask & (1 << path)) != 0)
            output.candidate[static_cast<std::size_t>(path)]
                = analysePath(tracker, path, workspace);
    }
}

CandidateSet runSerial(Tracker& tracker,
                       Tracker::AnalysisWorkspace& workspace)
{
    CandidateSet output {};
    runMask(tracker, kSuperMask, workspace, output);
    return output;
}

class HotWorker
{
public:
    explicit HotWorker(Tracker& tracker)
        : tracker_(tracker), thread_([this] { run(); })
    {
    }

    ~HotWorker()
    {
        stop_.store(true, std::memory_order_release);
        request_.fetch_add(1, std::memory_order_release);
        thread_.join();
    }

    std::uint64_t submit() noexcept
    {
        const auto ticket = request_.load(std::memory_order_relaxed) + 1u;
        request_.store(ticket, std::memory_order_release);
        return ticket;
    }

    void wait(std::uint64_t ticket) noexcept
    {
        while (completed_.load(std::memory_order_acquire) < ticket)
            std::this_thread::yield();
    }

    CandidateSet result() const noexcept
    {
        return result_;
    }

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
            runMask(tracker_, kWorkerMask, workspace_, local);
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
    CandidateSet result_ {};
};

class ParkedWorker
{
public:
    explicit ParkedWorker(Tracker& tracker)
        : tracker_(tracker)
    {
        if (sem_init(&wake_, 0, 0) != 0)
            throw std::runtime_error("sem_init failed");

        thread_ = std::thread([this] { run(); });
    }

    ~ParkedWorker()
    {
        stop_.store(true, std::memory_order_release);
        sem_post(&wake_);
        thread_.join();
        sem_destroy(&wake_);
    }

    std::uint64_t submit() noexcept
    {
        const auto ticket = request_.load(std::memory_order_relaxed) + 1u;
        request_.store(ticket, std::memory_order_release);
        sem_post(&wake_);
        return ticket;
    }

    void wait(std::uint64_t ticket) noexcept
    {
        while (completed_.load(std::memory_order_acquire) < ticket)
            std::this_thread::yield();
    }

    CandidateSet result() const noexcept
    {
        return result_;
    }

private:
    void run()
    {
        while (true)
        {
            int waitResult = 0;
            do
            {
                waitResult = sem_wait(&wake_);
            }
            while (waitResult != 0 && errno == EINTR);

            if (stop_.load(std::memory_order_acquire))
                break;

            const auto ticket = request_.load(std::memory_order_acquire);
            CandidateSet local {};
            runMask(tracker_, kWorkerMask, workspace_, local);
            result_ = local;
            completed_.store(ticket, std::memory_order_release);
        }
    }

    Tracker& tracker_;
    Tracker::AnalysisWorkspace workspace_ {};
    sem_t wake_ {};
    std::thread thread_;
    std::atomic<bool> stop_ { false };
    std::atomic<std::uint64_t> request_ { 0 };
    std::atomic<std::uint64_t> completed_ { 0 };
    CandidateSet result_ {};
};

template <typename Worker>
CandidateSet runParallel(Tracker& tracker,
                         Worker& worker,
                         Tracker::AnalysisWorkspace& mainWorkspace)
{
    CandidateSet output {};
    const auto ticket = worker.submit();
    runMask(tracker, kMainMask, mainWorkspace, output);
    worker.wait(ticket);

    const auto workerOutput = worker.result();
    output.candidate[1] = workerOutput.candidate[1];
    output.candidate[3] = workerOutput.candidate[3];
    return output;
}

template <typename Worker>
bool verifyWorker(Tracker& tracker, const char* name)
{
    Worker worker(tracker);
    Tracker::AnalysisWorkspace serialWorkspace {};
    Tracker::AnalysisWorkspace mainWorkspace {};

    for (int iteration = 0; iteration < 300; ++iteration)
    {
        const auto serial = runSerial(tracker, serialWorkspace);
        const auto parallel = runParallel(tracker, worker, mainWorkspace);
        if (!sameSet(serial, parallel, kSuperMask))
        {
            std::cerr << "PARKED_WORKER_CENSUS=FAIL reason=bit_mismatch"
                      << " worker=" << name
                      << " iteration=" << iteration << '\n';
            return false;
        }
    }

    return true;
}

struct WorkerMetrics
{
    Distribution burst;
    Distribution cadenced;
    double cadenceWallMs = 0.0;
    double cadenceCpuMs = 0.0;
    double cadenceCpuRatio = 0.0;
};

template <typename Worker>
WorkerMetrics measureWorker(Tracker& tracker)
{
    Worker worker(tracker);
    Tracker::AnalysisWorkspace mainWorkspace {};
    WorkerMetrics metrics;
    metrics.burst.values.reserve(400);
    metrics.cadenced.values.reserve(400);

    // Warm the exact same path before measurements.
    for (int iteration = 0; iteration < 30; ++iteration)
        (void) runParallel(tracker, worker, mainWorkspace);

    for (int iteration = 0; iteration < 400; ++iteration)
    {
        const auto begin = std::chrono::steady_clock::now();
        (void) runParallel(tracker, worker, mainWorkspace);
        const auto end = std::chrono::steady_clock::now();
        metrics.burst.add(
            std::chrono::duration<double, std::micro>(end - begin).count());
    }

    constexpr int cadenceRepetitions = 400;
    auto nextDeadline = std::chrono::steady_clock::now();
    const auto wallBegin = nextDeadline;
    const std::clock_t cpuBegin = std::clock();

    for (int iteration = 0; iteration < cadenceRepetitions; ++iteration)
    {
        nextDeadline += kSuperHopCadence;
        std::this_thread::sleep_until(nextDeadline);

        const auto begin = std::chrono::steady_clock::now();
        (void) runParallel(tracker, worker, mainWorkspace);
        const auto end = std::chrono::steady_clock::now();

        metrics.cadenced.add(
            std::chrono::duration<double, std::micro>(end - begin).count());
    }

    const std::clock_t cpuEnd = std::clock();
    const auto wallEnd = std::chrono::steady_clock::now();
    metrics.cadenceWallMs =
        std::chrono::duration<double, std::milli>(wallEnd - wallBegin).count();
    metrics.cadenceCpuMs =
        1000.0 * static_cast<double>(cpuEnd - cpuBegin)
        / static_cast<double>(CLOCKS_PER_SEC);
    metrics.cadenceCpuRatio =
        metrics.cadenceCpuMs / std::max(1.0e-9, metrics.cadenceWallMs);

    return metrics;
}

void printMetrics(const char* workerName, const WorkerMetrics& metrics)
{
    std::cout << "PARKED_WORKER_METRIC"
              << " worker=" << workerName
              << " burst_p50_us=" << metrics.burst.percentile(0.50)
              << " burst_p95_us=" << metrics.burst.percentile(0.95)
              << " burst_p99_us=" << metrics.burst.percentile(0.99)
              << " burst_max_us=" << metrics.burst.maximum()
              << " cadence_p50_us=" << metrics.cadenced.percentile(0.50)
              << " cadence_p95_us=" << metrics.cadenced.percentile(0.95)
              << " cadence_p99_us=" << metrics.cadenced.percentile(0.99)
              << " cadence_max_us=" << metrics.cadenced.maximum()
              << " cadence_wall_ms=" << metrics.cadenceWallMs
              << " cadence_cpu_ms=" << metrics.cadenceCpuMs
              << " cadence_cpu_ratio=" << metrics.cadenceCpuRatio
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
        std::cerr << "PARKED_WORKER_CENSUS=FAIL reason=insufficient_warmup\n";
        return 2;
    }

    if (!verifyWorker<HotWorker>(tracker, "hot"))
        return 3;
    if (!verifyWorker<ParkedWorker>(tracker, "parked"))
        return 4;

    std::cout << "PARKED_WORKER_BIT_EQUIVALENCE=PASS repetitions=300\n";

    const bool parkedFirst = std::getenv("NEUMATON_PARKED_FIRST") != nullptr;

    WorkerMetrics hot;
    WorkerMetrics parked;

    if (parkedFirst)
    {
        parked = measureWorker<ParkedWorker>(tracker);
        hot = measureWorker<HotWorker>(tracker);
    }
    else
    {
        hot = measureWorker<HotWorker>(tracker);
        parked = measureWorker<ParkedWorker>(tracker);
    }

    std::cout << std::fixed << std::setprecision(3);
    printMetrics("hot", hot);
    printMetrics("parked", parked);

    const double p99Reduction =
        100.0 * (1.0 - parked.cadenced.percentile(0.99)
                         / std::max(1.0e-9, hot.cadenced.percentile(0.99)));
    const double cpuReduction =
        100.0 * (1.0 - parked.cadenceCpuRatio
                         / std::max(1.0e-9, hot.cadenceCpuRatio));

    std::cout << "PARKED_WORKER_COMPARISON"
              << " cadence_p99_reduction_pct=" << p99Reduction
              << " process_cpu_ratio_reduction_pct=" << cpuReduction
              << " hot_cpu_ratio=" << hot.cadenceCpuRatio
              << " parked_cpu_ratio=" << parked.cadenceCpuRatio
              << '\n';

    return 0;
}
