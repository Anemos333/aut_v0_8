#include "PitchCore.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace neumaton::pitch
{
namespace
{
[[nodiscard]] float finiteSample(float x) noexcept
{
    if (!std::isfinite(x) || std::fpclassify(x) == FP_SUBNORMAL)
        return 0.0f;
    return std::clamp(x, -16.0f, 16.0f);
}

[[nodiscard]] float clamp01(float x) noexcept
{
    return std::clamp(x, 0.0f, 1.0f);
}
}

void MeasurementGate::prepare(double sampleRate) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate) : 48000.0;
    reset();
}

void MeasurementGate::reset() noexcept
{
    ring_.fill(0.0f);
    write_ = 0;
    available_ = 0;
    hop_ = 0;
    open_ = true; // uncertainty passes by contract
}

bool MeasurementGate::processSample(float sample) noexcept
{
    ring_[static_cast<std::size_t>(write_)] = finiteSample(sample);
    write_ = (write_ + 1) & ringMask;
    available_ = std::min(available_ + 1, ringSize);

    if (++hop_ >= evaluationHop)
    {
        hop_ = 0;
        evaluate();
    }
    return open_;
}

void MeasurementGate::evaluate() noexcept
{
    if (available_ < 96)
    {
        open_ = true;
        return;
    }

    constexpr int frame = 192;
    const int n = std::min(frame, available_);
    double energy = 0.0;
    double diffEnergy = 0.0;
    int zeroCrossings = 0;
    std::array<int, 48> crossingPositions {};
    int crossingCount = 0;
    float previous = ring_[static_cast<std::size_t>((write_ - n + ringSize) & ringMask)];

    for (int i = 0; i < n; ++i)
    {
        const int index = (write_ - n + i + ringSize) & ringMask;
        const float x = ring_[static_cast<std::size_t>(index)];
        energy += static_cast<double>(x) * x;
        if (i > 0)
        {
            const double d = static_cast<double>(x) - previous;
            diffEnergy += d * d;
            if ((x >= 0.0f) != (previous >= 0.0f))
            {
                ++zeroCrossings;
                if (crossingCount < static_cast<int>(crossingPositions.size()))
                    crossingPositions[static_cast<std::size_t>(crossingCount++)] = i;
            }
        }
        previous = x;
    }

    const double rms = std::sqrt(energy / static_cast<double>(std::max(1, n)));
    if (rms < 1.5e-4)
    {
        open_ = false;
        return;
    }

    const double diffRatio = diffEnergy / std::max(1.0e-18, energy);

    double intervalMean = 0.0;
    double intervalVar = 0.0;
    int intervals = 0;
    for (int i = 1; i < crossingCount; ++i)
    {
        const double interval = static_cast<double>(
            crossingPositions[static_cast<std::size_t>(i)]
          - crossingPositions[static_cast<std::size_t>(i - 1)]);
        intervalMean += interval;
        ++intervals;
    }
    if (intervals > 0)
        intervalMean /= static_cast<double>(intervals);
    if (intervals > 1 && intervalMean > 0.0)
    {
        for (int i = 1; i < crossingCount; ++i)
        {
            const double interval = static_cast<double>(
                crossingPositions[static_cast<std::size_t>(i)]
              - crossingPositions[static_cast<std::size_t>(i - 1)]);
            const double d = interval - intervalMean;
            intervalVar += d * d;
        }
        intervalVar /= static_cast<double>(intervals - 1);
    }
    const double intervalCv = intervalMean > 0.0
        ? std::sqrt(intervalVar) / intervalMean
        : 0.0;

    float bestCorrelation = 0.0f;
    const int maximumLag = std::min(96, n / 2);
    for (int lag = 12; lag <= maximumLag; lag += 4)
    {
        double xy = 0.0;
        double xx = 0.0;
        double yy = 0.0;
        for (int i = lag; i < n; i += 2)
        {
            const float a = ring_[static_cast<std::size_t>(
                (write_ - n + i + ringSize) & ringMask)];
            const float b = ring_[static_cast<std::size_t>(
                (write_ - n + i - lag + ringSize) & ringMask)];
            xy += static_cast<double>(a) * b;
            xx += static_cast<double>(a) * a;
            yy += static_cast<double>(b) * b;
        }
        const double denom = std::sqrt(std::max(1.0e-20, xx * yy));
        if (denom > 0.0)
            bestCorrelation = std::max(bestCorrelation,
                static_cast<float>(xy / denom));
    }

    // Reject only when several independent signs all say aperiodic. Low notes,
    // noisy vowels and unresolved material remain open and are left to the
    // detector, which is the intended false-positive-biased gate policy.
    const bool enoughCrossingsToJudge = zeroCrossings >= 10 && intervals >= 6;
    const bool certainlyAperiodic = diffRatio > 1.05
        && bestCorrelation < 0.16f
        && enoughCrossingsToJudge
        && intervalCv > 0.48;
    open_ = !certainlyAperiodic;
}

