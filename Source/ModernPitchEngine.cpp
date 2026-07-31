#include "ModernPitchEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr float audioFloor = 1.0e-12f;

[[nodiscard]] float smoothStep(float edge0, float edge1, float value) noexcept
{
    if (edge1 <= edge0)
        return value >= edge1 ? 1.0f : 0.0f;

    const float x = std::clamp((value - edge0) / (edge1 - edge0),
                               0.0f,
                               1.0f);
    return x * x * (3.0f - 2.0f * x);
}

[[nodiscard]] double safeLog2(double value) noexcept
{
    return std::log2(std::max(value, 1.0e-12));
}
} // namespace

//==============================================================================
// PitchTracker
void ModernPitchEngine::PitchTracker::prepare(double sampleRate,
                                              LatencyMode mode)
{
    sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate)
                                            : 48000.0;
    detectorSampleRate_ = sampleRate_ * 0.5;

    switch (mode)
    {
        case LatencyMode::ultraLive:
            analysisWindow_ = 1024;
            analysisHop_ = 32;
            break;
        case LatencyMode::live:
            analysisWindow_ = 1536;
            analysisHop_ = 48;
            break;
        case LatencyMode::quality:
            analysisWindow_ = 2048;
            analysisHop_ = 64;
            break;
    }

    analysisWindow_ = std::clamp(analysisWindow_, 512, detectorRingSize);
    analysisHop_ = std::max(16, analysisHop_);
    reset();
}

void ModernPitchEngine::PitchTracker::reset() noexcept
{
    ring_.fill(0.0f);
    scratch_.fill(0.0f);
    writeIndex_ = 0;
    filled_ = 0;
    downsampleCounter_ = 0;
    downsampleAccumulator_ = 0.0f;
    samplesSinceAnalysis_ = 0;
    shortEnergy_ = 0.0f;
    longEnergy_ = 0.0f;
    previousFrequencyHz_ = 0.0f;
    pendingOctaveDirection_ = 0;
    pendingOctaveCount_ = 0;
}

float ModernPitchEngine::PitchTracker::sampleFromNewest(int age) const noexcept
{
    if (age < 0 || age >= filled_)
        return 0.0f;

    int index = writeIndex_ - 1 - age;
    while (index < 0)
        index += detectorRingSize;
    return ring_[static_cast<std::size_t>(index)];
}

float ModernPitchEngine::PitchTracker::correlationAt(int lag,
                                                     int stride) const noexcept
{
    if (lag <= 0 || lag >= analysisWindow_)
        return -1.0f;

    double xy = 0.0;
    double xx = 0.0;
    double yy = 0.0;
    int count = 0;

    const int safeStride = std::max(1, stride);
    for (int index = lag; index < analysisWindow_; index += safeStride)
    {
        const double a = static_cast<double>(
            scratch_[static_cast<std::size_t>(index)]);
        const double b = static_cast<double>(
            scratch_[static_cast<std::size_t>(index - lag)]);
        xy += a * b;
        xx += a * a;
        yy += b * b;
        ++count;
    }

    if (count < 8 || xx <= 1.0e-16 || yy <= 1.0e-16)
        return -1.0f;

    return static_cast<float>(xy / std::sqrt(xx * yy));
}

