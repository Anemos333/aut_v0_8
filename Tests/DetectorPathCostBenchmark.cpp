#include <JuceHeader.h>
#include "../Source/Tempo.h"
#include "../Source/SingleWetSpectralRenderer.h"

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string_view>

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
        const float noise = static_cast<float>(
            (static_cast<int>(x & 0xffffu) - 32768) / 32768.0) * 0.004f;
        ++index;
        return static_cast<float>(
            0.18 * std::sin(phase)
          + 0.11 * std::sin(2.0 * phase + 0.18)
          + 0.065 * std::sin(3.0 * phase - 0.23)
          + 0.035 * std::sin(4.0 * phase + 0.51)) + noise;
    }

    std::uint64_t index = 0;
    std::uint32_t noiseState = 0x51f15e5du;
};

template <typename Fn>
double benchmarkCalls(int iterations, Fn&& fn)
{
    volatile float sink = 0.0f;
    for (int i = 0; i < 4; ++i)
        sink += fn().frequencyHz * 1.0e-9f;

    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
        sink += fn().frequencyHz * 1.0e-9f;
    const auto end = std::chrono::steady_clock::now();
    static_cast<void>(sink);
    return std::chrono::duration<double, std::milli>(end - begin).count()
         / static_cast<double>(iterations);
}
}