void FundamentalDetector::prepare(double sampleRate,
                                  LatencyMode mode,
                                  float minimumPitchHz,
                                  float maximumPitchHz) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate) : 48000.0;
    mode_ = mode;
    minimumPitchHz_ = std::clamp(minimumPitchHz, 25.0f, 500.0f);
    maximumPitchHz_ = std::clamp(maximumPitchHz,
                                 minimumPitchHz_ + 20.0f,
                                 static_cast<float>(0.40 * sampleRate_));

    // 64 samples is 1.33 ms at 48 kHz. Keeping the detector cadence identical
    // across modes avoids trading octave safety for the declared audio latency.
    analysisHop_ = 64;
    decimation_ = std::clamp(static_cast<int>(std::lround(sampleRate_ / 12000.0)), 1, 8);
    fastEnergyCoefficient_ = 1.0 - std::exp(-1.0 / (0.0025 * sampleRate_));
    slowEnergyCoefficient_ = 1.0 - std::exp(-1.0 / (0.040 * sampleRate_));
    reset();
}

void FundamentalDetector::reset() noexcept
{
    ring_.fill(0.0f);
    coarse_.fill(0.0f);
    difference_.fill(0.0f);
    cmndf_.fill(1.0f);
    write_ = 0;
    available_ = 0;
    analysisCounter_ = 0;
    state_ = TrackingState::acquire;
    stableHz_ = 0.0f;
    pendingHz_ = 0.0f;
    pendingConfirmations_ = 0;
    latest_ = {};
    previousSample_ = 0.0f;
    fastEnergy_ = 0.0;
    slowEnergy_ = 0.0;
    differenceEnergy_ = 0.0;
}

float FundamentalDetector::readAgo(int samplesAgo) const noexcept
{
    if (samplesAgo < 0 || samplesAgo >= available_)
        return 0.0f;
    const int index = (write_ - 1 - samplesAgo + ringSize) & ringMask;
    return ring_[static_cast<std::size_t>(index)];
}

float FundamentalDetector::centsDistance(float a, float b) noexcept
{
    if (!(a > 0.0f) || !(b > 0.0f))
        return 100000.0f;
    return std::abs(1200.0f * std::log2(a / b));
}

bool FundamentalDetector::octaveLike(float a, float b) noexcept
{
    if (!(a > 0.0f) || !(b > 0.0f))
        return false;
    const float octaves = std::log2(a / b);
    const float nearest = std::round(octaves);
    return std::abs(nearest) >= 1.0f
        && std::abs(nearest) <= 2.0f
        && std::abs((octaves - nearest) * 1200.0f) < 85.0f;
}

float FundamentalDetector::periodScore(double periodSamples,
                                       int cycles,
                                       int sampleStride) const noexcept
{
    if (!(periodSamples >= 2.0) || !std::isfinite(periodSamples))
        return -1.0f;

    const int lag = static_cast<int>(std::lround(periodSamples));
    const int needed = lag * std::max(2, cycles);
    if (lag < 2 || available_ < needed)
        return -1.0f;

    const int usable = std::min(available_ - lag,
                                lag * std::max(2, cycles - 1));
    if (usable < 24)
        return -1.0f;

    double xy = 0.0;
    double xx = 0.0;
    double yy = 0.0;
    for (int ago = 0; ago < usable; ago += std::max(1, sampleStride))
    {
        const double a = readAgo(ago);
        const double b = readAgo(ago + lag);
        xy += a * b;
        xx += a * a;
        yy += b * b;
    }
    const double denom = std::sqrt(std::max(1.0e-20, xx * yy));
    if (denom <= 0.0)
        return -1.0f;
    return static_cast<float>(std::clamp(xy / denom, -1.0, 1.0));
}

