#include "ModernPitchEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr float minimumDetectorRms = 0.0012f;

[[nodiscard]] float sanitiseAudioSample(float value) noexcept
{
    if (!std::isfinite(value) || std::fpclassify(value) == FP_SUBNORMAL)
        return 0.0f;
    return std::clamp(value, -32.0f, 32.0f);
}

[[nodiscard]] float smoothStep(float edge0, float edge1, float value) noexcept
{
    if (edge1 <= edge0)
        return value >= edge1 ? 1.0f : 0.0f;
    const float x = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

[[nodiscard]] double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] float finiteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}
} // namespace

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

//==============================================================================
// ScaleQuantizer

std::uint64_t ModernPitchEngine::ScaleQuantizer::hashScale(
    const double* ratios,
    int count,
    double root) noexcept
{
    constexpr std::uint64_t offset = 1469598103934665603ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    const auto mix = [&hash](std::uint64_t value) noexcept
    {
        hash ^= value;
        hash *= prime;
    };

    mix(static_cast<std::uint64_t>(std::max(0, count)));
    std::uint64_t bits = 0;
    std::memcpy(&bits, &root, sizeof(bits));
    mix(bits);
    for (int i = 0; ratios != nullptr && i < count; ++i)
    {
        bits = 0;
        std::memcpy(&bits, ratios + i, sizeof(bits));
        mix(bits);
    }
    return hash;
}

void ModernPitchEngine::ScaleQuantizer::reset() noexcept
{
    logRatios_.fill(0.0);
    ratioCount_ = 1;
    rootLog2_ = ModernPitchEngine::safeLog2(440.0);
    hash_ = 0;
    minStepCents_ = 1200.0f;
    asymmetry_ = 0.0f;
    targetValid_ = false;
    targetLog2_ = 0.0;
    pendingValid_ = false;
    pendingLog2_ = 0.0;
    pendingCount_ = 0;
}

bool ModernPitchEngine::ScaleQuantizer::setScale(
    const double* ratios,
    int ratioCount,
    double rootFrequency) noexcept
{
    const double safeRoot = std::isfinite(rootFrequency) && rootFrequency > 0.0
        ? rootFrequency : 440.0;
    const int safeCount = std::clamp(ratioCount, 0, maxScaleRatios);
    const auto nextHash = hashScale(ratios, safeCount, safeRoot);
    if (nextHash == hash_)
        return false;

    hash_ = nextHash;
    rootLog2_ = ModernPitchEngine::safeLog2(safeRoot);
    ratioCount_ = 0;
    logRatios_[static_cast<std::size_t>(ratioCount_++)] = 0.0;

    for (int i = 0; ratios != nullptr && i < safeCount
         && ratioCount_ < maxScaleRatios; ++i)
    {
        const double ratio = ratios[i];
        if (!std::isfinite(ratio) || ratio <= 0.0)
            continue;
        double folded = std::log2(ratio);
        folded -= std::floor(folded);
        if (folded >= 1.0 - 1.0e-10)
            folded = 0.0;

        bool duplicate = false;
        for (int j = 0; j < ratioCount_; ++j)
        {
            if (std::abs(logRatios_[static_cast<std::size_t>(j)] - folded) < 1.0e-8)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            logRatios_[static_cast<std::size_t>(ratioCount_++)] = folded;
    }

    std::sort(logRatios_.begin(), logRatios_.begin() + ratioCount_);

    std::array<double, maxScaleRatios> steps {};
    double mean = 1200.0 / static_cast<double>(std::max(1, ratioCount_));
    minStepCents_ = 1200.0f;
    double variance = 0.0;
    for (int i = 0; i < ratioCount_; ++i)
    {
        const int next = (i + 1) % ratioCount_;
        double step = next > i
            ? (logRatios_[static_cast<std::size_t>(next)]
               - logRatios_[static_cast<std::size_t>(i)]) * 1200.0
            : (1.0 + logRatios_[0]
               - logRatios_[static_cast<std::size_t>(i)]) * 1200.0;
        step = std::max(0.1, step);
        steps[static_cast<std::size_t>(i)] = step;
        minStepCents_ = std::min(minStepCents_, static_cast<float>(step));
    }
    for (int i = 0; i < ratioCount_; ++i)
    {
        const double d = steps[static_cast<std::size_t>(i)] - mean;
        variance += d * d;
    }
    variance /= static_cast<double>(std::max(1, ratioCount_));
    asymmetry_ = static_cast<float>(std::clamp(
        std::sqrt(variance) / std::max(1.0, mean), 0.0, 1.0));

    targetValid_ = false;
    pendingValid_ = false;
    pendingCount_ = 0;
    return true;
}

double ModernPitchEngine::ScaleQuantizer::chooseTargetLog2(
    double inputLog2,
    float hysteresisCents,
    float strictness,
    float confidence,
    bool hardLock,
    bool onset,
    int& pendingObservations) noexcept
{
    pendingObservations = 0;
    if (!std::isfinite(inputLog2) || ratioCount_ <= 0)
        return inputLog2;

    const double relative = inputLog2 - rootLog2_;
    const double octave = std::floor(relative);
    double nearest = inputLog2;
    double nearestDistance = std::numeric_limits<double>::infinity();

    for (int i = 0; i < ratioCount_; ++i)
    {
        const double degree = logRatios_[static_cast<std::size_t>(i)];
        for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset)
        {
            const double candidate = rootLog2_ + octave
                + static_cast<double>(octaveOffset) + degree;
            const double distance = std::abs(candidate - inputLog2);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = candidate;
            }
        }
    }

    if (!targetValid_ || onset)
    {
        targetLog2_ = nearest;
        targetValid_ = true;
        pendingValid_ = false;
        pendingCount_ = 0;
        return targetLog2_;
    }

    // Keep the existing target until the challenger wins by the requested
    // hysteresis margin. This acts in target selection, never as a dry/wet gate.
    const double previousDistance = std::abs(targetLog2_ - inputLog2);
    const double hysteresisOctaves = std::max(0.0f, hysteresisCents) / 1200.0;
    if (previousDistance <= nearestDistance + hysteresisOctaves)
    {
        pendingValid_ = false;
        pendingCount_ = 0;
        return targetLog2_;
    }

    const double jumpCents = std::abs(nearest - targetLog2_) * 1200.0;
    if (!hardLock || jumpCents < 0.5)
    {
        targetLog2_ = nearest;
        pendingValid_ = false;
        pendingCount_ = 0;
        return targetLog2_;
    }

    if (pendingValid_ && std::abs(pendingLog2_ - nearest) * 1200.0 < 2.0)
        ++pendingCount_;
    else
    {
        pendingValid_ = true;
        pendingLog2_ = nearest;
        pendingCount_ = 1;
    }

    const float safeStrictness = ModernPitchEngine::clamp01(strictness);
    const float safeConfidence = ModernPitchEngine::clamp01(confidence);
    const int required = 1 + static_cast<int>(std::lround(
        2.0f * safeStrictness + 1.5f * (1.0f - safeConfidence)));
    pendingObservations = pendingCount_;
    if (pendingCount_ >= required)
    {
        targetLog2_ = pendingLog2_;
        pendingValid_ = false;
        pendingCount_ = 0;
        pendingObservations = 0;
    }
    return targetLog2_;
}