ModernPitchEngine::PitchObservation
ModernPitchEngine::PitchTracker::analyse(float minimumPitchHz,
                                         float maximumPitchHz,
                                         float sensitivity) noexcept
{
    PitchObservation result;

    if (filled_ < analysisWindow_)
        return result;

    for (int index = 0; index < analysisWindow_; ++index)
    {
        const int age = analysisWindow_ - 1 - index;
        const float sample = sampleFromNewest(age);
        const double phase = twoPi * static_cast<double>(index)
                           / static_cast<double>(analysisWindow_);
        const float window = static_cast<float>(0.5 - 0.5 * std::cos(phase));
        scratch_[static_cast<std::size_t>(index)] = sample * window;
    }

    const float safeMinimum = std::clamp(minimumPitchHz, 35.0f, 1200.0f);
    const float safeMaximum = std::clamp(maximumPitchHz,
                                         safeMinimum + 1.0f,
                                         4000.0f);
    int minimumLag = static_cast<int>(std::floor(
        detectorSampleRate_ / static_cast<double>(safeMaximum)));
    int maximumLag = static_cast<int>(std::ceil(
        detectorSampleRate_ / static_cast<double>(safeMinimum)));
    minimumLag = std::clamp(minimumLag, 3, analysisWindow_ / 3);
    maximumLag = std::clamp(maximumLag,
                            minimumLag + 2,
                            analysisWindow_ - 8);

    float bestCorrelation = -1.0f;
    int bestLag = minimumLag;
    constexpr int coarseStep = 2;
    for (int lag = minimumLag; lag <= maximumLag; lag += coarseStep)
    {
        const float correlation = correlationAt(lag, 2);
        if (correlation > bestCorrelation)
        {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }

    int refinedLag = bestLag;
    for (int lag = std::max(minimumLag, bestLag - 3);
         lag <= std::min(maximumLag, bestLag + 3);
         ++lag)
    {
        const float correlation = correlationAt(lag, 1);
        if (correlation > bestCorrelation)
        {
            bestCorrelation = correlation;
            refinedLag = lag;
        }
    }
    bestLag = refinedLag;

    int fundamentalLag = bestLag;
    float fundamentalCorrelation = bestCorrelation;

    // Only the 2:1 ambiguity is promoted. Correlation peaks repeat at every
    // integer multiple of the true period, so accepting 3x/4x merely because
    // they are coherent would manufacture subharmonic octaves. The doubled
    // period wins only when it is measurably better than the short-period peak,
    // which is the signature of alternating cycles caused by a real
    // fundamental under a dominant second harmonic.
    const int doubledLag = bestLag * 2;
    if (doubledLag <= maximumLag)
    {
        int localLag = doubledLag;
        float localCorrelation = correlationAt(doubledLag, 1);
        for (int offset = -2; offset <= 2; ++offset)
        {
            const int lag = doubledLag + offset;
            if (lag < minimumLag || lag > maximumLag)
                continue;
            const float correlation = correlationAt(lag, 1);
            if (correlation > localCorrelation)
            {
                localCorrelation = correlation;
                localLag = lag;
            }
        }

        const float improvement = localCorrelation - bestCorrelation;
        if (localCorrelation >= 0.34f && improvement >= 0.012f)
        {
            fundamentalLag = localLag;
            fundamentalCorrelation = localCorrelation;
        }
    }

    double fractionalLag = static_cast<double>(fundamentalLag);
    if (fundamentalLag > minimumLag && fundamentalLag < maximumLag)
    {
        const double left = correlationAt(fundamentalLag - 1, 1);
        const double centre = correlationAt(fundamentalLag, 1);
        const double right = correlationAt(fundamentalLag + 1, 1);
        const double denominator = left - 2.0 * centre + right;
        if (std::abs(denominator) > 1.0e-9)
        {
            fractionalLag += 0.5 * (left - right) / denominator;
            fractionalLag = std::clamp(fractionalLag,
                                       static_cast<double>(fundamentalLag - 1),
                                       static_cast<double>(fundamentalLag + 1));
        }
    }

    if (!(fractionalLag > 0.0) || !std::isfinite(fractionalLag))
        return result;

    float detectedFrequency = static_cast<float>(
        detectorSampleRate_ / fractionalLag);

    int octaveDirection = 0;
    if (previousFrequencyHz_ > 0.0f && detectedFrequency > 0.0f)
    {
        const double jumpCents = 1200.0 * safeLog2(
            static_cast<double>(detectedFrequency)
            / static_cast<double>(previousFrequencyHz_));

        if (std::abs(jumpCents) > 850.0 && std::abs(jumpCents) < 1350.0)
            octaveDirection = jumpCents > 0.0 ? 1 : -1;
    }

    if (octaveDirection != 0)
    {
        if (pendingOctaveDirection_ == octaveDirection)
            ++pendingOctaveCount_;
        else
        {
            pendingOctaveDirection_ = octaveDirection;
            pendingOctaveCount_ = 1;
        }

        const int required = fundamentalCorrelation > 0.78f ? 3 : 5;
        if (pendingOctaveCount_ < required)
        {
            detectedFrequency = previousFrequencyHz_;
        }
        else
        {
            pendingOctaveCount_ = 0;
            pendingOctaveDirection_ = 0;
        }
    }
    else
    {
        pendingOctaveCount_ = 0;
        pendingOctaveDirection_ = 0;
    }

    const float safeSensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
    const float correlationThreshold = 0.50f - 0.22f * safeSensitivity;
    const float energy = std::sqrt(std::max(0.0f, shortEnergy_));
    const float energyGate = smoothStep(0.00035f, 0.0040f, energy);
    const float periodicityGate = smoothStep(correlationThreshold,
                                             std::min(0.96f,
                                                      correlationThreshold + 0.30f),
                                             fundamentalCorrelation);

    result.frequencyHz = detectedFrequency;
    result.confidence = std::clamp(
        periodicityGate * (0.45f + 0.55f * energyGate), 0.0f, 1.0f);
    result.periodicity = std::clamp(fundamentalCorrelation, 0.0f, 1.0f);
    result.voicing = std::clamp(periodicityGate * energyGate, 0.0f, 1.0f);
    result.consensus = std::clamp(
        0.65f * result.periodicity
        + 0.35f * smoothStep(0.0f,
                             0.12f,
                             fundamentalCorrelation
                                 - correlationAt(std::max(minimumLag,
                                                          fundamentalLag / 2),
                                                 1)),
        0.0f,
        1.0f);
    result.detectorSupport = 1;
    if (fundamentalLag * 2 <= maximumLag
        && correlationAt(fundamentalLag * 2, 1) > 0.55f)
        ++result.detectorSupport;
    if (fundamentalLag * 3 <= maximumLag
        && correlationAt(fundamentalLag * 3, 1) > 0.45f)
        ++result.detectorSupport;

    result.pendingOctaveObservations = pendingOctaveCount_;
    result.octaveState = octaveDirection;
    result.valid = std::isfinite(result.frequencyHz)
        && result.frequencyHz >= safeMinimum
        && result.frequencyHz <= safeMaximum
        && result.confidence > 0.10f
        && result.voicing > 0.08f;

    if (result.valid)
        previousFrequencyHz_ = result.frequencyHz;

    return result;
}

bool ModernPitchEngine::PitchTracker::push(float sample,
                                           float minimumPitchHz,
                                           float maximumPitchHz,
                                           float sensitivity,
                                           PitchObservation& result) noexcept
{
    const float safeSample = std::isfinite(sample) ? sample : 0.0f;
    const float square = safeSample * safeSample;
    shortEnergy_ += 0.020f * (square - shortEnergy_);
    longEnergy_ += 0.0012f * (square - longEnergy_);

    downsampleAccumulator_ += safeSample;
    ++downsampleCounter_;
    if (downsampleCounter_ < 2)
        return false;

    const float downsampled = 0.5f * downsampleAccumulator_;
    downsampleAccumulator_ = 0.0f;
    downsampleCounter_ = 0;

    ring_[static_cast<std::size_t>(writeIndex_)] = downsampled;
    writeIndex_ = (writeIndex_ + 1) % detectorRingSize;
    filled_ = std::min(detectorRingSize, filled_ + 1);
    ++samplesSinceAnalysis_;

    if (samplesSinceAnalysis_ < analysisHop_)
        return false;
    samplesSinceAnalysis_ = 0;

    result = analyse(minimumPitchHz, maximumPitchHz, sensitivity);

    const float energyRise = std::max(
        0.0f,
        (shortEnergy_ - longEnergy_) / std::max(1.0e-8f, longEnergy_));
    result.onsetStrength = smoothStep(0.30f, 2.2f, energyRise);
    result.onset = result.onsetStrength > 0.42f;
    return true;
}

//==============================================================================
// ScaleQuantizer
void ModernPitchEngine::ScaleQuantizer::reset() noexcept
{
    ratios_.fill(1.0);
    ratioCount_ = 1;
    rootFrequency_ = 440.0;
    targetValid_ = false;
    currentTargetHz_ = 0.0;
    previousDetectedHz_ = 0.0;
    pendingTargetValid_ = false;
    pendingTargetHz_ = 0.0;
    pendingTargetCount_ = 0;
}

void ModernPitchEngine::ScaleQuantizer::setScale(const double* ratios,
                                                 int ratioCount,
                                                 double rootFrequency) noexcept
{
    rootFrequency_ = std::isfinite(rootFrequency) && rootFrequency > 0.0
        ? rootFrequency
        : 440.0;

    ratios_.fill(1.0);
    ratioCount_ = 0;
    ratios_[static_cast<std::size_t>(ratioCount_++)] = 1.0;

    if (ratios != nullptr && ratioCount > 0)
    {
        const int safeCount = std::min(ratioCount, maxScaleRatios);
        for (int index = 0;
             index < safeCount && ratioCount_ < maxScaleRatios;
             ++index)
        {
            double ratio = ratios[index];
            if (!std::isfinite(ratio) || ratio <= 0.0)
                continue;

            const double logarithm = safeLog2(ratio);
            ratio = std::exp2(logarithm - std::floor(logarithm));
            if (ratio >= 2.0)
                ratio = 1.0;

            bool duplicate = false;
            for (int existing = 0; existing < ratioCount_; ++existing)
            {
                if (std::abs(ratios_[static_cast<std::size_t>(existing)]
                             - ratio) < 1.0e-8)
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                ratios_[static_cast<std::size_t>(ratioCount_++)] = ratio;
        }
    }

    std::sort(ratios_.begin(),
              ratios_.begin() + std::max(1, ratioCount_));
}

double ModernPitchEngine::ScaleQuantizer::centsBetween(double a,
                                                       double b) noexcept
{
    if (!(a > 0.0) || !(b > 0.0)
        || !std::isfinite(a) || !std::isfinite(b))
        return std::numeric_limits<double>::infinity();
    return 1200.0 * safeLog2(a / b);
}

double ModernPitchEngine::ScaleQuantizer::nearestScaleFrequency(
    double frequencyHz) const noexcept
{
    if (!(frequencyHz > 0.0)
        || !std::isfinite(frequencyHz)
        || !(rootFrequency_ > 0.0))
        return frequencyHz;

    double bestFrequency = frequencyHz;
    double bestDistance = std::numeric_limits<double>::infinity();

    for (int index = 0; index < ratioCount_; ++index)
    {
        const double base = rootFrequency_
            * ratios_[static_cast<std::size_t>(index)];
        if (!(base > 0.0) || !std::isfinite(base))
            continue;

        const double octave = std::nearbyint(safeLog2(frequencyHz / base));
        for (int offset = -1; offset <= 1; ++offset)
        {
            const double candidate = base
                * std::exp2(octave + static_cast<double>(offset));
            const double distance = std::abs(centsBetween(frequencyHz,
                                                          candidate));
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestFrequency = candidate;
            }
        }
    }

    return bestFrequency;
}

double ModernPitchEngine::ScaleQuantizer::chooseTarget(
    double detectedPitchHz,
    float humanize,
    float minimumScaleStepCents,
    bool onset,
    float detectorConfidence,
    int& pendingOctaveObservations,
    int& octaveState) noexcept
{
    pendingOctaveObservations = 0;
    octaveState = 0;

    if (!(detectedPitchHz > 0.0) || !std::isfinite(detectedPitchHz))
        return currentTargetHz();

    const double candidate = nearestScaleFrequency(detectedPitchHz);
    if (!(candidate > 0.0) || !std::isfinite(candidate))
        return currentTargetHz();

    if (!targetValid_)
    {
        currentTargetHz_ = candidate;
        targetValid_ = true;
        previousDetectedHz_ = detectedPitchHz;
        return currentTargetHz_;
    }

    const double safeStep = std::clamp(
        static_cast<double>(minimumScaleStepCents), 0.1, 1200.0);
    const double sameNoteBandCents = 3.0
        + static_cast<double>(std::clamp(humanize, 0.0f, 1.0f))
            * std::min(80.0, 0.45 * safeStep);

    const double distanceFromCurrent = std::abs(
        centsBetween(detectedPitchHz, currentTargetHz_));
    if (!onset && distanceFromCurrent <= sameNoteBandCents)
    {
        previousDetectedHz_ = detectedPitchHz;
        pendingTargetValid_ = false;
        pendingTargetCount_ = 0;
        return currentTargetHz_;
    }

    const double targetJump = centsBetween(candidate, currentTargetHz_);
    const double observedJump = previousDetectedHz_ > 0.0
        ? centsBetween(detectedPitchHz, previousDetectedHz_)
        : targetJump;

    int requiredObservations = onset ? 1 : 2;
    if (std::abs(targetJump) > 850.0 && std::abs(targetJump) < 1350.0)
    {
        octaveState = targetJump > 0.0 ? 1 : -1;
        const bool observedSupportsOctave = std::abs(observedJump) > 700.0;
        requiredObservations = observedSupportsOctave ? 3 : 6;
        if (detectorConfidence < 0.65f)
            ++requiredObservations;
    }

    if (std::abs(targetJump) <= 0.1)
    {
        previousDetectedHz_ = detectedPitchHz;
        pendingTargetValid_ = false;
        pendingTargetCount_ = 0;
        return currentTargetHz_;
    }

    if (pendingTargetValid_
        && std::abs(centsBetween(candidate, pendingTargetHz_)) < 8.0)
    {
        ++pendingTargetCount_;
    }
    else
    {
        pendingTargetValid_ = true;
        pendingTargetHz_ = candidate;
        pendingTargetCount_ = 1;
    }

    pendingOctaveObservations = pendingTargetCount_;
    if (pendingTargetCount_ >= requiredObservations)
    {
        currentTargetHz_ = pendingTargetHz_;
        pendingTargetValid_ = false;
        pendingTargetCount_ = 0;
    }

    previousDetectedHz_ = detectedPitchHz;
    return currentTargetHz_;
}

//==============================================================================
// TransportClock
void ModernPitchEngine::TransportClock::prepare(
    int reportedLatencySamples) noexcept
{
    minimumDelay_ = 8;
    rangeSamples_ = std::max(
        16, 2 * (std::max(16, reportedLatencySamples) - minimumDelay_));
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

    if (deviation < 1.0e-8)
    {
        phase_ = 0.5;
        plan.delayA = static_cast<double>(minimumDelay_)
                    + 0.5 * static_cast<double>(rangeSamples_);
        plan.delayB = plan.delayA;
        plan.gainA = 1.0f;
        plan.gainB = 0.0f;
        return plan;
    }

    const double phaseB = phase_ < 0.5 ? phase_ + 0.5 : phase_ - 0.5;
    const auto weight = [](double phase) noexcept -> float
    {
        return static_cast<float>(0.5 - 0.5 * std::cos(twoPi * phase));
    };
    const auto delay = [this, safeRatio](double phase) noexcept -> double
    {
        const double directionPhase = safeRatio >= 1.0 ? 1.0 - phase : phase;
        return static_cast<double>(minimumDelay_)
             + directionPhase * static_cast<double>(rangeSamples_);
    };

    plan.delayA = delay(phase_);
    plan.delayB = delay(phaseB);
    plan.gainA = weight(phase_);
    plan.gainB = weight(phaseB);

    const float gainSum = plan.gainA + plan.gainB;
    if (gainSum > 1.0e-8f)
    {
        plan.gainA /= gainSum;
        plan.gainB /= gainSum;
    }

    const double increment = std::min(
        0.24, deviation / static_cast<double>(rangeSamples_));
    phase_ += increment;
    phase_ -= std::floor(phase_);
    return plan;
}

//==============================================================================
// ChannelPath
void ModernPitchEngine::ChannelPath::prepare(double sampleRate,
                                             int reportedLatencySamples)
{
    static_cast<void>(reportedLatencySamples);
    const double safeRate = std::isfinite(sampleRate)
        ? std::max(8000.0, sampleRate)
        : 48000.0;
    coefficientSmoothing_ = 1.0f - static_cast<float>(
        std::exp(-1.0 / (0.018 * safeRate)));
    coefficientSmoothing_ = std::clamp(coefficientSmoothing_, 0.0005f, 0.08f);
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
}

void ModernPitchEngine::ChannelPath::setLpcTarget(
    const std::array<float, maximumLpcOrder>& coefficients,
    float strength) noexcept
{
    const float safeStrength = std::clamp(strength, 0.0f, 0.96f);
    for (int index = 0; index < maximumLpcOrder; ++index)
    {
        const float bandwidth = std::pow(0.94f,
                                         static_cast<float>(index + 1));
        targetLpc_[static_cast<std::size_t>(index)] = std::clamp(
            coefficients[static_cast<std::size_t>(index)]
                * safeStrength * bandwidth,
            -0.95f,
            0.95f);
    }
}

float ModernPitchEngine::ChannelPath::sanitise(float value) noexcept
{
    if (!std::isfinite(value)
        || std::fpclassify(value) == FP_SUBNORMAL)
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

    for (int index = 0; index < maximumLpcOrder; ++index)
    {
        currentLpc_[static_cast<std::size_t>(index)]
            += coefficientSmoothing_
                * (targetLpc_[static_cast<std::size_t>(index)]
                   - currentLpc_[static_cast<std::size_t>(index)]);
    }

    double prediction = 0.0;
    for (int index = 0; index < maximumLpcOrder; ++index)
    {
        prediction += static_cast<double>(
            currentLpc_[static_cast<std::size_t>(index)])
            * static_cast<double>(
                inputHistory_[static_cast<std::size_t>(index)]);
    }
    const float residual = sanitise(
        safeInput - static_cast<float>(prediction));

    for (int index = maximumLpcOrder - 1; index > 0; --index)
    {
        inputHistory_[static_cast<std::size_t>(index)]
            = inputHistory_[static_cast<std::size_t>(index - 1)];
    }
    inputHistory_[0] = safeInput;

    const int writeIndex = static_cast<int>(
        sampleCounter_ & (transportRingSize - 1));
    residualRing_[static_cast<std::size_t>(writeIndex)] = residual;

    const float shiftedResidual = sanitise(
        plan.gainA * interpolateResidual(
            static_cast<double>(sampleCounter_) - plan.delayA)
        + plan.gainB * interpolateResidual(
            static_cast<double>(sampleCounter_) - plan.delayB));

    double synthesisPrediction = 0.0;
    for (int index = 0; index < maximumLpcOrder; ++index)
    {
        synthesisPrediction += static_cast<double>(
            currentLpc_[static_cast<std::size_t>(index)])
            * static_cast<double>(
                outputHistory_[static_cast<std::size_t>(index)]);
    }

    const float output = sanitise(
        shiftedResidual + static_cast<float>(synthesisPrediction));

    for (int index = maximumLpcOrder - 1; index > 0; --index)
    {
        outputHistory_[static_cast<std::size_t>(index)]
            = outputHistory_[static_cast<std::size_t>(index - 1)];
    }
    outputHistory_[0] = output;

    ++sampleCounter_;
    return output;
}

float ModernPitchEngine::ChannelPath::processBypassed(
    float input,
    int latencySamples) noexcept
{
    const float safeInput = sanitise(input);
    const int writeIndex = static_cast<int>(
        sampleCounter_ & (transportRingSize - 1));
    bypassRing_[static_cast<std::size_t>(writeIndex)] = safeInput;

    const std::int64_t readAbsolute = sampleCounter_
        - static_cast<std::int64_t>(std::max(0, latencySamples));
    const int readIndex = static_cast<int>(
        readAbsolute & (transportRingSize - 1));
    const float output = bypassRing_[static_cast<std::size_t>(readIndex)];
    ++sampleCounter_;
    return sanitise(output);
}

//==============================================================================
// ModernPitchEngine
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

float ModernPitchEngine::clamp01(float value) noexcept
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

double ModernPitchEngine::wrapToNearestOctave(double cents) noexcept
{
    if (!std::isfinite(cents))
        return 0.0;
    return cents - 1200.0 * std::nearbyint(cents / 1200.0);
}

void ModernPitchEngine::prepare(double sampleRate,
                                int maximumExpectedSamplesPerBlock,
                                int numberOfChannels,
                                LatencyMode latencyMode)
{
    sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate)
                                            : 48000.0;
    maximumBlockSize_ = std::max(1, maximumExpectedSamplesPerBlock);
    channelCount_ = std::clamp(numberOfChannels, 1, maxSupportedChannels);
    latencyMode_ = latencyMode;
    latencySamples_ = latencyForMode(latencyMode_);

    monoScratch_.assign(static_cast<std::size_t>(maximumBlockSize_), 0.0f);

    linkedTracker_.prepare(sampleRate_, latencyMode_);
    linkedQuantizer_.reset();
    linkedClock_.prepare(latencySamples_);

    for (int channel = 0; channel < maxSupportedChannels; ++channel)
    {
        channelTrackers_[static_cast<std::size_t>(channel)].prepare(
            sampleRate_, latencyMode_);
        channelQuantizers_[static_cast<std::size_t>(channel)].reset();
        channelClocks_[static_cast<std::size_t>(channel)].prepare(
            latencySamples_);
        channelPaths_[static_cast<std::size_t>(channel)].prepare(
            sampleRate_, latencySamples_);
    }

    correctionSmoothingCoefficient_ = 1.0
        - std::exp(-1.0 / (0.001 * sampleRate_));
    reset();
}

