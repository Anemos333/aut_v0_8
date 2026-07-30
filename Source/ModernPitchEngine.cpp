#include "ModernPitchEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// NEUMATON_V6_CSV_DIAGNOSTICS
// Debug-only CSV output meter logging.  This writes from the audio block
// boundary at a low rate and is intended for laboratory/test builds, not
// final release builds.  Set to 0 to compile it out while leaving the code.
#ifndef NEUMATON_V6_CSV_DIAGNOSTICS
#define NEUMATON_V6_CSV_DIAGNOSTICS 1
#endif

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr float minimumDetectorRms = 0.0012f;

[[nodiscard]] double safeLog2(double value) noexcept
{
    return std::log2(std::max(value, 1.0e-12));
}

[[nodiscard]] float sanitiseAudioSample(float value) noexcept
{
    if (!std::isfinite(value) || std::fpclassify(value) == FP_SUBNORMAL)
        return 0.0f;
    return std::clamp(value, -32.0f, 32.0f);
}

[[nodiscard]] float finiteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] float smoothStep(float edge0, float edge1, float value) noexcept
{
    if (edge1 <= edge0)
        return value >= edge1 ? 1.0f : 0.0f;

    const float x = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}


[[nodiscard]] float neumatonCorrectionSeverityFromAmountSpeed(float amount01,
                                                              float retuneTimeMs) noexcept
{
    // Internal severity S(amount, speed). Humanize intentionally does not enter.
    // Amount 100 / Speed 0 -> 1.0: strict reconstructive correction.
    // Around Amount 30 / Speed 200 -> 0.0: previous cautious behaviour.
    const float safeAmount = std::clamp(amount01, 0.0f, 1.0f);
    const float safeSpeedMs = std::isfinite(retuneTimeMs)
        ? std::clamp(retuneTimeMs, 0.0f, 500.0f)
        : 200.0f;

    const float amountDrive = std::clamp((safeAmount - 0.30f) / 0.70f,
                                         0.0f,
                                         1.0f);
    const float speedDrive = std::clamp((200.0f - safeSpeedMs) / 200.0f,
                                        0.0f,
                                        1.0f);

    const auto logPower = [](float x, float exponent) noexcept -> float
    {
        x = std::clamp(x, 0.0f, 1.0f);
        const float logged = std::log1p(9.0f * x) / std::log(10.0f);
        return std::pow(std::clamp(logged, 0.0f, 1.0f), exponent);
    };

    const float amountTerm = logPower(amountDrive, 1.55f);
    const float speedTerm = logPower(speedDrive, 1.25f);
    const float linearTerm = amountDrive * speedDrive;

    return std::clamp(0.72f * amountTerm * speedTerm
                      + 0.28f * linearTerm,
                      0.0f,
                      1.0f);
}


// NEUMATON_FULL_SPECTRUM_TRANSPORT_V4_AMOUNT_TOLERANCE
// Amount is no longer a dry/wet-like correction-depth multiplier.  It defines
// when the pitch corrector should intervene.  Low detector trust widens the
// tolerance instead of opening an uncorrected dry/residual path.
[[nodiscard]] double neumatonApplyAmountToleranceGate(double errorCents,
                                                      float amount01,
                                                      float confidence,
                                                      float consensus) noexcept
{
    const float safeAmount = std::clamp(amount01, 0.0f, 1.0f);
    if (safeAmount <= 0.0001f || !std::isfinite(errorCents))
        return 0.0;

    const float absError = static_cast<float>(std::abs(errorCents));

    // Amount 100: intervene on very small errors.
    // Amount 50: accept small expressive intonation deviations.
    // Amount 0: bypass correction completely, handled above.
    const float amountForgiveness = std::pow(1.0f - safeAmount, 2.10f);
    const float baseToleranceCents = 0.50f + 18.0f * amountForgiveness;

    const float detectorReliability = std::clamp(0.65f * confidence
                                               + 0.35f * consensus,
                                               0.0f,
                                               1.0f);
    const float lowTrust = 1.0f - smoothStep(0.25f, 0.78f, detectorReliability);

    // Design target: at Amount 100, very low trust adds roughly +/-1 cent.
    // At Amount 20, very low trust adds roughly +/-7 cents.
    const float trustToleranceWideningCents = lowTrust
        * (1.0f + 7.5f * (1.0f - safeAmount));

    const float totalToleranceCents = baseToleranceCents
        + trustToleranceWideningCents;

    const float transitionWidthCents = 0.90f
        + 3.50f * (1.0f - safeAmount)
        + 2.00f * lowTrust;

    const float interventionGate = smoothStep(totalToleranceCents,
                                              totalToleranceCents + transitionWidthCents,
                                              absError);

    return errorCents * static_cast<double>(interventionGate);
}

[[nodiscard]] float retuneFloorForLatencyMode(ModernPitchEngine::LatencyMode mode) noexcept
{
    switch (mode)
    {
        case ModernPitchEngine::LatencyMode::ultraLive: return 4.5f;
        case ModernPitchEngine::LatencyMode::live:      return 6.0f;
        case ModernPitchEngine::LatencyMode::quality:   return 9.0f;
    }
    return 6.0f;
}

[[nodiscard]] double wrapCorrectionToNearestOctave(double cents) noexcept
{
    if (!std::isfinite(cents))
        return 0.0;

    // The scale engine is octave-repeating. A correction that differs by a
    // whole octave addresses the same scale degree but the wrong register.
    // Keep the correction in the nearest octave: [-600, +600) cents.
    double wrapped = std::fmod(cents + 600.0, 1200.0);
    if (wrapped < 0.0)
        wrapped += 1200.0;
    return wrapped - 600.0;
}

[[nodiscard]] double alignTargetToNearestOctave(double targetLog2,
                                                 double referenceLog2) noexcept
{
    if (!std::isfinite(targetLog2) || !std::isfinite(referenceLog2))
        return referenceLog2;

    return targetLog2 + std::round(referenceLog2 - targetLog2);
}
} // namespace

//==============================================================================
// Utilities

int ModernPitchEngine::nextPowerOfTwo(int value) noexcept
{
    int result = 1;
    while (result < value)
        result <<= 1;
    return result;
}

float ModernPitchEngine::clamp01(float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}

int ModernPitchEngine::frameSizeForMode(double sampleRate, LatencyMode mode) noexcept
{
    const double safeRate = std::isfinite(sampleRate)
        ? std::max(8000.0, sampleRate)
        : 48000.0;
    int baseAt48k = 256;

    switch (mode)
    {
        case LatencyMode::ultraLive: baseAt48k = 128; break;
        case LatencyMode::live:      baseAt48k = 256; break;
        case LatencyMode::quality:   baseAt48k = 512; break;
    }

    const int requested = std::max(64,
        static_cast<int>(std::lround(static_cast<double>(baseAt48k)
                                     * safeRate / 48000.0)));
    return nextPowerOfTwo(requested);
}

//==============================================================================
// BiquadLowPass

void ModernPitchEngine::BiquadLowPass::prepare(double sampleRate,
                                                double cutoffHz,
                                                double q) noexcept
{
    const double safeSampleRate = std::max(1.0, sampleRate);
    const double safeCutoff = std::clamp(cutoffHz, 10.0, safeSampleRate * 0.45);
    const double safeQ = std::max(0.05, q);

    const double omega = twoPi * safeCutoff / safeSampleRate;
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double alpha = sine / (2.0 * safeQ);
    const double a0 = 1.0 + alpha;

    b0_ = ((1.0 - cosine) * 0.5) / a0;
    b1_ = (1.0 - cosine) / a0;
    b2_ = b0_;
    a1_ = (-2.0 * cosine) / a0;
    a2_ = (1.0 - alpha) / a0;
    reset();
}

void ModernPitchEngine::BiquadLowPass::reset() noexcept
{
    z1_ = 0.0;
    z2_ = 0.0;
}

float ModernPitchEngine::BiquadLowPass::process(float input) noexcept
{
    const double x = static_cast<double>(sanitiseAudioSample(input));
    const double output = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * output + z2_;
    z2_ = b2_ * x - a2_ * output;
    if (!std::isfinite(output) || !std::isfinite(z1_) || !std::isfinite(z2_))
    {
        reset();
        return 0.0f;
    }
    return static_cast<float>(output);
}

//==============================================================================
// MultiRatePitchTracker

void ModernPitchEngine::MultiRatePitchTracker::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate)
        ? std::max(8000.0, sampleRate)
        : 48000.0;

    halfRateAntiAlias_.prepare(sampleRate_, std::min(5200.0, sampleRate_ * 0.20));
    quarterRateAntiAlias_.prepare(sampleRate_ * 0.5,
                                  std::min(2600.0, sampleRate_ * 0.10));
    eighthRateAntiAlias_.prepare(sampleRate_ * 0.25,
                                 std::min(1300.0, sampleRate_ * 0.05));

    dcBlockCoefficient_ = static_cast<float>(std::exp(-twoPi * 22.0 / sampleRate_));

    fastEnergyCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.0018 * sampleRate_)));
    slowEnergyCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.035 * sampleRate_)));

    reset();
}

void ModernPitchEngine::MultiRatePitchTracker::reset() noexcept
{
    fullRateRing_.fill(0.0f);
    halfRateRing_.fill(0.0f);
    quarterRateRing_.fill(0.0f);
    eighthRateRing_.fill(0.0f);
    frame_.fill(0.0f);
    difference_.fill(1.0f);

    fullRateWritePosition_ = 0;
    halfRateWritePosition_ = 0;
    quarterRateWritePosition_ = 0;
    eighthRateWritePosition_ = 0;
    fullRateAvailableSamples_ = 0;
    halfRateAvailableSamples_ = 0;
    quarterRateAvailableSamples_ = 0;
    eighthRateAvailableSamples_ = 0;
    halfRateDecimationCounter_ = 0;
    quarterRateDecimationCounter_ = 0;
    eighthRateDecimationCounter_ = 0;
    hopCounter_ = 0;
    analysisHopCounter_ = 0;

    halfRateAntiAlias_.reset();
    quarterRateAntiAlias_.reset();
    eighthRateAntiAlias_.reset();

    previousInput_ = 0.0f;
    previousDcOutput_ = 0.0f;
    fastEnergy_ = 0.0f;
    slowEnergy_ = 0.0f;
    onsetEnvelope_ = 0.0f;
    onsetCooldownSamples_ = 0;
    onsetPending_ = false;

    fullRateCandidate_ = {};
    halfRateCandidate_ = {};
    quarterRateCandidate_ = {};
    eighthRateCandidate_ = {};
    decoderBeam_.fill({});

    trackedPitchHz_ = 0.0f;
    trackedConfidence_ = 0.0f;
    trackedPeriodicity_ = 0.0f;
    trackedConsensus_ = 0.0f;
    trackedSupportCount_ = 0;
    invalidHopCount_ = 0;

    octaveState_ = 0;
    pendingOctaveDelta_ = 0;
    pendingOctaveCount_ = 0;
    pendingOctaveFrequencyHz_ = 0.0f;
    committedOctaveFrequencyHz_ = 0.0f;
    octaveCommitGuardHops_ = 0;
}

void ModernPitchEngine::MultiRatePitchTracker::setRange(float minimumPitchHz,
                                                         float maximumPitchHz) noexcept
{
    minimumPitchHz_ = std::clamp(minimumPitchHz, 35.0f, 500.0f);
    maximumPitchHz_ = std::clamp(maximumPitchHz,
                                 minimumPitchHz_ + 20.0f,
                                 3000.0f);
}

void ModernPitchEngine::MultiRatePitchTracker::setSensitivity(float sensitivity) noexcept
{
    sensitivity_ = clamp01(sensitivity);
}

void ModernPitchEngine::MultiRatePitchTracker::push(
    std::array<float, ringSize>& ring,
    int& writePosition,
    int& availableSamples,
    float sample) noexcept
{
    ring[static_cast<std::size_t>(writePosition)] = sample;
    writePosition = (writePosition + 1) & ringMask;
    availableSamples = std::min(availableSamples + 1, ringSize);
}