//==============================================================================
// Full-signal transport

void ModernPitchEngine::TransportClock::prepare(int reportedLatencySamples) noexcept
{
    minimumDelay_ = 8;
    rangeSamples_ = std::max(16,
        2 * (std::max(16, reportedLatencySamples) - minimumDelay_));
    reset();
}

void ModernPitchEngine::TransportClock::reset() noexcept
{
    phase_ = 0.5;
}

ModernPitchEngine::TransportPlan
ModernPitchEngine::TransportClock::next(double ratio) noexcept
{
    TransportPlan plan;
    const double safeRatio = std::clamp(
        std::isfinite(ratio) ? ratio : 1.0, 0.25, 4.0);
    const double deviation = std::abs(1.0 - safeRatio);

    if (deviation < 1.0e-9)
    {
        phase_ = 0.5;
        plan.delayA = static_cast<double>(minimumDelay_)
                    + 0.5 * static_cast<double>(rangeSamples_);
        plan.delayB = plan.delayA;
        return plan;
    }

    const double phaseB = phase_ < 0.5 ? phase_ + 0.5 : phase_ - 0.5;
    const auto weight = [](double phase) noexcept
    {
        return static_cast<float>(0.5 - 0.5 * std::cos(twoPi * phase));
    };
    const auto delay = [this, safeRatio](double phase) noexcept
    {
        const double direction = safeRatio >= 1.0 ? 1.0 - phase : phase;
        return static_cast<double>(minimumDelay_)
             + direction * static_cast<double>(rangeSamples_);
    };

    plan.delayA = delay(phase_);
    plan.delayB = delay(phaseB);
    plan.gainA = weight(phase_);
    plan.gainB = weight(phaseB);
    const float sum = plan.gainA + plan.gainB;
    if (sum > 1.0e-8f)
    {
        plan.gainA /= sum;
        plan.gainB /= sum;
    }

    phase_ += std::min(0.24, deviation / static_cast<double>(rangeSamples_));
    phase_ -= std::floor(phase_);
    return plan;
}

void ModernPitchEngine::ChannelPath::prepare(double sampleRate,
                                             int reportedLatencySamples)
{
    static_cast<void>(reportedLatencySamples);
    const double safeRate = std::max(8000.0, finiteOr(sampleRate, 48000.0));
    coefficientSmoothing_ = std::clamp(static_cast<float>(
        1.0 - std::exp(-1.0 / (0.018 * safeRate))), 0.0005f, 0.08f);
    breathLowPassCoefficient_ = std::clamp(static_cast<float>(
        1.0 - std::exp(-twoPi * 3200.0 / safeRate)), 0.001f, 0.95f);
    reset();
}

void ModernPitchEngine::ChannelPath::reset() noexcept
{
    residualRing_.fill(0.0f);
    bypassRing_.fill(0.0f);
    inputHistory_.fill(0.0f);
    outputHistory_.fill(0.0f);
    currentLpc_.fill(0.0f);
    targetLpc_.fill(0.0f);
    sampleCounter_ = 0;
    breathLowPass_ = 0.0f;
    breathReduction_ = 0.0f;
    targetBreathReduction_ = 0.0f;
}

void ModernPitchEngine::ChannelPath::setVoiceModel(
    const std::array<float, maximumLpcOrder>& coefficients,
    float formantStrength,
    float breathReduction) noexcept
{
    const float safeStrength = std::clamp(formantStrength, 0.0f, 0.96f);
    for (int i = 0; i < maximumLpcOrder; ++i)
    {
        const float bandwidth = std::pow(0.94f, static_cast<float>(i + 1));
        targetLpc_[static_cast<std::size_t>(i)] = std::clamp(
            coefficients[static_cast<std::size_t>(i)]
                * safeStrength * bandwidth,
            -0.95f, 0.95f);
    }
    targetBreathReduction_ = std::clamp(breathReduction, 0.0f, 0.75f);
}

float ModernPitchEngine::ChannelPath::sanitise(float value) noexcept
{
    if (!std::isfinite(value) || std::fpclassify(value) == FP_SUBNORMAL)
        return 0.0f;
    return std::clamp(value, -8.0f, 8.0f);
}