FundamentalDetector::Candidate FundamentalDetector::refineCandidate(
    float coarseHz, float coarseConfidence) noexcept
{
    Candidate result;
    if (!(coarseHz >= minimumPitchHz_ * 0.90f)
        || !(coarseHz <= maximumPitchHz_ * 1.10f))
        return result;

    const double centrePeriod = sampleRate_ / static_cast<double>(coarseHz);
    const int centreLag = std::max(2, static_cast<int>(std::lround(centrePeriod)));
    if (available_ < 2 * centreLag)
        return result;

    int bestLag = centreLag;
    float bestScore = -2.0f;
    std::array<float, 7> scores {};
    scores.fill(-2.0f);
    constexpr int radius = 3;

    for (int offset = -radius; offset <= radius; ++offset)
    {
        const int lag = centreLag + offset;
        if (lag < 2)
            continue;
        const float score = periodScore(static_cast<double>(lag), 4, 1);
        scores[static_cast<std::size_t>(offset + radius)] = score;
        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    if (bestScore < 0.38f)
        return result;

    double refinedLag = static_cast<double>(bestLag);
    const int scoreIndex = bestLag - centreLag + radius;
    if (scoreIndex > 0 && scoreIndex + 1 < static_cast<int>(scores.size()))
    {
        const double left = scores[static_cast<std::size_t>(scoreIndex - 1)];
        const double centre = scores[static_cast<std::size_t>(scoreIndex)];
        const double right = scores[static_cast<std::size_t>(scoreIndex + 1)];
        const double denominator = left - 2.0 * centre + right;
        if (std::abs(denominator) > 1.0e-9)
            refinedLag += std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5);
    }

    const float refinedHz = static_cast<float>(sampleRate_ / refinedLag);
    if (!std::isfinite(refinedHz)
        || refinedHz < minimumPitchHz_
        || refinedHz > maximumPitchHz_)
        return result;

    result.hz = refinedHz;
    result.periodSamples = static_cast<float>(refinedLag);
    result.periodicity = clamp01(0.5f * (bestScore + 1.0f));
    result.confidence = clamp01(0.52f * result.periodicity
                              + 0.48f * coarseConfidence);
    result.halfPeriodScore = periodScore(0.5 * refinedLag, 5, 1);
    result.doublePeriodScore = periodScore(2.0 * refinedLag, 3, 1);

    const bool halfClearlyBetter = result.halfPeriodScore > bestScore + 0.035f;
    const bool doubleClearlyBetter = result.doublePeriodScore > bestScore + 0.035f;
    result.octaveAmbiguous = halfClearlyBetter || doubleClearlyBetter;
    result.valid = result.confidence >= 0.56f && result.periodicity >= 0.58f;
    return result;
}