void ModernPitchEngine::reset() noexcept
{
    linkedTracker_.reset();
    linkedQuantizer_.reset();
    linkedClock_.reset();

    for (int channel = 0; channel < maxSupportedChannels; ++channel)
    {
        channelTrackers_[static_cast<std::size_t>(channel)].reset();
        channelQuantizers_[static_cast<std::size_t>(channel)].reset();
        channelClocks_[static_cast<std::size_t>(channel)].reset();
        channelPaths_[static_cast<std::size_t>(channel)].reset();
    }

    std::fill(monoScratch_.begin(), monoScratch_.end(), 0.0f);
    currentLpcTarget_.fill(0.0f);

    desiredCorrectionCents_ = 0.0;
    currentCorrectionCents_ = 0.0;
    speedDelaySamplesRemaining_ = 0;
    targetValid_ = false;
    targetPitchHz_ = 0.0;
    targetRevision_ = 0;
    channelDesiredCorrectionCents_.fill(0.0);
    channelCurrentCorrectionCents_.fill(0.0);
    channelSpeedDelaySamplesRemaining_.fill(0);
    channelTargetValid_.fill(false);
    channelTargetPitchHz_.fill(0.0);
    latestObservation_ = {};
    latestChannelObservation_.fill(PitchObservation {});

    meterSequence_.store(0u, std::memory_order_relaxed);
    meterPitchHz_.store(0.0f, std::memory_order_relaxed);
    meterTargetHz_.store(0.0f, std::memory_order_relaxed);
    meterConfidence_.store(0.0f, std::memory_order_relaxed);
    meterVoicing_.store(0.0f, std::memory_order_relaxed);
    meterPeriodicity_.store(0.0f, std::memory_order_relaxed);
    meterCorrectionCents_.store(0.0f, std::memory_order_relaxed);
    meterOnsetStrength_.store(0.0f, std::memory_order_relaxed);
    meterDetectorSupport_.store(0, std::memory_order_relaxed);
    meterOctaveState_.store(0, std::memory_order_relaxed);
    meterPendingOctave_.store(0, std::memory_order_relaxed);
    meterTrackingState_.store(
        static_cast<int>(TrackingState::unvoiced),
        std::memory_order_relaxed);
}