float ModernPitchEngine::ChannelPath::interpolateResidual(
    double absolutePosition) const noexcept
{
    if (!std::isfinite(absolutePosition))
        return 0.0f;
    const auto lowerAbsolute = static_cast<std::int64_t>(
        std::floor(absolutePosition));
    const double fraction = absolutePosition
        - static_cast<double>(lowerAbsolute);
    const int lower = static_cast<int>(
        lowerAbsolute & (transportRingSize - 1));
    const int upper = (lower + 1) & (transportRingSize - 1);
    const float a = residualRing_[static_cast<std::size_t>(lower)];
    const float b = residualRing_[static_cast<std::size_t>(upper)];
    return a + static_cast<float>(fraction) * (b - a);
}

float ModernPitchEngine::ChannelPath::process(
    float input,
    const TransportPlan& plan) noexcept
{
    const float safeInput = sanitise(input);
    for (int i = 0; i < maximumLpcOrder; ++i)
    {
        currentLpc_[static_cast<std::size_t>(i)] += coefficientSmoothing_
            * (targetLpc_[static_cast<std::size_t>(i)]
               - currentLpc_[static_cast<std::size_t>(i)]);
    }
    breathReduction_ += coefficientSmoothing_
        * (targetBreathReduction_ - breathReduction_);

    double prediction = 0.0;
    for (int i = 0; i < maximumLpcOrder; ++i)
        prediction += static_cast<double>(currentLpc_[static_cast<std::size_t>(i)])
                    * static_cast<double>(inputHistory_[static_cast<std::size_t>(i)]);
    const float residual = sanitise(safeInput - static_cast<float>(prediction));

    for (int i = maximumLpcOrder - 1; i > 0; --i)
        inputHistory_[static_cast<std::size_t>(i)]
            = inputHistory_[static_cast<std::size_t>(i - 1)];
    inputHistory_[0] = safeInput;

    const int write = static_cast<int>(sampleCounter_ & (transportRingSize - 1));
    residualRing_[static_cast<std::size_t>(write)] = residual;
    const float shiftedResidual = sanitise(
        plan.gainA * interpolateResidual(static_cast<double>(sampleCounter_) - plan.delayA)
      + plan.gainB * interpolateResidual(static_cast<double>(sampleCounter_) - plan.delayB));

    double synthesisPrediction = 0.0;
    for (int i = 0; i < maximumLpcOrder; ++i)
        synthesisPrediction += static_cast<double>(currentLpc_[static_cast<std::size_t>(i)])
                             * static_cast<double>(outputHistory_[static_cast<std::size_t>(i)]);
    float output = sanitise(shiftedResidual + static_cast<float>(synthesisPrediction));

    // Breath reduction is a post-transport spectral tilt on the same full signal.
    // It is not a parallel dry path and cannot reduce pitch-correction authority.
    breathLowPass_ += breathLowPassCoefficient_ * (output - breathLowPass_);
    const float highBand = output - breathLowPass_;
    output = sanitise(output - highBand * breathReduction_);

    for (int i = maximumLpcOrder - 1; i > 0; --i)
        outputHistory_[static_cast<std::size_t>(i)]
            = outputHistory_[static_cast<std::size_t>(i - 1)];
    outputHistory_[0] = output;
    ++sampleCounter_;
    return output;
}

float ModernPitchEngine::ChannelPath::processBypassed(float input,
                                                      int latencySamples) noexcept
{
    const float safeInput = sanitise(input);
    const int write = static_cast<int>(sampleCounter_ & (transportRingSize - 1));
    bypassRing_[static_cast<std::size_t>(write)] = safeInput;
    const auto readAbsolute = sampleCounter_
        - static_cast<std::int64_t>(std::max(0, latencySamples));
    const int read = static_cast<int>(readAbsolute & (transportRingSize - 1));
    const float output = bypassRing_[static_cast<std::size_t>(read)];
    ++sampleCounter_;
    return sanitise(output);
}

//==============================================================================
// ModernPitchEngine control and processing

float ModernPitchEngine::clamp01(float value) noexcept
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

double ModernPitchEngine::safeLog2(double value) noexcept
{
    return std::log2(std::max(value, 1.0e-12));
}

double ModernPitchEngine::wrapToNearestOctave(double cents) noexcept
{
    if (!std::isfinite(cents))
        return 0.0;
    return cents - 1200.0 * std::nearbyint(cents / 1200.0);
}

int ModernPitchEngine::latencyForMode(LatencyMode mode) noexcept
{
    switch (mode)
    {
        case LatencyMode::ultraLive: return 128;
        case LatencyMode::live:      return 256;
        case LatencyMode::quality:   return 512;
    }
    return 256;
}

void ModernPitchEngine::prepare(double sampleRate,
                                int maximumExpectedSamplesPerBlock,
                                int numberOfChannels,
                                LatencyMode latencyMode)
{
    sampleRate_ = std::max(8000.0, finiteOr(sampleRate, 48000.0));
    maximumBlockSize_ = std::max(1, maximumExpectedSamplesPerBlock);
    channelCount_ = std::clamp(numberOfChannels, 1, maxSupportedChannels);
    latencyMode_ = latencyMode;
    latencySamples_ = latencyForMode(latencyMode_);
    monoScratch_.assign(static_cast<std::size_t>(maximumBlockSize_), 0.0f);

    linkedTracker_.prepare(sampleRate_);
    linkedClock_.prepare(latencySamples_);
    tempoController_.prepare(sampleRate_);
    for (int channel = 0; channel < maxSupportedChannels; ++channel)
    {
        channelTrackers_[static_cast<std::size_t>(channel)].prepare(sampleRate_);
        channelClocks_[static_cast<std::size_t>(channel)].prepare(latencySamples_);
        channelPaths_[static_cast<std::size_t>(channel)].prepare(sampleRate_, latencySamples_);
        channelTempoControllers_[static_cast<std::size_t>(channel)].prepare(sampleRate_);
    }
    reset();
}

