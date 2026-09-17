#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

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
        const float noise = static_cast<float>((static_cast<int>(x & 0xffffu) - 32768) / 32768.0) * 0.004f;
        ++index;
        return static_cast<float>(0.18 * std::sin(phase)
                                + 0.11 * std::sin(2.0 * phase + 0.18)
                                + 0.065 * std::sin(3.0 * phase - 0.23)
                                + 0.035 * std::sin(4.0 * phase + 0.51)) + noise;
    }

    std::uint64_t index = 0;
    std::uint32_t noiseState = 0x51f15e5du;
};

std::uint32_t bits(float value) noexcept
{
    std::uint32_t result = 0;
    static_assert(sizeof(result) == sizeof(value));
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

bool sameFloat(float a, float b) noexcept
{
    return bits(a) == bits(b);
}

bool sameCandidate(const ModernPitchEngine::MultiRatePitchTracker::PitchCandidate& a,
                   const ModernPitchEngine::MultiRatePitchTracker::PitchCandidate& b) noexcept
{
    return sameFloat(a.frequencyHz, b.frequencyHz)
        && sameFloat(a.confidence, b.confidence)
        && sameFloat(a.periodicity, b.periodicity)
        && sameFloat(a.harmonicFamily, b.harmonicFamily)
        && sameFloat(a.aperiodicity, b.aperiodicity)
        && sameFloat(a.tonalCleanliness, b.tonalCleanliness)
        && a.pathIndex == b.pathIndex
        && a.ageInHops == b.ageInHops
        && a.valid == b.valid;
}

bool sameSlot(const ModernPitchEngine::MultiRatePitchTracker::CandidateSlot& a,
              const ModernPitchEngine::MultiRatePitchTracker::CandidateSlot& b) noexcept
{
    return a.ageInHops == b.ageInHops && sameCandidate(a.candidate, b.candidate);
}

bool sameObservation(const ModernPitchEngine::PitchObservation& a,
                     const ModernPitchEngine::PitchObservation& b) noexcept
{
    return sameFloat(a.frequencyHz, b.frequencyHz)
        && sameFloat(a.correctionFrequencyHz, b.correctionFrequencyHz)
        && sameFloat(a.confidence, b.confidence)
        && sameFloat(a.periodicity, b.periodicity)
        && sameFloat(a.voicing, b.voicing)
        && sameFloat(a.consensus, b.consensus)
        && sameFloat(a.onsetStrength, b.onsetStrength)
        && a.detectorSupport == b.detectorSupport
        && a.octaveState == b.octaveState
        && a.pendingOctaveObservations == b.pendingOctaveObservations
        && a.valid == b.valid
        && a.measurementAvailable == b.measurementAvailable
        && a.onset == b.onset
        && a.audioPresent == b.audioPresent;
}

void configure(ModernPitchEngine::MultiRatePitchTracker& tracker) noexcept
{
    tracker.prepare(kSampleRate);
    tracker.setRange(45.0f, 1600.0f);
    tracker.setSensitivity(0.70f);
    tracker.setVoiceAuthorityContext(true, 0.88f, 0.10f, 0.90f,
                                     0.88f, 0.08f, 0.90f, 0.06f);
}

bool compareTrackerState(const ModernPitchEngine::MultiRatePitchTracker& a,
                         const ModernPitchEngine::MultiRatePitchTracker& b,
                         std::uint64_t sampleIndex) noexcept
{
    const bool same = sameSlot(a.fullRateCandidate_, b.fullRateCandidate_)
        && sameSlot(a.halfRateCandidate_, b.halfRateCandidate_)
        && sameSlot(a.quarterRateCandidate_, b.quarterRateCandidate_)
        && sameSlot(a.eighthRateCandidate_, b.eighthRateCandidate_)
        && sameFloat(a.trackedPitchHz_, b.trackedPitchHz_)
        && sameFloat(a.trackedConfidence_, b.trackedConfidence_)
        && sameFloat(a.trackedPeriodicity_, b.trackedPeriodicity_)
        && sameFloat(a.trackedConsensus_, b.trackedConsensus_)
        && a.trackedSupportCount_ == b.trackedSupportCount_
        && a.invalidHopCount_ == b.invalidHopCount_
        && a.octaveState_ == b.octaveState_
        && a.pendingOctaveDelta_ == b.pendingOctaveDelta_
        && a.pendingOctaveCount_ == b.pendingOctaveCount_
        && sameFloat(a.pendingOctaveFrequencyHz_, b.pendingOctaveFrequencyHz_)
        && sameFloat(a.committedOctaveFrequencyHz_, b.committedOctaveFrequencyHz_)
        && a.octaveCommitGuardHops_ == b.octaveCommitGuardHops_;

    if (!same)
        std::cerr << "GOLDEN_DETECTOR_STATE_MISMATCH sample=" << sampleIndex << '\n';
    return same;
}
}

int main()
{
    ModernPitchEngine::MultiRatePitchTracker trackerA;
    ModernPitchEngine::MultiRatePitchTracker trackerB;
    configure(trackerA);
    configure(trackerB);

    ModernPitchEngine::PitchObservation observationA;
    ModernPitchEngine::PitchObservation observationB;
    SignalGenerator generator;

    constexpr std::uint64_t sampleCount = static_cast<std::uint64_t>(kSampleRate * 8.0);
    std::uint64_t emittedObservations = 0;

    for (std::uint64_t sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const float sample = generator.next();
        const bool emittedA = trackerA.processSample(sample, observationA);
        const bool emittedB = trackerB.processSample(sample, observationB);

        if (emittedA != emittedB)
        {
            std::cerr << "GOLDEN_DETECTOR_EMISSION_MISMATCH sample=" << sampleIndex << '\n';
            return 1;
        }

        if (!emittedA)
            continue;

        ++emittedObservations;
        if (!sameObservation(observationA, observationB))
        {
            std::cerr << "GOLDEN_DETECTOR_OBSERVATION_MISMATCH sample=" << sampleIndex << '\n';
            return 1;
        }

        if (!compareTrackerState(trackerA, trackerB, sampleIndex))
            return 1;
    }

    if (emittedObservations == 0)
    {
        std::cerr << "GOLDEN_DETECTOR_NO_OBSERVATIONS\n";
        return 1;
    }

    std::cout << "GOLDEN_DETECTOR_BITWISE_DETERMINISM=PASS"
              << " observations=" << emittedObservations << '\n';
    return 0;
}
