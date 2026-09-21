#include <JuceHeader.h>
#include "../Source/ModernPitchEngine.h"

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
    int periodicHops = 0;
    int periodicMeasured = 0;
    int periodicCorrect = 0;
    int periodicWrong = 0;
    int octaveErrors = 0;
    int noiseHops = 0;
    int noiseHallucinations = 0;
    double maxAbsCents = 0.0;
    double firstCorrectMs = -1.0;
    double reacquireMs = -1.0;
    bool seenFirst = false;
    bool awaitingReacquire = false;
    int resumeSample = -1;
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
             bool noiseOnlyExpected,
             double targetHz,
             int sampleIndex,
             int segmentStart)
{
    if (periodicExpected)
    {
        ++m.periodicHops;
        if (o.measurementAvailable
            && std::isfinite(o.correctionFrequencyHz)
            && o.correctionFrequencyHz > 0.0f)
        {
            ++m.periodicMeasured;
            const double error = centsError(o.correctionFrequencyHz, targetHz);
            const double absError = std::abs(error);
            m.maxAbsCents = std::max(m.maxAbsCents, absError);

            if (absError <= kEpsilonCents)
            {
                ++m.periodicCorrect;
                const double elapsedMs =
                    1000.0 * static_cast<double>(sampleIndex - segmentStart)
                    / kSampleRate;
                if (!m.seenFirst)
                {
                    m.firstCorrectMs = elapsedMs;
                    m.seenFirst = true;
                }
                if (m.awaitingReacquire)
                {
                    m.reacquireMs = 1000.0
                        * static_cast<double>(sampleIndex - m.resumeSample)
                        / kSampleRate;
                    m.awaitingReacquire = false;
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
                    ++m.octaveErrors;
                }
            }
        }
    }

    if (noiseOnlyExpected)
    {
        ++m.noiseHops;
        if (o.measurementAvailable
            && std::isfinite(o.correctionFrequencyHz)
            && o.correctionFrequencyHz > 0.0f)
        {
            ++m.noiseHallucinations;
        }
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

    for (const auto& segment : plan)
    {
        const int count = static_cast<int>(std::lround(
            segment.milliseconds * 0.001 * kSampleRate));
        const int segmentStart = absoluteSample;

        if (segment.kind == Segment::resumedTone
            || segment.kind == Segment::finalTone)
        {
            metrics.awaitingReacquire = true;
            metrics.resumeSample = absoluteSample;
        }

        for (int i = 0; i < count; ++i, ++absoluteSample)
        {
            float sample = 0.0f;
            bool periodicExpected = false;
            bool noiseOnlyExpected = false;

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

                const float white = rng.next();
                sample += static_cast<float>(noiseAmp) * white;

                if (segment.kind == Segment::toneWithBurst)
                {
                    const int burstStart = count / 3;
                    const int burstEnd = burstStart
                        + static_cast<int>(std::lround(0.003 * kSampleRate));
                    if (i >= burstStart && i < burstEnd)
                        sample += static_cast<float>(toneAmp * 4.0) * rng.next();
                }
            }
            else if (segment.kind == Segment::whiteNoise)
            {
                noiseOnlyExpected = true;
                sample = static_cast<float>(toneAmp * 1.2) * rng.next();
            }

            ModernPitchEngine::PitchObservation observation;
            if (tracker.processSample(sample, observation))
            {
                observe(metrics, observation,
                        periodicExpected,
                        noiseOnlyExpected,
                        targetHz,
                        absoluteSample,
                        segmentStart);
            }
        }
    }

    return metrics;
}

bool checkCase(double hz, double snr, std::uint32_t seed)
{
    const auto m = runCase(hz, snr, seed);

    const double coverage = m.periodicHops > 0
        ? static_cast<double>(m.periodicMeasured)
            / static_cast<double>(m.periodicHops)
        : 0.0;

    const bool acquisitionPass =
        m.firstCorrectMs >= 0.0 && m.firstCorrectMs < kAcquireLimitMs;
    const bool reacquisitionPass =
        m.reacquireMs >= 0.0 && m.reacquireMs < kAcquireLimitMs;
    const bool continuumPass =
        m.periodicMeasured == m.periodicHops;
    const bool epsilonPass =
        m.periodicWrong == 0 && m.maxAbsCents <= kEpsilonCents;
    const bool familyPass = m.octaveErrors == 0;
    const bool noisePass = m.noiseHallucinations == 0;

    const bool pass = acquisitionPass
        && reacquisitionPass
        && continuumPass
        && epsilonPass
        && familyPass
        && noisePass;

    std::cout << std::fixed << std::setprecision(4)
              << "F0_HARD_CONTRACT"
              << " hz=" << hz
              << " snr=" << snr
              << " seed=" << seed
              << " periodic_hops=" << m.periodicHops
              << " measured=" << m.periodicMeasured
              << " coverage=" << coverage
              << " wrong=" << m.periodicWrong
              << " max_abs_cents=" << m.maxAbsCents
              << " family_errors=" << m.octaveErrors
              << " first_correct_ms=" << m.firstCorrectMs
              << " reacquire_ms=" << m.reacquireMs
              << " noise_hops=" << m.noiseHops
              << " noise_hallucinations=" << m.noiseHallucinations
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