void ModernPitchEngine::reset() noexcept
{
    linkedTracker_.reset();
    linkedQuantizer_.reset();
    linkedClock_.reset();
    tempoController_.reset();
    linkedCorrection_ = {};
    for (int channel = 0; channel < maxSupportedChannels; ++channel)
    {
        channelTrackers_[static_cast<std::size_t>(channel)].reset();
        channelQuantizers_[static_cast<std::size_t>(channel)].reset();
        channelClocks_[static_cast<std::size_t>(channel)].reset();
        channelPaths_[static_cast<std::size_t>(channel)].reset();
        channelTempoControllers_[static_cast<std::size_t>(channel)].reset();
        channelCorrections_[static_cast<std::size_t>(channel)] = {};
    }
    std::fill(monoScratch_.begin(), monoScratch_.end(), 0.0f);
    currentLpcTarget_.fill(0.0f);
    latestObservation_ = {};
    latestChannelObservation_.fill(PitchObservation {});
    audibleCorrectionCents_ = 0.0;
    sustainedSamples_ = 0;

    meterSequence_.store(0u, std::memory_order_relaxed);
    meterPitchHz_.store(0.0f, std::memory_order_relaxed);
    meterTargetHz_.store(0.0f, std::memory_order_relaxed);
    meterConfidence_.store(0.0f, std::memory_order_relaxed);
    meterVoicing_.store(0.0f, std::memory_order_relaxed);
    meterPeriodicity_.store(0.0f, std::memory_order_relaxed);
    meterCorrectionCents_.store(0.0f, std::memory_order_relaxed);
    meterCorrectionVelocity_.store(0.0f, std::memory_order_relaxed);
    meterOnsetStrength_.store(0.0f, std::memory_order_relaxed);
    meterTargetJumpCents_.store(0.0f, std::memory_order_relaxed);
    meterSustainedSeconds_.store(0.0f, std::memory_order_relaxed);
    meterDetectorSupport_.store(0, std::memory_order_relaxed);
    meterOctaveState_.store(0, std::memory_order_relaxed);
    meterPendingOctave_.store(0, std::memory_order_relaxed);
    meterTrackingState_.store(static_cast<int>(TrackingState::unvoiced),
                              std::memory_order_relaxed);
    meterTempoBpm_.store(120.0f, std::memory_order_relaxed);
    meterTempoGridPhase_.store(0.0f, std::memory_order_relaxed);
    meterTempoGlideTimeMs_.store(0.0f, std::memory_order_relaxed);
    meterTempoActive_.store(false, std::memory_order_relaxed);
    meterTempoWaiting_.store(false, std::memory_order_relaxed);
    meterTempoHostSync_.store(false, std::memory_order_relaxed);
    meterTempoMode_.store(static_cast<int>(CreativeTempo::Mode::off),
                          std::memory_order_relaxed);
}

float ModernPitchEngine::adaptiveHysteresis(
    const Parameters& parameters,
    const ScaleQuantizer& quantizer,
    const PitchObservation& observation) const noexcept
{
    if (!parameters.scaleLock)
    {
        return static_cast<float>(std::clamp(
            2.0 + 0.22 * static_cast<double>(quantizer.minimumStepCents())
                * static_cast<double>(clamp01(parameters.humanize)),
            1.0, 80.0));
    }

    float modeFactor = 0.78f;
    switch (latencyMode_)
    {
        case LatencyMode::quality:   modeFactor = 1.15f; break;
        case LatencyMode::live:      modeFactor = 0.78f; break;
        case LatencyMode::ultraLive: modeFactor = 0.42f; break;
    }
    const float tempoFactor = parameters.tempo.mode == CreativeTempo::Mode::glideLock
        ? 1.50f : parameters.tempo.mode == CreativeTempo::Mode::tempoGlide
        ? 1.12f : 1.0f;
    const float densityFactor = std::clamp(
        std::sqrt(std::max(0.1f, quantizer.minimumStepCents()) / 100.0f)
            * (1.0f - 0.32f * quantizer.asymmetry()),
        0.22f, 1.40f);
    const float confidenceFactor = 0.65f + 0.70f * clamp01(observation.confidence);
    const float strictnessFactor = 1.0f - 0.28f * clamp01(parameters.lockStrictness);
    return std::clamp(finiteOr(parameters.lockHysteresis, 24.0f)
        * modeFactor * tempoFactor * densityFactor
        * confidenceFactor * strictnessFactor, 0.0f, 80.0f);
}

double ModernPitchEngine::responseTimeMs(
    const Parameters& parameters,
    bool targetChanged,
    double targetJumpCents) const noexcept
{
    const double requested = std::clamp(
        static_cast<double>(finiteOr(parameters.retuneTimeMs, 50.0f)),
        0.0, 500.0);
    double response = std::max(0.35, requested);

    if (parameters.scaleLock)
    {
        const double norm = std::pow(requested / 500.0, 1.35);
        switch (latencyMode_)
        {
            case LatencyMode::quality:   response = 3.0 + 4.0 * norm; break;
            case LatencyMode::live:      response = 1.5 + 3.5 * norm; break;
            case LatencyMode::ultraLive: response = 0.35 + 2.65 * norm; break;
        }
        const double humanTiming = 0.8 * static_cast<double>(clamp01(parameters.humanize));
        response = std::min(7.0, response + humanTiming);
    }

    if (targetChanged)
    {
        const double transitionMs = std::clamp(
            static_cast<double>(finiteOr(parameters.transitionTimeMs, 35.0f)),
            0.0, 2000.0);
        if (parameters.tempo.mode != CreativeTempo::Mode::off)
        {
            // Creative Tempo controls the correction trajectory itself. The
            // single full-signal transport remains the only audio renderer.
            response = std::max(response, transitionMs);
        }
        else
        {
            const double jumpWeight = std::clamp(
                std::abs(targetJumpCents) / 600.0, 0.0, 1.0);
            response = std::max(response, 0.35 * transitionMs * jumpWeight);
        }
    }
    return std::clamp(response, 0.35, 500.0);
}