int main()
{
    using Tracker = ModernPitchEngine::MultiRatePitchTracker;
    Tracker tracker;
    tracker.prepare(kSampleRate);
    tracker.setRange(45.0f, 1600.0f);
    tracker.setSensitivity(0.70f);
    tracker.setVoiceAuthorityContext(
        true, 0.88f, 0.10f, 0.90f, 0.88f, 0.08f, 0.90f, 0.06f);

    ModernPitchEngine::PitchObservation observation;
    SignalGenerator generator;
    for (int i = 0; i < static_cast<int>(kSampleRate); ++i)
        tracker.processSample(generator.next(), observation);

    Tracker::AnalysisWorkspace fullWorkspace {};
    Tracker::AnalysisWorkspace halfWorkspace {};
    Tracker::AnalysisWorkspace quarterWorkspace {};
    Tracker::AnalysisWorkspace eighthWorkspace {};

    constexpr int iterations = 120;

    const double fullMs = benchmarkCalls(iterations, [&]
    {
        return tracker.analyse(
            tracker.fullRateRing_,
            tracker.fullRateWritePosition_,
            tracker.fullRateAvailableSamples_,
            kSampleRate,
            std::max(160.0f, tracker.minimumPitchHz_),
            std::min(tracker.maximumPitchHz_, 2600.0f),
            Tracker::standardAnalysisSize,
            fullWorkspace);
    });

    const double halfMs = benchmarkCalls(iterations, [&]
    {
        return tracker.analyse(
            tracker.halfRateRing_,
            tracker.halfRateWritePosition_,
            tracker.halfRateAvailableSamples_,
            kSampleRate * 0.5,
            std::max(78.0f, tracker.minimumPitchHz_),
            std::min(tracker.maximumPitchHz_, 900.0f),
            Tracker::standardAnalysisSize,
            halfWorkspace);
    });

    const double quarterMs = benchmarkCalls(iterations, [&]
    {
        return tracker.analyse(
            tracker.quarterRateRing_,
            tracker.quarterRateWritePosition_,
            tracker.quarterRateAvailableSamples_,
            kSampleRate * 0.25,
            std::max(35.0f, tracker.minimumPitchHz_),
            std::min(tracker.maximumPitchHz_, 460.0f),
            384,
            quarterWorkspace);
    });

    const double eighthMs = benchmarkCalls(iterations, [&]
    {
        return tracker.analyse(
            tracker.eighthRateRing_,
            tracker.eighthRateWritePosition_,
            tracker.eighthRateAvailableSamples_,
            kSampleRate * 0.125,
            std::max(25.0f, tracker.minimumPitchHz_),
            std::min(tracker.maximumPitchHz_, 230.0f),
            Tracker::maxAnalysisSize,
            eighthWorkspace);
    });

    tracker.fullRateCandidate_.candidate = tracker.analyse(
        tracker.fullRateRing_, tracker.fullRateWritePosition_,
        tracker.fullRateAvailableSamples_, kSampleRate,
        std::max(160.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 2600.0f),
        Tracker::standardAnalysisSize, fullWorkspace);
    tracker.fullRateCandidate_.candidate.pathIndex = 0;
    tracker.fullRateCandidate_.ageInHops = 0;

    tracker.halfRateCandidate_.candidate = tracker.analyse(
        tracker.halfRateRing_, tracker.halfRateWritePosition_,
        tracker.halfRateAvailableSamples_, kSampleRate * 0.5,
        std::max(78.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 900.0f),
        Tracker::standardAnalysisSize, halfWorkspace);
    tracker.halfRateCandidate_.candidate.pathIndex = 1;
    tracker.halfRateCandidate_.ageInHops = 0;

    tracker.quarterRateCandidate_.candidate = tracker.analyse(
        tracker.quarterRateRing_, tracker.quarterRateWritePosition_,
        tracker.quarterRateAvailableSamples_, kSampleRate * 0.25,
        std::max(35.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 460.0f),
        384, quarterWorkspace);
    tracker.quarterRateCandidate_.candidate.pathIndex = 2;
    tracker.quarterRateCandidate_.ageInHops = 0;

    tracker.eighthRateCandidate_.candidate = tracker.analyse(
        tracker.eighthRateRing_, tracker.eighthRateWritePosition_,
        tracker.eighthRateAvailableSamples_, kSampleRate * 0.125,
        std::max(25.0f, tracker.minimumPitchHz_),
        std::min(tracker.maximumPitchHz_, 230.0f),
        Tracker::maxAnalysisSize, eighthWorkspace);
    tracker.eighthRateCandidate_.candidate.pathIndex = 3;
    tracker.eighthRateCandidate_.ageInHops = 0;

    volatile float decodeSink = 0.0f;
    constexpr int decodeIterations = 10000;
    const auto decodeBegin = std::chrono::steady_clock::now();
    for (int i = 0; i < decodeIterations; ++i)
    {
        const auto decision = tracker.decodeCandidate(false);
        decodeSink += decision.candidate.frequencyHz * 1.0e-9f;
    }
    const auto decodeEnd = std::chrono::steady_clock::now();
    static_cast<void>(decodeSink);
    const double decodeMs =
        std::chrono::duration<double, std::milli>(decodeEnd - decodeBegin).count()
        / static_cast<double>(decodeIterations);

    const double weightedAnalyseMsPerHop =
          fullMs
        + halfMs * 0.5
        + quarterMs * 0.25
        + eighthMs * 0.125;
    const double weightedTotalMsPerHop = weightedAnalyseMsPerHop + decodeMs;

    std::cout << "DETECTOR_PATH_COST"
              << " full_ms=" << fullMs
              << " half_ms=" << halfMs
              << " quarter_ms=" << quarterMs
              << " eighth_ms=" << eighthMs
              << " decode_ms=" << decodeMs
              << " weighted_analyse_ms_per_hop=" << weightedAnalyseMsPerHop
              << " weighted_total_ms_per_hop=" << weightedTotalMsPerHop
              << " full_share=" << fullMs / weightedTotalMsPerHop
              << " half_share=" << (halfMs * 0.5) / weightedTotalMsPerHop
              << " quarter_share=" << (quarterMs * 0.25) / weightedTotalMsPerHop
              << " eighth_share=" << (eighthMs * 0.125) / weightedTotalMsPerHop
              << " decode_share=" << decodeMs / weightedTotalMsPerHop
              << '\n';
    return 0;
}