ModernPitchEngine::MultiRatePitchTracker::PitchCandidate
ModernPitchEngine::MultiRatePitchTracker::analyse(
    const std::array<float, ringSize>& ring,
    int writePosition,
    int availableSamples,
    double effectiveSampleRate,
    float minimumFrequency,
    float maximumFrequency,
    int analysisLength) noexcept
{
    PitchCandidate result;
    analysisLength = std::clamp(analysisLength, 64, maxAnalysisSize);

    if (availableSamples < analysisLength || effectiveSampleRate <= 0.0
        || minimumFrequency >= maximumFrequency)
    {
        return result;
    }

    const int startPosition = (writePosition - analysisLength + ringSize) & ringMask;

    double mean = 0.0;
    for (int index = 0; index < analysisLength; ++index)
    {
        const float sample = ring[static_cast<std::size_t>((startPosition + index) & ringMask)];
        frame_[static_cast<std::size_t>(index)] = sample;
        mean += static_cast<double>(sample);
    }
    mean /= static_cast<double>(analysisLength);

    double squaredSum = 0.0;
    for (int index = 0; index < analysisLength; ++index)
    {
        float& sample = frame_[static_cast<std::size_t>(index)];
        sample -= static_cast<float>(mean);
        squaredSum += static_cast<double>(sample) * static_cast<double>(sample);
    }

    const float rms = static_cast<float>(std::sqrt(
        squaredSum / static_cast<double>(analysisLength)));
    if (rms < minimumDetectorRms)
        return result;

    const int tauMinimum = std::clamp(
        static_cast<int>(std::floor(effectiveSampleRate
                                    / static_cast<double>(maximumFrequency))),
        2,
        analysisLength - 16);

    const int tauMaximum = std::clamp(
        static_cast<int>(std::ceil(effectiveSampleRate
                                   / static_cast<double>(minimumFrequency))),
        tauMinimum + 1,
        analysisLength - 16);

    difference_.fill(1.0f);
    difference_[0] = 1.0f;

    for (int tau = 1; tau <= tauMaximum; ++tau)
    {
        const int overlap = analysisLength - tau;
        float sum0 = 0.0f;
        float sum1 = 0.0f;
        float sum2 = 0.0f;
        float sum3 = 0.0f;

        int index = 0;
        const int vectorEnd = overlap & ~3;
        for (; index < vectorEnd; index += 4)
        {
            const float delta0 = frame_[static_cast<std::size_t>(index)]
                               - frame_[static_cast<std::size_t>(index + tau)];
            const float delta1 = frame_[static_cast<std::size_t>(index + 1)]
                               - frame_[static_cast<std::size_t>(index + tau + 1)];
            const float delta2 = frame_[static_cast<std::size_t>(index + 2)]
                               - frame_[static_cast<std::size_t>(index + tau + 2)];
            const float delta3 = frame_[static_cast<std::size_t>(index + 3)]
                               - frame_[static_cast<std::size_t>(index + tau + 3)];
            sum0 += delta0 * delta0;
            sum1 += delta1 * delta1;
            sum2 += delta2 * delta2;
            sum3 += delta3 * delta3;
        }

        float differenceSum = (sum0 + sum1) + (sum2 + sum3);
        for (; index < overlap; ++index)
        {
            const float delta = frame_[static_cast<std::size_t>(index)]
                              - frame_[static_cast<std::size_t>(index + tau)];
            differenceSum += delta * delta;
        }

        difference_[static_cast<std::size_t>(tau)] = differenceSum
            / static_cast<float>(std::max(1, overlap));
    }

    double cumulativeSum = 0.0;
    for (int tau = 1; tau <= tauMaximum; ++tau)
    {
        cumulativeSum += static_cast<double>(difference_[static_cast<std::size_t>(tau)]);
        difference_[static_cast<std::size_t>(tau)] = cumulativeSum > 1.0e-20
            ? static_cast<float>(static_cast<double>(difference_[static_cast<std::size_t>(tau)])
                                 * static_cast<double>(tau) / cumulativeSum)
            : 1.0f;
    }

    const float yinThreshold = 0.12f + 0.16f * sensitivity_;
    const float fallbackThreshold = 0.26f + 0.20f * sensitivity_;

    int thresholdTau = -1;
    int globalTau = tauMinimum;
    float globalValue = difference_[static_cast<std::size_t>(tauMinimum)];

    for (int tau = tauMinimum; tau <= tauMaximum; ++tau)
    {
        const float value = difference_[static_cast<std::size_t>(tau)];
        if (value < globalValue)
        {
            globalValue = value;
            globalTau = tau;
        }

        if (thresholdTau < 0 && value < yinThreshold)
        {
            int localTau = tau;
            while (localTau + 1 <= tauMaximum
                   && difference_[static_cast<std::size_t>(localTau + 1)]
                        < difference_[static_cast<std::size_t>(localTau)])
            {
                ++localTau;
            }
            thresholdTau = localTau;
        }
    }

    if (thresholdTau < 0 && globalValue > fallbackThreshold)
        return result;

    // Alternative periods are deliberately retained because a weak fundamental
    // can be recovered from its harmonics.  They are not equally trusted:
    // doubled periods (subharmonics) receive the strongest prior penalty and
    // must subsequently survive the cross-rate consensus and temporal decoder.
    std::array<int, 5> candidateTaus {
        thresholdTau >= 0 ? thresholdTau : globalTau,
        globalTau,
        std::max(tauMinimum, globalTau / 2),
        std::min(tauMaximum, globalTau * 2),
        std::min(tauMaximum, (globalTau * 3) / 2)
    };
    constexpr std::array<float, 5> candidatePriors {
        1.00f, 0.98f, 0.88f, 0.70f, 0.78f
    };

    float bestScore = -1.0f;
    int bestTau = -1;
    float bestPeriodicity = 0.0f;

    for (std::size_t candidateIndex = 0;
         candidateIndex < candidateTaus.size();
         ++candidateIndex)
    {
        int tau = std::clamp(candidateTaus[candidateIndex],
                             tauMinimum,
                             tauMaximum);

        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - tau;

        for (int index = 0; index < overlap; ++index)
        {
            const double a = frame_[static_cast<std::size_t>(index)];
            const double b = frame_[static_cast<std::size_t>(index + tau)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }

        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        const float normalisedCorrelation = denominator > 0.0
            ? static_cast<float>(correlation / denominator)
            : 0.0f;
        const float periodicity = clamp01(0.5f * (normalisedCorrelation + 1.0f));
        const float yinConfidence = clamp01(
            1.0f - difference_[static_cast<std::size_t>(tau)]);

        // Prefer candidates containing at least two periods, but do not reject
        // low notes whose fundamental is mainly inferred from their harmonics.
        const float periodsInWindow = static_cast<float>(analysisLength)
                                    / static_cast<float>(std::max(1, tau));
        const float periodSupport = std::clamp(periodsInWindow / 2.2f, 0.55f, 1.0f);
        const float score = (0.67f * yinConfidence + 0.33f * periodicity)
                          * periodSupport
                          * candidatePriors[candidateIndex];

        if (score > bestScore)
        {
            bestScore = score;
            bestTau = tau;
            bestPeriodicity = periodicity;
        }
    }

    if (bestTau < 2 || bestScore < 0.45f)
        return result;

    double refinedTau = static_cast<double>(bestTau);
    if (bestTau > tauMinimum && bestTau < tauMaximum)
    {
        const double left = difference_[static_cast<std::size_t>(bestTau - 1)];
        const double centre = difference_[static_cast<std::size_t>(bestTau)];
        const double right = difference_[static_cast<std::size_t>(bestTau + 1)];
        const double denominator = left - 2.0 * centre + right;

        if (std::abs(denominator) > 1.0e-12)
            refinedTau += 0.5 * (left - right) / denominator;
    }

    if (refinedTau <= 0.0)
        return result;

    const float frequency = static_cast<float>(effectiveSampleRate / refinedTau);
    if (!std::isfinite(frequency)
        || frequency < minimumFrequency * 0.82f
        || frequency > maximumFrequency * 1.18f)
    {
        return result;
    }

    result.frequencyHz = frequency;
    result.confidence = clamp01(bestScore);
    result.periodicity = bestPeriodicity;
    result.valid = true;
    return result;
}

float ModernPitchEngine::MultiRatePitchTracker::centsDistance(
    float frequencyA,
    float frequencyB) noexcept
{
    if (frequencyA <= 0.0f || frequencyB <= 0.0f)
        return 100000.0f;

    return std::abs(1200.0f * std::log2(frequencyA / frequencyB));
}

float ModernPitchEngine::MultiRatePitchTracker::candidateBaseScore(
    const PitchCandidate& candidate) const noexcept
{
    if (!candidate.valid || candidate.frequencyHz <= 0.0f)
        return 0.0f;

    const float ageWeight = std::exp(-0.22f
        * static_cast<float>(std::max(0, candidate.ageInHops)));
    return clamp01((0.70f * candidate.confidence
                  + 0.30f * candidate.periodicity) * ageWeight);
}

float ModernPitchEngine::MultiRatePitchTracker::pathReliability(
    int pathIndex,
    float frequencyHz) const noexcept
{
    const auto bandWeight = [](float frequency,
                               float lowerSoft,
                               float lowerFull,
                               float upperFull,
                               float upperSoft) noexcept
    {
        const float lower = smoothStep(lowerSoft, lowerFull, frequency);
        const float upper = 1.0f - smoothStep(upperFull, upperSoft, frequency);
        return std::clamp(lower * upper, 0.08f, 1.0f);
    };

    switch (pathIndex)
    {
        case 0: return bandWeight(frequencyHz, 125.0f, 185.0f, 1250.0f, 2300.0f);
        case 1: return bandWeight(frequencyHz, 62.0f, 92.0f, 650.0f, 980.0f);
        case 2: return bandWeight(frequencyHz, 30.0f, 48.0f, 330.0f, 500.0f);
        case 3: return bandWeight(frequencyHz, 22.0f, 36.0f, 165.0f, 250.0f);
        default: break;
    }

    return 0.0f;
}

int ModernPitchEngine::MultiRatePitchTracker::collectFreshCandidates(
    std::array<PitchCandidate, detectorPathCount>& candidates) const noexcept
{
    int count = 0;

    const auto append = [&candidates, &count](const CandidateSlot& slot,
                                              int pathIndex,
                                              int maximumAge)
    {
        if (!slot.candidate.valid || slot.ageInHops > maximumAge
            || count >= detectorPathCount)
        {
            return;
        }

        PitchCandidate candidate = slot.candidate;
        candidate.pathIndex = pathIndex;
        candidate.ageInHops = slot.ageInHops;
        candidates[static_cast<std::size_t>(count++)] = candidate;
    };

    append(fullRateCandidate_,    0, 2);
    append(halfRateCandidate_,    1, 3);
    append(quarterRateCandidate_, 2, 5);
    append(eighthRateCandidate_,  3, 9);
    return count;
}

int ModernPitchEngine::MultiRatePitchTracker::buildConsensusHypotheses(
    const std::array<PitchCandidate, detectorPathCount>& candidates,
    int candidateCount,
    std::array<ConsensusHypothesis, maxConsensusHypotheses>& hypotheses) const noexcept
{
    int seedCount = 0;

    // Every detector contributes octave-explicit seeds.  A detector can only
    // contribute once to a resulting cluster, so generated octave variants do
    // not create fake consensus by themselves.
    for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
    {
        const auto& candidate = candidates[static_cast<std::size_t>(candidateIndex)];
        if (!candidate.valid)
            continue;

        for (int octaveShift = -2; octaveShift <= 2; ++octaveShift)
        {
            const float frequency = std::ldexp(candidate.frequencyHz, octaveShift);
            if (frequency < minimumPitchHz_ || frequency > maximumPitchHz_)
                continue;

            bool duplicate = false;
            for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)
            {
                if (centsDistance(hypotheses[static_cast<std::size_t>(seedIndex)].frequencyHz,
                                  frequency) < 28.0f)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate && seedCount < maxConsensusHypotheses)
            {
                auto& seed = hypotheses[static_cast<std::size_t>(seedCount++)];
                seed = {};
                seed.frequencyHz = frequency;
                seed.valid = true;
            }
        }
    }

    int validCount = 0;
    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)
    {
        const float seedFrequency = hypotheses[static_cast<std::size_t>(seedIndex)].frequencyHz;
        double weightedLogFrequency = 0.0;
        float totalWeight = 0.0f;
        float confidenceSum = 0.0f;
        float periodicitySum = 0.0f;
        int supportCount = 0;
        int directSupportCount = 0;
        std::uint8_t supportMask = 0;
        std::uint8_t freshSupportMask = 0;

        for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
        {
            const auto& candidate = candidates[static_cast<std::size_t>(candidateIndex)];
            int bestOctaveShift = 0;
            float bestFrequency = candidate.frequencyHz;
            float bestDistance = centsDistance(bestFrequency, seedFrequency);

            for (int octaveShift = -2; octaveShift <= 2; ++octaveShift)
            {
                const float shiftedFrequency = std::ldexp(candidate.frequencyHz, octaveShift);
                if (shiftedFrequency < minimumPitchHz_ || shiftedFrequency > maximumPitchHz_)
                    continue;

                const float distance = centsDistance(shiftedFrequency, seedFrequency);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestFrequency = shiftedFrequency;
                    bestOctaveShift = octaveShift;
                }
            }

            const bool direct = bestOctaveShift == 0;
            const float tolerance = direct ? 55.0f : 38.0f;
            if (bestDistance > tolerance)
                continue;

            const float octavePrior = direct ? 1.0f
                : (std::abs(bestOctaveShift) == 1 ? 0.52f : 0.25f);
            const float reliability = pathReliability(candidate.pathIndex,
                                                       candidate.frequencyHz);
            const float baseScore = candidateBaseScore(candidate);
            const float weight = baseScore * reliability * octavePrior;

            // Octave-transposed support is useful as harmonic evidence, but it
            // must be genuinely strong; otherwise it is ignored rather than
            // being allowed to manufacture a low subharmonic.
            if ((!direct && baseScore < 0.60f) || weight < 0.10f)
                continue;

            weightedLogFrequency += static_cast<double>(weight)
                                  * safeLog2(static_cast<double>(bestFrequency));
            totalWeight += weight;
            confidenceSum += weight * candidate.confidence;
            periodicitySum += weight * candidate.periodicity;
            ++supportCount;
            if (direct)
                ++directSupportCount;

            const auto bit = static_cast<std::uint8_t>(1u << candidate.pathIndex);
            supportMask = static_cast<std::uint8_t>(supportMask | bit);
            if (candidate.ageInHops == 0)
                freshSupportMask = static_cast<std::uint8_t>(freshSupportMask | bit);
        }

        if (supportCount <= 0 || totalWeight <= 1.0e-6f)
            continue;

        ConsensusHypothesis hypothesis;
        hypothesis.frequencyHz = static_cast<float>(std::exp2(
            weightedLogFrequency / static_cast<double>(totalWeight)));
        hypothesis.confidence = clamp01(confidenceSum / totalWeight);
        hypothesis.periodicity = clamp01(periodicitySum / totalWeight);
        hypothesis.supportCount = supportCount;
        hypothesis.directSupportCount = directSupportCount;
        hypothesis.supportMask = supportMask;
        hypothesis.freshSupportMask = freshSupportMask;

        const float pathConsensus = static_cast<float>(supportCount - 1)
                                  / static_cast<float>(detectorPathCount - 1);
        const float directConsensus = static_cast<float>(directSupportCount)
                                    / static_cast<float>(detectorPathCount);
        hypothesis.consensus = clamp01(0.12f
                                     + 0.58f * pathConsensus
                                     + 0.30f * directConsensus);

        const float meanEvidence = clamp01(totalWeight
            / static_cast<float>(std::max(1, supportCount)));
        const float directPenalty = directSupportCount == 0 ? 0.16f : 0.0f;
        hypothesis.evidenceScore = meanEvidence
                                 * (0.70f + 0.30f * hypothesis.consensus)
                                 + 0.045f * static_cast<float>(directSupportCount)
                                 - directPenalty;
        hypothesis.valid = hypothesis.evidenceScore > 0.20f;

        if (!hypothesis.valid)
            continue;

        // Merge clusters that converged after weighted refinement.
        int mergeIndex = -1;
        for (int existing = 0; existing < validCount; ++existing)
        {
            if (centsDistance(hypotheses[static_cast<std::size_t>(existing)].frequencyHz,
                              hypothesis.frequencyHz) < 24.0f)
            {
                mergeIndex = existing;
                break;
            }
        }

        if (mergeIndex >= 0)
        {
            if (hypothesis.evidenceScore
                > hypotheses[static_cast<std::size_t>(mergeIndex)].evidenceScore)
            {
                hypotheses[static_cast<std::size_t>(mergeIndex)] = hypothesis;
            }
        }
        else if (validCount < maxConsensusHypotheses)
        {
            hypotheses[static_cast<std::size_t>(validCount++)] = hypothesis;
        }
    }

    std::sort(hypotheses.begin(),
              hypotheses.begin() + validCount,
              [](const ConsensusHypothesis& left,
                 const ConsensusHypothesis& right)
              {
                  return left.evidenceScore > right.evidenceScore;
              });
    return validCount;
}

bool ModernPitchEngine::MultiRatePitchTracker::isOctaveLikeTransition(
    float fromFrequency,
    float toFrequency,
    int& octaveDelta,
    float& residualCents) noexcept
{
    octaveDelta = 0;
    residualCents = 100000.0f;
    if (fromFrequency <= 0.0f || toFrequency <= 0.0f)
        return false;

    const float octaveDistance = std::log2(toFrequency / fromFrequency);
    octaveDelta = static_cast<int>(std::lround(octaveDistance));
    residualCents = std::abs(1200.0f
        * (octaveDistance - static_cast<float>(octaveDelta)));
    return octaveDelta != 0 && std::abs(octaveDelta) <= 2
        && residualCents <= 85.0f;
}