void ModernPitchEngine::updateCorrectionState(
    CorrectionState& state,
    ScaleQuantizer& quantizer,
    const PitchObservation& observation,
    const Parameters& parameters) noexcept
{
    if (!observation.valid || observation.frequencyHz <= 0.0f)
        return;

    const double observedLog2 = safeLog2(observation.frequencyHz);
    if (!state.pitchCentreValid || observation.onset)
    {
        state.pitchCentreLog2 = observedLog2;
        state.pitchCentreValid = true;
        state.stableObservations = 0;
    }
    else
    {
        const double distanceCents = std::abs(observedLog2 - state.pitchCentreLog2) * 1200.0;
        const double baseAlpha = distanceCents > 95.0 ? 0.30 : 0.07;
        const double stableGate = 0.35
            + 0.65 * static_cast<double>(clamp01(observation.confidence)
                                      * clamp01(observation.periodicity));
        state.pitchCentreLog2 += baseAlpha * stableGate
            * (observedLog2 - state.pitchCentreLog2);
        ++state.stableObservations;
    }

    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);
    int pending = 0;
    double newTarget = quantizer.chooseTargetLog2(
        state.pitchCentreLog2,
        hysteresis,
        parameters.lockStrictness,
        observation.confidence,
        parameters.scaleLock && parameters.hardLockActive,
        observation.onset,
        pending);
    newTarget += std::round(state.pitchCentreLog2 - newTarget);

    const bool targetChanged = !state.targetValid
        || std::abs(newTarget - state.targetLog2) * 1200.0 > 0.1;
    const double targetJump = state.targetValid
        ? (newTarget - state.targetLog2) * 1200.0 : 0.0;
    if (targetChanged)
    {
        ++state.revision;
        state.lastTargetJumpCents = targetJump;
    }
    state.targetLog2 = newTarget;
    state.targetValid = true;

    const double vibratoComponent = observedLog2 - state.pitchCentreLog2;
    const float stable = clamp01(0.45f * observation.confidence
                               + 0.35f * observation.consensus
                               + 0.20f * std::min(1.0f,
                                   static_cast<float>(state.stableObservations) / 5.0f));
    const float periodic = clamp01(observation.periodicity);
    const double halfStep = 0.5 * static_cast<double>(quantizer.minimumStepCents());
    const double centreError = std::abs((state.targetLog2 - state.pitchCentreLog2) * 1200.0);
    const float boundarySafety = 1.0f - smoothStep(
        static_cast<float>(0.58 * halfStep),
        static_cast<float>(0.92 * halfStep),
        static_cast<float>(centreError));

    float preserve = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve
                + 0.35f * clamp01(parameters.humanize))
        : clamp01(parameters.preserveVibrato);
    preserve *= stable * periodic * boundarySafety;

    const double correctedLog2 = state.targetLog2
        + static_cast<double>(preserve) * vibratoComponent;
    double errorCents = wrapToNearestOctave(
        (correctedLog2 - observedLog2) * 1200.0);

    const double humanWindow = parameters.scaleLock
        ? 2.0 + 10.0 * static_cast<double>(clamp01(parameters.humanize))
        : 1.5 + 16.0 * static_cast<double>(clamp01(parameters.humanize));
    if (std::abs(errorCents) <= humanWindow)
        errorCents = 0.0;
    else
        errorCents = std::copysign(std::abs(errorCents) - humanWindow, errorCents);

    const double maximumCents = 1200.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    errorCents = std::clamp(errorCents, -maximumCents, maximumCents);

    // Amount scales the pitch destination itself. No unprocessed signal is mixed.
    state.desiredCents = errorCents
        * static_cast<double>(clamp01(parameters.amount));
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);

    meterPendingOctave_.store(pending, std::memory_order_relaxed);
    meterOctaveState_.store(observation.octaveState, std::memory_order_relaxed);
}

double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept
{
    if (!state.targetValid)
        return 0.0;

    const double dt = 1.0 / sampleRate_;
    const double responseSeconds = std::max(0.00035, state.responseMs * 0.001);
    const double omega = std::min(0.22 / dt, 4.6 / responseSeconds);
    double acceleration = omega * omega
        * (state.desiredCents - state.currentCents)
        - 2.0 * omega * state.velocityCentsPerSecond;
    const double maximumVelocity = std::max(3600.0,
        10.0 * std::max(120.0, std::abs(state.desiredCents)) / responseSeconds);
    const double maximumAcceleration = maximumVelocity
        / std::max(0.0005, responseSeconds * 0.30);
    acceleration = std::clamp(acceleration,
                              -maximumAcceleration,
                              maximumAcceleration);
    state.velocityCentsPerSecond += acceleration * dt;
    state.velocityCentsPerSecond = std::clamp(state.velocityCentsPerSecond,
                                              -maximumVelocity,
                                              maximumVelocity);
    state.currentCents += state.velocityCentsPerSecond * dt;
    if (std::abs(state.desiredCents - state.currentCents) < 0.001
        && std::abs(state.velocityCentsPerSecond) < 0.02)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
    }
    return state.currentCents;
}

