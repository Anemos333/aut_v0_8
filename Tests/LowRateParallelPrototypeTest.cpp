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

bool sameCandidate(const ModernPitchEngine::MultiRatePitchTracker::PitchCandidate& a,
                   const ModernPitchEngine::MultiRatePitchTracker::PitchCandidate& b) noexcept
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

struct Distribution
{
    std::vector<double> values;

    void add(double microseconds) { values.push_back(microseconds); }

    double percentile(double q) const
    {
        if (values.empty()) return 0.0;
        auto sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const double position = std::clamp(q, 0.0, 1.0)
                              * static_cast<double>(sorted.size() - 1);
        const auto lo = static_cast<std::size_t>(std::floor(position));
        const auto hi = static_cast<std::size_t>(std::ceil(position));
        const double f = position - static_cast<double>(lo);
        return sorted[lo] + (sorted[hi] - sorted[lo]) * f;
    }
};

struct CandidateSet
{
    ModernPitchEngine::MultiRatePitchTracker::PitchCandidate full;
    ModernPitchEngine::MultiRatePitchTracker::PitchCandidate half;
    ModernPitchEngine::MultiRatePitchTracker::PitchCandidate quarter;
    ModernPitchEngine::MultiRatePitchTracker::PitchCandidate eighth;
};

class LowRatePrototypeWorker
{
public:
    explicit LowRatePrototypeWorker(ModernPitchEngine::MultiRatePitchTracker& tracker)
        : tracker_(tracker), thread_([this] { run(); })
    {
    }

    ~LowRatePrototypeWorker()
    {
        stop_.store(true, std::memory_order_release);
        request_.fetch_add(1, std::memory_order_release);
        thread_.join();
    }

    std::uint64_t submit(bool doQuarter, bool doEighth) noexcept
    {
        doQuarter_ = doQuarter;
        doEighth_ = doEighth;
        const auto ticket = request_.load(std::memory_order_relaxed) + 1u;
        request_.store(ticket, std::memory_order_release);
        return ticket;
    }

    void wait(std::uint64_t ticket) noexcept
    {
        while (completed_.load(std::memory_order_acquire) < ticket)
            std::this_thread::yield();
    }

    ModernPitchEngine::MultiRatePitchTracker::PitchCandidate quarter() const noexcept
    {
        return quarter_;
    }

    ModernPitchEngine::MultiRatePitchTracker::PitchCandidate eighth() const noexcept
    {
        return eighth_;
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

            if (doQuarter_)
            {
                quarter_ = tracker_.analyse(
                    tracker_.quarterRateRing_,
                    tracker_.quarterRateWritePosition_,
                    tracker_.quarterRateAvailableSamples_,
                    kSampleRate * 0.25,
                    std::max(35.0f, tracker_.minimumPitchHz_),
                    std::min(tracker_.maximumPitchHz_, 460.0f),
                    384,
                    workspace_);
                quarter_.pathIndex = 2;
                quarter_.ageInHops = 0;
            }

            if (doEighth_)
            {
                eighth_ = tracker_.analyse(
                    tracker_.eighthRateRing_,
                    tracker_.eighthRateWritePosition_,
                    tracker_.eighthRateAvailableSamples_,
                    kSampleRate * 0.125,
                    std::max(25.0f, tracker_.minimumPitchHz_),
                    std::min(tracker_.maximumPitchHz_, 230.0f),
                    ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize,
                    workspace_);
                eighth_.pathIndex = 3;
                eighth_.ageInHops = 0;
            }

            completed_.store(seen, std::memory_order_release);
        }
    }

    ModernPitchEngine::MultiRatePitchTracker& tracker_;
    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace workspace_ {};
    std::thread thread_;
    std::atomic<bool> stop_ { false };
    std::atomic<std::uint64_t> request_ { 0 };
    std::atomic<std::uint64_t> completed_ { 0 };

    bool doQuarter_ = false;
    bool doEighth_ = false;
    ModernPitchEngine::MultiRatePitchTracker::PitchCandidate quarter_ {};
    ModernPitchEngine::MultiRatePitchTracker::PitchCandidate eighth_ {};
};