void ModernPitchEngine::MultiRatePitchTracker::updateDecoderBeam(
    const std::array<ConsensusHypothesis, maxConsensusHypotheses>& hypotheses,
    int hypothesisCount,
    bool onsetPending) noexcept
{
    std::array<DecoderState, maxConsensusHypotheses + decoderBeamWidth> proposals {};
    int proposalCount = 0;

    for (int hypothesisIndex = 0;
         hypothesisIndex < hypothesisCount && proposalCount < maxConsensusHypotheses;
         ++hypothesisIndex)
    {
        const auto& hypothesis = hypotheses[static_cast<std::size_t>(hypothesisIndex)];
        if (!hypothesis.valid)
            continue;

        DecoderState proposal;
        proposal.valid = true;
        proposal.logFrequency = safeLog2(hypothesis.frequencyHz);
        proposal.score = hypothesis.evidenceScore + 0.26f * hypothesis.consensus;
        proposal.octaveIndex = octaveState_;

        float bestTransitionScore = -1000.0f;
        int bestOctaveIndex = octaveState_;
        bool foundPrevious = false;

        for (const auto& previous : decoderBeam_)
        {
            if (!previous.valid)
                continue;

            foundPrevious = true;
            const float deltaCents = static_cast<float>(1200.0
                * (proposal.logFrequency - previous.logFrequency));
            const float absoluteCents = std::abs(deltaCents);
            const float continuityBonus = 0.30f * std::exp(-absoluteCents / 85.0f);
            const float transitionPenalty = onsetPending
                ? 0.10f * std::min(1.0f, absoluteCents / 1800.0f)
                : 0.19f * std::min(2.0f, absoluteCents / 650.0f);

            int octaveDelta = 0;
            float residualCents = 0.0f;
            const bool octaveLike = isOctaveLikeTransition(
                static_cast<float>(std::exp2(previous.logFrequency)),
                hypothesis.frequencyHz,
                octaveDelta,
                residualCents);
            const float octavePenalty = octaveLike
                ? 0.24f * static_cast<float>(std::abs(octaveDelta))
                    * (1.0f - 0.70f * hypothesis.consensus)
                : 0.0f;

            const float historyWeight = onsetPending ? 0.24f : 0.72f;
            const float transitionScore = historyWeight * previous.score
                                        + proposal.score
                                        + continuityBonus
                                        - transitionPenalty
                                        - octavePenalty;
            if (transitionScore > bestTransitionScore)
            {
                bestTransitionScore = transitionScore;
                bestOctaveIndex = previous.octaveIndex
                    + (octaveLike ? octaveDelta : 0);
            }
        }

        if (foundPrevious)
            proposal.score = bestTransitionScore;
        proposal.octaveIndex = bestOctaveIndex;
        proposals[static_cast<std::size_t>(proposalCount++)] = proposal;
    }

    // A short hold branch prevents a single weak hop from forcing a jump.  It
    // decays quickly, so genuine new notes still win after fresh evidence.
    for (const auto& previous : decoderBeam_)
    {
        if (!previous.valid || proposalCount >= static_cast<int>(proposals.size()))
            continue;

        DecoderState held = previous;
        held.score = previous.score * (onsetPending ? 0.22f : 0.76f)
                   - (onsetPending ? 0.10f : 0.055f);
        ++held.ageInHops;
        if (held.ageInHops <= 4)
            proposals[static_cast<std::size_t>(proposalCount++)] = held;
    }

    std::sort(proposals.begin(),
              proposals.begin() + proposalCount,
              [](const DecoderState& left, const DecoderState& right)
              {
                  return left.score > right.score;
              });

    decoderBeam_.fill({});
    int accepted = 0;
    for (int proposalIndex = 0;
         proposalIndex < proposalCount && accepted < decoderBeamWidth;
         ++proposalIndex)
    {
        const auto& proposal = proposals[static_cast<std::size_t>(proposalIndex)];
        if (!proposal.valid)
            continue;

        bool duplicate = false;
        for (int existing = 0; existing < accepted; ++existing)
        {
            const float distance = static_cast<float>(1200.0
                * std::abs(proposal.logFrequency
                         - decoderBeam_[static_cast<std::size_t>(existing)].logFrequency));
            if (distance < 24.0f)
            {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
            decoderBeam_[static_cast<std::size_t>(accepted++)] = proposal;
    }
}

ModernPitchEngine::MultiRatePitchTracker::DecoderDecision
ModernPitchEngine::MultiRatePitchTracker::decodeCandidate(bool onsetPending) noexcept
{
    std::array<PitchCandidate, detectorPathCount> candidates {};
    const int candidateCount = collectFreshCandidates(candidates);
    if (candidateCount <= 0)
        return {};

    std::array<ConsensusHypothesis, maxConsensusHypotheses> hypotheses {};
    const int hypothesisCount = buildConsensusHypotheses(candidates,
                                                         candidateCount,
                                                         hypotheses);
    if (hypothesisCount <= 0)
        return {};

    updateDecoderBeam(hypotheses, hypothesisCount, onsetPending);
    if (!decoderBeam_[0].valid)
        return {};

    const float decodedFrequency = static_cast<float>(
        std::exp2(decoderBeam_[0].logFrequency));

    int matchedHypothesis = -1;
    float matchedDistance = 100000.0f;
    for (int index = 0; index < hypothesisCount; ++index)
    {
        const float distance = centsDistance(
            hypotheses[static_cast<std::size_t>(index)].frequencyHz,
            decodedFrequency);
        if (distance < matchedDistance)
        {
            matchedDistance = distance;
            matchedHypothesis = index;
        }
    }

    if (matchedHypothesis < 0 || matchedDistance > 65.0f)
        return {}; // the winning branch is only a decaying hold state

    const auto& hypothesis = hypotheses[static_cast<std::size_t>(matchedHypothesis)];
    DecoderDecision decision;
    decision.candidate.frequencyHz = hypothesis.frequencyHz;
    decision.candidate.confidence = clamp01(hypothesis.confidence
        * (0.76f + 0.24f * hypothesis.consensus));
    decision.candidate.periodicity = hypothesis.periodicity;
    decision.candidate.valid = true;
    decision.consensus = hypothesis.consensus;
    decision.supportCount = hypothesis.supportCount;
    decision.directSupportCount = hypothesis.directSupportCount;
    decision.freshSupportMask = hypothesis.freshSupportMask;
    decision.decoderOctaveIndex = decoderBeam_[0].octaveIndex;

    const bool closeToTrack = trackedPitchHz_ > 0.0f
        && centsDistance(trackedPitchHz_, decision.candidate.frequencyHz) < 95.0f;
    const bool sufficientInitialEvidence = decision.supportCount >= 2
        || decision.candidate.confidence >= 0.78f;
    decision.valid = closeToTrack || sufficientInitialEvidence;
    return decision;
}

bool ModernPitchEngine::MultiRatePitchTracker::confirmOctaveTransition(
    DecoderDecision& decision,
    bool onsetPending) noexcept
{
    if (!decision.valid || trackedPitchHz_ <= 0.0f)
    {
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        return decision.valid;
    }

    if (octaveCommitGuardHops_ > 0)
    {
        --octaveCommitGuardHops_;
        if (committedOctaveFrequencyHz_ > 0.0f
            && centsDistance(committedOctaveFrequencyHz_,
                             decision.candidate.frequencyHz) < 95.0f)
        {
            decision.decoderOctaveIndex = octaveState_;
            return true;
        }
    }

    int octaveDelta = 0;
    float residualCents = 0.0f;
    if (!isOctaveLikeTransition(trackedPitchHz_,
                                decision.candidate.frequencyHz,
                                octaveDelta,
                                residualCents))
    {
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        return true;
    }

    const bool samePending = pendingOctaveDelta_ == octaveDelta
        && pendingOctaveFrequencyHz_ > 0.0f
        && centsDistance(pendingOctaveFrequencyHz_,
                         decision.candidate.frequencyHz) < 70.0f;

    if (!samePending)
    {
        pendingOctaveDelta_ = octaveDelta;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = decision.candidate.frequencyHz;
    }

    // Count only genuinely refreshed evidence.  Reusing an old low-rate
    // candidate over several full-rate hops must not confirm a subharmonic.
    if (decision.freshSupportMask != 0)
        ++pendingOctaveCount_;

    int requiredObservations = octaveDelta < 0 ? 3 : 2;
    if (onsetPending && decision.supportCount >= 2
        && decision.directSupportCount >= 2
        && decision.consensus > 0.82f)
    {
        requiredObservations = 2;
    }

    const bool credibleConsensus = octaveDelta < 0
        ? (decision.directSupportCount >= 1
           && (decision.supportCount >= 2
               || (decision.candidate.confidence > 0.92f
                   && decision.consensus > 0.68f)))
        : (decision.supportCount >= 2
           || (decision.directSupportCount >= 1
               && decision.candidate.confidence > 0.90f
               && decision.consensus > 0.62f));

    if (!credibleConsensus || pendingOctaveCount_ < requiredObservations)
    {
        // Hold the committed octave while the challenger accumulates evidence.
        decision.candidate.frequencyHz = trackedPitchHz_;
        decision.candidate.confidence = trackedConfidence_ * 0.97f;
        decision.candidate.periodicity = trackedPeriodicity_;
        decision.consensus = trackedConsensus_;
        decision.supportCount = trackedSupportCount_;
        decision.decoderOctaveIndex = octaveState_;
        decision.valid = trackedPitchHz_ > 0.0f;
        return false;
    }

    octaveState_ = std::clamp(octaveState_ + octaveDelta, -4, 4);
    committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
    octaveCommitGuardHops_ = 12;
    pendingOctaveDelta_ = 0;
    pendingOctaveCount_ = 0;
    pendingOctaveFrequencyHz_ = 0.0f;
    decision.decoderOctaveIndex = octaveState_;
    return true;
}

bool ModernPitchEngine::MultiRatePitchTracker::processSample(
    float inputSample,
    PitchObservation& observation) noexcept
{
    observation = {};
    inputSample = sanitiseAudioSample(inputSample);

    const float dcBlocked = inputSample - previousInput_
                          + dcBlockCoefficient_ * previousDcOutput_;
    previousInput_ = inputSample;
    previousDcOutput_ = dcBlocked;

    const float energy = dcBlocked * dcBlocked;
    fastEnergy_ += fastEnergyCoefficient_ * (energy - fastEnergy_);
    slowEnergy_ += slowEnergyCoefficient_ * (energy - slowEnergy_);

    if (onsetCooldownSamples_ > 0)
        --onsetCooldownSamples_;

    const float energyRatio = fastEnergy_ / std::max(1.0e-9f, slowEnergy_);
    const float energeticEnough = fastEnergy_ > minimumDetectorRms * minimumDetectorRms * 3.0f;
    const float onsetStrength = clamp01((energyRatio - 1.8f) / 3.2f);

    onsetEnvelope_ = std::max(onsetStrength, onsetEnvelope_ * 0.985f);

    if (energeticEnough && energyRatio > 3.1f && onsetCooldownSamples_ == 0)
    {
        onsetPending_ = true;
        onsetCooldownSamples_ = std::max(1,
            static_cast<int>(std::lround(sampleRate_ * 0.010)));
    }

    push(fullRateRing_, fullRateWritePosition_, fullRateAvailableSamples_, dcBlocked);

    const float halfFiltered = halfRateAntiAlias_.process(dcBlocked);
    if (++halfRateDecimationCounter_ >= 2)
    {
        halfRateDecimationCounter_ = 0;
        push(halfRateRing_, halfRateWritePosition_, halfRateAvailableSamples_, halfFiltered);

        const float quarterFiltered = quarterRateAntiAlias_.process(halfFiltered);
        if (++quarterRateDecimationCounter_ >= 2)
        {
            quarterRateDecimationCounter_ = 0;
            push(quarterRateRing_, quarterRateWritePosition_,
                 quarterRateAvailableSamples_, quarterFiltered);

            const float eighthFiltered = eighthRateAntiAlias_.process(quarterFiltered);
            if (++eighthRateDecimationCounter_ >= 2)
            {
                eighthRateDecimationCounter_ = 0;
                push(eighthRateRing_, eighthRateWritePosition_,
                     eighthRateAvailableSamples_, eighthFiltered);
            }
        }
    }

    if (++hopCounter_ < detectorHop)
        return false;

    hopCounter_ = 0;
    ++analysisHopCounter_;

    ++fullRateCandidate_.ageInHops;
    ++halfRateCandidate_.ageInHops;
    ++quarterRateCandidate_.ageInHops;
    ++eighthRateCandidate_.ageInHops;

    const float fullMinimum = std::max(160.0f, minimumPitchHz_);
    const float fullMaximum = std::min(maximumPitchHz_, 2600.0f);
    if (fullMinimum < fullMaximum)
    {
        fullRateCandidate_.candidate = analyse(fullRateRing_,
                                               fullRateWritePosition_,
                                               fullRateAvailableSamples_,
                                               sampleRate_,
                                               fullMinimum,
                                               fullMaximum,
                                               standardAnalysisSize);
        fullRateCandidate_.candidate.pathIndex = 0;
        fullRateCandidate_.candidate.ageInHops = 0;
        fullRateCandidate_.ageInHops = 0;
    }

    if ((analysisHopCounter_ & 1) == 0)
    {
        const float halfMinimum = std::max(78.0f, minimumPitchHz_);
        const float halfMaximum = std::min(maximumPitchHz_, 900.0f);
        if (halfMinimum < halfMaximum)
        {
            halfRateCandidate_.candidate = analyse(halfRateRing_,
                                                   halfRateWritePosition_,
                                                   halfRateAvailableSamples_,
                                                   sampleRate_ * 0.5,
                                                   halfMinimum,
                                                   halfMaximum,
                                                   standardAnalysisSize);
            halfRateCandidate_.candidate.pathIndex = 1;
            halfRateCandidate_.candidate.ageInHops = 0;
            halfRateCandidate_.ageInHops = 0;
        }
    }

    if ((analysisHopCounter_ & 3) == 0)
    {
        const float quarterMinimum = std::max(35.0f, minimumPitchHz_);
        const float quarterMaximum = std::min(maximumPitchHz_, 460.0f);
        if (quarterMinimum < quarterMaximum)
        {
            quarterRateCandidate_.candidate = analyse(quarterRateRing_,
                                                      quarterRateWritePosition_,
                                                      quarterRateAvailableSamples_,
                                                      sampleRate_ * 0.25,
                                                      quarterMinimum,
                                                      quarterMaximum,
                                                      384);
            quarterRateCandidate_.candidate.pathIndex = 2;
            quarterRateCandidate_.candidate.ageInHops = 0;
            quarterRateCandidate_.ageInHops = 0;
        }
    }

    if ((analysisHopCounter_ & 7) == 0)
    {
        const float eighthMinimum = std::max(25.0f, minimumPitchHz_);
        const float eighthMaximum = std::min(maximumPitchHz_, 230.0f);
        if (eighthMinimum < eighthMaximum)
        {
            eighthRateCandidate_.candidate = analyse(eighthRateRing_,
                                                     eighthRateWritePosition_,
                                                     eighthRateAvailableSamples_,
                                                     sampleRate_ * 0.125,
                                                     eighthMinimum,
                                                     eighthMaximum,
                                                     maxAnalysisSize);
            eighthRateCandidate_.candidate.pathIndex = 3;
            eighthRateCandidate_.candidate.ageInHops = 0;
            eighthRateCandidate_.ageInHops = 0;
        }
    }

    DecoderDecision decision = decodeCandidate(onsetPending_);
    const int previousOctaveState = octaveState_;
    const bool decoderDecisionAccepted = confirmOctaveTransition(decision,
                                                                  onsetPending_);
    const bool committedOctaveChange = octaveState_ != previousOctaveState;

    if (decision.valid && decision.candidate.valid)
    {
        const bool firstLock = trackedPitchHz_ <= 0.0f;
        const float selectedLog = std::log2(decision.candidate.frequencyHz);
        const float trackedLog = firstLock ? selectedLog : std::log2(trackedPitchHz_);

        // An onset may move faster than a stable note, but it no longer bypasses
        // the decoder.  Confirmed octave changes remain intentionally smoother
        // to avoid a low-frequency burst when the decision is first committed.
        float smoothing = 0.32f;
        if (firstLock)
            smoothing = 1.0f;
        else if (committedOctaveChange)
            smoothing = 0.82f;
        else if (onsetPending_)
            smoothing = decoderDecisionAccepted ? 0.58f : 0.36f;

        trackedPitchHz_ = std::exp2(trackedLog
            + smoothing * (selectedLog - trackedLog));
        trackedConfidence_ += 0.38f
            * (decision.candidate.confidence - trackedConfidence_);
        trackedPeriodicity_ += 0.38f
            * (decision.candidate.periodicity - trackedPeriodicity_);
        trackedConsensus_ += 0.35f
            * (decision.consensus - trackedConsensus_);
        trackedSupportCount_ = decision.supportCount;
        invalidHopCount_ = 0;

        const float rmsGate = smoothStep(minimumDetectorRms,
                                         minimumDetectorRms * 4.0f,
                                         std::sqrt(std::max(0.0f, slowEnergy_)));
        const float confidenceGate = smoothStep(0.42f, 0.88f, trackedConfidence_);
        const float periodicityGate = smoothStep(0.48f, 0.90f, trackedPeriodicity_);
        const float consensusGate = smoothStep(0.10f, 0.78f, trackedConsensus_);

        observation.frequencyHz = trackedPitchHz_;
        observation.confidence = trackedConfidence_;
        observation.periodicity = trackedPeriodicity_;
        observation.consensus = trackedConsensus_;
        observation.detectorSupport = trackedSupportCount_;
        observation.octaveState = octaveState_;
        observation.pendingOctaveObservations = pendingOctaveCount_;
        observation.voicing = clamp01(rmsGate
            * (0.48f * confidenceGate
             + 0.30f * periodicityGate
             + 0.22f * consensusGate));
        observation.valid = observation.voicing > 0.08f;
    }
    else
    {
        ++invalidHopCount_;
        trackedConfidence_ *= 0.90f;
        trackedPeriodicity_ *= 0.90f;
        trackedConsensus_ *= 0.88f;

        if (invalidHopCount_ > 12)
        {
            trackedPitchHz_ = 0.0f;
            trackedConfidence_ = 0.0f;
            trackedPeriodicity_ = 0.0f;
            trackedConsensus_ = 0.0f;
            trackedSupportCount_ = 0;
            decoderBeam_.fill({});
            pendingOctaveDelta_ = 0;
            pendingOctaveCount_ = 0;
            pendingOctaveFrequencyHz_ = 0.0f;
        }

        observation.frequencyHz = trackedPitchHz_;
        observation.confidence = trackedConfidence_;
        observation.periodicity = trackedPeriodicity_;
        observation.consensus = trackedConsensus_;
        observation.detectorSupport = trackedSupportCount_;
        observation.octaveState = octaveState_;
        observation.pendingOctaveObservations = pendingOctaveCount_;
        observation.voicing = 0.0f;
        observation.valid = false;
    }

    observation.onset = onsetPending_;
    observation.onsetStrength = onsetPending_ ? std::max(0.65f, onsetEnvelope_)
                                             : onsetEnvelope_;
    onsetPending_ = false;
    return true;
}

//==============================================================================
// ScaleQuantizer

std::uint64_t ModernPitchEngine::ScaleQuantizer::hashScale(
    const double* scaleRatios,
    int numberOfScaleRatios,
    double rootFrequency) noexcept
{
    constexpr std::uint64_t offsetBasis = 1469598103934665603ull;
    std::uint64_t hash = offsetBasis;

    const auto mix = [&hash](std::uint64_t value)
    {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    const int safeCount = std::clamp(numberOfScaleRatios, 0, maxScaleRatios);
    mix(static_cast<std::uint64_t>(safeCount));

    std::uint64_t rootBits = 0;
    std::memcpy(&rootBits, &rootFrequency, sizeof(rootBits));
    mix(rootBits);

    for (int index = 0; index < safeCount; ++index)
    {
        const double ratio = scaleRatios != nullptr ? scaleRatios[index] : 0.0;
        std::uint64_t ratioBits = 0;
        std::memcpy(&ratioBits, &ratio, sizeof(ratioBits));
        mix(ratioBits);
    }

    return hash;
}

bool ModernPitchEngine::ScaleQuantizer::update(const double* scaleRatios,
                                                int numberOfScaleRatios,
                                                double rootFrequency) noexcept
{
    if (scaleRatios == nullptr || numberOfScaleRatios <= 0
        || !std::isfinite(rootFrequency) || rootFrequency <= 0.0)
    {
        const bool changed = cachedScaleSize_ != 0;
        cachedScaleSize_ = 0;
        targetValid_ = false;
        scaleHash_ = 0;
        return changed;
    }

    const int safeCount = std::clamp(numberOfScaleRatios, 1, maxScaleRatios);
    const std::uint64_t newHash = hashScale(scaleRatios, safeCount, rootFrequency);
    if (newHash == scaleHash_)
        return false;

    scaleHash_ = newHash;
    rootLog2_ = safeLog2(rootFrequency);
    cachedScaleSize_ = 0;

    for (int index = 0; index < safeCount; ++index)
    {
        double ratio = scaleRatios[index];
        if (!std::isfinite(ratio) || ratio <= 0.0)
            continue;

        while (ratio < 1.0)
            ratio *= 2.0;
        while (ratio >= 2.0)
            ratio *= 0.5;

        cachedScaleLogRatios_[static_cast<std::size_t>(cachedScaleSize_++)]
            = std::log2(ratio);
    }

    std::sort(cachedScaleLogRatios_.begin(),
              cachedScaleLogRatios_.begin() + cachedScaleSize_);

    int uniqueCount = 0;
    for (int index = 0; index < cachedScaleSize_; ++index)
    {
        const double value = cachedScaleLogRatios_[static_cast<std::size_t>(index)];
        if (uniqueCount == 0
            || std::abs(value
                        - cachedScaleLogRatios_[static_cast<std::size_t>(uniqueCount - 1)])
                   > 1.0e-8)
        {
            cachedScaleLogRatios_[static_cast<std::size_t>(uniqueCount++)] = value;
        }
    }

    cachedScaleSize_ = uniqueCount;
    targetValid_ = false;
    return true;
}

void ModernPitchEngine::ScaleQuantizer::resetTarget() noexcept
{
    targetValid_ = false;
}

void ModernPitchEngine::ScaleQuantizer::forceTargetLog2(double targetLog2) noexcept
{
    currentTargetLog2_ = targetLog2;
    targetValid_ = true;
}

double ModernPitchEngine::ScaleQuantizer::chooseTargetLog2(double inputLog2,
                                                            float hysteresisCents) noexcept
{
    if (cachedScaleSize_ <= 0 || !std::isfinite(inputLog2))
        return inputLog2;

    const double relativeLog = inputLog2 - rootLog2_;
    const double baseOctave = std::floor(relativeLog);
    double bestTarget = inputLog2;
    double bestDistance = std::numeric_limits<double>::max();

    for (int index = 0; index < cachedScaleSize_; ++index)
    {
        const double scalePosition = cachedScaleLogRatios_[static_cast<std::size_t>(index)];
        for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset)
        {
            const double target = rootLog2_ + baseOctave
                                + static_cast<double>(octaveOffset)
                                + scalePosition;
            const double distance = std::abs(target - inputLog2);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestTarget = target;
            }
        }
    }

    if (targetValid_)
    {
        const double previousDistance = std::abs(currentTargetLog2_ - inputLog2);
        const double hysteresisOctaves = std::max(0.0f, hysteresisCents) / 1200.0;
        if (previousDistance <= bestDistance + hysteresisOctaves)
            bestTarget = currentTargetLog2_;
    }

    currentTargetLog2_ = bestTarget;
    targetValid_ = true;
    return currentTargetLog2_;
}

//==============================================================================
// CorrectionController

void ModernPitchEngine::CorrectionController::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::max(8000.0, sampleRate);

    authorityAttackCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.006 * sampleRate_)));
    authorityReleaseCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.035 * sampleRate_)));
    wetAttackCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.010 * sampleRate_)));
    wetReleaseCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.055 * sampleRate_)));

    reset();
}

void ModernPitchEngine::CorrectionController::reset() noexcept
{
    state_ = TrackingState::unvoiced;
    stateSamplesRemaining_ = 0;
    stableObservationCount_ = 0;
    invalidObservationCount_ = 0;
    observedLog2_ = 0.0;
    pitchCentreLog2_ = 0.0;
    targetLog2_ = 0.0;
    pitchCentreValid_ = false;
    targetValid_ = false;
    desiredCorrectionCents_ = 0.0;
    synthesisTargetCorrectionCents_ = 0.0;
    currentCorrectionCents_ = 0.0;
    correctionVelocityCentsPerSecond_ = 0.0;
    targetRevision_ = 0;
    currentConfidence_ = 0.0f;
    currentVoicing_ = 0.0f;
    currentOnsetStrength_ = 0.0f;
    spectralBreathiness_ = 0.0f;
    spectralHarmonicity_ = 1.0f;
    spectralPolyphony_ = 0.0f;
    spectralReliability_ = 1.0f;
    authority_ = 0.0f;
    authorityTarget_ = 0.0f;
    wetMix_ = 0.0f;
    wetMixTarget_ = 0.0f;
    smoothedVoicing_ = 0.0f;
    voicedLatched_ = false;
    voicedEnterCount_ = 0;
    voicedExitCount_ = 0;
    revisionCandidateValid_ = false;
    revisionCandidateLog2_ = 0.0;
    revisionCandidateCount_ = 0;
}

void ModernPitchEngine::CorrectionController::setSpectralReliability(
    float breathiness,
    float harmonicity,
    float polyphony,
    float spectralReliability) noexcept
{
    spectralBreathiness_ = clamp01(breathiness);
    spectralHarmonicity_ = clamp01(harmonicity);
    spectralPolyphony_ = clamp01(polyphony);
    spectralReliability_ = clamp01(spectralReliability);
}

void ModernPitchEngine::CorrectionController::enterState(TrackingState newState,
                                                          int durationSamples) noexcept
{
    state_ = newState;
    stateSamplesRemaining_ = std::max(0, durationSamples);
}

void ModernPitchEngine::CorrectionController::updateVoicingLatch(
    bool observationUsable,
    float voicing,
    float sensitivity) noexcept
{
    const float safeSensitivity = clamp01(sensitivity);
    const float targetVoicing = observationUsable ? clamp01(voicing) : 0.0f;

    // This is deliberately evaluated only when a new pitch observation arrives.
    // A relatively fast attack and a slower release prevent noisy vowels from
    // repeatedly switching the corrected path on and off.
    const float smoothing = targetVoicing > smoothedVoicing_ ? 0.42f : 0.18f;
    smoothedVoicing_ += smoothing * (targetVoicing - smoothedVoicing_);

    const float enterThreshold = 0.60f - 0.10f * safeSensitivity;
    const float exitThreshold = 0.38f - 0.06f * safeSensitivity;

    if (!voicedLatched_)
    {
        voicedExitCount_ = 0;
        if (observationUsable && smoothedVoicing_ >= enterThreshold)
            ++voicedEnterCount_;
        else
            voicedEnterCount_ = 0;

        if (voicedEnterCount_ >= 2)
        {
            voicedLatched_ = true;
            voicedEnterCount_ = 0;
        }
    }
    else
    {
        voicedEnterCount_ = 0;
        if (!observationUsable || smoothedVoicing_ <= exitThreshold)
            ++voicedExitCount_;
        else
            voicedExitCount_ = 0;

        if (voicedExitCount_ >= 4)
        {
            voicedLatched_ = false;
            voicedExitCount_ = 0;
        }
    }
}

float ModernPitchEngine::CorrectionController::confidenceAuthority(
    float confidence,
    float sensitivity) const noexcept
{
    const float safeSensitivity = clamp01(sensitivity);
    const float low = 0.62f - 0.20f * safeSensitivity;
    const float high = 0.90f - 0.08f * safeSensitivity;
    return smoothStep(low, high, confidence);
}

double ModernPitchEngine::CorrectionController::sanitisedMinStepCents(const Parameters& parameters) noexcept
{
    double minStep = static_cast<double>(parameters.minScaleStepCents);
    if (!std::isfinite(minStep) || minStep <= 0.0)
    {
        const int safeSize = std::max(1, parameters.scaleSize);
        minStep = 1200.0 / static_cast<double>(safeSize);
    }
    return std::clamp(minStep, 0.1, 1200.0);
}