FundamentalDetector::Candidate FundamentalDetector::estimateGlobal() noexcept
{
    Candidate result;
    if (available_ < 64)
        return result;

    const double coarseRate = sampleRate_ / static_cast<double>(decimation_);
    const int coarseAvailable = std::min(coarseFrameMax, available_ / decimation_);
    const int tauMinimum = std::max(2,
        static_cast<int>(std::floor(coarseRate / maximumPitchHz_)));
    const int theoreticalTauMaximum = static_cast<int>(
        std::ceil(coarseRate / minimumPitchHz_));
    const int tauMaximum = std::min({ theoreticalTauMaximum,
                                     coarseLagMax,
                                     coarseAvailable / 2 - 2 });
    if (coarseAvailable < 48 || tauMaximum <= tauMinimum + 1)
        return result;

    // One causal coarse view. This is the estimate stage of the same detector,
    // not a second pitch path and it never votes with another F0 estimate.
    for (int i = 0; i < coarseAvailable; ++i)
    {
        const int ago = (coarseAvailable - 1 - i) * decimation_;
        double sum = 0.0;
        for (int k = 0; k < decimation_; ++k)
            sum += readAgo(ago + k);
        coarse_[static_cast<std::size_t>(i)] = static_cast<float>(
            sum / static_cast<double>(decimation_));
    }

    difference_.fill(0.0f);
    cmndf_.fill(1.0f);
    for (int tau = 1; tau <= tauMaximum; ++tau)
    {
        double sum = 0.0;
        int samplesCompared = 0;
        const int overlap = coarseAvailable - tau;
        for (int i = 0; i < overlap; i += 2)
        {
            const double d = static_cast<double>(coarse_[static_cast<std::size_t>(i)])
                           - coarse_[static_cast<std::size_t>(i + tau)];
            sum += d * d;
            ++samplesCompared;
        }
        difference_[static_cast<std::size_t>(tau)] = static_cast<float>(
            sum / static_cast<double>(std::max(1, samplesCompared)));
    }

    double cumulative = 0.0;
    for (int tau = 1; tau <= tauMaximum; ++tau)
    {
        cumulative += difference_[static_cast<std::size_t>(tau)];
        cmndf_[static_cast<std::size_t>(tau)] = cumulative > 1.0e-20
            ? static_cast<float>(static_cast<double>(difference_[static_cast<std::size_t>(tau)])
                * static_cast<double>(tau) / cumulative)
            : 1.0f;
    }

    // Select the strongest periodic family first. The previous "first minimum
    // under threshold" rule was biased toward short periods and could promote
    // strong H2/H3 structure to the fundamental.
    int chosenTau = -1;
    float chosenValue = 1.0f;
    for (int tau = tauMinimum; tau <= tauMaximum; ++tau)
    {
        const float value = cmndf_[static_cast<std::size_t>(tau)];
        const bool localMinimum = tau <= tauMinimum
            || tau >= tauMaximum
            || (value <= cmndf_[static_cast<std::size_t>(tau - 1)]
                && value < cmndf_[static_cast<std::size_t>(tau + 1)]);
        if (localMinimum && value < chosenValue)
        {
            chosenTau = tau;
            chosenValue = value;
        }
    }

    // Keep the old global validity ceiling. This change is only about family
    // identity; it must not make the detector more permissive.
    if (chosenTau < 0 || chosenValue > 0.38f)
        return result;

    // A long candidate period can be a false 2:1, 3:1 or 4:1 subharmonic.
    // Collapse it only when the global family itself is already strong. Weak
    // transition frames must not be compressed upward into a harmonic.
    if (chosenValue <= 0.30f)
    {
        const int familyTau = chosenTau;
        const float familyValue = chosenValue;
        for (int divisor = 4; divisor >= 2; --divisor)
        {
            const double targetTau = static_cast<double>(familyTau)
                                   / static_cast<double>(divisor);
            if (targetTau < static_cast<double>(tauMinimum)
                || targetTau > static_cast<double>(tauMaximum))
                continue;

            const int centreTau = static_cast<int>(std::lround(targetTau));
            int divisorTau = -1;
            float divisorValue = 2.0f;
            const int firstTau = std::max(tauMinimum, centreTau - 2);
            const int lastTau = std::min(tauMaximum, centreTau + 2);
            for (int tau = firstTau; tau <= lastTau; ++tau)
            {
                const float value = cmndf_[static_cast<std::size_t>(tau)];
                const bool localMinimum = tau <= tauMinimum
                    || tau >= tauMaximum
                    || (value <= cmndf_[static_cast<std::size_t>(tau - 1)]
                        && value < cmndf_[static_cast<std::size_t>(tau + 1)]);
                if (localMinimum && value < divisorValue)
                {
                    divisorTau = tau;
                    divisorValue = value;
                }
            }

            if (divisorTau > 0
                && divisorValue <= familyValue * 1.35f + 0.005f)
            {
                chosenTau = divisorTau;
                chosenValue = divisorValue;
                break;
            }
        }
    }

    double refinedTau = static_cast<double>(chosenTau);
    if (chosenTau > tauMinimum && chosenTau < tauMaximum)
    {
        const double left = cmndf_[static_cast<std::size_t>(chosenTau - 1)];
        const double centre = cmndf_[static_cast<std::size_t>(chosenTau)];
        const double right = cmndf_[static_cast<std::size_t>(chosenTau + 1)];
        const double denominator = left - 2.0 * centre + right;
        if (std::abs(denominator) > 1.0e-9)
            refinedTau += std::clamp(0.5 * (left - right) / denominator, -0.5, 0.5);
    }

    const float coarseHz = static_cast<float>(coarseRate / refinedTau);
    const float coarseConfidence = clamp01(1.0f - chosenValue);
    return refineCandidate(coarseHz, coarseConfidence);
}

