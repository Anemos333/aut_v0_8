#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kEpsilonCents = 1.5;
constexpr double kAcquireLimitMs = 10.0;

struct Rng
{
    std::uint32_t state = 0x51f15e5du;
    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state & 0xffffu) / 32767.5f - 1.0f;
    }
};

double centsError(double measured, double target)
{
    if (!(measured > 0.0) || !(target > 0.0))
        return std::numeric_limits<double>::infinity();
    return 1200.0 * std::log2(measured / target);
}

struct Metrics
{
    int periodicHopsAfterGrace = 0;
    int periodicMeasuredAfterGrace = 0;
    int periodicWrong = 0;
    int holesAfterFirstCorrect = 0;
    int familyErrors = 0;
    int nonPeriodicHops = 0;
    int nonPeriodicHallucinations = 0;
    double maxAbsCents = 0.0;
    std::array<double, 3> acquisitionMs { -1.0, -1.0, -1.0 };
    std::array<bool, 3> acquired { false, false, false };
};

float vocalSample(double phase) noexcept
{
    return static_cast<float>(
          0.72 * std::sin(phase)
        + 0.34 * std::sin(2.0 * phase + 0.17)
        + 0.21 * std::sin(3.0 * phase - 0.31)
        + 0.13 * std::sin(4.0 * phase + 0.49)
        + 0.08 * std::sin(5.0 * phase - 0.63));
}

void observe(Metrics& m,
             const ModernPitchEngine::PitchObservation& o,
             bool periodicExpected,
             bool nonPeriodicExpected,
             double targetHz,
             int sampleIndex,
             int episodeStart,
             int episodeIndex)
{
    const bool measured =
        o.measurementAvailable
        && std::isfinite(o.correctionFrequencyHz)
        && o.correctionFrequencyHz > 0.0f;

    if (periodicExpected)
    {
        const double elapsedMs =
            1000.0 * static_cast<double>(sampleIndex - episodeStart)
            / kSampleRate;

        if (measured)
        {
            const double error = centsError(o.correctionFrequencyHz, targetHz);
            const double absError = std::abs(error);
            m.maxAbsCents = std::max(m.maxAbsCents, absError);

            if (absError <= kEpsilonCents)
            {
                if (episodeIndex >= 0
                    && episodeIndex < static_cast<int>(m.acquisitionMs.size()))
                {
                    const auto episode = static_cast<std::size_t>(episodeIndex);
                    if (m.acquisitionMs[episode] < 0.0)
                        m.acquisitionMs[episode] = elapsedMs;
                    m.acquired[episode] = true;
                }
            }
            else
            {
                ++m.periodicWrong;
                const double ratio = o.correctionFrequencyHz / targetHz;
                if (std::abs(1200.0 * std::log2(ratio / 2.0)) <= 35.0
                    || std::abs(1200.0 * std::log2(ratio * 2.0)) <= 35.0
                    || std::abs(1200.0 * std::log2(ratio / 3.0)) <= 35.0
                    || std::abs(1200.0 * std::log2(ratio * 3.0)) <= 35.0
                    || std::abs(1200.0 * std::log2(ratio / 4.0)) <= 35.0
                    || std::abs(1200.0 * std::log2(ratio * 4.0)) <= 35.0)
                {
                    ++m.familyErrors;
                }
            }
        }

        if (!measured
            && episodeIndex >= 0
            && episodeIndex < static_cast<int>(m.acquired.size())
            && m.acquired[static_cast<std::size_t>(episodeIndex)])
        {
            ++m.holesAfterFirstCorrect;
        }

        if (elapsedMs >= kAcquireLimitMs)
        {
            ++m.periodicHopsAfterGrace;
            if (measured)
                ++m.periodicMeasuredAfterGrace;
        }
    }

    if (nonPeriodicExpected)
    {
        ++m.nonPeriodicHops;
        if (measured)
            ++m.nonPeriodicHallucinations;
    }
}