double ModernPitchEngine::CorrectionController::scaleLockRevisionThresholdCents(const Parameters& parameters,
                                                                                float strictness,
                                                                                float vibratoProtection) noexcept
{
    const double minStep = sanitisedMinStepCents(parameters);
    const double baseTolerance = minStep * 0.45;
    const double vibratoAllowance = minStep * 0.50 * static_cast<double>(vibratoProtection);
    const double tightTolerance = std::min(baseTolerance, minStep * 0.28);
    const double hardTolerance = tightTolerance + (baseTolerance - tightTolerance) * (1.0 - strictness);

    return parameters.hardLockActive
        ? hardTolerance + vibratoAllowance
        : baseTolerance + vibratoAllowance;
}

double ModernPitchEngine::CorrectionController::scaleLockTransitionThresholdCents(const Parameters& parameters,
                                                                                  float strictness,
                                                                                  float vibratoProtection) noexcept
{
    const double revisionThreshold = scaleLockRevisionThresholdCents(parameters, strictness, vibratoProtection);
    return revisionThreshold + 6.0;
}

double ModernPitchEngine::CorrectionController::guardScaleLockTarget(double candidateLog2,
                                                                     const PitchObservation& observation,
                                                                     const Parameters& parameters,
                                                                     double minStepCents,
                                                                     float strictness,
                                                                     bool hardScaleLock) noexcept
{
    if (!targetValid_ || !hardScaleLock)
    {
        revisionCandidateValid_ = false;
        return candidateLog2;
    }

    const double jumpCents = std::abs((candidateLog2 - targetLog2_) * 1200.0);
    const double vibratoProtection = clamp01(parameters.vibratoPreserve);
    const double dynamicThreshold = scaleLockRevisionThresholdCents(parameters, strictness, static_cast<float>(vibratoProtection));

    if (jumpCents > dynamicThreshold)
    {
        if (!revisionCandidateValid_ || std::abs((candidateLog2 - revisionCandidateLog2_) * 1200.0) > 2.0)
        {
            revisionCandidateLog2_ = candidateLog2;
            revisionCandidateCount_ = 1;
            revisionCandidateValid_ = true;
        }
        else
        {
            ++revisionCandidateCount_;
        }

        const int requiredConfirmations = 2 + static_cast<int>(strictness * 2.0f);
        if (revisionCandidateCount_ >= requiredConfirmations)
        {
            revisionCandidateValid_ = false;
            return candidateLog2;
        }

        return targetLog2_;
    }

    revisionCandidateValid_ = false;
    return candidateLog2;
}

float ModernPitchEngine::CorrectionController::getFormantStability() const noexcept
{
    // During attacks and note changes the spectral envelope is less reliable.
    // Reducing, rather than disabling, formant compensation avoids a timbral
    // discontinuity while the new trajectory settles.
    switch (state_)
    {
        case TrackingState::unvoiced:   return 0.45f;
        case TrackingState::attack:     return 0.35f;
        case TrackingState::acquire:    return 0.68f;
        case TrackingState::stable:     return 1.00f;
        case TrackingState::transition: return 0.58f;
        case TrackingState::release:    return 0.65f;
    }

    return 1.0f;
}

void ModernPitchEngine::CorrectionController::acceptObservation(
    const PitchObservation& observation,
    ScaleQuantizer& quantizer,
    const Parameters& parameters) noexcept
{
    currentConfidence_ = clamp01(observation.confidence);
    currentVoicing_ = clamp01(observation.voicing);
    currentOnsetStrength_ = clamp01(observation.onsetStrength);
    const float lockStrictness = parameters.scaleLock && parameters.hardLockActive
    ? clamp01(parameters.lockStrictness)
    : 0.0f;
const bool hardScaleLock = parameters.scaleLock && parameters.hardLockActive && lockStrictness > 0.001f;

    const float minimumUsableVoicing = 0.07f + 0.12f
        * (1.0f - clamp01(parameters.detectorSensitivity));
    const bool observationUsable = observation.valid
                                && observation.frequencyHz > 0.0f
                                && currentVoicing_ >= minimumUsableVoicing
                                && quantizer.hasScale();

    updateVoicingLatch(observationUsable,
                       currentVoicing_,
                       parameters.detectorSensitivity);

    if (observation.onset)
    {
        const float protection = clamp01(parameters.transientProtection);
        const double attackMs = 1.5 + 8.5 * static_cast<double>(protection);
        enterState(TrackingState::attack,
                   std::max(1, static_cast<int>(std::lround(
                       attackMs * 0.001 * sampleRate_))));
        stableObservationCount_ = 0;
        invalidObservationCount_ = 0;
        pitchCentreValid_ = false;
        targetValid_ = false;
        quantizer.resetTarget();

        // Do not hard-mute the wet path here. The state machine and the
        // asymmetric release below fade it smoothly, preserving continuity.
    }

    if (!observationUsable)
    {
        ++invalidObservationCount_;
        stableObservationCount_ = 0;

        // Short confidence drop-outs are common inside vowels. Hold the last
        // musical decision for a few observations instead of immediately
        // returning the shifter to ratio 1, which sounds like wind/pumping.
        if (invalidObservationCount_ <= 3)
        {
            // Keep the musical target through short detector drop-outs, but do
            // not preserve noisy pitch modulation when the spectral path says
            // that the tail is predominantly breath/residual.
            const float breathRelease = 1.0f - 0.20f * spectralBreathiness_;
            authorityTarget_ *= 0.94f * breathRelease;
            wetMixTarget_ *= 0.96f;
            return;
        }

        authorityTarget_ = 0.0f;
        wetMixTarget_ = 0.0f;
        desiredCorrectionCents_ = 0.0;

        if (invalidObservationCount_ > 3 && state_ != TrackingState::attack)
            enterState(TrackingState::release,
                       std::max(1, static_cast<int>(std::lround(0.040 * sampleRate_))));
        if (invalidObservationCount_ > 18)
        {
            enterState(TrackingState::unvoiced);
            pitchCentreValid_ = false;
            targetValid_ = false;
        }
        return;
    }

    invalidObservationCount_ = 0;
    observedLog2_ = safeLog2(observation.frequencyHz);

    if (!pitchCentreValid_ || observation.onset)
    {
        pitchCentreLog2_ = observedLog2_;
        pitchCentreValid_ = true;
    }
    else
    {
        const double distanceCents = std::abs((observedLog2_ - pitchCentreLog2_) * 1200.0);
        const double baseCentreAlpha = distanceCents > 90.0 ? 0.32 : 0.10;
        const double breathStability = std::clamp(
            1.0 - 0.72 * static_cast<double>(spectralBreathiness_),
            0.20,
            1.0);
        const double harmonicStability = 0.55
            + 0.45 * static_cast<double>(spectralHarmonicity_);
        const double centreAlpha = baseCentreAlpha
            * breathStability
            * harmonicStability;
        pitchCentreLog2_ += centreAlpha * (observedLog2_ - pitchCentreLog2_);
    }

    double hysteresisCents = 18.0f + 38.0f * clamp01(parameters.humanize);
    double targetBoundaryCents = hysteresisCents;
    
    if (parameters.scaleLock)
    {
        ScaleLock::Parameters slParams;
        slParams.userHysteresis = parameters.lockHysteresis;
        slParams.vibratoAmount = parameters.vibratoPreserve;
        slParams.humanize = parameters.humanize;
        slParams.latencyMode = parameters.latencyMode;
        slParams.confidence = currentConfidence_;
        slParams.breathiness = spectralBreathiness_;
slParams.stability = clamp01(0.45f * spectralReliability_
                            + 0.35f * spectralHarmonicity_
                            + 0.20f * observation.consensus);
slParams.periodicity = clamp01(observation.periodicity);
slParams.strictness = lockStrictness;
slParams.hardLock = hardScaleLock;
        slParams.tempoLockActive = (parameters.tempo.mode == CreativeTempo::Mode::glideLock);
        slParams.scaleSize = parameters.scaleSize > 0 ? parameters.scaleSize : 12;
    slParams.minScaleStepCents = (std::isfinite(parameters.minScaleStepCents)
                              && parameters.minScaleStepCents > 0.0f)
    ? parameters.minScaleStepCents
    : 100.0f;
        
        
        hysteresisCents = scaleLockProcessor_.calculateHysteresis(slParams);
        targetBoundaryCents = hysteresisCents;
    }

    double newTargetLog2 = quantizer.chooseTargetLog2(pitchCentreLog2_, hysteresisCents);

    if (parameters.scaleLock)
    {
        const double effectiveMinStep = sanitisedMinStepCents(parameters);
        newTargetLog2 = guardScaleLockTarget(newTargetLog2,
                                             observation,
                                             parameters,
                                             effectiveMinStep,
                                             lockStrictness,
                                             hardScaleLock);
        quantizer.forceTargetLog2(newTargetLog2);
    }

    // Defensive register lock. Scale degrees repeat every octave, therefore
    // the selected target must always be the octave-equivalent target nearest
    // to the tracked pitch centre. This prevents a stale octave state or a
    // custom-scale edge case from publishing an accidental -/+1200-cent move.
    newTargetLog2 = alignTargetToNearestOctave(newTargetLog2,
                                               pitchCentreLog2_);

    const double targetJumpCents = targetValid_
        ? std::abs((newTargetLog2 - targetLog2_) * 1200.0)
        : std::numeric_limits<double>::infinity();

    // A revision marks a real musical target change, independently from the
    // per-sample correction trajectory. The downstream TransitionManager uses
    // it to arm the second synthesis layer exactly once per note decision.
    // 18 cents keeps microtonal steps eligible while rejecting detector jitter.
    double revisionThreshold = 18.0;
    double transitionThreshold = 48.0;

    if (parameters.scaleLock)
    {
        const double vibratoProtection = clamp01(parameters.vibratoPreserve);
        revisionThreshold = scaleLockRevisionThresholdCents(parameters, lockStrictness, static_cast<float>(vibratoProtection));
        transitionThreshold = scaleLockTransitionThresholdCents(parameters, lockStrictness, static_cast<float>(vibratoProtection));
    }

    if (!targetValid_ || targetJumpCents > revisionThreshold)
        ++targetRevision_;

    if (targetValid_ && targetJumpCents > transitionThreshold
        && state_ != TrackingState::attack)
    {
        const double transitionMs = std::max(1.0f, parameters.transitionTimeMs);
        enterState(TrackingState::transition,
                   std::max(1, static_cast<int>(std::lround(
                       transitionMs * 0.001 * sampleRate_))));
        stableObservationCount_ = 0;
    }

    targetLog2_ = newTargetLog2;
    targetValid_ = true;

    if (parameters.scaleLock)
    {
        ScaleLock::Parameters slParams;
        slParams.userHysteresis = parameters.lockHysteresis;
        slParams.vibratoAmount = parameters.vibratoPreserve;
        slParams.humanize = parameters.humanize;
        slParams.latencyMode = parameters.latencyMode;
        slParams.confidence = currentConfidence_;
        slParams.breathiness = spectralBreathiness_;
slParams.stability = clamp01(0.45f * spectralReliability_
                            + 0.35f * spectralHarmonicity_
                            + 0.20f * observation.consensus);
slParams.periodicity = clamp01(observation.periodicity);
slParams.strictness = lockStrictness;
slParams.hardLock = hardScaleLock;
        slParams.tempoLockActive = (parameters.tempo.mode == CreativeTempo::Mode::glideLock);
        slParams.scaleSize = parameters.scaleSize > 0 ? parameters.scaleSize : 12;
    slParams.minScaleStepCents = (std::isfinite(parameters.minScaleStepCents)
                              && parameters.minScaleStepCents > 0.0f)
    ? parameters.minScaleStepCents
    : 100.0f;
        
        
        ScaleLock::ProcessResult res = scaleLockProcessor_.process(
            observation.frequencyHz > 0.0f ? std::log2(observation.frequencyHz) : 0.0,
            targetLog2_,
            pitchCentreLog2_,
            slParams,
            sampleRate_);
            
        double errorCents = res.targetCorrectionCents;
        const double maxCorrectionCents = 1200.0 * std::clamp(
            static_cast<double>(parameters.maximumCorrectionSemitones), 0.0, 24.0);
        errorCents = std::clamp(errorCents, -maxCorrectionCents, maxCorrectionCents);
        desiredCorrectionCents_ = neumatonApplyAmountToleranceGate(
            errorCents,
            parameters.amount,
            currentConfidence_,
            observation.consensus);
    }
    else
    {
        const double vibratoComponent = observedLog2_ - pitchCentreLog2_;
        const float cleanBreath = 1.0f - spectralBreathiness_;
        const float vibratoReliability = clamp01(
            spectralHarmonicity_ * cleanBreath * cleanBreath);
        const float effectivePreserveVibrato =
            clamp01(parameters.preserveVibrato) * vibratoReliability;
        const double correctedLog2 = targetLog2_
            + static_cast<double>(effectivePreserveVibrato) * vibratoComponent;
        double errorCents = (correctedLog2 - observedLog2_) * 1200.0;
        errorCents = wrapCorrectionToNearestOctave(errorCents);

        const double deadBandCents = 1.5 + 20.0 * static_cast<double>(clamp01(parameters.humanize));
        if (std::abs(errorCents) <= deadBandCents)
        {
            errorCents = 0.0;
        }
        else
        {
            errorCents = std::copysign(std::abs(errorCents) - deadBandCents,
                                       errorCents);
        }

        const double maxCorrectionCents = 1200.0 * std::clamp(
            static_cast<double>(parameters.maximumCorrectionSemitones), 0.0, 24.0);
        errorCents = std::clamp(errorCents,
                                -maxCorrectionCents,
                                maxCorrectionCents);
        desiredCorrectionCents_ = neumatonApplyAmountToleranceGate(
            errorCents,
            parameters.amount,
            currentConfidence_,
            observation.consensus);
    }

    const float confidenceGate = confidenceAuthority(currentConfidence_,
                                                      parameters.detectorSensitivity);
    // Reliability is a safety gate, not a permanent loss of correction
    // strength.  Clean monophonic frames pass at unity; only clearly weak
    // consensus, noise-dominant spectra or competing pitch families retreat
    // toward the aligned dry path.
    const float consensusGate = 0.30f + 0.70f
        * smoothStep(0.08f, 0.48f, observation.consensus);
    const float spectralGate = 0.25f + 0.75f
        * smoothStep(0.22f, 0.68f, spectralReliability_);
    const float polyphonyGate = 1.0f - 0.82f
        * smoothStep(0.12f, 0.65f, spectralPolyphony_);
    authorityTarget_ = clamp01(confidenceGate
                               * (voicedLatched_ ? smoothedVoicing_ : 0.0f)
                               * consensusGate
                               * spectralGate
                               * polyphonyGate);

    const float correctionNeed = smoothStep(
        1.5f, 10.0f, static_cast<float>(std::abs(desiredCorrectionCents_)));
    const float transientAttenuation = 1.0f
        - clamp01(parameters.transientProtection) * currentOnsetStrength_;

    wetMixTarget_ = clamp01((voicedLatched_ ? smoothedVoicing_ : 0.0f)
                            * correctionNeed
                            * transientAttenuation
                            * consensusGate
                            * spectralGate
                            * polyphonyGate);

    // NEUMATON_RECONSTRUCTIVE_WET_V1_SEVERITY
    // Amount and Speed define correction severity, not just correction depth.
    // Humanize is intentionally excluded: Humanize shapes naturalness, while
    // S(amount, speed) decides whether the whole wet spectrum must be rebuilt
    // toward the requested target.
    const float correctionAssertiveness = neumatonCorrectionSeverityFromAmountSpeed(
        parameters.amount,
        parameters.retuneTimeMs);
    const float correctionPresent = smoothStep(
        1.0f, 8.0f, static_cast<float>(std::abs(desiredCorrectionCents_)));
    const float scaleLockAuthority = parameters.scaleLock
        ? std::max(0.82f, correctionAssertiveness)
        : correctionAssertiveness;
    const float hardCorrectionIntent = clamp01(scaleLockAuthority * correctionPresent);

    // In assertive settings, consensus/spectral/polyphony gates remain safety
    // rails, but they must not make a clearly requested correction timid.
    const float voicedGateForAuthority = voicedLatched_ ? smoothedVoicing_ : 0.0f;
    const float hardSafetyGate = clamp01(
        voicedGateForAuthority
        * confidenceGate
        * (0.70f + 0.30f * observation.consensus)
        * (1.0f - 0.45f * smoothStep(0.35f, 0.80f, spectralPolyphony_)));
    const float hardTransientGate = 1.0f
        - 0.35f * clamp01(parameters.transientProtection) * currentOnsetStrength_;

    const float hardAuthorityTarget = clamp01(hardCorrectionIntent * hardSafetyGate);
    const float hardWetTarget = clamp01(hardCorrectionIntent * hardSafetyGate * hardTransientGate);

    authorityTarget_ = std::max(authorityTarget_, hardAuthorityTarget);
    wetMixTarget_ = std::max(wetMixTarget_, hardWetTarget);

    if (parameters.scaleLock)
    {
        // NEUMATON_FULL_SPECTRUM_TRANSPORT_V2_SCALELOCK_GATE_BLEND
        // hardBlend = 0 -> previous cautious Scale Lock gate.
        // hardBlend = 1 -> no extra Scale Lock timidity: the target scale is
        // a contract, while safety is handled by confidence/voicing gates.
        const float slConfidence = clamp01(currentConfidence_ + 0.15f * lockStrictness);
        const float slGate = 0.50f + 0.50f * slConfidence;
        const float hardBlend = hardCorrectionIntent;
        const float blendedSlGate = slGate + hardBlend * (1.0f - slGate);

        authorityTarget_ = clamp01(authorityTarget_ * blendedSlGate);
        wetMixTarget_ = clamp01(wetMixTarget_ * blendedSlGate);
    }

    ++stableObservationCount_;
    if (state_ == TrackingState::unvoiced || state_ == TrackingState::release)
        enterState(TrackingState::acquire,
                   std::max(1, static_cast<int>(std::lround(0.008 * sampleRate_))));
    else if (state_ == TrackingState::acquire && stableObservationCount_ >= 3)
        enterState(TrackingState::stable);
}