std::array<float, ModernPitchEngine::maximumLpcOrder>
ModernPitchEngine::calculateLpc(const float* mono,
                                int samples) noexcept
{
    std::array<float, maximumLpcOrder> result {};
    if (mono == nullptr || samples <= maximumLpcOrder + 2)
        return result;

    std::array<double, maximumLpcOrder + 1> autocorrelation {};
    for (int lag = 0; lag <= maximumLpcOrder; ++lag)
    {
        double sum = 0.0;
        for (int index = lag; index < samples; ++index)
        {
            sum += static_cast<double>(mono[index])
                * static_cast<double>(mono[index - lag]);
        }
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
        for (int index = 1; index < order; ++index)
        {
            numerator -= coefficients[static_cast<std::size_t>(index)]
                * autocorrelation[static_cast<std::size_t>(order - index)];
        }

        double reflection = numerator / std::max(1.0e-12, error);
        reflection = std::clamp(reflection, -0.96, 0.96);

        auto previous = coefficients;
        coefficients[static_cast<std::size_t>(order)] = reflection;
        for (int index = 1; index < order; ++index)
        {
            coefficients[static_cast<std::size_t>(index)]
                = previous[static_cast<std::size_t>(index)]
                - reflection
                    * previous[static_cast<std::size_t>(order - index)];
        }

        error *= std::max(0.02, 1.0 - reflection * reflection);
    }

    for (int index = 0; index < maximumLpcOrder; ++index)
    {
        result[static_cast<std::size_t>(index)] = static_cast<float>(
            coefficients[static_cast<std::size_t>(index + 1)]);
    }
    return result;
}