CandidateSet runSerial(ModernPitchEngine::MultiRatePitchTracker& tracker,
                       bool doEighth,
                       ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace& workspace)
{
    CandidateSet out;

    out.full = tracker.analyse(
        tracker.fullRateRing_,
        tracker.fullRateWritePosition_,
        tracker.fullRateAvailableSamples_,
        kSampleRate,
        std::max(160.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 2600.0f),
        ModernPitchEngine::MultiRatePitchTracker::standardAnalysisSize,
        workspace);
    out.full.pathIndex = 0;
    out.full.ageInHops = 0;

    out.half = tracker.analyse(
        tracker.halfRateRing_,
        tracker.halfRateWritePosition_,
        tracker.halfRateAvailableSamples_,
        kSampleRate * 0.5,
        std::max(78.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 900.0f),
        ModernPitchEngine::MultiRatePitchTracker::standardAnalysisSize,
        workspace);
    out.half.pathIndex = 1;
    out.half.ageInHops = 0;

    out.quarter = tracker.analyse(
        tracker.quarterRateRing_,
        tracker.quarterRateWritePosition_,
        tracker.quarterRateAvailableSamples_,
        kSampleRate * 0.25,
        std::max(35.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 460.0f),
        384,
        workspace);
    out.quarter.pathIndex = 2;
    out.quarter.ageInHops = 0;

    if (doEighth)
    {
        out.eighth = tracker.analyse(
            tracker.eighthRateRing_,
            tracker.eighthRateWritePosition_,
            tracker.eighthRateAvailableSamples_,
            kSampleRate * 0.125,
            std::max(25.0f, tracker.minimumPitchHz_),
            std::min(tracker.maximumPitchHz_, 230.0f),
            ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize,
            workspace);
        out.eighth.pathIndex = 3;
        out.eighth.ageInHops = 0;
    }

    return out;
}

CandidateSet runParallel(ModernPitchEngine::MultiRatePitchTracker& tracker,
                         LowRatePrototypeWorker& worker,
                         bool doEighth,
                         ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace& mainWorkspace)
{
    CandidateSet out;
    const auto ticket = worker.submit(true, doEighth);

    out.full = tracker.analyse(
        tracker.fullRateRing_,
        tracker.fullRateWritePosition_,
        tracker.fullRateAvailableSamples_,
        kSampleRate,
        std::max(160.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 2600.0f),
        ModernPitchEngine::MultiRatePitchTracker::standardAnalysisSize,
        mainWorkspace);
    out.full.pathIndex = 0;
    out.full.ageInHops = 0;

    out.half = tracker.analyse(
        tracker.halfRateRing_,
        tracker.halfRateWritePosition_,
        tracker.halfRateAvailableSamples_,
        kSampleRate * 0.5,
        std::max(78.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 900.0f),
        ModernPitchEngine::MultiRatePitchTracker::standardAnalysisSize,
        mainWorkspace);
    out.half.pathIndex = 1;
    out.half.ageInHops = 0;

    worker.wait(ticket);
    out.quarter = worker.quarter();
    if (doEighth)
        out.eighth = worker.eighth();
    return out;
}

bool sameSet(const CandidateSet& a, const CandidateSet& b, bool doEighth) noexcept
{
    return sameCandidate(a.full, b.full)
        && sameCandidate(a.half, b.half)
        && sameCandidate(a.quarter, b.quarter)
        && (!doEighth || sameCandidate(a.eighth, b.eighth));
}

void printResult(const char* name,
                 const Distribution& serial,
                 const Distribution& parallel)
{
    const double s50 = serial.percentile(0.50);
    const double s95 = serial.percentile(0.95);
    const double s99 = serial.percentile(0.99);
    const double p50 = parallel.percentile(0.50);
    const double p95 = parallel.percentile(0.95);
    const double p99 = parallel.percentile(0.99);

    std::cout << "PARALLEL_PROTOTYPE"
              << " name=" << name
              << " serial_p50_us=" << s50
              << " parallel_p50_us=" << p50
              << " p50_reduction_pct=" << (100.0 * (1.0 - p50 / std::max(1.0e-9, s50)))
              << " serial_p95_us=" << s95
              << " parallel_p95_us=" << p95
              << " p95_reduction_pct=" << (100.0 * (1.0 - p95 / std::max(1.0e-9, s95)))
              << " serial_p99_us=" << s99
              << " parallel_p99_us=" << p99
              << " p99_reduction_pct=" << (100.0 * (1.0 - p99 / std::max(1.0e-9, s99)))
              << '\n';
}

} // namespace