std::array<float, ModernPitchEngine::maximumLpcOrder>
ModernPitchEngine::calculateLpc(const float* mono, int samples) noexcept
{
    std::array<float, maximumLpcOrder> result {};
    if (mono == nullptr || samples <= maximumLpcOrder + 2)
        return result;

    std::array<double, maximumLpcOrder + 1> autocorrelation {};
    for (int lag = 0; lag <= maximumLpcOrder; ++lag)
    {
        double sum = 0.0;
        for (int i = lag; i < samples; ++i)
            sum += static_cast<double>(mono[i]) * mono[i - lag];
        autocorrelation[static_cast<std::size_t>(lag)] = sum;
    }
    double error = autocorrelation[0];
    if (!(error > 1.0e-10) || !std::isfinite(error))
        return result;

    std::array<double, maximumLpcOrder + 1> coefficients {};
    coefficients[0] = 1.0;
    for (int order = 1; order <= maximumLpcOrder; ++order)
    {
        double numerator = autocorrelation[static_cast<std::size_t>(order)];
        for (int i = 1; i < order; ++i)
            numerator -= coefficients[static_cast<std::size_t>(i)]
                * autocorrelation[static_cast<std::size_t>(order - i)];
        double reflection = numerator / std::max(1.0e-12, error);
        reflection = std::clamp(reflection, -0.96, 0.96);
        const auto previous = coefficients;
        coefficients[static_cast<std::size_t>(order)] = reflection;
        for (int i = 1; i < order; ++i)
            coefficients[static_cast<std::size_t>(i)]
                = previous[static_cast<std::size_t>(i)]
                - reflection * previous[static_cast<std::size_t>(order - i)];
        error *= std::max(0.02, 1.0 - reflection * reflection);
    }
    for (int i = 0; i < maximumLpcOrder; ++i)
        result[static_cast<std::size_t>(i)] = static_cast<float>(
            coefficients[static_cast<std::size_t>(i + 1)]);
    return result;
}

void ModernPitchEngine::updateLpcTarget(
    const juce::AudioBuffer<float>& buffer,
    int channels,
    int samples,
    const Parameters& parameters,
    const PitchObservation& observation) noexcept
{
    const int count = std::min(samples, static_cast<int>(monoScratch_.size()));
    if (count <= maximumLpcOrder + 2 || channels <= 0)
        return;
    for (int sample = 0; sample < count; ++sample)
    {
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel)
            sum += buffer.getSample(channel, sample);
        monoScratch_[static_cast<std::size_t>(sample)] = static_cast<float>(
            sum / static_cast<double>(channels));
    }
    currentLpcTarget_ = calculateLpc(monoScratch_.data(), count);

    const float periodicity = clamp01(observation.periodicity);
    const float transientSafety = 1.0f
        - clamp01(parameters.transientProtection)
            * smoothStep(0.25f, 0.85f, observation.onsetStrength);
    const float formantStrength = clamp01(parameters.formantPreservation)
        * (0.45f + 0.55f * periodicity) * transientSafety;
    const float breathEvidence = (1.0f - periodicity)
        * (1.0f - 0.85f * smoothStep(0.20f, 0.75f, observation.onsetStrength));
    const float breathReduction = 0.65f * clamp01(parameters.breathReduction)
        * breathEvidence;
    for (int channel = 0; channel < channels; ++channel)
        channelPaths_[static_cast<std::size_t>(channel)].setVoiceModel(
            currentLpcTarget_, formantStrength, breathReduction);
}

void ModernPitchEngine::process(
    juce::AudioBuffer<float>& buffer,
    const double* scaleRatios,
    int numberOfScaleRatios,
    double rootFrequency,
    const Parameters& parameters)
{
    process(buffer, scaleRatios, numberOfScaleRatios, rootFrequency,
            parameters, CreativeTempo::HostPosition {});
}