void ModernPitchEngine::updateLpcTarget(
    const juce::AudioBuffer<float>& buffer,
    int channels,
    int samples,
    float requestedAmount,
    float periodicity,
    float onsetStrength) noexcept
{
    if (samples <= 0 || channels <= 0)
        return;

    const int safeSamples = std::min(
        samples, static_cast<int>(monoScratch_.size()));
    for (int sample = 0; sample < safeSamples; ++sample)
    {
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel)
            sum += static_cast<double>(buffer.getSample(channel, sample));
        monoScratch_[static_cast<std::size_t>(sample)] = static_cast<float>(
            sum / static_cast<double>(channels));
    }

    currentLpcTarget_ = calculateLpc(monoScratch_.data(), safeSamples);

    const float periodicEnvelope = smoothStep(0.28f, 0.72f, periodicity);
    const float transientRelease = 1.0f
        - 0.92f * smoothStep(0.32f, 0.85f, onsetStrength);
    const float strength = clamp01(requestedAmount)
        * periodicEnvelope
        * transientRelease;

    for (int channel = 0; channel < channels; ++channel)
    {
        channelPaths_[static_cast<std::size_t>(channel)].setLpcTarget(
            currentLpcTarget_, strength);
    }
}

double ModernPitchEngine::correctionForObservation(
    double detectedHz,
    double targetHz,
    const Parameters& parameters) const noexcept
{
    if (!(detectedHz > 0.0) || !(targetHz > 0.0)
        || !std::isfinite(detectedHz) || !std::isfinite(targetHz))
        return 0.0;

    double errorCents = 1200.0 * safeLog2(targetHz / detectedHz);
    errorCents = wrapToNearestOctave(errorCents);

    const double maximumCents = 1200.0 * std::clamp(
        static_cast<double>(parameters.maximumCorrectionSemitones),
        0.0,
        24.0);
    errorCents = std::clamp(errorCents, -maximumCents, maximumCents);

    const double allowedErrorCents = 50.0
        * (1.0 - static_cast<double>(clamp01(parameters.amount)));
    const double magnitude = std::abs(errorCents);
    if (magnitude <= allowedErrorCents)
        return 0.0;

    return std::copysign(magnitude - allowedErrorCents, errorCents);
}