int main()
{
    ModernPitchEngine::MultiRatePitchTracker tracker;
    tracker.prepare(kSampleRate);
    tracker.setRange(45.0f, 1600.0f);
    tracker.setSensitivity(0.70f);
    tracker.setVoiceAuthorityContext(true, 0.88f, 0.10f, 0.90f,
                                     0.88f, 0.08f, 0.90f, 0.06f);

    SignalGenerator generator;
    ModernPitchEngine::PitchObservation observation;
    for (int sample = 0; sample < static_cast<int>(kSampleRate); ++sample)
        tracker.processSample(generator.next(), observation);

    if (tracker.eighthRateAvailableSamples_
        < ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize)
    {
        std::cerr << "PARALLEL_PROTOTYPE=FAIL reason=insufficient_warmup\n";
        return 2;
    }

    LowRatePrototypeWorker worker(tracker);
    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace serialWorkspace {};
    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace mainWorkspace {};

    // Warm caches and worker.
    for (int i = 0; i < 20; ++i)
    {
        const auto serial = runSerial(tracker, true, serialWorkspace);
        const auto parallel = runParallel(tracker, worker, true, mainWorkspace);
        if (!sameSet(serial, parallel, true))
        {
            std::cerr << "PARALLEL_PROTOTYPE=FAIL reason=warmup_bit_mismatch iteration="
                      << i << '\n';
            return 3;
        }
    }

    constexpr int repetitions = 300;
    Distribution quarterSerial, quarterParallel, superSerial, superParallel;
    quarterSerial.values.reserve(repetitions);
    quarterParallel.values.reserve(repetitions);
    superSerial.values.reserve(repetitions);
    superParallel.values.reserve(repetitions);

    for (int i = 0; i < repetitions; ++i)
    {
        const bool parallelFirst = (i & 1) != 0;

        auto measureSerial = [&](bool doEighth, Distribution& distribution)
        {
            const auto begin = std::chrono::steady_clock::now();
            const auto result = runSerial(tracker, doEighth, serialWorkspace);
            const auto end = std::chrono::steady_clock::now();
            distribution.add(std::chrono::duration<double, std::micro>(end - begin).count());
            return result;
        };

        auto measureParallel = [&](bool doEighth, Distribution& distribution)
        {
            const auto begin = std::chrono::steady_clock::now();
            const auto result = runParallel(tracker, worker, doEighth, mainWorkspace);
            const auto end = std::chrono::steady_clock::now();
            distribution.add(std::chrono::duration<double, std::micro>(end - begin).count());
            return result;
        };

        CandidateSet qs, qp, ss, sp;
        if (!parallelFirst)
        {
            qs = measureSerial(false, quarterSerial);
            qp = measureParallel(false, quarterParallel);
            ss = measureSerial(true, superSerial);
            sp = measureParallel(true, superParallel);
        }
        else
        {
            qp = measureParallel(false, quarterParallel);
            qs = measureSerial(false, quarterSerial);
            sp = measureParallel(true, superParallel);
            ss = measureSerial(true, superSerial);
        }

        if (!sameSet(qs, qp, false))
        {
            std::cerr << "PARALLEL_PROTOTYPE=FAIL reason=quarter_bit_mismatch iteration="
                      << i << '\n';
            return 4;
        }
        if (!sameSet(ss, sp, true))
        {
            std::cerr << "PARALLEL_PROTOTYPE=FAIL reason=super_bit_mismatch iteration="
                      << i << '\n';
            return 5;
        }
    }

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "PARALLEL_PROTOTYPE_BIT_EQUIVALENCE=PASS repetitions="
              << repetitions << '\n';
    printResult("quarter_hop", quarterSerial, quarterParallel);
    printResult("super_hop", superSerial, superParallel);

    return 0;
}