void ModernPitchEngine::CorrectionController::advanceOneSample(
    const Parameters& parameters) noexcept
{
    if (stateSamplesRemaining_ > 0)
    {
        --stateSamplesRemaining_;
        if (stateSamplesRemaining_ == 0)
        {
            if (state_ == TrackingState::attack)
                enterState(TrackingState::acquire,
                           std::max(1, static_cast<int>(std::lround(0.007 * sampleRate_))));
            else if (state_ == TrackingState::transition)
                enterState(TrackingState::stable);
            else if (state_ == TrackingState::release)
                enterState(TrackingState::unvoiced);
        }
    }

    float stateAuthorityScale = 1.0f;
    switch (state_)
    {
        case TrackingState::unvoiced:   stateAuthorityScale = 0.0f; break;
        case TrackingState::attack:     stateAuthorityScale = 0.10f; break;
        case TrackingState::acquire:    stateAuthorityScale = 0.68f; break;
        case TrackingState::stable:     stateAuthorityScale = 1.0f; break;
        case TrackingState::transition: stateAuthorityScale = 0.82f; break;
        case TrackingState::release:    stateAuthorityScale = 0.20f; break;
    }

    const float effectiveAuthorityTarget = authorityTarget_ * stateAuthorityScale;
    const float authorityCoefficient = effectiveAuthorityTarget > authority_
        ? authorityAttackCoefficient_
        : authorityReleaseCoefficient_;
    authority_ += authorityCoefficient * (effectiveAuthorityTarget - authority_);

    const float effectiveWetTarget = wetMixTarget_ * stateAuthorityScale;
    const float wetCoefficient = effectiveWetTarget > wetMix_
        ? wetAttackCoefficient_
        : wetReleaseCoefficient_;
    wetMix_ += wetCoefficient * (effectiveWetTarget - wetMix_);

    const double targetCorrectionCents = desiredCorrectionCents_
                                       * static_cast<double>(authority_);
    synthesisTargetCorrectionCents_ = targetCorrectionCents;

    double responseMs = std::max(0.35, static_cast<double>(parameters.retuneTimeMs));
    if (state_ == TrackingState::transition)
        responseMs = std::max(responseMs,
                              static_cast<double>(parameters.transitionTimeMs));
    else if (state_ == TrackingState::acquire)
        responseMs = std::max(responseMs, 4.0);

    const double dt = 1.0 / sampleRate_;
    const double responseSeconds = responseMs * 0.001;
    const double omega = std::min(0.22 / dt, 4.6 / std::max(0.00035, responseSeconds));

    double acceleration = omega * omega
                            * (targetCorrectionCents - currentCorrectionCents_)
                        - 2.0 * omega * correctionVelocityCentsPerSecond_;

    const double maximumVelocity = std::max(2400.0,
        8.0 * std::max(1200.0, std::abs(targetCorrectionCents))
            / std::max(0.001, responseSeconds));
    const double maximumAcceleration = maximumVelocity
                                     / std::max(0.0005, responseSeconds * 0.35);
    acceleration = std::clamp(acceleration,
                              -maximumAcceleration,
                              maximumAcceleration);

    correctionVelocityCentsPerSecond_ += acceleration * dt;
    correctionVelocityCentsPerSecond_ = std::clamp(
        correctionVelocityCentsPerSecond_,
        -maximumVelocity,
        maximumVelocity);
    currentCorrectionCents_ += correctionVelocityCentsPerSecond_ * dt;

    if (std::abs(targetCorrectionCents - currentCorrectionCents_) < 0.002
        && std::abs(correctionVelocityCentsPerSecond_) < 0.02)
    {
        currentCorrectionCents_ = targetCorrectionCents;
        correctionVelocityCentsPerSecond_ = 0.0;
    }
}

double ModernPitchEngine::CorrectionController::getPitchRatio() const noexcept
{
    return std::exp2(currentCorrectionCents_ / 1200.0);
}

float ModernPitchEngine::CorrectionController::getTargetPitchHz() const noexcept
{
    if (!targetValid_)
        return 0.0f;
    return static_cast<float>(std::exp2(targetLog2_));
}

//==============================================================================
// TransitionManager

void ModernPitchEngine::TransitionManager::prepare(
    double sampleRate,
    int synthesisFrameSize,
    LatencyMode latencyMode) noexcept
{
    sampleRate_ = std::max(8000.0, sampleRate);
    synthesisFrameSize_ = std::max(64, synthesisFrameSize);
    synthesisHopSize_ = std::max(1, synthesisFrameSize_ / 4);
    latencyMode_ = latencyMode;
    reset();
}

void ModernPitchEngine::TransitionManager::reset() noexcept
{
    phase_ = Phase::idle;
    initialised_ = false;
    pendingTarget_ = false;
    pendingForceTransition_ = false;
    beginEventPending_ = false;
    lastSeenRevision_ = 0;
    pendingRevision_ = 0;
    transitionRevision_ = 0;
    idleCents_ = 0.0;
    primaryCents_ = 0.0;
    secondaryCents_ = 0.0;
    secondaryVelocityCentsPerSecond_ = 0.0;
    transitionTargetCents_ = 0.0;
    pendingTargetCents_ = 0.0;
    preRollSamplesRemaining_ = 0;
    crossfadeSamplesTotal_ = 1;
    crossfadeSampleIndex_ = 0;
    transitionCooldownSamples_ = 0;
    publishedBlend_ = 0.0f;
}

double ModernPitchEngine::TransitionManager::transitionThresholdCents(const Parameters& parameters) const noexcept
{
    if (parameters.scaleLock)
    {
        const float lockStrictness = parameters.hardLockActive ? clamp01(parameters.lockStrictness) : 0.0f;
        const double vibratoProtection = clamp01(parameters.vibratoPreserve);
        return CorrectionController::scaleLockTransitionThresholdCents(parameters, lockStrictness, static_cast<float>(vibratoProtection));
    }

    // Adjacent microtonal targets must remain eligible, while fluctuations
    // smaller than a true musical step are better handled by the primary
    // trajectory.  The dynamic gate in processSample() raises this further on
    // confident sustained vowels with high wet authority.
    switch (latencyMode_)
    {
        case LatencyMode::ultraLive: return 36.0;
        case LatencyMode::live:      return 40.0;
        case LatencyMode::quality:   return 44.0;
    }

    return 40.0;
}

int ModernPitchEngine::TransitionManager::crossfadeLengthSamples(
    const Parameters& parameters) const noexcept
{
    double minimumMs = 10.0;
    switch (latencyMode_)
    {
        case LatencyMode::ultraLive: minimumMs = 7.0; break;
        case LatencyMode::live:      minimumMs = 10.0; break;
        case LatencyMode::quality:   minimumMs = 14.0; break;
    }

    const double requestedMs = std::clamp(
        0.30 * static_cast<double>(parameters.transitionTimeMs),
        minimumMs,
        24.0);
    return std::max(1, static_cast<int>(std::lround(
        requestedMs * 0.001 * sampleRate_)));
}

void ModernPitchEngine::TransitionManager::startTransition(
    double currentCents,
    double targetCents,
    const Parameters& parameters) noexcept
{
    primaryCents_ = currentCents;
    secondaryCents_ = currentCents;
    secondaryVelocityCentsPerSecond_ = 0.0;
    transitionTargetCents_ = targetCents;

    // The second layer is generated immediately but remains inaudible until a
    // complete overlap-add history exists. This is synthesis pre-roll, not
    // added plugin latency: the primary layer continues to produce audio.
    preRollSamplesRemaining_ = synthesisFrameSize_ + synthesisHopSize_;
    crossfadeSamplesTotal_ = crossfadeLengthSamples(parameters);
    crossfadeSampleIndex_ = 0;
    phase_ = Phase::preRoll;
    beginEventPending_ = true;
    publishedBlend_ = 0.0f;
}

void ModernPitchEngine::TransitionManager::updateSecondaryTrajectory(
    double targetCents,
    const Parameters& parameters) noexcept
{
    transitionTargetCents_ = std::clamp(targetCents, -2400.0, 2400.0);

    const double dt = 1.0 / sampleRate_;
    double minimumResponseMs = 7.0;
    switch (latencyMode_)
    {
        case LatencyMode::ultraLive: minimumResponseMs = 7.0; break;
        case LatencyMode::live:      minimumResponseMs = 10.0; break;
        case LatencyMode::quality:   minimumResponseMs = 14.0; break;
    }

    const double responseMs = std::clamp(
        std::max(minimumResponseMs,
                 static_cast<double>(parameters.transitionTimeMs)),
        minimumResponseMs,
        85.0);
    const double responseSeconds = responseMs * 0.001;
    const double omega = std::min(0.20 / dt,
                                  4.6 / std::max(0.001, responseSeconds));
    const double error = transitionTargetCents_ - secondaryCents_;

    double acceleration = omega * omega * error
                        - 2.0 * omega * secondaryVelocityCentsPerSecond_;
    const double maximumVelocity = std::max(
        4800.0,
        9.0 * std::max(100.0, std::abs(error))
            / std::max(0.001, responseSeconds));
    const double maximumAcceleration = maximumVelocity
        / std::max(0.0005, responseSeconds * 0.30);

    acceleration = std::clamp(acceleration,
                              -maximumAcceleration,
                              maximumAcceleration);
    secondaryVelocityCentsPerSecond_ += acceleration * dt;
    secondaryVelocityCentsPerSecond_ = std::clamp(
        secondaryVelocityCentsPerSecond_,
        -maximumVelocity,
        maximumVelocity);
    secondaryCents_ += secondaryVelocityCentsPerSecond_ * dt;

    if (std::abs(error) < 0.003
        && std::abs(secondaryVelocityCentsPerSecond_) < 0.03)
    {
        secondaryCents_ = transitionTargetCents_;
        secondaryVelocityCentsPerSecond_ = 0.0;
    }
}

ModernPitchEngine::TransitionManager::Command
ModernPitchEngine::TransitionManager::processSample(
    double controllerCorrectionCents,
    double destinationCorrectionCents,
    std::uint64_t targetRevision,
    TrackingState trackingState,
    float wetMix,
    float tonalEvidence,
    float correctionDistanceCents,
    const Parameters& parameters,
    bool forceTransition) noexcept
{
    Command command;
    controllerCorrectionCents = wrapCorrectionToNearestOctave(
        controllerCorrectionCents);
    destinationCorrectionCents = wrapCorrectionToNearestOctave(
        destinationCorrectionCents);

    if (!initialised_)
    {
        initialised_ = true;
        lastSeenRevision_ = targetRevision;
        idleCents_ = controllerCorrectionCents;
        primaryCents_ = idleCents_;
        secondaryCents_ = idleCents_;
    }

    if (targetRevision != lastSeenRevision_)
    {
        lastSeenRevision_ = targetRevision;
        pendingRevision_ = targetRevision;
        pendingTargetCents_ = destinationCorrectionCents;
        pendingTarget_ = true;
        pendingForceTransition_ = forceTransition;
    }
    else if (pendingTarget_)
    {
        // Authority and transient protection may still be settling after the
        // note decision; retain the latest effective destination.
        pendingTargetCents_ = destinationCorrectionCents;
        pendingForceTransition_ = pendingForceTransition_ || forceTransition;
    }

    if (phase_ == Phase::idle)
    {
        if (transitionCooldownSamples_ > 0)
            --transitionCooldownSamples_;

        // Follow the already-smoothed controller with a very high slew limit.
        // This keeps normal vibrato continuous without reintroducing a second
        // audible low-pass stage after CorrectionController.
        const double maximumStep = 24000.0 / sampleRate_;
        idleCents_ += std::clamp(controllerCorrectionCents - idleCents_,
                                 -maximumStep,
                                 maximumStep);

        if (pendingTarget_)
        {
            const double jumpCents = pendingTargetCents_ - idleCents_;
            const bool musicalState = trackingState != TrackingState::unvoiced
                                   && trackingState != TrackingState::release;

            const double baseRequiredJump = pendingForceTransition_
                ? 1.0
                : transitionThresholdCents(parameters);
            const float tonalGate = smoothStep(0.35f, 0.82f, tonalEvidence);
            const float wetGate = smoothStep(0.42f, 0.86f, wetMix);
            const float microTransitionGate = 1.0f
                - smoothStep(42.0f, 96.0f, correctionDistanceCents);
            const double dynamicMicroLift = (!pendingForceTransition_
                                             && !parameters.scaleLock)
                ? 18.0 * static_cast<double>(tonalGate * wetGate * microTransitionGate)
                : 0.0;
            const double requiredJump = baseRequiredJump + dynamicMicroLift;

            if (musicalState
                && transitionCooldownSamples_ <= 0
                && std::abs(jumpCents) >= requiredJump)
            {
                startTransition(idleCents_, pendingTargetCents_, parameters);
                transitionRevision_ = pendingRevision_;
                pendingTarget_ = false;
                pendingForceTransition_ = false;
            }
            else if ((!pendingForceTransition_
                      && std::abs(jumpCents) < requiredJump)
                     || (trackingState == TrackingState::unvoiced
                         && wetMix < 0.01f))
            {
                // Small pitch motion stays on the primary path. This is useful
                // for vibrato and very fine scale steps where dual synthesis
                // would be more expensive than beneficial.
                pendingTarget_ = false;
                pendingForceTransition_ = false;
            }
        }

        if (phase_ == Phase::idle)
        {
            command.primaryCents = wrapCorrectionToNearestOctave(idleCents_);
            command.secondaryCents = command.primaryCents;
            publishedBlend_ = 0.0f;
            return command;
        }
    }

    // A new decision received early in the transition can safely retarget the
    // secondary path. Later decisions are queued for the next transition so a
    // crossfade is never reversed halfway through.
    if (pendingTarget_)
    {
        const bool earlyEnough = phase_ == Phase::preRoll
                              || publishedBlend_ < 0.35f;
        if (earlyEnough)
        {
            transitionTargetCents_ = pendingTargetCents_;
            transitionRevision_ = pendingRevision_;
            pendingTarget_ = false;
        }
    }

    if (targetRevision == transitionRevision_)
        transitionTargetCents_ = destinationCorrectionCents;

    updateSecondaryTrajectory(transitionTargetCents_, parameters);

    command.primaryCents = wrapCorrectionToNearestOctave(primaryCents_);
    command.secondaryCents = wrapCorrectionToNearestOctave(secondaryCents_);
    command.dualSynthesis = true;
    command.beginSecondary = beginEventPending_;
    beginEventPending_ = false;

    if (phase_ == Phase::preRoll)
    {
        command.blend = 0.0f;
        if (preRollSamplesRemaining_ > 0)
            --preRollSamplesRemaining_;
        if (preRollSamplesRemaining_ <= 0)
        {
            phase_ = Phase::crossfade;
            crossfadeSampleIndex_ = 0;
        }
    }
    else
    {
        const float linearPhase = crossfadeSamplesTotal_ > 1
            ? static_cast<float>(crossfadeSampleIndex_)
                / static_cast<float>(crossfadeSamplesTotal_ - 1)
            : 1.0f;
        command.blend = smoothStep(0.0f, 1.0f, linearPhase);
        ++crossfadeSampleIndex_;

        if (crossfadeSampleIndex_ >= crossfadeSamplesTotal_)
        {
            command.blend = 1.0f;
            command.commitSecondary = true;
            idleCents_ = secondaryCents_;
            primaryCents_ = secondaryCents_;
            phase_ = Phase::idle;
            crossfadeSampleIndex_ = 0;
            transitionCooldownSamples_ = std::max(
                synthesisFrameSize_ / 2,
                static_cast<int>(std::lround(0.006 * sampleRate_)));
        }
    }

    publishedBlend_ = command.blend;
    return command;
}

//==============================================================================
// FixedDelay

void ModernPitchEngine::FixedDelay::prepare(int delaySamples)
{
    delaySamples_ = std::max(0, delaySamples);
    const int requiredSize = std::max(2, delaySamples_ + 2);
    const int bufferSize = nextPowerOfTwo(requiredSize);
    buffer_.assign(static_cast<std::size_t>(bufferSize), 0.0f);
    mask_ = bufferSize - 1;
    reset();
}

void ModernPitchEngine::FixedDelay::reset() noexcept
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    sampleCounter_ = 0;
}

float ModernPitchEngine::FixedDelay::process(float inputSample) noexcept
{
    inputSample = sanitiseAudioSample(inputSample);
    if (buffer_.empty())
        return inputSample;

    const int writeIndex = static_cast<int>(sampleCounter_ & mask_);
    buffer_[static_cast<std::size_t>(writeIndex)] = inputSample;

    float output = 0.0f;
    if (sampleCounter_ >= delaySamples_)
    {
        const int readIndex = static_cast<int>(
            (sampleCounter_ - delaySamples_) & mask_);
        output = sanitiseAudioSample(buffer_[static_cast<std::size_t>(readIndex)]);
    }

    ++sampleCounter_;
    return output;
}

//==============================================================================
// ModernPitchEngine

void ModernPitchEngine::prepare(double sampleRate,
                                int /*maximumExpectedSamplesPerBlock*/,
                                int numberOfChannels,
                                LatencyMode latencyMode)
{
    sampleRate_ = std::isfinite(sampleRate)
        ? std::max(8000.0, sampleRate)
        : 48000.0;
    preparedChannels_ = std::clamp(numberOfChannels, 1, maxSupportedChannels);
    const int latencyValue = std::clamp(static_cast<int>(latencyMode), 0, 2);
    latencyMode_ = static_cast<LatencyMode>(latencyValue);
    latencySamples_ = frameSizeForMode(sampleRate_, latencyMode_);

    pitchTracker_.prepare(sampleRate_);
    // Analysis-only conditioning: reduce high-frequency air noise before YIN
    // without touching the audible path or adding output latency.
    detectorConditioner_.prepare(sampleRate_,
        std::min(4600.0, sampleRate_ * 0.20));
    correctionController_.prepare(sampleRate_);
    transitionManager_.prepare(sampleRate_, latencySamples_, latencyMode_);
    tempoController_.prepare(sampleRate_);

    // Allocate FFT/LUT state only for channels the host actually exposes.
    // The previous code prepared all eight possible channels even for a mono
    // vocal track, multiplying setup time and memory without any runtime value.
    for (int channel = 0; channel < preparedChannels_; ++channel)
    {
        shifters_[static_cast<std::size_t>(channel)].prepare(sampleRate_,
                                                             latencySamples_);
        auxiliaryDelays_[static_cast<std::size_t>(channel)].prepare(latencySamples_);
    }

    reset();
}