void ModernPitchEngine::updateCorrection(
    const PitchObservation& observation,
    const Parameters& parameters) noexcept
{
    if (!observation.valid)
        return;

    int pending = 0;
    int octaveState = 0;
    const double previousTarget = targetPitchHz_;
    const double target = linkedQuantizer_.chooseTarget(
        observation.frequencyHz,
        parameters.humanize,
        parameters.minScaleStepCents,
        observation.onset,
        observation.confidence,
        pending,
        octaveState);

    if (!(target > 0.0) || !std::isfinite(target))
        return;

    const bool targetChanged = !targetValid_
        || std::abs(1200.0 * safeLog2(target
            / std::max(1.0e-12, previousTarget))) > 0.1;

    targetPitchHz_ = target;
    targetValid_ = true;
    desiredCorrectionCents_ = correctionForObservation(
        observation.frequencyHz,
        targetPitchHz_,
        parameters);

    if (observation.onset || targetChanged)
    {
        speedDelaySamplesRemaining_ = std::max(
            0,
            static_cast<int>(std::lround(
                std::clamp(static_cast<double>(parameters.retuneTimeMs),
                           0.0,
                           500.0)
                * 0.001 * sampleRate_)));
        currentCorrectionCents_ = 0.0;
        ++targetRevision_;
    }

    meterOctaveState_.store(octaveState, std::memory_order_relaxed);
    meterPendingOctave_.store(pending, std::memory_order_relaxed);
}