FundamentalDetector::Candidate FundamentalDetector::trackLocal(float centreHz) noexcept
{
    Candidate result;
    if (!(centreHz > 0.0f))
        return result;

    const double centrePeriod = sampleRate_ / static_cast<double>(centreHz);
    const int centreLag = static_cast<int>(std::lround(centrePeriod));
    const int radius = std::max(3, static_cast<int>(std::ceil(centrePeriod * 0.045)));
    int bestLag = centreLag;
    float bestScore = -2.0f;

    for (int offset = -radius; offset <= radius; ++offset)
    {
        const int lag = centreLag + offset;
        if (lag < 2)
            continue;
        const float score = periodScore(static_cast<double>(lag), 4, 1);
        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    if (bestScore < 0.40f)
        return result;

    return refineCandidate(static_cast<float>(sampleRate_ / bestLag),
                           clamp01(0.5f * (bestScore + 1.0f)));
}

FundamentalDetector::OctaveAdvice FundamentalDetector::octaveSensor(
    const Candidate& candidate) const noexcept
{
    if (!candidate.valid)
        return OctaveAdvice::none;

    const float currentScore = 2.0f * candidate.periodicity - 1.0f;
    const bool lowerPeriodWins = candidate.doublePeriodScore > currentScore + 0.045f;
    const bool upperPeriodWins = candidate.halfPeriodScore > currentScore + 0.045f;

    if (lowerPeriodWins && !upperPeriodWins)
        return OctaveAdvice::rejectUpperCandidate;
    if (upperPeriodWins && !lowerPeriodWins)
        return OctaveAdvice::rejectLowerCandidate;
    if (!lowerPeriodWins && !upperPeriodWins)
        return OctaveAdvice::candidateSupported;
    return OctaveAdvice::none;
}

bool FundamentalDetector::emergencyOctaveCheck(const Candidate& candidate) const noexcept
{
    if (!candidate.valid || candidate.periodSamples <= 0.0f)
        return false;

    // Expensive by design and called only after the sibilant and octave sensors
    // disagree. It observes; it never overrules sensor 3.
    const float upperScore = periodScore(candidate.periodSamples, 7, 1);
    const float lowerScore = periodScore(2.0 * candidate.periodSamples, 4, 1);
    return lowerScore > upperScore + 0.025f;
}

FundamentalDetector::SensorDecision FundamentalDetector::queryNatureSensors(
    const Candidate& candidate) const noexcept
{
    SensorDecision decision;
    if (!candidate.valid)
        return decision;

    const double energyFloor = 1.0e-12;
    const double energyRatio = fastEnergy_ / std::max(energyFloor, slowEnergy_);
    const double highFrequencyProxy = differenceEnergy_
        / std::max(energyFloor, slowEnergy_ * 4.0);

    // No fixed consonant timer: as the event ceases to be a short energy/high
    // frequency excursion, fast and slow energies converge and this condition
    // naturally turns itself off.
    const bool consonant = energyRatio > 1.85
        && highFrequencyProxy > 0.18
        && candidate.periodicity < 0.78f;
    const bool sibilant = highFrequencyProxy > 0.32
        && candidate.periodicity < 0.74f;

    const OctaveAdvice octave = (candidate.octaveAmbiguous
        || (stableHz_ > 0.0f && octaveLike(candidate.hz, stableHz_)))
        ? octaveSensor(candidate)
        : OctaveAdvice::none;

    // 1 + 2 => consonant, not a new F0.
    if (consonant && sibilant)
    {
        decision.holdTransition = true;
        decision.rejectCandidate = true;
        return decision;
    }

    // 2 + 3 agree => reject upper octave. 2 + 3 disagree => call 4; 3 still
    // owns the final decision. Every other combination has no authority.
    if (sibilant && octave != OctaveAdvice::none)
    {
        const bool sibilantSaysFalseUpper = stableHz_ > 0.0f
            && candidate.hz > stableHz_ * 1.65f;
        const bool octaveSaysFalseUpper = octave == OctaveAdvice::rejectUpperCandidate;

        if (sibilantSaysFalseUpper && octaveSaysFalseUpper)
        {
            decision.rejectCandidate = true;
            decision.holdTransition = true;
            return decision;
        }

        if (sibilantSaysFalseUpper != octaveSaysFalseUpper)
        {
            decision.emergencyRan = true;
            static_cast<void>(emergencyOctaveCheck(candidate));
            if (octave == OctaveAdvice::rejectUpperCandidate
                || octave == OctaveAdvice::rejectLowerCandidate)
            {
                decision.rejectCandidate = true;
                decision.holdTransition = true;
            }
            return decision;
        }
    }

    return decision;
}

void FundamentalDetector::acceptStable(const Candidate& candidate) noexcept
{
    stableHz_ = candidate.hz;
    state_ = TrackingState::stable;
    pendingHz_ = 0.0f;
    pendingConfirmations_ = 0;
}

void FundamentalDetector::enterTransition() noexcept
{
    if (stableHz_ > 0.0f)
        state_ = TrackingState::transition;
    else
        state_ = TrackingState::acquire;
    pendingHz_ = 0.0f;
    pendingConfirmations_ = 0;
}

void FundamentalDetector::handleCandidate(const Candidate& candidate) noexcept
{
    if (!candidate.valid)
    {
        if (stableHz_ > 0.0f)
            enterTransition();
        else
        {
            pendingHz_ = 0.0f;
            pendingConfirmations_ = 0;
        }
        return;
    }

    if (stableHz_ <= 0.0f)
    {
        const SensorDecision sensors = queryNatureSensors(candidate);
        if (sensors.rejectCandidate || candidate.octaveAmbiguous)
        {
            pendingHz_ = 0.0f;
            pendingConfirmations_ = 0;
            return;
        }

        // The first Stable F0 needs the same minimal continuity proof used for
        // a new Stable F0 after Transition. This prevents a single uncertain
        // gate false-positive from becoming persistent pitch memory. One extra
        // analysis hop is enough; there is no timer or confidence smoothing.
        if (pendingHz_ <= 0.0f || centsDistance(candidate.hz, pendingHz_) > 20.0f)
        {
            pendingHz_ = candidate.hz;
            pendingConfirmations_ = 1;
            return;
        }

        ++pendingConfirmations_;
        if (pendingConfirmations_ >= 2)
            acceptStable(candidate);
        return;
    }

    const float distance = centsDistance(candidate.hz, stableHz_);

    if (state_ == TrackingState::stable && distance <= 105.0f
        && !candidate.octaveAmbiguous)
    {
        // In Stable a validated measurement is authoritative. No confidence
        // smoothing may bias a microtonal F0 away from the measured value.
        acceptStable(candidate);
        return;
    }

    if (distance <= 75.0f && !candidate.octaveAmbiguous)
    {
        acceptStable(candidate);
        return;
    }

    state_ = TrackingState::transition;

    // A downward-octave candidate is accepted only if its longer waveform
    // period explains alternating cycles materially better than the old upper
    // family. This prevents subharmonic capture without banning genuine octave
    // changes. The check is evidence-driven and has no fixed timeout.
    if (octaveLike(candidate.hz, stableHz_) && candidate.hz < stableHz_)
    {
        const float candidateScore = 2.0f * candidate.periodicity - 1.0f;
        const float upperAlternative = candidate.halfPeriodScore;
        if (upperAlternative < -0.5f
            || candidateScore < upperAlternative + 0.040f)
        {
            pendingHz_ = 0.0f;
            pendingConfirmations_ = 0;
            return;
        }
    }

    const SensorDecision sensors = queryNatureSensors(candidate);
    if (sensors.rejectCandidate || sensors.holdTransition)
        return;

    if (candidate.octaveAmbiguous)
    {
        const OctaveAdvice advice = octaveSensor(candidate);
        if (advice == OctaveAdvice::rejectUpperCandidate
            || advice == OctaveAdvice::rejectLowerCandidate
            || advice == OctaveAdvice::none)
            return;
    }

    if (pendingHz_ <= 0.0f || centsDistance(candidate.hz, pendingHz_) > 20.0f)
    {
        pendingHz_ = candidate.hz;
        pendingConfirmations_ = 1;
        return;
    }

    ++pendingConfirmations_;
    if (pendingConfirmations_ >= 2)
        acceptStable(candidate);
}

void FundamentalDetector::publish(bool gateOpen,
                                  const Candidate* candidate,
                                  bool stableChanged,
                                  bool newMeasurement) noexcept
{
    latest_.state = state_;
    latest_.stableHz = stableHz_;
    latest_.hasStable = stableHz_ > 0.0f;
    latest_.gateOpen = gateOpen;
    latest_.newMeasurement = newMeasurement;
    latest_.stableChanged = stableChanged;
    if (candidate != nullptr)
    {
        latest_.measuredHz = candidate->hz;
        latest_.confidence = candidate->confidence;
        latest_.periodicity = candidate->periodicity;
        latest_.octaveAmbiguous = candidate->octaveAmbiguous;
    }
    else
    {
        latest_.measuredHz = 0.0f;
        latest_.confidence = 0.0f;
        latest_.periodicity = 0.0f;
        latest_.octaveAmbiguous = false;
    }
}

PitchResult FundamentalDetector::processSample(float sample, bool gateOpen) noexcept
{
    sample = finiteSample(sample);
    ring_[static_cast<std::size_t>(write_)] = sample;
    write_ = (write_ + 1) & ringMask;
    available_ = std::min(available_ + 1, ringSize);

    const double x2 = static_cast<double>(sample) * sample;
    const double d = static_cast<double>(sample - previousSample_);
    previousSample_ = sample;
    fastEnergy_ += fastEnergyCoefficient_ * (x2 - fastEnergy_);
    slowEnergy_ += slowEnergyCoefficient_ * (x2 - slowEnergy_);
    differenceEnergy_ += fastEnergyCoefficient_ * (d * d - differenceEnergy_);

    if (!gateOpen)
    {
        analysisCounter_ = 0;
        if (stableHz_ > 0.0f)
            enterTransition();
        else
            state_ = TrackingState::acquire;
        publish(false, nullptr, false, false);
        return latest_;
    }

    if (++analysisCounter_ < analysisHop_)
    {
        publish(true, nullptr, false, false);
        return latest_;
    }
    analysisCounter_ = 0;

    const float before = stableHz_;
    Candidate candidate;
    if (state_ == TrackingState::stable && stableHz_ > 0.0f)
    {
        candidate = trackLocal(stableHz_);
        // Stable tracking is cheap. The instant it becomes weak or octave-
        // ambiguous we stop coasting and run the full tuner acquisition.
        if (!candidate.valid || candidate.octaveAmbiguous)
            candidate = estimateGlobal();
    }
    else
    {
        candidate = estimateGlobal();
    }

    handleCandidate(candidate);
    const bool changed = before <= 0.0f
        ? stableHz_ > 0.0f
        : (stableHz_ > 0.0f && centsDistance(before, stableHz_) > 0.05f);
    publish(true, &candidate, changed, candidate.valid);
    return latest_;
}

bool ScaleQuantizer::setScale(const double* ratios,
                              int count,
                              double referenceHz,
                              double equaveRatio) noexcept
{
    count_ = 0;
    if (ratios == nullptr || count <= 0
        || !std::isfinite(referenceHz) || referenceHz <= 0.0
        || !std::isfinite(equaveRatio) || equaveRatio <= 1.0)
        return false;

    referenceHz_ = referenceHz;
    equaveRatio_ = equaveRatio;
    logEquave_ = std::log(equaveRatio_);

    const int safeCount = std::min(count, maxDegrees);
    for (int i = 0; i < safeCount; ++i)
    {
        const double ratio = ratios[i];
        if (!std::isfinite(ratio) || ratio <= 0.0)
            continue;
        double position = std::log(ratio) / logEquave_;
        position -= std::floor(position);
        if (position < 0.0)
            position += 1.0;
        positions_[static_cast<std::size_t>(count_++)] = position;
    }

    if (count_ <= 0)
        return false;

    std::sort(positions_.begin(), positions_.begin() + count_);
    int unique = 0;
    for (int i = 0; i < count_; ++i)
    {
        const double p = positions_[static_cast<std::size_t>(i)];
        if (unique == 0
            || std::abs(p - positions_[static_cast<std::size_t>(unique - 1)]) > 1.0e-9)
            positions_[static_cast<std::size_t>(unique++)] = p;
    }
    count_ = unique;

    minimumStepCents_ = std::numeric_limits<double>::max();
    for (int i = 0; i < count_; ++i)
    {
        const double a = positions_[static_cast<std::size_t>(i)];
        const double b = (i + 1 < count_)
            ? positions_[static_cast<std::size_t>(i + 1)]
            : positions_[0] + 1.0;
        const double ratioStep = std::exp((b - a) * logEquave_);
        const double cents = 1200.0 * std::log2(ratioStep);
        if (cents > 1.0e-6)
            minimumStepCents_ = std::min(minimumStepCents_, cents);
    }
    if (!std::isfinite(minimumStepCents_))
        minimumStepCents_ = 100.0;
    return true;
}

ScaleQuantizer::Target ScaleQuantizer::quantize(double fundamentalHz,
                                                float amount,
                                                float humanize) const noexcept
{
    Target result;
    if (count_ <= 0 || !std::isfinite(fundamentalHz) || fundamentalHz <= 0.0)
        return result;

    const double relative = std::log(fundamentalHz / referenceHz_) / logEquave_;
    const double base = std::floor(relative);
    double bestRelative = relative;
    double bestDistance = std::numeric_limits<double>::max();

    for (int i = 0; i < count_; ++i)
    {
        const double degree = positions_[static_cast<std::size_t>(i)];
        for (int equaveOffset = -1; equaveOffset <= 1; ++equaveOffset)
        {
            const double candidate = base + static_cast<double>(equaveOffset) + degree;
            const double distance = std::abs(candidate - relative);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestRelative = candidate;
            }
        }
    }

    result.targetHz = referenceHz_ * std::exp(bestRelative * logEquave_);
    result.correctionCents = 1200.0 * std::log2(result.targetHz / fundamentalHz);

    const double a = std::clamp(static_cast<double>(amount), 0.0, 1.0);
    const double h = std::clamp(static_cast<double>(humanize), 0.0, 1.0);
    // Target identity remains exact. These controls only specify the allowed
    // live window that the later one-path correction trajectory may use.
    const double amountWindow = (1.0 - a) * 0.18 * minimumStepCents_;
    const double humanWindow = h * 0.12 * minimumStepCents_;
    result.liveWindowCents = std::clamp(amountWindow + humanWindow,
                                        0.0,
                                        0.45 * minimumStepCents_);
    result.valid = true;
    return result;
}

void PitchCore::prepare(double sampleRate,
                        LatencyMode mode,
                        float minimumPitchHz,
                        float maximumPitchHz) noexcept
{
    gate_.prepare(sampleRate);
    detector_.prepare(sampleRate, mode, minimumPitchHz, maximumPitchHz);
}

void PitchCore::reset() noexcept
{
    gate_.reset();
    detector_.reset();
}

PitchResult PitchCore::processSample(float sample) noexcept
{
    const bool measure = gate_.processSample(sample);
    return detector_.processSample(sample, measure);
}

} // namespace neumaton::pitch