void ModernPitchEngine::process(
    juce::AudioBuffer<float>& buffer,
    const double* scaleRatios,
    int numberOfScaleRatios,
    double rootFrequency,
    const Parameters& parameters,
    const CreativeTempo::HostPosition& hostTempoPosition)
{
    Parameters safe = parameters;
    safe.amount = clamp01(finiteOr(safe.amount, 1.0f));
    safe.retuneTimeMs = std::clamp(finiteOr(safe.retuneTimeMs, 50.0f), 0.0f, 500.0f);
    safe.transitionTimeMs = std::clamp(finiteOr(safe.transitionTimeMs, 35.0f), 0.0f, 2000.0f);
    safe.preserveVibrato = clamp01(finiteOr(safe.preserveVibrato, 0.70f));
    safe.humanize = clamp01(finiteOr(safe.humanize, 0.20f));
    safe.formantPreservation = clamp01(finiteOr(safe.formantPreservation, 0.90f));
    safe.transientProtection = clamp01(finiteOr(safe.transientProtection, 0.85f));
    safe.detectorSensitivity = clamp01(finiteOr(safe.detectorSensitivity, 0.70f));
    safe.breathReduction = clamp01(finiteOr(safe.breathReduction, 0.50f));
    safe.lockHysteresis = std::clamp(finiteOr(safe.lockHysteresis, 24.0f), 0.0f, 80.0f);
    safe.vibratoPreserve = clamp01(finiteOr(safe.vibratoPreserve, 0.0f));
    safe.lockStrictness = clamp01(finiteOr(safe.lockStrictness, 0.0f));
    safe.minimumPitchHz = std::clamp(finiteOr(safe.minimumPitchHz, 45.0f), 25.0f, 500.0f);
    safe.maximumPitchHz = std::clamp(finiteOr(safe.maximumPitchHz, 1600.0f),
                                     safe.minimumPitchHz + 20.0f, 3000.0f);
    safe.latencyMode = static_cast<int>(latencyMode_);

    const int channels = std::min({buffer.getNumChannels(), channelCount_, maxSupportedChannels});
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    const bool linkedScaleChanged = linkedQuantizer_.setScale(
        scaleRatios, numberOfScaleRatios, rootFrequency);
    for (int channel = 0; channel < channels; ++channel)
    {
        const bool changed = channelQuantizers_[static_cast<std::size_t>(channel)].setScale(
            scaleRatios, numberOfScaleRatios, rootFrequency);
        if (changed)
            channelCorrections_[static_cast<std::size_t>(channel)] = {};
    }
    if (linkedScaleChanged)
        linkedCorrection_ = {};

    linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);
    linkedTracker_.setSensitivity(safe.detectorSensitivity);
    for (int channel = 0; channel < channels; ++channel)
    {
        channelTrackers_[static_cast<std::size_t>(channel)].setRange(
            safe.minimumPitchHz, safe.maximumPitchHz);
        channelTrackers_[static_cast<std::size_t>(channel)].setSensitivity(
            safe.detectorSensitivity);
    }

    tempoController_.beginBlock(hostTempoPosition, safe.tempo, samples);
    if (safe.tempo.mode != CreativeTempo::Mode::off)
        safe.transitionTimeMs = tempoController_.getGlideTimeMs();
    for (int channel = 0; channel < channels; ++channel)
        channelTempoControllers_[static_cast<std::size_t>(channel)].beginBlock(
            hostTempoPosition, safe.tempo, samples);

    updateLpcTarget(buffer, channels, samples, safe, latestObservation_);

    std::array<float*, maxSupportedChannels> data {};
    for (int channel = 0; channel < channels; ++channel)
    {
        data[static_cast<std::size_t>(channel)] = buffer.getWritePointer(channel);
        for (int sample = 0; sample < samples; ++sample)
            data[static_cast<std::size_t>(channel)][sample]
                = sanitiseAudioSample(data[static_cast<std::size_t>(channel)][sample]);
    }

    const bool dualMono = safe.stereoMode == StereoMode::dualMono && channels > 1;
    for (int sample = 0; sample < samples; ++sample)
    {
        if (dualMono)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                PitchObservation observation;
                auto& tracker = channelTrackers_[static_cast<std::size_t>(channel)];
                if (tracker.processSample(data[static_cast<std::size_t>(channel)][sample],
                                          observation))
                {
                    latestChannelObservation_[static_cast<std::size_t>(channel)] = observation;
                    updateCorrectionState(
                        channelCorrections_[static_cast<std::size_t>(channel)],
                        channelQuantizers_[static_cast<std::size_t>(channel)],
                        observation, safe);
                }

                auto& correction = channelCorrections_[static_cast<std::size_t>(channel)];
                const double controllerCents = advanceCorrection(correction);
                const auto decision = channelTempoControllers_[static_cast<std::size_t>(channel)]
                    .processSample(controllerCents,
                                   correction.desiredCents,
                                   correction.revision,
                                   latestChannelObservation_[static_cast<std::size_t>(channel)].onsetStrength,
                                   correction.targetValid,
                                   sample,
                                   safe.tempo,
                                   static_cast<float>(correction.responseMs));
                if (decision.waitingForGrid)
                {
                    // Freeze the internal trajectory together with the audible
                    // one. Otherwise it would race to the destination while
                    // Glide Lock is waiting and jump immediately on release.
                    correction.currentCents = decision.controllerCents;
                    correction.velocityCentsPerSecond = 0.0;
                }
                const double audible = decision.controllerCents;
                const double ratio = std::clamp(std::exp2(audible / 1200.0), 0.25, 4.0);
                const auto plan = channelClocks_[static_cast<std::size_t>(channel)].next(ratio);
                data[static_cast<std::size_t>(channel)][sample]
                    = channelPaths_[static_cast<std::size_t>(channel)].process(
                        data[static_cast<std::size_t>(channel)][sample], plan);
                if (channel == 0)
                {
                    latestObservation_ = latestChannelObservation_[0];
                    linkedCorrection_ = correction;
                    audibleCorrectionCents_ = audible;
                }
            }
        }
        else
        {
            double analysis = 0.0;
            for (int channel = 0; channel < channels; ++channel)
                analysis += data[static_cast<std::size_t>(channel)][sample];
            PitchObservation observation;
            if (linkedTracker_.processSample(
                static_cast<float>(analysis / static_cast<double>(channels)), observation))
            {
                latestObservation_ = observation;
                updateCorrectionState(linkedCorrection_, linkedQuantizer_, observation, safe);
            }

            const double controllerCents = advanceCorrection(linkedCorrection_);
            const auto decision = tempoController_.processSample(
                controllerCents,
                linkedCorrection_.desiredCents,
                linkedCorrection_.revision,
                latestObservation_.onsetStrength,
                linkedCorrection_.targetValid,
                sample,
                safe.tempo,
                static_cast<float>(linkedCorrection_.responseMs));
            if (decision.waitingForGrid)
            {
                linkedCorrection_.currentCents = decision.controllerCents;
                linkedCorrection_.velocityCentsPerSecond = 0.0;
            }
            audibleCorrectionCents_ = decision.controllerCents;
            const double ratio = std::clamp(
                std::exp2(audibleCorrectionCents_ / 1200.0), 0.25, 4.0);
            const auto plan = linkedClock_.next(ratio);
            for (int channel = 0; channel < channels; ++channel)
                data[static_cast<std::size_t>(channel)][sample]
                    = channelPaths_[static_cast<std::size_t>(channel)].process(
                        data[static_cast<std::size_t>(channel)][sample], plan);
        }

        if (latestObservation_.valid)
            ++sustainedSamples_;
        else
            sustainedSamples_ = 0;
    }

    const auto tempoMeter = dualMono
        ? channelTempoControllers_[0].getMetering()
        : tempoController_.getMetering();
    publishMetering(latestObservation_, linkedCorrection_,
                    audibleCorrectionCents_, tempoMeter);
}