void ModernPitchEngine::updateChannelCorrection(
    int channel,
    const PitchObservation& observation,
    const Parameters& parameters) noexcept
{
    if (channel < 0 || channel >= channelCount_ || !observation.valid)
        return;

    auto& quantizer = channelQuantizers_[static_cast<std::size_t>(channel)];
    int pending = 0;
    int octaveState = 0;
    const double previousTarget =
        channelTargetPitchHz_[static_cast<std::size_t>(channel)];
    const double target = quantizer.chooseTarget(
        observation.frequencyHz,
        parameters.humanize,
        parameters.minScaleStepCents,
        observation.onset,
        observation.confidence,
        pending,
        octaveState);

    if (!(target > 0.0) || !std::isfinite(target))
        return;

    const bool targetChanged =
        !channelTargetValid_[static_cast<std::size_t>(channel)]
        || std::abs(1200.0 * safeLog2(
            target / std::max(1.0e-12, previousTarget))) > 0.1;

    channelTargetPitchHz_[static_cast<std::size_t>(channel)] = target;
    channelTargetValid_[static_cast<std::size_t>(channel)] = true;
    channelDesiredCorrectionCents_[static_cast<std::size_t>(channel)]
        = correctionForObservation(observation.frequencyHz,
                                   target,
                                   parameters);

    if (observation.onset || targetChanged)
    {
        channelSpeedDelaySamplesRemaining_[static_cast<std::size_t>(channel)]
            = std::max(
                0,
                static_cast<int>(std::lround(
                    std::clamp(
                        static_cast<double>(parameters.retuneTimeMs),
                        0.0,
                        500.0)
                    * 0.001 * sampleRate_)));
        channelCurrentCorrectionCents_[static_cast<std::size_t>(channel)] = 0.0;
    }

    if (channel == 0)
    {
        meterOctaveState_.store(octaveState, std::memory_order_relaxed);
        meterPendingOctave_.store(pending, std::memory_order_relaxed);
    }
}

double ModernPitchEngine::currentRatioForSample() noexcept
{
    if (!targetValid_)
        return 1.0;

    if (speedDelaySamplesRemaining_ > 0)
    {
        --speedDelaySamplesRemaining_;
        currentCorrectionCents_ = 0.0;
        return 1.0;
    }

    currentCorrectionCents_ += correctionSmoothingCoefficient_
        * (desiredCorrectionCents_ - currentCorrectionCents_);
    return std::clamp(std::exp2(currentCorrectionCents_ / 1200.0),
                      0.25,
                      4.0);
}

double ModernPitchEngine::currentChannelRatioForSample(int channel) noexcept
{
    if (channel < 0 || channel >= channelCount_)
        return 1.0;

    const auto index = static_cast<std::size_t>(channel);
    if (!channelTargetValid_[index])
        return 1.0;

    if (channelSpeedDelaySamplesRemaining_[index] > 0)
    {
        --channelSpeedDelaySamplesRemaining_[index];
        channelCurrentCorrectionCents_[index] = 0.0;
        return 1.0;
    }

    channelCurrentCorrectionCents_[index] += correctionSmoothingCoefficient_
        * (channelDesiredCorrectionCents_[index]
           - channelCurrentCorrectionCents_[index]);
    return std::clamp(
        std::exp2(channelCurrentCorrectionCents_[index] / 1200.0),
        0.25,
        4.0);
}

void ModernPitchEngine::publishMetering(
    const PitchObservation& observation) noexcept
{
    meterSequence_.fetch_add(1u, std::memory_order_acq_rel);
    meterPitchHz_.store(observation.frequencyHz, std::memory_order_relaxed);
    meterTargetHz_.store(static_cast<float>(targetPitchHz_),
                         std::memory_order_relaxed);
    meterConfidence_.store(observation.confidence,
                           std::memory_order_relaxed);
    meterVoicing_.store(observation.voicing, std::memory_order_relaxed);
    meterPeriodicity_.store(observation.periodicity,
                            std::memory_order_relaxed);
    meterCorrectionCents_.store(
        static_cast<float>(currentCorrectionCents_),
        std::memory_order_relaxed);
    meterOnsetStrength_.store(observation.onsetStrength,
                              std::memory_order_relaxed);
    meterDetectorSupport_.store(observation.detectorSupport,
                                std::memory_order_relaxed);

    TrackingState state = TrackingState::unvoiced;
    if (observation.valid)
        state = speedDelaySamplesRemaining_ > 0
            ? TrackingState::attack
            : TrackingState::stable;
    else if (targetValid_)
        state = TrackingState::release;

    meterTrackingState_.store(static_cast<int>(state),
                              std::memory_order_relaxed);
    meterSequence_.fetch_add(1u, std::memory_order_release);
}

void ModernPitchEngine::process(
    juce::AudioBuffer<float>& buffer,
    const double* scaleRatios,
    int numberOfScaleRatios,
    double rootFrequency,
    const Parameters& parameters)
{
    CreativeTempo::HostPosition emptyPosition;
    process(buffer,
            scaleRatios,
            numberOfScaleRatios,
            rootFrequency,
            parameters,
            emptyPosition);
}