void ModernPitchEngine::reset() noexcept
{
    pitchTracker_.reset();
    detectorConditioner_.reset();
    scaleQuantizer_.resetTarget();
    correctionController_.reset();
    transitionManager_.reset();
    tempoController_.reset();
    noteAgeTargetHz_ = 0.0f;
    noteAgeSamples_ = 0;
    outputTemporalInitialised_ = false;
    outputTemporalPreviousTargetHz_ = 0.0f;
    outputTemporalPreviousDetectedHz_ = 0.0f;
    outputTemporalPreviousCorrectionCents_ = 0.0f;
    outputTemporalStability_ = 0.0f;
    outputTargetJumpCents_ = 0.0f;
    outputCorrectionVelocityCentsPerSecond_ = 0.0f;
    outputOctaveConflict_ = 0.0f;
    outputTransitionStress_ = 0.0f;
    diagnosticCsvInitialised_ = false;
    diagnosticCsvFile_ = juce::File();
    diagnosticCsvSampleCounter_ = 0;
    diagnosticCsvNextSample_ = 0;

    for (auto& shifter : shifters_)
        shifter.reset();
    for (auto& delay : auxiliaryDelays_)
        delay.reset();

    meterSequence_.store(1u, std::memory_order_release);
    meterPitchHz_.store(0.0f, std::memory_order_relaxed);
    meterTargetHz_.store(0.0f, std::memory_order_relaxed);
    meterConfidence_.store(0.0f, std::memory_order_relaxed);
    meterVoicing_.store(0.0f, std::memory_order_relaxed);
    meterBreathiness_.store(0.0f, std::memory_order_relaxed);
    meterHarmonicity_.store(0.0f, std::memory_order_relaxed);
    meterNoisePath_.store(0.0f, std::memory_order_relaxed);
    meterNoiseReductionDb_.store(0.0f, std::memory_order_relaxed);
    meterPolyphony_.store(0.0f, std::memory_order_relaxed);
    meterSpectralReliability_.store(0.0f, std::memory_order_relaxed);
    meterMaskStability_.store(1.0f, std::memory_order_relaxed);
    meterSustainedNoteSeconds_.store(0.0f, std::memory_order_relaxed);
    meterConsensus_.store(0.0f, std::memory_order_relaxed);
    meterCorrectionCents_.store(0.0f, std::memory_order_relaxed);
    meterWetMix_.store(0.0f, std::memory_order_relaxed);
    meterTransitionBlend_.store(0.0f, std::memory_order_relaxed);
    meterOutputSourceCorrespondence_.store(0.0f, std::memory_order_relaxed);
    meterOutputTargetCoherence_.store(0.0f, std::memory_order_relaxed);
    meterOutputPhysicalHarmonicFit_.store(0.0f, std::memory_order_relaxed);
    meterOutputLedgerHealth_.store(100.0f, std::memory_order_relaxed);
    meterOutputPhaseCoherence_.store(0.0f, std::memory_order_relaxed);
    meterOutputReconstructionNeed_.store(0.0f, std::memory_order_relaxed);
    meterOutputMeterValid_.store(0.0f, std::memory_order_relaxed);
    meterOutputTemporalStability_.store(0.0f, std::memory_order_relaxed);
    meterOutputTargetJumpCents_.store(0.0f, std::memory_order_relaxed);
    meterOutputCorrectionVelocityCentsPerSecond_.store(0.0f, std::memory_order_relaxed);
    meterOutputOctaveConflict_.store(0.0f, std::memory_order_relaxed);
    meterOutputTransitionStress_.store(0.0f, std::memory_order_relaxed);
    meterOutputSourceMirrorFit_.store(0.0f, std::memory_order_relaxed);
    meterOutputDoubleFamilyRisk_.store(0.0f, std::memory_order_relaxed);
    meterOutputLedgerDeficit_.store(0.0f, std::memory_order_relaxed);
    meterOutputMemoryReliability_.store(0.0f, std::memory_order_relaxed);
    meterOutputPreIfftConsensus_.store(0.0f, std::memory_order_relaxed);
    meterOutputSelectiveReconstructionNeed_.store(0.0f, std::memory_order_relaxed);
    meterShadowRidgeObservationCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeActiveCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeBirthCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeCoastCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeDeathCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeIdentitySwitchCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgePredictionErrorRadians_.store(0.0f, std::memory_order_relaxed);
    meterShadowRidgeReliability_.store(0.0f, std::memory_order_relaxed);
    meterShadowRidgeResolvedBinCoverage_.store(0.0f, std::memory_order_relaxed);
    meterShadowRidgeValid_.store(false, std::memory_order_relaxed);
    meterDualSynthesisActive_.store(false, std::memory_order_relaxed);
    meterDetectorSupport_.store(0, std::memory_order_relaxed);
    meterOctaveState_.store(0, std::memory_order_relaxed);
    meterPendingOctaveObservations_.store(0, std::memory_order_relaxed);
    meterState_.store(static_cast<int>(TrackingState::unvoiced),
                      std::memory_order_relaxed);
    meterTempoBpm_.store(120.0f, std::memory_order_relaxed);
    meterTempoGridPhase_.store(0.0f, std::memory_order_relaxed);
    meterTempoGlideTimeMs_.store(0.0f, std::memory_order_relaxed);
    meterTempoActive_.store(false, std::memory_order_relaxed);
    meterTempoWaiting_.store(false, std::memory_order_relaxed);
    meterTempoHostSync_.store(false, std::memory_order_relaxed);
    meterTempoMode_.store(static_cast<int>(CreativeTempo::Mode::off),
                          std::memory_order_relaxed);
    bypassActive_ = false;
    meterSequence_.store(2u, std::memory_order_release);
}


// NEUMATON_V6_CSV_DIAGNOSTICS
// Debug-only CSV logger for the V6.0 output diagnostics.  It is deliberately
// outside the synthesis path: values are written after the block meters have
// been published.  For release builds, set NEUMATON_V6_CSV_DIAGNOSTICS to 0.
void ModernPitchEngine::appendV6DiagnosticsCsv(const Metering& meter,
                                               int numberOfSamples) noexcept
{
#if NEUMATON_V6_CSV_DIAGNOSTICS
    if (numberOfSamples <= 0 || sampleRate_ <= 0.0)
        return;

    diagnosticCsvSampleCounter_ += static_cast<std::uint64_t>(numberOfSamples);

    const auto intervalSamples = static_cast<std::uint64_t>(std::max(
        1.0,
        std::round(sampleRate_ * 0.050))); // about 20 rows per second

    if (diagnosticCsvSampleCounter_ < diagnosticCsvNextSample_)
        return;

    diagnosticCsvNextSample_ = diagnosticCsvSampleCounter_ + intervalSamples;

    if (!diagnosticCsvInitialised_)
    {
        const auto folder = juce::File::getSpecialLocation(
            juce::File::userDocumentsDirectory)
            .getChildFile("NeumatonDiagnostics");
        folder.createDirectory();

        const char* modeName = "live";
        switch (latencyMode_)
        {
            case LatencyMode::ultraLive: modeName = "experimental"; break;
            case LatencyMode::live:      modeName = "live"; break;
            case LatencyMode::quality:   modeName = "quality"; break;
        }

        const juce::String stamp(juce::Time::currentTimeMillis());
        diagnosticCsvFile_ = folder.getChildFile(
            juce::String("neumaton_v6_diagnostics_")
            + modeName + "_" + stamp + ".csv");

        const juce::String headerLine =
            "time_seconds,latency_mode,detected_pitch_hz,target_pitch_hz,"
            "correction_cents,confidence,voicing,harmonicity,breathiness,"
            "noise_path,spectral_reliability,mask_stability,consensus,"
            "tracking_state,octave_state,pending_octave_observations,"
            "transition_blend,dual_synthesis,output_meter_valid,active_tonal_frame,source_correspondence,"
            "target_coherence,physical_harmonic_fit,ledger_health,"
            "phase_coherence,reconstruction_need,temporal_stability,"
            "target_jump_cents,correction_velocity_cps,octave_conflict,"
            "transition_stress,source_mirror_fit,double_family_risk,"
            "ledger_deficit,memory_reliability,pre_ifft_consensus,"
            "selective_reconstruction_need,shadow_observations,shadow_active,"
            "shadow_births,shadow_coasts,shadow_deaths,shadow_identity_switches,"
            "shadow_prediction_error_rad,shadow_reliability,"
            "shadow_resolved_bin_coverage,shadow_valid\n";

        diagnosticCsvFile_.replaceWithText(headerLine, false, false, "\n");
        diagnosticCsvInitialised_ = diagnosticCsvFile_.existsAsFile();
        if (!diagnosticCsvInitialised_)
            return;
    }

    const double timeSeconds = static_cast<double>(diagnosticCsvSampleCounter_)
        / std::max(1.0, sampleRate_);

    juce::String line;
    line << juce::String(timeSeconds, 6) << ','
         << static_cast<int>(latencyMode_) << ','
         << juce::String(meter.detectedPitchHz, 6) << ','
         << juce::String(meter.targetPitchHz, 6) << ','
         << juce::String(meter.correctionCents, 6) << ','
         << juce::String(meter.confidence, 6) << ','
         << juce::String(meter.voicing, 6) << ','
         << juce::String(meter.harmonicity, 6) << ','
         << juce::String(meter.breathiness, 6) << ','
         << juce::String(meter.noisePath, 6) << ','
         << juce::String(meter.spectralReliability, 6) << ','
         << juce::String(meter.maskStability, 6) << ','
         << juce::String(meter.consensus, 6) << ','
         << static_cast<int>(meter.state) << ','
         << meter.octaveState << ','
         << meter.pendingOctaveObservations << ','
         << juce::String(meter.transitionBlend, 6) << ','
         << (meter.dualSynthesisActive ? 1 : 0) << ','
         << juce::String(meter.outputMeterValid, 6) << ','
         << ((meter.detectedPitchHz > 20.0f && meter.confidence > 0.15f && meter.voicing > 0.15f) ? 1 : 0) << ','
         << juce::String(meter.outputSourceCorrespondence, 6) << ','
         << juce::String(meter.outputTargetCoherence, 6) << ','
         << juce::String(meter.outputPhysicalHarmonicFit, 6) << ','
         << juce::String(meter.outputLedgerHealth, 6) << ','
         << juce::String(meter.outputPhaseCoherence, 6) << ','
         << juce::String(meter.outputReconstructionNeed, 6) << ','
         << juce::String(meter.outputTemporalStability, 6) << ','
         << juce::String(meter.outputTargetJumpCents, 6) << ','
         << juce::String(meter.outputCorrectionVelocityCentsPerSecond, 6) << ','
         << juce::String(meter.outputOctaveConflict, 6) << ','
         << juce::String(meter.outputTransitionStress, 6) << ','
         << juce::String(meter.outputSourceMirrorFit, 6) << ','
         << juce::String(meter.outputDoubleFamilyRisk, 6) << ','
         << juce::String(meter.outputLedgerDeficit, 6) << ','
         << juce::String(meter.outputMemoryReliability, 6) << ','
         << juce::String(meter.outputPreIfftConsensus, 6) << ','
         << juce::String(meter.outputSelectiveReconstructionNeed, 6) << ','
         << meter.shadowRidgeObservationCount << ','
         << meter.shadowRidgeActiveCount << ','
         << meter.shadowRidgeBirthCount << ','
         << meter.shadowRidgeCoastCount << ','
         << meter.shadowRidgeDeathCount << ','
         << meter.shadowRidgeIdentitySwitchCount << ','
         << juce::String(meter.shadowRidgePredictionErrorRadians, 6) << ','
         << juce::String(meter.shadowRidgeReliability, 6) << ','
         << juce::String(meter.shadowRidgeResolvedBinCoverage, 6) << ','
         << (meter.shadowRidgeValid ? 1 : 0) << '\n';

    diagnosticCsvFile_.appendText(line, false, false, "\n");
#else
    juce::ignoreUnused(meter, numberOfSamples);
#endif
}

void ModernPitchEngine::process(juce::AudioBuffer<float>& buffer,
                                const double* scaleRatios,
                                int numberOfScaleRatios,
                                double rootFrequency,
                                const Parameters& parameters)
{
    process(buffer,
            scaleRatios,
            numberOfScaleRatios,
            rootFrequency,
            parameters,
            CreativeTempo::HostPosition {});
}