void ModernPitchEngine::process(
    juce::AudioBuffer<float>& buffer,
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

void ModernPitchEngine::process(
    float* monoData,
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
    const int channels = std::min({buffer.getNumChannels(), channelCount_, maxSupportedChannels});
    const int samples = buffer.getNumSamples();
    for (int channel = 0; channel < channels; ++channel)
    {
        float* data = buffer.getWritePointer(channel);
        auto& path = channelPaths_[static_cast<std::size_t>(channel)];
        for (int sample = 0; sample < samples; ++sample)
            data[sample] = path.processBypassed(data[sample], latencySamples_);
    }
}

void ModernPitchEngine::publishMetering(
    const PitchObservation& observation,
    const CorrectionState& state,
    double audibleCents,
    const CreativeTempo::Metering& tempoMeter) noexcept
{
    meterSequence_.fetch_add(1u, std::memory_order_acq_rel);
    meterPitchHz_.store(observation.frequencyHz, std::memory_order_relaxed);
    meterTargetHz_.store(state.targetValid
        ? static_cast<float>(std::exp2(state.targetLog2)) : 0.0f,
        std::memory_order_relaxed);
    meterConfidence_.store(observation.confidence, std::memory_order_relaxed);
    meterVoicing_.store(observation.voicing, std::memory_order_relaxed);
    meterPeriodicity_.store(observation.periodicity, std::memory_order_relaxed);
    meterCorrectionCents_.store(static_cast<float>(audibleCents),
                                std::memory_order_relaxed);
    meterCorrectionVelocity_.store(static_cast<float>(state.velocityCentsPerSecond),
                                   std::memory_order_relaxed);
    meterOnsetStrength_.store(observation.onsetStrength, std::memory_order_relaxed);
    meterTargetJumpCents_.store(static_cast<float>(state.lastTargetJumpCents),
                                std::memory_order_relaxed);
    meterSustainedSeconds_.store(static_cast<float>(
        std::min(12.0, static_cast<double>(sustainedSamples_) / sampleRate_)),
        std::memory_order_relaxed);
    meterDetectorSupport_.store(observation.detectorSupport, std::memory_order_relaxed);
    meterOctaveState_.store(observation.octaveState, std::memory_order_relaxed);
    TrackingState tracking = TrackingState::unvoiced;
    if (observation.valid)
        tracking = observation.onset ? TrackingState::attack
            : std::abs(state.desiredCents - state.currentCents) > 1.0
            ? TrackingState::transition : TrackingState::stable;
    else if (state.targetValid)
        tracking = TrackingState::release;
    meterTrackingState_.store(static_cast<int>(tracking), std::memory_order_relaxed);
    meterTempoBpm_.store(tempoMeter.bpm, std::memory_order_relaxed);
    meterTempoGridPhase_.store(tempoMeter.gridPhase, std::memory_order_relaxed);
    meterTempoGlideTimeMs_.store(tempoMeter.glideTimeMs, std::memory_order_relaxed);
    meterTempoActive_.store(tempoMeter.active, std::memory_order_relaxed);
    meterTempoWaiting_.store(tempoMeter.waitingForGrid, std::memory_order_relaxed);
    meterTempoHostSync_.store(tempoMeter.hostSyncValid, std::memory_order_relaxed);
    meterTempoMode_.store(static_cast<int>(tempoMeter.mode), std::memory_order_relaxed);
    meterSequence_.fetch_add(1u, std::memory_order_release);
}

ModernPitchEngine::Metering ModernPitchEngine::getMetering() const noexcept
{
    Metering result;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = meterSequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;
        result.detectedPitchHz = meterPitchHz_.load(std::memory_order_relaxed);
        result.targetPitchHz = meterTargetHz_.load(std::memory_order_relaxed);
        result.confidence = meterConfidence_.load(std::memory_order_relaxed);
        result.voicing = meterVoicing_.load(std::memory_order_relaxed);
        result.harmonicity = meterPeriodicity_.load(std::memory_order_relaxed);
        result.breathiness = 1.0f - result.harmonicity;
        result.noisePath = result.breathiness;
        result.spectralReliability = clamp01(0.55f * result.confidence
                                           + 0.45f * result.harmonicity);
        result.maskStability = result.harmonicity;
        result.sustainedNoteSeconds = meterSustainedSeconds_.load(std::memory_order_relaxed);
        result.consensus = result.harmonicity;
        result.correctionCents = meterCorrectionCents_.load(std::memory_order_relaxed);
        result.wetMix = 1.0f;
        result.outputSourceCorrespondence = 100.0f * result.spectralReliability;
        result.outputTargetCoherence = 100.0f * result.confidence;
        result.outputPhysicalHarmonicFit = 100.0f * result.harmonicity;
        result.outputPhaseCoherence = 100.0f * result.harmonicity;
        result.outputMeterValid = result.detectedPitchHz > 0.0f ? 1.0f : 0.0f;
        result.outputTemporalStability = 100.0f * result.maskStability;
        result.outputTargetJumpCents = meterTargetJumpCents_.load(std::memory_order_relaxed);
        result.outputCorrectionVelocityCentsPerSecond
            = meterCorrectionVelocity_.load(std::memory_order_relaxed);
        result.outputPreIfftConsensus = 100.0f * result.consensus;
        result.detectorSupport = meterDetectorSupport_.load(std::memory_order_relaxed);
        result.octaveState = meterOctaveState_.load(std::memory_order_relaxed);
        result.pendingOctaveObservations = meterPendingOctave_.load(std::memory_order_relaxed);
        result.state = static_cast<TrackingState>(
            meterTrackingState_.load(std::memory_order_relaxed));
        result.tempoBpm = meterTempoBpm_.load(std::memory_order_relaxed);
        result.tempoGridPhase = meterTempoGridPhase_.load(std::memory_order_relaxed);
        result.tempoGlideTimeMs = meterTempoGlideTimeMs_.load(std::memory_order_relaxed);
        result.tempoActive = meterTempoActive_.load(std::memory_order_relaxed);
        result.tempoWaitingForGrid = meterTempoWaiting_.load(std::memory_order_relaxed);
        result.tempoHostSyncValid = meterTempoHostSync_.load(std::memory_order_relaxed);
        result.tempoMode = static_cast<CreativeTempo::Mode>(
            meterTempoMode_.load(std::memory_order_relaxed));
        const auto after = meterSequence_.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u)
            return result;
    }
    return result;
}