Metrics runCase(double targetHz, double snrDb, std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker tracker;
    tracker.prepare(kSampleRate);
    tracker.setRange(55.0f, 1600.0f);
    tracker.setSensitivity(0.70f);

    Metrics metrics;
    Rng rng { seed };
    double phase = 0.0;

    const double toneAmp = std::pow(10.0, -24.0 / 20.0);
    const double noiseAmp = toneAmp / std::pow(10.0, snrDb / 20.0);

    enum class Segment
    {
        silence,
        tone,
        gap,
        resumedTone,
        toneWithBurst,
        whiteNoise,
        finalTone
    };

    struct SegmentSpec
    {
        Segment kind;
        double milliseconds;
    };

    const std::array<SegmentSpec, 7> plan {{
        { Segment::silence,       20.0 },
        { Segment::tone,         180.0 },
        { Segment::gap,            4.0 },
        { Segment::resumedTone,  120.0 },
        { Segment::toneWithBurst, 40.0 },
        { Segment::whiteNoise,    30.0 },
        { Segment::finalTone,    120.0 }
    }};

    int absoluteSample = 0;
    int currentEpisode = -1;
    int episodeStart = -1;

    for (const auto& segment : plan)
    {
        const int count = static_cast<int>(std::lround(
            segment.milliseconds * 0.001 * kSampleRate));

        const bool beginsPeriodicEpisode =
            segment.kind == Segment::tone
            || segment.kind == Segment::resumedTone
            || segment.kind == Segment::finalTone;

        if (beginsPeriodicEpisode)
        {
            ++currentEpisode;
            episodeStart = absoluteSample;
        }

        for (int i = 0; i < count; ++i, ++absoluteSample)
        {
            float sample = 0.0f;
            bool periodicExpected = false;
            bool nonPeriodicExpected = false;

            const bool isTone =
                segment.kind == Segment::tone
                || segment.kind == Segment::resumedTone
                || segment.kind == Segment::toneWithBurst
                || segment.kind == Segment::finalTone;

            if (isTone)
            {
                periodicExpected = true;
                phase += 2.0 * kPi * targetHz / kSampleRate;
                if (phase >= 2.0 * kPi)
                    phase -= 2.0 * kPi;

                sample = static_cast<float>(toneAmp) * vocalSample(phase);
                sample += static_cast<float>(noiseAmp) * rng.next();

                if (segment.kind == Segment::toneWithBurst)
                {
                    const int burstStart = count / 3;
                    const int burstEnd = burstStart
                        + static_cast<int>(std::lround(0.003 * kSampleRate));
                    if (i >= burstStart && i < burstEnd)
                        sample += static_cast<float>(toneAmp * 4.0) * rng.next();
                }
            }
            else
            {
                nonPeriodicExpected = true;
                if (segment.kind == Segment::whiteNoise)
                    sample = static_cast<float>(toneAmp * 1.2) * rng.next();
            }

            ModernPitchEngine::PitchObservation observation;
            if (tracker.processSample(sample, observation))
            {
                observe(metrics, observation,
                        periodicExpected,
                        nonPeriodicExpected,
                        targetHz,
                        absoluteSample,
                        episodeStart,
                        currentEpisode);
            }
        }
    }

    return metrics;
}

bool checkCase(double hz, double snr, std::uint32_t seed)
{
    const auto m = runCase(hz, snr, seed);

    const double coverage = m.periodicHopsAfterGrace > 0
        ? static_cast<double>(m.periodicMeasuredAfterGrace)
            / static_cast<double>(m.periodicHopsAfterGrace)
        : 0.0;

    bool acquisitionPass = true;
    for (double ms : m.acquisitionMs)
        acquisitionPass &= ms >= 0.0 && ms < kAcquireLimitMs;

    const bool continuumPass =
        m.periodicMeasuredAfterGrace == m.periodicHopsAfterGrace
        && m.holesAfterFirstCorrect == 0;
    const bool epsilonPass =
        m.periodicWrong == 0 && m.maxAbsCents <= kEpsilonCents;
    const bool familyPass = m.familyErrors == 0;
    const bool nonPeriodicPass = m.nonPeriodicHallucinations == 0;

    const bool pass = acquisitionPass
        && continuumPass
        && epsilonPass
        && familyPass
        && nonPeriodicPass;

    std::cout << std::fixed << std::setprecision(4)
              << "F0_HARD_CONTRACT"
              << " hz=" << hz
              << " snr=" << snr
              << " seed=" << seed
              << " periodic_hops_after_grace=" << m.periodicHopsAfterGrace
              << " measured_after_grace=" << m.periodicMeasuredAfterGrace
              << " coverage=" << coverage
              << " wrong=" << m.periodicWrong
              << " holes_after_first_correct=" << m.holesAfterFirstCorrect
              << " max_abs_cents=" << m.maxAbsCents
              << " family_errors=" << m.familyErrors
              << " acquire0_ms=" << m.acquisitionMs[0]
              << " acquire1_ms=" << m.acquisitionMs[1]
              << " acquire2_ms=" << m.acquisitionMs[2]
              << " nonperiodic_hops=" << m.nonPeriodicHops
              << " nonperiodic_hallucinations=" << m.nonPeriodicHallucinations
              << " pass=" << (pass ? 1 : 0)
              << '\n';

    return pass;
}

}

int main()
{
    bool success = true;

    constexpr std::array<double, 6> frequencies {
        82.4069, 110.0, 220.0, 440.0, 660.0, 880.0
    };
    constexpr std::array<double, 3> snrs { 12.0, 6.0, 3.0 };
    constexpr std::array<std::uint32_t, 3> seeds {
        0x1234567u, 0x51f15e5du, 0x9e3779b9u
    };

    for (double hz : frequencies)
        for (double snr : snrs)
            for (auto seed : seeds)
                success &= checkCase(hz, snr, seed);

    std::cout << "F0_HARD_CONTRACT_RESULT=" << (success ? "PASS" : "FAIL") << '\n';
    return success ? 0 : 1;
}