void ModernPitchEngine::process(juce::AudioBuffer<float>& buffer,
                                const double* scaleRatios,
                                int numberOfScaleRatios,
                                double rootFrequency,
                                const Parameters& parameters,
                                const CreativeTempo::HostPosition& hostTempoPosition)
{
    Parameters safeParameters = parameters;
    safeParameters.amount = clamp01(finiteOr(safeParameters.amount, 0.0f));
    safeParameters.retuneTimeMs = std::clamp(finiteOr(safeParameters.retuneTimeMs, 50.0f), 0.0f, 500.0f);
    safeParameters.retuneTimeMs = std::max(safeParameters.retuneTimeMs,
                                           retuneFloorForLatencyMode(latencyMode_));
    safeParameters.transitionTimeMs = std::clamp(finiteOr(safeParameters.transitionTimeMs, 35.0f), 0.0f, 2000.0f);
    safeParameters.preserveVibrato = clamp01(finiteOr(safeParameters.preserveVibrato, 0.70f));
    safeParameters.humanize = clamp01(finiteOr(safeParameters.humanize, 0.20f));
    safeParameters.formantPreservation = clamp01(finiteOr(safeParameters.formantPreservation, 0.90f));
    safeParameters.transientProtection = clamp01(finiteOr(safeParameters.transientProtection, 0.85f));
    safeParameters.detectorSensitivity = clamp01(finiteOr(safeParameters.detectorSensitivity, 0.70f));
    safeParameters.maximumCorrectionSemitones = std::clamp(
        finiteOr(safeParameters.maximumCorrectionSemitones, 12.0f), 0.0f, 48.0f);
    safeParameters.minimumPitchHz = std::clamp(
        finiteOr(safeParameters.minimumPitchHz, 45.0f), 25.0f, 500.0f);
    safeParameters.maximumPitchHz = std::clamp(
        finiteOr(safeParameters.maximumPitchHz, 1600.0f),
        safeParameters.minimumPitchHz + 20.0f, 3000.0f);
    safeParameters.breathReduction = clamp01(finiteOr(safeParameters.breathReduction, 0.50f));
    safeParameters.tempo.mode = static_cast<CreativeTempo::Mode>(
        std::clamp(static_cast<int>(safeParameters.tempo.mode), 0, 2));
    safeParameters.tempo.division = CreativeTempo::divisionFromIndex(
        CreativeTempo::divisionToIndex(safeParameters.tempo.division));
    safeParameters.tempo.glideFraction = std::clamp(
        finiteOr(safeParameters.tempo.glideFraction, 0.35f), 0.05f, 1.0f);
    safeParameters.tempo.lockStrength = clamp01(
        finiteOr(safeParameters.tempo.lockStrength, 1.0f));
    safeParameters.tempo.smartOnsetWindow = std::clamp(
        finiteOr(safeParameters.tempo.smartOnsetWindow, 0.18f), 0.0f, 0.5f);
    safeParameters.tempo.fallbackBpm = std::clamp(
        finiteOr(safeParameters.tempo.fallbackBpm, 120.0), 20.0, 400.0);
    const int stereoValue = std::clamp(static_cast<int>(safeParameters.stereoMode), 0, 1);
    safeParameters.stereoMode = static_cast<StereoMode>(stereoValue);
    safeParameters.scaleSize = numberOfScaleRatios > 0 ? numberOfScaleRatios : 12;
    safeParameters.latencyMode = static_cast<int>(latencyMode_);

    if (scaleQuantizer_.update(scaleRatios,
                               numberOfScaleRatios,
                               rootFrequency))
    {
        correctionController_.reset();
        transitionManager_.reset();
        tempoController_.reset();
    }

    pitchTracker_.setRange(safeParameters.minimumPitchHz,
                           safeParameters.maximumPitchHz);
    pitchTracker_.setSensitivity(safeParameters.detectorSensitivity);

    const int numberOfSamples = buffer.getNumSamples();
    const int numberOfChannels = std::min({ buffer.getNumChannels(),
                                            preparedChannels_,
                                            maxSupportedChannels });
    if (numberOfSamples <= 0 || numberOfChannels <= 0)
        return;

    bypassActive_ = false;

    tempoController_.beginBlock(hostTempoPosition,
                                safeParameters.tempo,
                                numberOfSamples);
    Parameters transitionParameters = safeParameters;
    if (tempoController_.isActive())
        transitionParameters.transitionTimeMs = tempoController_.getGlideTimeMs();

    std::array<float*, maxSupportedChannels> channelData {};
    for (int channel = 0; channel < numberOfChannels; ++channel)
    {
        channelData[static_cast<std::size_t>(channel)] = buffer.getWritePointer(channel);
        float* data = channelData[static_cast<std::size_t>(channel)];
        for (int sample = 0; sample < numberOfSamples; ++sample)
            data[sample] = sanitiseAudioSample(data[sample]);
    }

    currentStereoMode_ = safeParameters.stereoMode;
    bool stereoAutoFallback = false;
    if (currentStereoMode_ == StereoMode::linkedMidSide && numberOfChannels >= 2)
    {
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        double crossEnergy = 0.0;
        double midEnergy = 0.0;
        double sideEnergy = 0.0;
        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            const double left = channelData[0][sample];
            const double right = channelData[1][sample];
            const double mid = 0.5 * (left + right);
            const double side = 0.5 * (left - right);
            leftEnergy += left * left;
            rightEnergy += right * right;
            crossEnergy += left * right;
            midEnergy += mid * mid;
            sideEnergy += side * side;
        }

        const double stereoPower = midEnergy + sideEnergy;
        const float sideRatio = stereoPower > 1.0e-16
            ? static_cast<float>(sideEnergy / stereoPower)
            : 0.0f;
        const float lrCorrelation = (leftEnergy > 1.0e-16 && rightEnergy > 1.0e-16)
            ? static_cast<float>(crossEnergy / std::sqrt(leftEnergy * rightEnergy))
            : 1.0f;

        // linked mid/side is stable for mostly centred mono sources.  Wide
        // doubles, choruses and decorrelated stereo material are safer as
        // dual-mono because a shared mid shifter can make the image breathe.
        stereoAutoFallback = sideRatio > 0.34f || lrCorrelation < 0.22f;
    }
    const bool useMidSide = currentStereoMode_ == StereoMode::linkedMidSide
                         && numberOfChannels >= 2
                         && !stereoAutoFallback;

    PitchObservation observation;
    bool forcePhaseReset = false;
    float latestPitchHz = meterPitchHz_.load(std::memory_order_relaxed);
    float latestConfidence = meterConfidence_.load(std::memory_order_relaxed);
    float latestVoicing = meterVoicing_.load(std::memory_order_relaxed);
    float latestBreathiness = meterBreathiness_.load(std::memory_order_relaxed);
    float latestHarmonicity = meterHarmonicity_.load(std::memory_order_relaxed);
    float latestNoisePath = meterNoisePath_.load(std::memory_order_relaxed);
    float latestNoiseReductionDb = meterNoiseReductionDb_.load(std::memory_order_relaxed);
    float latestPolyphony = meterPolyphony_.load(std::memory_order_relaxed);
    float latestSpectralReliability = meterSpectralReliability_.load(std::memory_order_relaxed);
    float latestMaskStability = meterMaskStability_.load(std::memory_order_relaxed);
    float latestConsensus = meterConsensus_.load(std::memory_order_relaxed);
    float latestOnsetStrength = 0.0f;
    int latestDetectorSupport = meterDetectorSupport_.load(std::memory_order_relaxed);
    int latestOctaveState = meterOctaveState_.load(std::memory_order_relaxed);
    int latestPendingOctave = meterPendingOctaveObservations_.load(std::memory_order_relaxed);

    for (int sampleIndex = 0; sampleIndex < numberOfSamples; ++sampleIndex)
    {
        float detectorInput = 0.0f;
        if (useMidSide)
        {
            detectorInput = 0.5f
                * (channelData[0][sampleIndex] + channelData[1][sampleIndex]);
        }
        else
        {
            for (int channel = 0; channel < numberOfChannels; ++channel)
                detectorInput += channelData[static_cast<std::size_t>(channel)][sampleIndex];
            detectorInput /= static_cast<float>(numberOfChannels);
        }

        detectorInput = detectorConditioner_.process(detectorInput);

        forcePhaseReset = false;
        if (pitchTracker_.processSample(detectorInput, observation))
        {
            float spectralBreath = shifters_[0].getBreathiness();
            float spectralHarmonicity = shifters_[0].getHarmonicity();
            float spectralPolyphony = shifters_[0].getPolyphony();
            float spectralReliability = shifters_[0].getSpectralReliability();
            if (!useMidSide && numberOfChannels > 1)
            {
                spectralBreath = 0.0f;
                spectralHarmonicity = 0.0f;
                spectralPolyphony = 0.0f;
                spectralReliability = 0.0f;
                for (int channel = 0; channel < numberOfChannels; ++channel)
                {
                    const auto& shifter = shifters_[static_cast<std::size_t>(channel)];
                    spectralBreath += shifter.getBreathiness();
                    spectralHarmonicity += shifter.getHarmonicity();
                    spectralPolyphony += shifter.getPolyphony();
                    spectralReliability += shifter.getSpectralReliability();
                }
                const float inverseChannels = 1.0f
                    / static_cast<float>(numberOfChannels);
                spectralBreath *= inverseChannels;
                spectralHarmonicity *= inverseChannels;
                spectralPolyphony *= inverseChannels;
                spectralReliability *= inverseChannels;
            }
            correctionController_.setSpectralReliability(spectralBreath,
                                                         spectralHarmonicity,
                                                         spectralPolyphony,
                                                         spectralReliability);
            correctionController_.acceptObservation(observation,
                                                    scaleQuantizer_,
                                                    safeParameters);
            // Do not reset spectral phase on musical onsets. The renderer
            // handles transients with soft phase anchoring and wet suppression.
            forcePhaseReset = false;

            latestPitchHz = observation.frequencyHz;
            latestConfidence = observation.confidence;
            latestVoicing = observation.voicing;
            latestConsensus = observation.consensus;
            latestDetectorSupport = observation.detectorSupport;
            latestOctaveState = observation.octaveState;
            latestPendingOctave = observation.pendingOctaveObservations;
            latestOnsetStrength = observation.onsetStrength;
        }

        correctionController_.advanceOneSample(safeParameters);
        const float wetMix = correctionController_.getWetMix();
        const auto trackingState = correctionController_.getState();
        const bool musicalState = trackingState != TrackingState::unvoiced
                               && trackingState != TrackingState::release;
        const float currentTargetPitchHz =
            correctionController_.getTargetPitchHz();
        if (musicalState && currentTargetPitchHz > 0.0f)
        {
            bool restartSustainClock = noteAgeTargetHz_ <= 0.0f;
            if (!restartSustainClock)
            {
                const float targetJumpCents = static_cast<float>(
                    1200.0 * std::abs(std::log2(
                        std::max(1.0e-6f, currentTargetPitchHz)
                        / std::max(1.0e-6f, noteAgeTargetHz_))));
                restartSustainClock = targetJumpCents > 70.0f
                    || latestOnsetStrength > 0.62f;
            }

            if (restartSustainClock)
            {
                noteAgeSamples_ = 0;
                noteAgeTargetHz_ = currentTargetPitchHz;
            }
            else
            {
                ++noteAgeSamples_;
            }
        }
        else
        {
            noteAgeSamples_ = 0;
            noteAgeTargetHz_ = 0.0f;
        }
        const float noteAgeSeconds = static_cast<float>(
            std::min(12.0, static_cast<double>(noteAgeSamples_) / sampleRate_));
        const auto tempoDecision = tempoController_.processSample(
            correctionController_.getCurrentCorrectionCents(),
            correctionController_.getDesiredCorrectionCents(),
            correctionController_.getTargetRevision(),
            latestOnsetStrength,
            musicalState,
            sampleIndex,
            safeParameters.tempo,
            safeParameters.transitionTimeMs);
        const float transitionTonalEvidence = clamp01(
            latestVoicing
            * latestConfidence
            * (0.35f + 0.65f * latestConsensus)
            * (0.35f + 0.65f * latestHarmonicity)
            * (1.0f - 0.55f * latestNoisePath)
            * (1.0f - 0.50f * latestPolyphony));
        const float transitionCorrectionDistanceCents = static_cast<float>(
            std::abs(tempoDecision.destinationCents));
        const auto transition = transitionManager_.processSample(
            tempoDecision.controllerCents,
            tempoDecision.destinationCents,
            tempoDecision.targetRevision,
            trackingState,
            wetMix,
            transitionTonalEvidence,
            transitionCorrectionDistanceCents,
            transitionParameters,
            tempoDecision.forceTransition);
        const float formant = clamp01(safeParameters.formantPreservation
            * correctionController_.getFormantStability());
        
        // NEUMATON_RECONSTRUCTIVE_WET_V1_CONTEXT_SET
        const float correctionAssertivenessForAuditors = neumatonCorrectionSeverityFromAmountSpeed(
            safeParameters.amount,
            safeParameters.retuneTimeMs);
        const float correctionPresentForAuditors = smoothStep(
        1.0f, 8.0f, static_cast<float>(std::abs(
            correctionController_.getSynthesisTargetCorrectionCents())));
    const float scaleLockAuthorityForAuditors = safeParameters.scaleLock
        ? std::max(0.82f, correctionAssertivenessForAuditors)
        : correctionAssertivenessForAuditors;
    const float hardCorrectionIntentForAuditors = clamp01(
        scaleLockAuthorityForAuditors * correctionPresentForAuditors);

        const HarmonicNoiseContext harmonicNoiseContext {
            latestPitchHz,
            latestConfidence,
            latestVoicing,
            latestConsensus,
            latestOnsetStrength,
            clamp01(safeParameters.breathReduction),
            correctionAssertivenessForAuditors,
            hardCorrectionIntentForAuditors,
            trackingState,
            noteAgeSeconds,
            clamp01(safeParameters.humanize),
            safeParameters.scaleLock,
            correctionController_.getTargetPitchHz(),
            tempoDecision.targetRevision
        };

        if (useMidSide)
        {
            const float left = channelData[0][sampleIndex];
            const float right = channelData[1][sampleIndex];
            const float mid = 0.5f * (left + right);
            const float side = 0.5f * (left - right);

            const float processedMid = shifters_[0].processSample(mid,
                                                                  transition,
                                                                  wetMix,
                                                                  formant,
                                                                  harmonicNoiseContext,
                                                                  forcePhaseReset);
            const float delayedSide = auxiliaryDelays_[0].process(side);
            channelData[0][sampleIndex] = processedMid + delayedSide;
            channelData[1][sampleIndex] = processedMid - delayedSide;

            for (int channel = 2; channel < numberOfChannels; ++channel)
            {
                float& sample = channelData[static_cast<std::size_t>(channel)][sampleIndex];
                sample = shifters_[static_cast<std::size_t>(channel)].processSample(
                    sample, transition, wetMix, formant,
                    harmonicNoiseContext, forcePhaseReset);
            }
        }
        else
        {
            for (int channel = 0; channel < numberOfChannels; ++channel)
            {
                float& sample = channelData[static_cast<std::size_t>(channel)][sampleIndex];
                sample = shifters_[static_cast<std::size_t>(channel)].processSample(
                    sample, transition, wetMix, formant,
                    harmonicNoiseContext, forcePhaseReset);
            }
        }
    }

    latestBreathiness = 0.0f;
    latestHarmonicity = 0.0f;
    latestNoisePath = 0.0f;
    latestNoiseReductionDb = 0.0f;
    latestPolyphony = 0.0f;
    latestSpectralReliability = 0.0f;
    latestMaskStability = 0.0f;
    float latestOutputSourceCorrespondence = 0.0f;
    float latestOutputTargetCoherence = 0.0f;
    float latestOutputPhysicalHarmonicFit = 0.0f;
    float latestOutputLedgerHealth = 0.0f;
    float latestOutputPhaseCoherence = 0.0f;
    float latestOutputReconstructionNeed = 0.0f;
    float latestOutputMeterValid = 0.0f;
    float latestOutputSourceMirrorFit = 0.0f;
    float latestOutputDoubleFamilyRisk = 0.0f;
    float latestOutputLedgerDeficit = 0.0f;
    float latestOutputMemoryReliability = 0.0f;
    float latestOutputPreIfftConsensus = 0.0f;
    float latestOutputSelectiveReconstructionNeed = 0.0f;
    int latestShadowRidgeObservationCount = 0;
    int latestShadowRidgeActiveCount = 0;
    int latestShadowRidgeBirthCount = 0;
    int latestShadowRidgeCoastCount = 0;
    int latestShadowRidgeDeathCount = 0;
    int latestShadowRidgeIdentitySwitchCount = 0;
    float latestShadowRidgePredictionErrorRadians = 0.0f;
    float latestShadowRidgeReliability = 0.0f;
    float latestShadowRidgeResolvedBinCoverage = 0.0f;
    int latestShadowRidgeValidCount = 0;
    const int meteredShifters = useMidSide ? 1 : numberOfChannels;
    for (int channel = 0; channel < meteredShifters; ++channel)
    {
        const auto& shifter = shifters_[static_cast<std::size_t>(channel)];
        latestBreathiness += shifter.getBreathiness();
        latestHarmonicity += shifter.getHarmonicity();
        latestNoisePath += shifter.getNoisePathAmount();
        latestNoiseReductionDb += shifter.getNoiseReductionDb();
        latestPolyphony += shifter.getPolyphony();
        latestSpectralReliability += shifter.getSpectralReliability();
        latestMaskStability += shifter.getMaskStability();
        latestOutputSourceCorrespondence += shifter.getOutputSourceCorrespondence();
        latestOutputTargetCoherence += shifter.getOutputTargetCoherence();
        latestOutputPhysicalHarmonicFit += shifter.getOutputPhysicalHarmonicFit();
        latestOutputLedgerHealth += shifter.getOutputLedgerHealth();
        latestOutputPhaseCoherence += shifter.getOutputPhaseCoherence();
        latestOutputReconstructionNeed += shifter.getOutputReconstructionNeed();
        latestOutputMeterValid += shifter.getOutputMeterValid();
        latestOutputSourceMirrorFit += shifter.getOutputSourceMirrorFit();
        latestOutputDoubleFamilyRisk += shifter.getOutputDoubleFamilyRisk();
        latestOutputLedgerDeficit += shifter.getOutputLedgerDeficit();
        latestOutputMemoryReliability += shifter.getOutputMemoryReliability();
        latestOutputPreIfftConsensus += shifter.getOutputPreIfftConsensus();
        latestOutputSelectiveReconstructionNeed += shifter.getOutputSelectiveReconstructionNeed();
        const auto& shadow = shifter.getShadowRidgeDiagnostics();
        latestShadowRidgeObservationCount += shadow.observationCount;
        latestShadowRidgeActiveCount += shadow.activeTrackCount;
        latestShadowRidgeBirthCount += shadow.bornTrackCount;
        latestShadowRidgeCoastCount += shadow.coastTrackCount;
        latestShadowRidgeDeathCount += shadow.deadTrackCount;
        latestShadowRidgeIdentitySwitchCount += shadow.identitySwitchCount;
        if (shadow.frameValid)
        {
            latestShadowRidgePredictionErrorRadians += shadow.meanPredictionErrorRadians;
            latestShadowRidgeReliability += shadow.meanReliability;
            latestShadowRidgeResolvedBinCoverage += shadow.resolvedBinCoverage;
            ++latestShadowRidgeValidCount;
        }
    }
    const float inverseMeteredShifters = 1.0f
        / static_cast<float>(std::max(1, meteredShifters));
    latestBreathiness *= inverseMeteredShifters;
    latestHarmonicity *= inverseMeteredShifters;
    latestNoisePath *= inverseMeteredShifters;
    latestNoiseReductionDb *= inverseMeteredShifters;
    latestPolyphony *= inverseMeteredShifters;
    latestSpectralReliability *= inverseMeteredShifters;
    latestMaskStability *= inverseMeteredShifters;
    latestOutputSourceCorrespondence *= inverseMeteredShifters;
    latestOutputTargetCoherence *= inverseMeteredShifters;
    latestOutputPhysicalHarmonicFit *= inverseMeteredShifters;
    latestOutputLedgerHealth *= inverseMeteredShifters;
    latestOutputPhaseCoherence *= inverseMeteredShifters;
    latestOutputReconstructionNeed *= inverseMeteredShifters;
    latestOutputMeterValid *= inverseMeteredShifters;
    latestOutputSourceMirrorFit *= inverseMeteredShifters;
    latestOutputDoubleFamilyRisk *= inverseMeteredShifters;
    latestOutputLedgerDeficit *= inverseMeteredShifters;
    latestOutputMemoryReliability *= inverseMeteredShifters;
    latestOutputPreIfftConsensus *= inverseMeteredShifters;
    latestOutputSelectiveReconstructionNeed *= inverseMeteredShifters;
    const float inverseValidShadowRidges = latestShadowRidgeValidCount > 0
        ? 1.0f / static_cast<float>(latestShadowRidgeValidCount)
        : 0.0f;
    latestShadowRidgePredictionErrorRadians *= inverseValidShadowRidges;
    latestShadowRidgeReliability *= inverseValidShadowRidges;
    latestShadowRidgeResolvedBinCoverage *= inverseValidShadowRidges;
    const bool latestShadowRidgeValid = latestShadowRidgeValidCount > 0;
    const float latestSustainedNoteSeconds = static_cast<float>(
        std::min(12.0, static_cast<double>(noteAgeSamples_) / sampleRate_));
    const auto tempoMeter = tempoController_.getMetering();


    // NEUMATON_V6_TEMPORAL_OCTAVE_DIAGNOSTICS
    // Block-level temporal diagnostics.  The spectral meters say whether a frame
    // is harmonically plausible; these meters say whether the output trajectory is
    // musically stable through time.  This is especially important for Live, where
    // a 256-sample frame can look plausible frame-by-frame while the target path
    // jumps or octave-locks incorrectly.
    const float latestTargetHzForTemporal = correctionController_.getTargetPitchHz();
    const float latestCorrectionCentsForTemporal = correctionController_.getCorrectionCents();
    const float temporalDtSeconds = static_cast<float>(std::max(
        1.0e-4,
        static_cast<double>(std::max(1, numberOfSamples)) / std::max(1.0, sampleRate_)));

    const bool temporalActiveFrame = latestOutputMeterValid > 0.50f
        && latestPitchHz > 20.0f
        && latestTargetHzForTemporal > 20.0f
        && latestConfidence > 0.15f
        && latestVoicing > 0.15f;

    const auto octaveResidualCents = [](float cents) noexcept -> float
    {
        const float absCents = std::abs(cents);
        const float nearestOctave = 1200.0f * static_cast<float>(
            std::max(1, static_cast<int>(std::lround(absCents / 1200.0f))));
        return std::abs(absCents - nearestOctave);
    };

    const auto smoothStress = [](float previous, float target) noexcept -> float
    {
        target = std::max(0.0f, target);
        const float coefficient = target > previous ? 0.34f : 0.10f;
        return std::max(0.0f, previous + coefficient * (target - previous));
    };

    const auto smoothStability = [](float previous, float target) noexcept -> float
    {
        target = std::clamp(target, 0.0f, 100.0f);
        const float coefficient = target < previous ? 0.34f : 0.10f;
        return std::clamp(previous + coefficient * (target - previous), 0.0f, 100.0f);
    };

    float targetJumpCentsRaw = 0.0f;
    float detectedJumpCentsRaw = 0.0f;
    float correctionVelocityRaw = 0.0f;
    float octaveConflictRaw = 0.0f;
    float transitionStressRaw = 0.0f;
    float temporalStabilityRaw = temporalActiveFrame ? 100.0f : 0.0f;

    if (temporalActiveFrame && outputTemporalInitialised_)
    {
        if (outputTemporalPreviousTargetHz_ > 20.0f)
        {
            targetJumpCentsRaw = static_cast<float>(std::abs(
                1200.0 * std::log2(
                    static_cast<double>(latestTargetHzForTemporal)
                    / static_cast<double>(outputTemporalPreviousTargetHz_))));
        }

        if (outputTemporalPreviousDetectedHz_ > 20.0f)
        {
            detectedJumpCentsRaw = static_cast<float>(std::abs(
                1200.0 * std::log2(
                    static_cast<double>(latestPitchHz)
                    / static_cast<double>(outputTemporalPreviousDetectedHz_))));
        }

        const float correctionDeltaCents = std::abs(
            latestCorrectionCentsForTemporal - outputTemporalPreviousCorrectionCents_);
        correctionVelocityRaw = correctionDeltaCents / temporalDtSeconds;

        const float targetJumpStress = smoothStep(60.0f, 360.0f, targetJumpCentsRaw);
        const float detectedJumpStress = smoothStep(80.0f, 480.0f, detectedJumpCentsRaw);
        const float velocityStress = smoothStep(700.0f, 5200.0f, correctionVelocityRaw);
        const float phaseDropStress = 1.0f - smoothStep(48.0f, 88.0f, latestOutputPhaseCoherence);
        const float transitionBlendStress = smoothStep(0.05f, 0.72f, transitionManager_.getBlend());

        const float targetOctaveResidual = octaveResidualCents(targetJumpCentsRaw);
        const float correctionOctaveResidual = octaveResidualCents(latestCorrectionCentsForTemporal);
        const float octaveLikeTargetJump = smoothStep(620.0f, 1020.0f, targetJumpCentsRaw)
            * (1.0f - smoothStep(85.0f, 260.0f, targetOctaveResidual));
        const float octaveLikeCorrection = smoothStep(680.0f, 1100.0f, std::abs(latestCorrectionCentsForTemporal))
            * (1.0f - smoothStep(85.0f, 280.0f, correctionOctaveResidual));
        const float pendingOctaveRisk = clamp01(
            0.34f * static_cast<float>(std::abs(latestOctaveState))
            + 0.22f * static_cast<float>(latestPendingOctave));
        const float lowConsensusRisk = 1.0f - smoothStep(0.26f, 0.82f, latestConsensus);

        octaveConflictRaw = 100.0f * clamp01(
            (0.58f * octaveLikeTargetJump + 0.42f * octaveLikeCorrection)
                * (0.32f + 0.68f * lowConsensusRisk)
            + 0.34f * pendingOctaveRisk);

        transitionStressRaw = 100.0f * clamp01(
            0.34f * velocityStress
            + 0.25f * targetJumpStress
            + 0.14f * detectedJumpStress
            + 0.13f * transitionBlendStress
            + 0.10f * phaseDropStress
            + 0.20f * (octaveConflictRaw / 100.0f));

        temporalStabilityRaw = 100.0f * (1.0f - clamp01(
            0.34f * velocityStress
            + 0.25f * targetJumpStress
            + 0.13f * detectedJumpStress
            + 0.10f * phaseDropStress
            + 0.22f * (octaveConflictRaw / 100.0f)));
    }

    if (temporalActiveFrame)
    {
        outputTemporalInitialised_ = true;
        outputTemporalPreviousTargetHz_ = latestTargetHzForTemporal;
        outputTemporalPreviousDetectedHz_ = latestPitchHz;
        outputTemporalPreviousCorrectionCents_ = latestCorrectionCentsForTemporal;
    }
    else
    {
        outputTemporalInitialised_ = false;
    }

    outputTemporalStability_ = smoothStability(outputTemporalStability_, temporalStabilityRaw);
    outputTargetJumpCents_ = smoothStress(outputTargetJumpCents_, targetJumpCentsRaw);
    outputCorrectionVelocityCentsPerSecond_ = smoothStress(
        outputCorrectionVelocityCentsPerSecond_, correctionVelocityRaw);
    outputOctaveConflict_ = smoothStress(outputOctaveConflict_, octaveConflictRaw);
    outputTransitionStress_ = smoothStress(outputTransitionStress_, transitionStressRaw);

    meterSequence_.fetch_add(1u, std::memory_order_acq_rel); // odd: publishing
    meterPitchHz_.store(latestPitchHz, std::memory_order_relaxed);
    meterTargetHz_.store(correctionController_.getTargetPitchHz(),
                         std::memory_order_relaxed);
    meterConfidence_.store(latestConfidence, std::memory_order_relaxed);
    meterVoicing_.store(latestVoicing, std::memory_order_relaxed);
    meterBreathiness_.store(latestBreathiness, std::memory_order_relaxed);
    meterHarmonicity_.store(latestHarmonicity, std::memory_order_relaxed);
    meterNoisePath_.store(latestNoisePath, std::memory_order_relaxed);
    meterNoiseReductionDb_.store(latestNoiseReductionDb, std::memory_order_relaxed);
    meterPolyphony_.store(latestPolyphony, std::memory_order_relaxed);
    meterSpectralReliability_.store(latestSpectralReliability, std::memory_order_relaxed);
    meterMaskStability_.store(latestMaskStability, std::memory_order_relaxed);
    meterSustainedNoteSeconds_.store(latestSustainedNoteSeconds,
                                     std::memory_order_relaxed);
    meterConsensus_.store(latestConsensus, std::memory_order_relaxed);
    meterCorrectionCents_.store(correctionController_.getCorrectionCents(),
                                std::memory_order_relaxed);
    meterWetMix_.store(correctionController_.getWetMix(),
                       std::memory_order_relaxed);
    meterTransitionBlend_.store(transitionManager_.getBlend(),
                                std::memory_order_relaxed);
    meterOutputSourceCorrespondence_.store(latestOutputSourceCorrespondence,
                                            std::memory_order_relaxed);
    meterOutputTargetCoherence_.store(latestOutputTargetCoherence,
                                      std::memory_order_relaxed);
    meterOutputPhysicalHarmonicFit_.store(latestOutputPhysicalHarmonicFit,
                                           std::memory_order_relaxed);
    meterOutputLedgerHealth_.store(latestOutputLedgerHealth,
                                   std::memory_order_relaxed);
    meterOutputPhaseCoherence_.store(latestOutputPhaseCoherence,
                                      std::memory_order_relaxed);
    meterOutputReconstructionNeed_.store(latestOutputReconstructionNeed,
                                           std::memory_order_relaxed);
    meterOutputMeterValid_.store(latestOutputMeterValid,
                                 std::memory_order_relaxed);
    meterOutputTemporalStability_.store(outputTemporalStability_,
                                         std::memory_order_relaxed);
    meterOutputTargetJumpCents_.store(outputTargetJumpCents_,
                                      std::memory_order_relaxed);
    meterOutputCorrectionVelocityCentsPerSecond_.store(
        outputCorrectionVelocityCentsPerSecond_, std::memory_order_relaxed);
    meterOutputOctaveConflict_.store(outputOctaveConflict_,
                                     std::memory_order_relaxed);
    meterOutputTransitionStress_.store(outputTransitionStress_,
                                       std::memory_order_relaxed);
    meterOutputSourceMirrorFit_.store(latestOutputSourceMirrorFit,
                                            std::memory_order_relaxed);
    meterOutputDoubleFamilyRisk_.store(latestOutputDoubleFamilyRisk,
                                             std::memory_order_relaxed);
    meterOutputLedgerDeficit_.store(latestOutputLedgerDeficit,
                                           std::memory_order_relaxed);
    meterOutputMemoryReliability_.store(latestOutputMemoryReliability,
                                               std::memory_order_relaxed);
    meterOutputPreIfftConsensus_.store(latestOutputPreIfftConsensus,
                                              std::memory_order_relaxed);
    meterOutputSelectiveReconstructionNeed_.store(
        latestOutputSelectiveReconstructionNeed, std::memory_order_relaxed);
    meterShadowRidgeObservationCount_.store(
        latestShadowRidgeObservationCount, std::memory_order_relaxed);
    meterShadowRidgeActiveCount_.store(
        latestShadowRidgeActiveCount, std::memory_order_relaxed);
    meterShadowRidgeBirthCount_.store(
        latestShadowRidgeBirthCount, std::memory_order_relaxed);
    meterShadowRidgeCoastCount_.store(
        latestShadowRidgeCoastCount, std::memory_order_relaxed);
    meterShadowRidgeDeathCount_.store(
        latestShadowRidgeDeathCount, std::memory_order_relaxed);
    meterShadowRidgeIdentitySwitchCount_.store(
        latestShadowRidgeIdentitySwitchCount, std::memory_order_relaxed);
    meterShadowRidgePredictionErrorRadians_.store(
        latestShadowRidgePredictionErrorRadians, std::memory_order_relaxed);
    meterShadowRidgeReliability_.store(
        latestShadowRidgeReliability, std::memory_order_relaxed);
    meterShadowRidgeResolvedBinCoverage_.store(
        latestShadowRidgeResolvedBinCoverage, std::memory_order_relaxed);
    meterShadowRidgeValid_.store(
        latestShadowRidgeValid, std::memory_order_relaxed);
    meterDualSynthesisActive_.store(
        transitionManager_.isDualSynthesisActive(),
        std::memory_order_relaxed);
    meterDetectorSupport_.store(latestDetectorSupport,
                                std::memory_order_relaxed);
    meterOctaveState_.store(latestOctaveState,
                            std::memory_order_relaxed);
    meterPendingOctaveObservations_.store(latestPendingOctave,
                                          std::memory_order_relaxed);
    meterState_.store(static_cast<int>(correctionController_.getState()),
                      std::memory_order_relaxed);
    meterTempoBpm_.store(tempoMeter.bpm, std::memory_order_relaxed);
    meterTempoGridPhase_.store(tempoMeter.gridPhase, std::memory_order_relaxed);
    meterTempoGlideTimeMs_.store(tempoMeter.glideTimeMs, std::memory_order_relaxed);
    meterTempoActive_.store(tempoMeter.active, std::memory_order_relaxed);
    meterTempoWaiting_.store(tempoMeter.waitingForGrid, std::memory_order_relaxed);
    meterTempoHostSync_.store(tempoMeter.hostSyncValid, std::memory_order_relaxed);
    meterTempoMode_.store(static_cast<int>(tempoMeter.mode),
                          std::memory_order_relaxed);
    meterSequence_.fetch_add(1u, std::memory_order_release); // even: complete
    appendV6DiagnosticsCsv(getMetering(), numberOfSamples);
}