void ModernPitchEngine::process(
    juce::AudioBuffer<float>& buffer,
    const double* scaleRatios,
    int numberOfScaleRatios,
    double rootFrequency,
    const Parameters& parameters,
    const CreativeTempo::HostPosition& hostTempoPosition)
{
    static_cast<void>(hostTempoPosition);

    const int channels = std::min({
        buffer.getNumChannels(),
        channelCount_,
        maxSupportedChannels
    });
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    linkedQuantizer_.setScale(scaleRatios,
                              numberOfScaleRatios,
                              rootFrequency);
    for (int channel = 0; channel < channels; ++channel)
    {
        channelQuantizers_[static_cast<std::size_t>(channel)].setScale(
            scaleRatios,
            numberOfScaleRatios,
            rootFrequency);
    }

    updateLpcTarget(buffer,
                    channels,
                    samples,
                    parameters.formantPreservation,
                    latestObservation_.periodicity,
                    latestObservation_.onsetStrength);

    std::array<float*, maxSupportedChannels> channelData {};
    for (int channel = 0; channel < channels; ++channel)
        channelData[static_cast<std::size_t>(channel)]
            = buffer.getWritePointer(channel);

    const bool dualMono = parameters.stereoMode == StereoMode::dualMono
        && channels > 1;

    for (int sample = 0; sample < samples; ++sample)
    {
        double linkedAnalysis = 0.0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const float input = channelData[static_cast<std::size_t>(channel)]
                [sample];
            linkedAnalysis += std::isfinite(input)
                ? static_cast<double>(input)
                : 0.0;
        }
        const float linkedSample = static_cast<float>(
            linkedAnalysis / static_cast<double>(channels));

        PitchObservation observation;
        if (linkedTracker_.push(linkedSample,
                                parameters.minimumPitchHz,
                                parameters.maximumPitchHz,
                                parameters.detectorSensitivity,
                                observation))
        {
            latestObservation_ = observation;
            if (!dualMono)
                updateCorrection(observation, parameters);
        }

        if (dualMono)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                PitchObservation channelObservation;
                const float input =
                    channelData[static_cast<std::size_t>(channel)][sample];
                if (channelTrackers_[static_cast<std::size_t>(channel)].push(
                        input,
                        parameters.minimumPitchHz,
                        parameters.maximumPitchHz,
                        parameters.detectorSensitivity,
                        channelObservation))
                {
                    latestChannelObservation_[static_cast<std::size_t>(channel)]
                        = channelObservation;
                    updateChannelCorrection(channel,
                                            channelObservation,
                                            parameters);
                }
            }

            for (int channel = 0; channel < channels; ++channel)
            {
                const double ratio = currentChannelRatioForSample(channel);
                const auto plan =
                    channelClocks_[static_cast<std::size_t>(channel)].next(
                        ratio);
                float& value =
                    channelData[static_cast<std::size_t>(channel)][sample];
                value = channelPaths_[static_cast<std::size_t>(channel)].process(
                    value,
                    plan);
            }

            targetPitchHz_ = channelTargetPitchHz_[0];
            targetValid_ = channelTargetValid_[0];
            currentCorrectionCents_ = channelCurrentCorrectionCents_[0];
            latestObservation_ = latestChannelObservation_[0];
        }
        else
        {
            const double ratio = currentRatioForSample();
            const auto plan = linkedClock_.next(ratio);
            for (int channel = 0; channel < channels; ++channel)
            {
                float& value =
                    channelData[static_cast<std::size_t>(channel)][sample];
                value = channelPaths_[static_cast<std::size_t>(channel)].process(
                    value,
                    plan);
            }
        }
    }

    publishMetering(latestObservation_);
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
    const int channels = std::min({
        buffer.getNumChannels(),
        channelCount_,
        maxSupportedChannels
    });
    const int samples = buffer.getNumSamples();

    for (int channel = 0; channel < channels; ++channel)
    {
        float* data = buffer.getWritePointer(channel);
        auto& path = channelPaths_[static_cast<std::size_t>(channel)];
        for (int sample = 0; sample < samples; ++sample)
            data[sample] = path.processBypassed(data[sample], latencySamples_);
    }
}

ModernPitchEngine::Metering ModernPitchEngine::getMetering() const noexcept
{
    Metering result;

    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const std::uint32_t before = meterSequence_.load(
            std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;

        result.detectedPitchHz = meterPitchHz_.load(
            std::memory_order_relaxed);
        result.targetPitchHz = meterTargetHz_.load(
            std::memory_order_relaxed);
        result.confidence = meterConfidence_.load(
            std::memory_order_relaxed);
        result.voicing = meterVoicing_.load(
            std::memory_order_relaxed);
        result.harmonicity = meterPeriodicity_.load(
            std::memory_order_relaxed);
        result.spectralReliability = result.confidence;
        result.consensus = result.harmonicity;
        result.correctionCents = meterCorrectionCents_.load(
            std::memory_order_relaxed);
        result.wetMix = 1.0f;
        result.transitionBlend = 0.0f;
        result.outputTargetCoherence = result.confidence * 100.0f;
        result.outputPhysicalHarmonicFit = result.harmonicity * 100.0f;
        result.outputMeterValid = result.detectedPitchHz > 0.0f ? 1.0f : 0.0f;
        result.detectorSupport = meterDetectorSupport_.load(
            std::memory_order_relaxed);
        result.octaveState = meterOctaveState_.load(
            std::memory_order_relaxed);
        result.pendingOctaveObservations = meterPendingOctave_.load(
            std::memory_order_relaxed);
        result.state = static_cast<TrackingState>(
            meterTrackingState_.load(std::memory_order_relaxed));

        const std::uint32_t after = meterSequence_.load(
            std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u)
            return result;
    }

    return result;
}