void ModernPitchEngine::process(juce::AudioBuffer<float>& buffer,
                                const std::vector<double>& scaleRatios,
                                double rootFrequency,
                                const Parameters& parameters)
{
    process(buffer,
            scaleRatios.empty() ? nullptr : scaleRatios.data(),
            static_cast<int>(scaleRatios.size()),
            rootFrequency,
            parameters);
}

void ModernPitchEngine::process(float* monoData,
                                int numberOfSamples,
                                const std::vector<double>& scaleRatios,
                                double rootFrequency,
                                const Parameters& parameters)
{
    if (monoData == nullptr || numberOfSamples <= 0)
        return;

    float* channels[] { monoData };
    juce::AudioBuffer<float> view(channels, 1, numberOfSamples);
    process(view, scaleRatios, rootFrequency, parameters);
}

void ModernPitchEngine::processBypassed(juce::AudioBuffer<float>& buffer)
{
    const int numberOfSamples = buffer.getNumSamples();
    const int numberOfChannels = std::min({ buffer.getNumChannels(),
                                            preparedChannels_,
                                            maxSupportedChannels });
    if (numberOfSamples <= 0 || numberOfChannels <= 0)
        return;

    std::array<float*, maxSupportedChannels> channelData {};
    for (int channel = 0; channel < numberOfChannels; ++channel)
    {
        channelData[static_cast<std::size_t>(channel)] = buffer.getWritePointer(channel);
        float* data = channelData[static_cast<std::size_t>(channel)];
        for (int sample = 0; sample < numberOfSamples; ++sample)
            data[sample] = sanitiseAudioSample(data[sample]);
    }

    if (!bypassActive_)
    {
        correctionController_.reset();
        transitionManager_.reset();
        tempoController_.reset();
        noteAgeTargetHz_ = 0.0f;
        noteAgeSamples_ = 0;
        bypassActive_ = true;
    }

    const bool useMidSide = currentStereoMode_ == StereoMode::linkedMidSide
                         && numberOfChannels >= 2;

    for (int sampleIndex = 0; sampleIndex < numberOfSamples; ++sampleIndex)
    {
        if (useMidSide)
        {
            const float left = channelData[0][sampleIndex];
            const float right = channelData[1][sampleIndex];
            const float mid = 0.5f * (left + right);
            const float side = 0.5f * (left - right);
            const float delayedMid = shifters_[0].processBypassedSample(mid);
            const float delayedSide = auxiliaryDelays_[0].process(side);
            channelData[0][sampleIndex] = delayedMid + delayedSide;
            channelData[1][sampleIndex] = delayedMid - delayedSide;

            for (int channel = 2; channel < numberOfChannels; ++channel)
            {
                float& sample = channelData[static_cast<std::size_t>(channel)][sampleIndex];
                sample = shifters_[static_cast<std::size_t>(channel)]
                    .processBypassedSample(sample);
            }
        }
        else
        {
            for (int channel = 0; channel < numberOfChannels; ++channel)
            {
                float& sample = channelData[static_cast<std::size_t>(channel)][sampleIndex];
                sample = shifters_[static_cast<std::size_t>(channel)]
                    .processBypassedSample(sample);
            }
        }
    }

    meterSequence_.fetch_add(1u, std::memory_order_acq_rel);
    meterPitchHz_.store(0.0f, std::memory_order_relaxed);
    meterTargetHz_.store(0.0f, std::memory_order_relaxed);
    meterConfidence_.store(0.0f, std::memory_order_relaxed);
    meterVoicing_.store(0.0f, std::memory_order_relaxed);
    meterBreathiness_.store(0.0f, std::memory_order_relaxed);
    meterHarmonicity_.store(0.0f, std::memory_order_relaxed);
    meterNoisePath_.store(0.0f, std::memory_order_relaxed);
    meterNoiseReductionDb_.store(0.0f, std::memory_order_relaxed);
    meterPolyphony_.store(0.0f, std::memory_order_relaxed);
    meterSpectralReliability_.store(0.0f, std::memory_order_relaxed);
    meterMaskStability_.store(1.0f, std::memory_order_relaxed);
    meterSustainedNoteSeconds_.store(0.0f, std::memory_order_relaxed);
    meterConsensus_.store(0.0f, std::memory_order_relaxed);
    meterCorrectionCents_.store(0.0f, std::memory_order_relaxed);
    meterWetMix_.store(0.0f, std::memory_order_relaxed);
    meterTransitionBlend_.store(0.0f, std::memory_order_relaxed);
    meterOutputSourceCorrespondence_.store(0.0f, std::memory_order_relaxed);
    meterOutputTargetCoherence_.store(0.0f, std::memory_order_relaxed);
    meterOutputPhysicalHarmonicFit_.store(0.0f, std::memory_order_relaxed);
    meterOutputLedgerHealth_.store(100.0f, std::memory_order_relaxed);
    meterOutputPhaseCoherence_.store(0.0f, std::memory_order_relaxed);
    meterOutputReconstructionNeed_.store(0.0f, std::memory_order_relaxed);
    meterOutputMeterValid_.store(0.0f, std::memory_order_relaxed);
    meterOutputTemporalStability_.store(0.0f, std::memory_order_relaxed);
    meterOutputTargetJumpCents_.store(0.0f, std::memory_order_relaxed);
    meterOutputCorrectionVelocityCentsPerSecond_.store(0.0f, std::memory_order_relaxed);
    meterOutputOctaveConflict_.store(0.0f, std::memory_order_relaxed);
    meterOutputTransitionStress_.store(0.0f, std::memory_order_relaxed);
    meterOutputSourceMirrorFit_.store(0.0f, std::memory_order_relaxed);
    meterOutputDoubleFamilyRisk_.store(0.0f, std::memory_order_relaxed);
    meterOutputLedgerDeficit_.store(0.0f, std::memory_order_relaxed);
    meterOutputMemoryReliability_.store(0.0f, std::memory_order_relaxed);
    meterOutputPreIfftConsensus_.store(0.0f, std::memory_order_relaxed);
    meterOutputSelectiveReconstructionNeed_.store(0.0f, std::memory_order_relaxed);
    meterShadowRidgeObservationCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeActiveCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeBirthCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeCoastCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeDeathCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgeIdentitySwitchCount_.store(0, std::memory_order_relaxed);
    meterShadowRidgePredictionErrorRadians_.store(0.0f, std::memory_order_relaxed);
    meterShadowRidgeReliability_.store(0.0f, std::memory_order_relaxed);
    meterShadowRidgeResolvedBinCoverage_.store(0.0f, std::memory_order_relaxed);
    meterShadowRidgeValid_.store(false, std::memory_order_relaxed);
    meterDualSynthesisActive_.store(false, std::memory_order_relaxed);
    meterDetectorSupport_.store(0, std::memory_order_relaxed);
    meterOctaveState_.store(0, std::memory_order_relaxed);
    meterPendingOctaveObservations_.store(0, std::memory_order_relaxed);
    meterState_.store(static_cast<int>(TrackingState::unvoiced),
                      std::memory_order_relaxed);
    meterTempoBpm_.store(120.0f, std::memory_order_relaxed);
    meterTempoGridPhase_.store(0.0f, std::memory_order_relaxed);
    meterTempoGlideTimeMs_.store(0.0f, std::memory_order_relaxed);
    meterTempoActive_.store(false, std::memory_order_relaxed);
    meterTempoWaiting_.store(false, std::memory_order_relaxed);
    meterTempoHostSync_.store(false, std::memory_order_relaxed);
    meterTempoMode_.store(static_cast<int>(CreativeTempo::Mode::off),
                          std::memory_order_relaxed);
    meterSequence_.fetch_add(1u, std::memory_order_release);
}

ModernPitchEngine::Metering ModernPitchEngine::getMetering() const noexcept
{
    Metering result;

    // A bounded seqlock read gives the GUI a coherent snapshot without ever
    // blocking the audio thread.  In the extremely unlikely case of repeated
    // contention, the last read is still safe because every field is atomic.
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const std::uint32_t before = meterSequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;

        result.detectedPitchHz = meterPitchHz_.load(std::memory_order_relaxed);
        result.targetPitchHz = meterTargetHz_.load(std::memory_order_relaxed);
        result.confidence = meterConfidence_.load(std::memory_order_relaxed);
        result.voicing = meterVoicing_.load(std::memory_order_relaxed);
        result.breathiness = meterBreathiness_.load(std::memory_order_relaxed);
        result.harmonicity = meterHarmonicity_.load(std::memory_order_relaxed);
        result.noisePath = meterNoisePath_.load(std::memory_order_relaxed);
        result.noiseReductionDb = meterNoiseReductionDb_.load(std::memory_order_relaxed);
        result.polyphony = meterPolyphony_.load(std::memory_order_relaxed);
        result.spectralReliability = meterSpectralReliability_.load(
            std::memory_order_relaxed);
        result.maskStability = meterMaskStability_.load(std::memory_order_relaxed);
        result.sustainedNoteSeconds = meterSustainedNoteSeconds_.load(
            std::memory_order_relaxed);
        result.consensus = meterConsensus_.load(std::memory_order_relaxed);
        result.correctionCents = meterCorrectionCents_.load(std::memory_order_relaxed);
        result.wetMix = meterWetMix_.load(std::memory_order_relaxed);
        result.transitionBlend = meterTransitionBlend_.load(
            std::memory_order_relaxed);
        result.outputSourceCorrespondence = meterOutputSourceCorrespondence_.load(
            std::memory_order_relaxed);
        result.outputTargetCoherence = meterOutputTargetCoherence_.load(
            std::memory_order_relaxed);
        result.outputPhysicalHarmonicFit = meterOutputPhysicalHarmonicFit_.load(
            std::memory_order_relaxed);
        result.outputLedgerHealth = meterOutputLedgerHealth_.load(
            std::memory_order_relaxed);
        result.outputPhaseCoherence = meterOutputPhaseCoherence_.load(
            std::memory_order_relaxed);
        result.outputReconstructionNeed = meterOutputReconstructionNeed_.load(
            std::memory_order_relaxed);
        result.outputMeterValid = meterOutputMeterValid_.load(
            std::memory_order_relaxed);
        result.outputTemporalStability = meterOutputTemporalStability_.load(
            std::memory_order_relaxed);
        result.outputTargetJumpCents = meterOutputTargetJumpCents_.load(
            std::memory_order_relaxed);
        result.outputCorrectionVelocityCentsPerSecond =
            meterOutputCorrectionVelocityCentsPerSecond_.load(
                std::memory_order_relaxed);
        result.outputOctaveConflict = meterOutputOctaveConflict_.load(
            std::memory_order_relaxed);
        result.outputTransitionStress = meterOutputTransitionStress_.load(
            std::memory_order_relaxed);
        result.outputSourceMirrorFit = meterOutputSourceMirrorFit_.load(
            std::memory_order_relaxed);
        result.outputDoubleFamilyRisk = meterOutputDoubleFamilyRisk_.load(
            std::memory_order_relaxed);
        result.outputLedgerDeficit = meterOutputLedgerDeficit_.load(
            std::memory_order_relaxed);
        result.outputMemoryReliability = meterOutputMemoryReliability_.load(
            std::memory_order_relaxed);
        result.outputPreIfftConsensus = meterOutputPreIfftConsensus_.load(
            std::memory_order_relaxed);
        result.outputSelectiveReconstructionNeed =
            meterOutputSelectiveReconstructionNeed_.load(std::memory_order_relaxed);
        result.shadowRidgeObservationCount =
            meterShadowRidgeObservationCount_.load(std::memory_order_relaxed);
        result.shadowRidgeActiveCount =
            meterShadowRidgeActiveCount_.load(std::memory_order_relaxed);
        result.shadowRidgeBirthCount =
            meterShadowRidgeBirthCount_.load(std::memory_order_relaxed);
        result.shadowRidgeCoastCount =
            meterShadowRidgeCoastCount_.load(std::memory_order_relaxed);
        result.shadowRidgeDeathCount =
            meterShadowRidgeDeathCount_.load(std::memory_order_relaxed);
        result.shadowRidgeIdentitySwitchCount =
            meterShadowRidgeIdentitySwitchCount_.load(std::memory_order_relaxed);
        result.shadowRidgePredictionErrorRadians =
            meterShadowRidgePredictionErrorRadians_.load(std::memory_order_relaxed);
        result.shadowRidgeReliability =
            meterShadowRidgeReliability_.load(std::memory_order_relaxed);
        result.shadowRidgeResolvedBinCoverage =
            meterShadowRidgeResolvedBinCoverage_.load(std::memory_order_relaxed);
        result.shadowRidgeValid =
            meterShadowRidgeValid_.load(std::memory_order_relaxed);
        result.dualSynthesisActive = meterDualSynthesisActive_.load(
            std::memory_order_relaxed);
        result.detectorSupport = meterDetectorSupport_.load(std::memory_order_relaxed);
        result.octaveState = meterOctaveState_.load(std::memory_order_relaxed);
        result.pendingOctaveObservations = meterPendingOctaveObservations_.load(
            std::memory_order_relaxed);
        result.state = static_cast<TrackingState>(
            meterState_.load(std::memory_order_relaxed));
        result.tempoBpm = meterTempoBpm_.load(std::memory_order_relaxed);
        result.tempoGridPhase = meterTempoGridPhase_.load(std::memory_order_relaxed);
        result.tempoGlideTimeMs = meterTempoGlideTimeMs_.load(std::memory_order_relaxed);
        result.tempoActive = meterTempoActive_.load(std::memory_order_relaxed);
        result.tempoWaitingForGrid = meterTempoWaiting_.load(std::memory_order_relaxed);
        result.tempoHostSyncValid = meterTempoHostSync_.load(std::memory_order_relaxed);
        result.tempoMode = static_cast<CreativeTempo::Mode>(
            meterTempoMode_.load(std::memory_order_relaxed));

        const std::uint32_t after = meterSequence_.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u)
            return result;
    }

    return result;
}


