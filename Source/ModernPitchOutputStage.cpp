#include "ModernPitchEngine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;

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
} // namespace

//==============================================================================
// Single-spectrum output stage. Detector, target controller and trajectory live
// in ModernPitchEngine.cpp and are intentionally not reimplemented here.

void ModernPitchEngine::SpectralVoiceShifter::prepare(double sampleRate,
                                                        int frameSize)
{
    sampleRate_ = std::max(8000.0, sampleRate);
    frameSize_ = std::max(64, nextPowerOfTwo(frameSize));
    hopSize_ = std::max(1, frameSize_ / 4);

    const int inputRingSize = nextPowerOfTwo(frameSize_ * 4);
    inputRing_.assign(static_cast<std::size_t>(inputRingSize), 0.0f);
    inputRingMask_ = inputRingSize - 1;
    const int outputRingSize = nextPowerOfTwo(frameSize_ * 8);

    window_.resize(static_cast<std::size_t>(frameSize_));
    for (int index = 0; index < frameSize_; ++index)
    {
        const double periodicHann = 0.5 - 0.5 * std::cos(
            twoPi * static_cast<double>(index)
            / static_cast<double>(frameSize_));
        window_[static_cast<std::size_t>(index)] = static_cast<float>(
            std::sqrt(std::max(0.0, periodicHann)));
    }

    fftBitReversal_.resize(static_cast<std::size_t>(frameSize_));
    int fftBits = 0;
    while ((1 << fftBits) < frameSize_)
        ++fftBits;
    for (int index = 0; index < frameSize_; ++index)
    {
        unsigned value = static_cast<unsigned>(index);
        unsigned reversed = 0;
        for (int bit = 0; bit < fftBits; ++bit)
        {
            reversed = (reversed << 1u) | (value & 1u);
            value >>= 1u;
        }
        fftBitReversal_[static_cast<std::size_t>(index)] =
            static_cast<int>(reversed);
    }

    fftTwiddles_.resize(static_cast<std::size_t>(frameSize_ / 2));
    for (int index = 0; index < frameSize_ / 2; ++index)
    {
        const double angle = -twoPi * static_cast<double>(index)
                           / static_cast<double>(frameSize_);
        fftTwiddles_[static_cast<std::size_t>(index)] = Complex(
            static_cast<float>(std::cos(angle)),
            static_cast<float>(std::sin(angle)));
    }

    envelopeUpdateInterval_ = 2;
    const int positiveBinCount = frameSize_ / 2 + 1;
    fftBuffer_.assign(static_cast<std::size_t>(frameSize_), Complex {});
    magnitudes_.assign(static_cast<std::size_t>(positiveBinCount), 0.0f);
    analysisPhases_.assign(static_cast<std::size_t>(positiveBinCount), 0.0f);
    previousMagnitudes_.assign(static_cast<std::size_t>(positiveBinCount), 0.0f);
    previousAnalysisPhases_.assign(static_cast<std::size_t>(positiveBinCount), 0.0f);
    trueSourceBins_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);
    logMagnitudes_.assign(static_cast<std::size_t>(positiveBinCount), 0.0f);
    rawSpectralEnvelope_.assign(static_cast<std::size_t>(positiveBinCount), 1.0f);
    spectralEnvelope_.assign(static_cast<std::size_t>(positiveBinCount), 1.0f);
    rawHarmonicMask_.assign(static_cast<std::size_t>(positiveBinCount), 1.0f);
    harmonicMask_.assign(static_cast<std::size_t>(positiveBinCount), 1.0f);
    harmonicMaskScratch_.assign(static_cast<std::size_t>(positiveBinCount), 1.0f);
    prefixSum_.assign(static_cast<std::size_t>(positiveBinCount + 1), 0.0);
    nearestPeak_.assign(static_cast<std::size_t>(positiveBinCount), 0);
    peakBins_.clear();
    peakBins_.reserve(static_cast<std::size_t>(positiveBinCount));

    neumaton::outputv3::OutputPrepareSpec outputSpec;
    outputSpec.sampleRate = sampleRate_;
    outputSpec.frameSize = frameSize_;
    outputSpec.hopSize = hopSize_;
    outputSpec.positiveBinCount = positiveBinCount;
    outputSpec.outputRingSize = outputRingSize;
    outputSpec.maximumRidges = std::min(128, positiveBinCount);
    outputSpec.maximumObservations = positiveBinCount;
    ridgeLedger_.prepare(outputSpec);
    outputRenderer_.prepare(outputSpec);

    // The modes share the same target and renderer. These profiles only adapt
    // analysis evidence to the available FFT resolution; they never reduce
    // correction strength or GUI authority.
    if (frameSize_ <= 128)
    {
        profile_.combWeight = 0.58f;
        profile_.peakWeight = 0.15f;
        profile_.phaseWeight = 0.12f;
        profile_.periodicWeight = 0.15f;
        profile_.bodyFloorBase = 0.30f;
        profile_.bodyFloorTracking = 0.70f;
        profile_.bodyUpperHz = 5200.0f;
        profile_.maskAttackMs = 24.0f;
        profile_.maskReleaseMs = 110.0f;
        profile_.maskRisePerSecond = 22.0f;
        profile_.maskFallPerSecond = 7.0f;
        profile_.breathAttackMs = 35.0f;
        profile_.breathReleaseMs = 220.0f;
        profile_.metricAttackMs = 30.0f;
        profile_.metricReleaseMs = 180.0f;
        profile_.polyphonyAttackMs = 45.0f;
        profile_.polyphonyReleaseMs = 260.0f;
        profile_.reliabilityAttackMs = 35.0f;
        profile_.reliabilityReleaseMs = 180.0f;
        profile_.breathPersistenceStartMs = 40.0f;
        profile_.breathPersistenceFullMs = 180.0f;
        profile_.noiseDominanceStartMs = 60.0f;
        profile_.noiseDominanceFullMs = 220.0f;
        profile_.noiseDominanceThreshold = 0.80f;
        profile_.maximumNoiseReductionDb = 10.0f;
        profile_.unresolvedCombBlend = 0.78f;
        profile_.breathMaskBodyReduction = 0.03f;
        profile_.breathMaskAirReduction = 0.32f;
        profile_.polyphonyTrust = 0.45f;
    }
    else if (frameSize_ <= 256)
    {
        profile_.combWeight = 0.54f;
        profile_.peakWeight = 0.20f;
        profile_.phaseWeight = 0.16f;
        profile_.periodicWeight = 0.10f;
        profile_.bodyFloorBase = 0.22f;
        profile_.bodyFloorTracking = 0.78f;
        profile_.bodyUpperHz = 4900.0f;
        profile_.maskAttackMs = 16.0f;
        profile_.maskReleaseMs = 75.0f;
        profile_.maskRisePerSecond = 30.0f;
        profile_.maskFallPerSecond = 9.0f;
        profile_.breathAttackMs = 28.0f;
        profile_.breathReleaseMs = 180.0f;
        profile_.metricAttackMs = 22.0f;
        profile_.metricReleaseMs = 130.0f;
        profile_.polyphonyAttackMs = 35.0f;
        profile_.polyphonyReleaseMs = 220.0f;
        profile_.reliabilityAttackMs = 28.0f;
        profile_.reliabilityReleaseMs = 150.0f;
        profile_.breathPersistenceStartMs = 32.0f;
        profile_.breathPersistenceFullMs = 150.0f;
        profile_.noiseDominanceStartMs = 48.0f;
        profile_.noiseDominanceFullMs = 200.0f;
        profile_.noiseDominanceThreshold = 0.80f;
        profile_.maximumNoiseReductionDb = 11.0f;
        profile_.unresolvedCombBlend = 0.58f;
        profile_.breathMaskBodyReduction = 0.06f;
        profile_.breathMaskAirReduction = 0.48f;
        profile_.polyphonyTrust = 0.75f;
    }
    else
    {
        profile_ = AnalysisProfile {};
        profile_.maskAttackMs = 8.0f;
        profile_.maskReleaseMs = 42.0f;
        profile_.maskRisePerSecond = 50.0f;
        profile_.maskFallPerSecond = 16.0f;
        profile_.breathAttackMs = 18.0f;
        profile_.breathReleaseMs = 120.0f;
        profile_.metricAttackMs = 12.0f;
        profile_.metricReleaseMs = 80.0f;
        profile_.polyphonyAttackMs = 25.0f;
        profile_.polyphonyReleaseMs = 160.0f;
        profile_.reliabilityAttackMs = 18.0f;
        profile_.reliabilityReleaseMs = 100.0f;
        profile_.breathPersistenceStartMs = 24.0f;
        profile_.breathPersistenceFullMs = 125.0f;
        profile_.noiseDominanceStartMs = 35.0f;
        profile_.noiseDominanceFullMs = 180.0f;
        profile_.maximumNoiseReductionDb = 12.0f;
        profile_.unresolvedCombBlend = 0.30f;
        profile_.breathMaskBodyReduction = 0.10f;
        profile_.breathMaskAirReduction = 0.62f;
        profile_.polyphonyTrust = 1.0f;
    }

    const double envelopeUpdateSeconds = static_cast<double>(
        hopSize_ * envelopeUpdateInterval_) / sampleRate_;
    envelopeAttackCoefficient_ = static_cast<float>(
        1.0 - std::exp(-envelopeUpdateSeconds / 0.008));
    envelopeReleaseCoefficient_ = static_cast<float>(
        1.0 - std::exp(-envelopeUpdateSeconds / 0.035));
    formantReductionCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.004 * sampleRate_)));
    formantRecoveryCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.028 * sampleRate_)));

    const double frameSeconds = static_cast<double>(hopSize_) / sampleRate_;
    const auto frameCoefficient = [frameSeconds](float milliseconds) noexcept
    {
        const double seconds = std::max(0.001,
            static_cast<double>(milliseconds) * 0.001);
        return static_cast<float>(1.0 - std::exp(-frameSeconds / seconds));
    };

    breathAttackCoefficient_ = frameCoefficient(profile_.breathAttackMs);
    breathReleaseCoefficient_ = frameCoefficient(profile_.breathReleaseMs);
    maskAttackCoefficient_ = frameCoefficient(profile_.maskAttackMs);
    maskReleaseCoefficient_ = frameCoefficient(profile_.maskReleaseMs);
    metricAttackCoefficient_ = frameCoefficient(profile_.metricAttackMs);
    metricReleaseCoefficient_ = frameCoefficient(profile_.metricReleaseMs);
    polyphonyAttackCoefficient_ = frameCoefficient(profile_.polyphonyAttackMs);
    polyphonyReleaseCoefficient_ = frameCoefficient(profile_.polyphonyReleaseMs);
    reliabilityAttackCoefficient_ = frameCoefficient(profile_.reliabilityAttackMs);
    reliabilityReleaseCoefficient_ = frameCoefficient(profile_.reliabilityReleaseMs);
    maskRiseLimitPerFrame_ = static_cast<float>(frameSeconds)
        * profile_.maskRisePerSecond;
    maskFallLimitPerFrame_ = static_cast<float>(frameSeconds)
        * profile_.maskFallPerSecond;
    noiseReductionAttackCoefficient_ = frameCoefficient(
        frameSize_ <= 128 ? 42.0f : frameSize_ <= 256 ? 34.0f : 28.0f);
    noiseReductionReleaseCoefficient_ = frameCoefficient(
        frameSize_ <= 128 ? 260.0f : frameSize_ <= 256 ? 220.0f : 180.0f);
    transientNoiseRestoreCoefficient_ = frameCoefficient(6.0f);

    reset();
}

void ModernPitchEngine::SpectralVoiceShifter::reset() noexcept
{
    std::fill(inputRing_.begin(), inputRing_.end(), 0.0f);
    std::fill(fftBuffer_.begin(), fftBuffer_.end(), Complex {});
    std::fill(magnitudes_.begin(), magnitudes_.end(), 0.0f);
    std::fill(analysisPhases_.begin(), analysisPhases_.end(), 0.0f);
    std::fill(previousMagnitudes_.begin(), previousMagnitudes_.end(), 0.0f);
    std::fill(previousAnalysisPhases_.begin(), previousAnalysisPhases_.end(), 0.0f);
    std::fill(trueSourceBins_.begin(), trueSourceBins_.end(), 0.0);
    std::fill(logMagnitudes_.begin(), logMagnitudes_.end(), 0.0f);
    std::fill(rawSpectralEnvelope_.begin(), rawSpectralEnvelope_.end(), 1.0f);
    std::fill(spectralEnvelope_.begin(), spectralEnvelope_.end(), 1.0f);
    std::fill(rawHarmonicMask_.begin(), rawHarmonicMask_.end(), 1.0f);
    std::fill(harmonicMask_.begin(), harmonicMask_.end(), 1.0f);
    std::fill(harmonicMaskScratch_.begin(), harmonicMaskScratch_.end(), 1.0f);
    std::fill(prefixSum_.begin(), prefixSum_.end(), 0.0);
    std::fill(nearestPeak_.begin(), nearestPeak_.end(), 0);
    peakBins_.clear();

    ridgeLedger_.reset();
    outputRenderer_.reset();
    ridgeDiagnostics_ = {};
    previousCorrectionCents_ = 0.0;
    previousTargetPitchHz_ = 0.0f;
    trajectoryInitialised_ = false;

    inputSampleCounter_ = 0;
    analysisPhaseInitialised_ = false;
    phaseResetPending_ = false;
    envelopeInitialised_ = false;
    bypassStatePrimed_ = false;
    envelopeFrameCounter_ = 0;
    smoothedFormantPreservation_ = 0.0f;

    smoothedBreathiness_ = 0.0f;
    smoothedHarmonicity_ = 1.0f;
    smoothedNoisePathAmount_ = 0.0f;
    smoothedNoiseGain_ = 1.0f;
    currentNoiseReductionDb_ = 0.0f;
    smoothedPolyphony_ = 0.0f;
    smoothedSpectralReliability_ = 1.0f;
    smoothedMaskStability_ = 1.0f;
    breathProtection_ = 0.0f;
    breathPersistenceMs_ = 0.0f;
    noiseDominanceMs_ = 0.0f;

    outputSourceCorrespondence_ = 0.0f;
    outputTargetCoherence_ = 0.0f;
    outputPhysicalHarmonicFit_ = 0.0f;
    outputLedgerHealth_ = 100.0f;
    outputPhaseCoherence_ = 0.0f;
    outputReconstructionNeed_ = 0.0f;
    outputMeterValid_ = 0.0f;
    outputSourceMirrorFit_ = 0.0f;
    outputDoubleFamilyRisk_ = 0.0f;
    outputLedgerDeficit_ = 0.0f;
    outputMemoryReliability_ = 0.0f;
    outputPreIfftConsensus_ = 0.0f;
    outputSelectiveReconstructionNeed_ = 0.0f;
}

double ModernPitchEngine::SpectralVoiceShifter::wrapPhase(double phase) noexcept
{
    while (phase > pi)
        phase -= twoPi;
    while (phase < -pi)
        phase += twoPi;
    return phase;
}

float ModernPitchEngine::SpectralVoiceShifter::readInputSample(
    std::int64_t absoluteSample) const noexcept
{
    if (absoluteSample < 0 || inputRing_.empty())
        return 0.0f;

    const int index = static_cast<int>(absoluteSample & inputRingMask_);
    return inputRing_[static_cast<std::size_t>(index)];
}

void ModernPitchEngine::SpectralVoiceShifter::fft(std::vector<Complex>& data,
                                                   bool inverse) noexcept
{
    const int size = static_cast<int>(data.size());
    if (size != frameSize_ || fftBitReversal_.size() != data.size())
        return;

    for (int index = 0; index < size; ++index)
    {
        const int reversed = fftBitReversal_[static_cast<std::size_t>(index)];
        if (index < reversed)
            std::swap(data[static_cast<std::size_t>(index)],
                      data[static_cast<std::size_t>(reversed)]);
    }

    for (int length = 2; length <= size; length <<= 1)
    {
        const int halfLength = length / 2;
        const int twiddleStride = size / length;

        for (int startIndex = 0; startIndex < size; startIndex += length)
        {
            for (int offset = 0; offset < halfLength; ++offset)
            {
                Complex twiddle = fftTwiddles_[static_cast<std::size_t>(
                    offset * twiddleStride)];
                if (inverse)
                    twiddle = std::conj(twiddle);

                const Complex even = data[static_cast<std::size_t>(startIndex + offset)];
                const Complex odd = data[static_cast<std::size_t>(
                    startIndex + offset + halfLength)] * twiddle;
                data[static_cast<std::size_t>(startIndex + offset)] = even + odd;
                data[static_cast<std::size_t>(startIndex + offset + halfLength)] = even - odd;
            }
        }
    }

    if (inverse)
    {
        const float scale = 1.0f / static_cast<float>(size);
        for (Complex& value : data)
            value *= scale;
    }
}

void ModernPitchEngine::SpectralVoiceShifter::calculateEnvelope(
    int positiveBins) noexcept
{
    const double binWidthHz = sampleRate_ / static_cast<double>(frameSize_);
    const int smoothingRadius = std::clamp(
        static_cast<int>(std::lround(420.0 / std::max(1.0, binWidthHz))),
        2,
        std::max(2, positiveBins / 12));

    prefixSum_[0] = 0.0;
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        logMagnitudes_[static_cast<std::size_t>(bin)] = static_cast<float>(
            std::log(std::max(1.0e-9f,
                              magnitudes_[static_cast<std::size_t>(bin)])));
        prefixSum_[static_cast<std::size_t>(bin + 1)] =
            prefixSum_[static_cast<std::size_t>(bin)]
            + static_cast<double>(logMagnitudes_[static_cast<std::size_t>(bin)]);
    }

    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        const int first = std::max(0, bin - smoothingRadius);
        const int last = std::min(positiveBins, bin + smoothingRadius);
        const double sum = prefixSum_[static_cast<std::size_t>(last + 1)]
                         - prefixSum_[static_cast<std::size_t>(first)];
        const double average = sum / static_cast<double>(last - first + 1);
        rawSpectralEnvelope_[static_cast<std::size_t>(bin)] =
            static_cast<float>(std::exp(average));
    }

    if (!envelopeInitialised_)
    {
        for (int bin = 0; bin <= positiveBins; ++bin)
            spectralEnvelope_[static_cast<std::size_t>(bin)] =
                rawSpectralEnvelope_[static_cast<std::size_t>(bin)];
        envelopeInitialised_ = true;
        return;
    }

    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        const std::size_t index = static_cast<std::size_t>(bin);
        const float target = rawSpectralEnvelope_[index];
        const float coefficient = target > spectralEnvelope_[index]
            ? envelopeAttackCoefficient_
            : envelopeReleaseCoefficient_;
        spectralEnvelope_[index] += coefficient
            * (target - spectralEnvelope_[index]);
    }
}

void ModernPitchEngine::SpectralVoiceShifter::calculatePeakRegions(
    int positiveBins) noexcept
{
    peakBins_.clear();

    float maximumMagnitude = 0.0f;
    int maximumBin = 0;
    for (int bin = 1; bin < positiveBins; ++bin)
    {
        const float magnitude = magnitudes_[static_cast<std::size_t>(bin)];
        if (magnitude > maximumMagnitude)
        {
            maximumMagnitude = magnitude;
            maximumBin = bin;
        }
    }

    const float threshold = maximumMagnitude * 0.012f;
    for (int bin = 1; bin < positiveBins; ++bin)
    {
        const float centre = magnitudes_[static_cast<std::size_t>(bin)];
        if (centre >= threshold
            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]
            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])
        {
            peakBins_.push_back(bin);
        }
    }

    if (peakBins_.empty())
        peakBins_.push_back(maximumBin);

    int peakIndex = 0;
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        while (peakIndex + 1 < static_cast<int>(peakBins_.size()))
        {
            const int currentPeak = peakBins_[static_cast<std::size_t>(peakIndex)];
            const int nextPeak = peakBins_[static_cast<std::size_t>(peakIndex + 1)];
            if (bin <= (currentPeak + nextPeak) / 2)
                break;
            ++peakIndex;
        }

        nearestPeak_[static_cast<std::size_t>(bin)] =
            peakBins_[static_cast<std::size_t>(peakIndex)];
    }
}

float ModernPitchEngine::SpectralVoiceShifter::interpolateEnvelope(
    double binPosition) const noexcept
{
    if (spectralEnvelope_.empty())
        return 1.0f;

    const int maximumBin = static_cast<int>(spectralEnvelope_.size()) - 1;
    const double clamped = std::clamp(binPosition,
                                      0.0,
                                      static_cast<double>(maximumBin));
    const int lower = static_cast<int>(std::floor(clamped));
    const int upper = std::min(maximumBin, lower + 1);
    const float fraction = static_cast<float>(
        clamped - static_cast<double>(lower));
    return spectralEnvelope_[static_cast<std::size_t>(lower)]
         + fraction * (spectralEnvelope_[static_cast<std::size_t>(upper)]
                       - spectralEnvelope_[static_cast<std::size_t>(lower)]);
}

float ModernPitchEngine::SpectralVoiceShifter::binFrequency(int bin) const noexcept
{
    if (frameSize_ <= 0)
        return 0.0f;

    return static_cast<float>(sampleRate_
        * static_cast<double>(std::max(0, bin))
        / static_cast<double>(frameSize_));
}

float ModernPitchEngine::SpectralVoiceShifter::calculateHighBandFlatness(
    int firstBin,
    int lastBin) const noexcept
{
    if (magnitudes_.empty())
        return 0.0f;

    const int maximumBin = static_cast<int>(magnitudes_.size()) - 1;
    firstBin = std::clamp(firstBin, 0, maximumBin);
    lastBin = std::clamp(lastBin, firstBin, maximumBin);

    double logSum = 0.0;
    double linearSum = 0.0;
    int count = 0;

    for (int bin = firstBin; bin <= lastBin; ++bin)
    {
        const double magnitude = std::max(
            1.0e-12,
            static_cast<double>(magnitudes_[static_cast<std::size_t>(bin)]));
        logSum += std::log(magnitude);
        linearSum += magnitude;
        ++count;
    }

    if (count <= 0 || linearSum <= 1.0e-12)
        return 0.0f;

    const double geometricMean = std::exp(logSum / static_cast<double>(count));
    const double arithmeticMean = linearSum / static_cast<double>(count);
    return clamp01(static_cast<float>(
        geometricMean / std::max(1.0e-12, arithmeticMean)));
}

void ModernPitchEngine::SpectralVoiceShifter::updateHarmonicNoiseAnalysis(
    int positiveBins,
    float spectralFlux,
    const HarmonicNoiseContext& context) noexcept
{
    if (positiveBins <= 3 || magnitudes_.empty())
    {
        smoothedBreathiness_ += breathReleaseCoefficient_
            * (0.0f - smoothedBreathiness_);
        smoothedHarmonicity_ += metricReleaseCoefficient_
            * (0.0f - smoothedHarmonicity_);
        smoothedNoisePathAmount_ += metricReleaseCoefficient_
            * (1.0f - smoothedNoisePathAmount_);
        smoothedPolyphony_ += polyphonyReleaseCoefficient_
            * (0.0f - smoothedPolyphony_);
        smoothedSpectralReliability_ += reliabilityReleaseCoefficient_
            * (0.0f - smoothedSpectralReliability_);
        smoothedMaskStability_ += metricAttackCoefficient_
            * (1.0f - smoothedMaskStability_);
        breathProtection_ = smoothStep(0.24f, 0.74f, smoothedBreathiness_);
        return;
    }

    const float frameDurationMs = 1000.0f * static_cast<float>(hopSize_)
        / static_cast<float>(sampleRate_);
    const float binWidthHz = static_cast<float>(sampleRate_
        / static_cast<double>(frameSize_));
    const float f0 = context.detectedPitchHz;
    const bool reliableF0 = f0 >= 42.0f
                         && f0 <= static_cast<float>(sampleRate_ * 0.22)
                         && context.confidence >= 0.20f;
    const float periodicEvidence = clamp01(
        0.42f * context.confidence
        + 0.32f * context.voicing
        + 0.26f * context.consensus);
    const float resolutionRatio = reliableF0
        ? f0 / std::max(1.0f, binWidthHz)
        : 0.0f;
    const float resolvedCombAmount = smoothStep(0.60f, 1.70f,
                                                 resolutionRatio);

    double totalEnergy = 0.0;
    double highEnergy = 0.0;
    double airEnergy = 0.0;
    double baseHarmonicEnergy = 0.0;
    double prominentPeakEnergy = 0.0;
    double offFamilyPeakEnergy = 0.0;

    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        const std::size_t index = static_cast<std::size_t>(bin);
        const float magnitude = magnitudes_[index];
        const double energy = static_cast<double>(magnitude) * magnitude;
        const float frequencyHz = static_cast<float>(bin) * binWidthHz;
        totalEnergy += energy;

        if (frequencyHz >= 2800.0f)
            highEnergy += energy;
        if (frequencyHz >= 5200.0f)
            airEnergy += energy;

        const int previousBin = std::max(0, bin - 2);
        const int nextBin = std::min(positiveBins, bin + 2);
        float neighbourSum = 0.0f;
        int neighbourCount = 0;
        for (int neighbour = previousBin; neighbour <= nextBin; ++neighbour)
        {
            if (neighbour == bin)
                continue;
            neighbourSum += magnitudes_[static_cast<std::size_t>(neighbour)];
            ++neighbourCount;
        }
        const float neighbourMean = neighbourCount > 0
            ? neighbourSum / static_cast<float>(neighbourCount)
            : magnitude;
        const float localCrest = magnitude
            / std::max(1.0e-9f, neighbourMean);
        const float peakEvidence = smoothStep(1.10f, 3.40f, localCrest);

        const int nearestPeak = nearestPeak_[index];
        const float peakDistance = static_cast<float>(std::abs(nearestPeak - bin));
        const float peakSpread = 1.0f - smoothStep(0.65f, 3.25f, peakDistance);
        const float peakMagnitude = magnitudes_[static_cast<std::size_t>(
            std::clamp(nearestPeak, 0, positiveBins))];
        const float peakProminence = smoothStep(
            1.20f,
            5.50f,
            peakMagnitude / std::max(1.0e-9f, neighbourMean));
        const float localPeakEvidence = clamp01(
            0.46f * peakEvidence + 0.54f * peakSpread * peakProminence);

        const float phaseDeviation = static_cast<float>(std::abs(
            trueSourceBins_[index] - static_cast<double>(bin)));
        const float phaseCoherence = 1.0f
            - smoothStep(0.22f, 1.35f, phaseDeviation);

        float resolvedCombEvidence = 0.0f;
        float binCoverageEvidence = 0.0f;
        if (reliableF0 && frequencyHz >= 0.55f * f0)
        {
            const int harmonicNumber = std::max(
                1,
                static_cast<int>(std::lround(frequencyHz / f0)));
            const float expectedFrequency = static_cast<float>(harmonicNumber) * f0;
            const float distanceHz = std::abs(frequencyHz - expectedFrequency);

            const float resolvedToleranceHz = std::max(
                0.34f * binWidthHz,
                0.022f * f0 + 0.0035f * frequencyHz);
            resolvedCombEvidence = 1.0f
                - smoothStep(resolvedToleranceHz,
                             std::max(resolvedToleranceHz + 0.25f * binWidthHz,
                                      2.45f * resolvedToleranceHz),
                             distanceHz);

            // For 128/256-sample FFTs several harmonics may lie inside one
            // analysis bin.  Test the whole bin footprint rather than only
            // its centre; this is the F0-guided parametric mask used when the
            // individual partials are unresolved.
            const float coverageToleranceHz = 0.58f * binWidthHz
                                            + 0.055f * f0;
            binCoverageEvidence = 1.0f
                - smoothStep(coverageToleranceHz,
                             1.75f * coverageToleranceHz,
                             distanceHz);
        }

        const float guidedBandWeight = 1.0f - smoothStep(
            0.72f * profile_.bodyUpperHz,
            1.45f * profile_.bodyUpperHz,
            frequencyHz);
        const float unresolvedEvidence = binCoverageEvidence
            * periodicEvidence
            * profile_.unresolvedCombBlend
            * guidedBandWeight;
        const float combEvidence = clamp01(
            resolvedCombAmount * resolvedCombEvidence
            + (1.0f - resolvedCombAmount) * unresolvedEvidence);

        const float lowBandPrior = 1.0f
            - smoothStep(2500.0f, 8500.0f, frequencyHz);
        float rawMask = reliableF0
            ? (profile_.combWeight * combEvidence
               + profile_.peakWeight * localPeakEvidence
               + profile_.phaseWeight * phaseCoherence
               + profile_.periodicWeight * periodicEvidence * lowBandPrior)
            : (0.55f * localPeakEvidence
               + 0.35f * phaseCoherence
               + 0.10f * periodicEvidence * lowBandPrior);

        if (frequencyHz >= 70.0f && frequencyHz <= profile_.bodyUpperHz)
        {
            const float bodyWeight = 1.0f - smoothStep(
                0.52f * profile_.bodyUpperHz,
                profile_.bodyUpperHz,
                frequencyHz);
            const float voicedFloor = bodyWeight
                * (profile_.bodyFloorBase
                   + profile_.bodyFloorTracking * periodicEvidence)
                * (0.82f + 0.18f * context.consensus);
            rawMask = std::max(rawMask, voicedFloor);
        }

        if (bin == 0 || bin == positiveBins)
            rawMask = 0.0f;

        rawHarmonicMask_[index] = clamp01(rawMask);
        baseHarmonicEnergy += energy
            * static_cast<double>(rawHarmonicMask_[index]);

        // Polyphony/bleed evidence is measured only on prominent body-band
        // peaks.  It is intentionally conservative on unresolved FFTs and is
        // later combined with cross-rate disagreement from the pitch tracker.
        if (frequencyHz >= 75.0f && frequencyHz <= 6000.0f
            && localPeakEvidence > 0.18f)
        {
            const double weightedPeakEnergy = energy
                * static_cast<double>(localPeakEvidence);
            const float familyEvidence = clamp01(
                resolvedCombAmount * resolvedCombEvidence
                + (1.0f - resolvedCombAmount)
                    * (0.65f * binCoverageEvidence
                       + 0.35f * periodicEvidence));
            prominentPeakEnergy += weightedPeakEnergy;
            offFamilyPeakEnergy += weightedPeakEnergy
                * static_cast<double>(1.0f - familyEvidence);
        }
    }

    // Frequency smoothing radius depends on resolution.  Wider smoothing in
    // short modes avoids isolated mask islands, a common source of musical
    // noise and the perceived "wind" modulation.
    const int smoothingRadius = frameSize_ <= 128 ? 2 : 1;
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        float weightedSum = 0.0f;
        float weightTotal = 0.0f;
        for (int offset = -smoothingRadius; offset <= smoothingRadius; ++offset)
        {
            const int sourceBin = std::clamp(bin + offset, 0, positiveBins);
            const float weight = static_cast<float>(smoothingRadius + 1
                                                     - std::abs(offset));
            weightedSum += weight
                * rawHarmonicMask_[static_cast<std::size_t>(sourceBin)];
            weightTotal += weight;
        }
        harmonicMaskScratch_[static_cast<std::size_t>(bin)] = clamp01(
            weightedSum / std::max(1.0f, weightTotal));
    }

    const float highRatio = totalEnergy > 1.0e-14
        ? static_cast<float>(highEnergy / totalEnergy)
        : 0.0f;
    const float airRatio = totalEnergy > 1.0e-14
        ? static_cast<float>(airEnergy / totalEnergy)
        : 0.0f;
    const float baseHarmonicity = totalEnergy > 1.0e-14
        ? clamp01(static_cast<float>(baseHarmonicEnergy / totalEnergy))
        : 0.0f;

    const int highFirstBin = std::clamp(
        static_cast<int>(std::ceil(2800.0f / std::max(1.0f, binWidthHz))),
        1,
        positiveBins);
    const float highFlatness = calculateHighBandFlatness(highFirstBin,
                                                          positiveBins);
    const float highScore = smoothStep(0.16f, 0.55f, highRatio);
    const float airScore = smoothStep(0.055f, 0.30f, airRatio);
    const float flatScore = smoothStep(0.20f, 0.67f, highFlatness);
    const float noiseScore = smoothStep(0.28f, 0.78f, 1.0f - baseHarmonicity);
    const float weakPeriodicScore = 1.0f - periodicEvidence;
    const float transientEvidence = clamp01(
        0.68f * smoothStep(0.18f, 0.62f, spectralFlux)
        + 0.32f * clamp01(context.onsetStrength));
    const float transientPenalty = 0.44f
        * smoothStep(0.20f, 0.72f, spectralFlux)
        + 0.24f * clamp01(context.onsetStrength);

    float rawBreathiness = 0.28f * highScore
                         + 0.18f * airScore
                         + 0.20f * flatScore
                         + 0.24f * noiseScore
                         + 0.16f * weakPeriodicScore
                         - transientPenalty;
    rawBreathiness = clamp01(rawBreathiness);

    if (rawBreathiness > 0.34f && spectralFlux < 0.48f)
        breathPersistenceMs_ += frameDurationMs;
    else
        breathPersistenceMs_ -= 1.8f * frameDurationMs;
    breathPersistenceMs_ = std::clamp(breathPersistenceMs_,
                                      0.0f,
                                      600.0f);

    const float persistence = smoothStep(
        profile_.breathPersistenceStartMs,
        profile_.breathPersistenceFullMs,
        breathPersistenceMs_);
    rawBreathiness *= 0.58f + 0.42f * persistence;

    const float frameLevel = totalEnergy > 0.0
        ? static_cast<float>(std::sqrt(totalEnergy
            / static_cast<double>(std::max(1, positiveBins))))
        : 0.0f;
    rawBreathiness *= smoothStep(0.00008f, 0.00110f, frameLevel);

    const float breathCoefficient = rawBreathiness > smoothedBreathiness_
        ? breathAttackCoefficient_
        : breathReleaseCoefficient_;
    smoothedBreathiness_ += breathCoefficient
        * (rawBreathiness - smoothedBreathiness_);
    breathProtection_ = smoothStep(0.24f, 0.74f, smoothedBreathiness_);

    const float offFamilyRatio = prominentPeakEnergy > 1.0e-14
        ? clamp01(static_cast<float>(offFamilyPeakEnergy
                                     / prominentPeakEnergy))
        : 0.0f;
    const float trackerDisagreement = 1.0f
        - smoothStep(0.16f, 0.72f, context.consensus);
    const float peakConflict = smoothStep(0.34f, 0.78f, offFamilyRatio);
    const float polyphonyTarget = clamp01(
        profile_.polyphonyTrust
        * periodicEvidence
        * (0.72f * peakConflict
           + 0.28f * trackerDisagreement)
        * (1.0f - 0.72f * transientEvidence));
    const float polyphonyCoefficient = polyphonyTarget > smoothedPolyphony_
        ? polyphonyAttackCoefficient_
        : polyphonyReleaseCoefficient_;
    smoothedPolyphony_ += polyphonyCoefficient
        * (polyphonyTarget - smoothedPolyphony_);

    double finalHarmonicEnergy = 0.0;
    double finalNoiseEnergy = 0.0;
    double weightedMaskMotion = 0.0;
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        const std::size_t index = static_cast<std::size_t>(bin);
        const float frequencyHz = static_cast<float>(bin) * binWidthHz;
        const float highBandProtection = smoothStep(
            2400.0f,
            7800.0f,
            frequencyHz);
        const float maskReduction = profile_.breathMaskBodyReduction
            + profile_.breathMaskAirReduction * highBandProtection;
        const float breathMaskScale = 1.0f
            - breathProtection_ * maskReduction;

        // NEUMATON_RECONSTRUCTIVE_WET_V1_ASSERTIVE_MASK
        // In assertive correction, breath protection must not automatically
        // declassify high, tonal, F0-related material as residual/noise.  True
        // breath still passes through the residual reconstruction logic later.
        const float assertiveTonalMaskProtection = clamp01(
            context.hardCorrectionIntent
            * periodicEvidence
            * (0.45f + 0.55f * context.consensus)
            * (1.0f - 0.72f * transientEvidence));
        const float protectedBreathMaskScale = breathMaskScale
            + assertiveTonalMaskProtection * (1.0f - breathMaskScale);
        const float targetMask = clamp01(
            harmonicMaskScratch_[index]
            * std::clamp(protectedBreathMaskScale, 0.28f, 1.0f));

        // Low-confidence frames must not redraw the complete mask.  Retain the
        // previous spectral classification and allow only bounded, mode-aware
        // movement per frame.  Falling mask values expose more residual, so
        // they deliberately move more slowly in Live/Experimental.
        const float analysisTrust = clamp01(
            (0.18f + 0.82f * periodicEvidence)
            * (1.0f - 0.70f * smoothedPolyphony_)
            * (1.0f - 0.65f * transientEvidence));
        const float inertialTarget = harmonicMask_[index]
            + analysisTrust * (targetMask - harmonicMask_[index]);
        const float coefficient = inertialTarget > harmonicMask_[index]
            ? maskAttackCoefficient_
            : maskReleaseCoefficient_;
        const float unconstrainedDelta = coefficient
            * (inertialTarget - harmonicMask_[index]);
        const float boundedDelta = std::clamp(
            unconstrainedDelta,
            -maskFallLimitPerFrame_,
            maskRiseLimitPerFrame_);
        harmonicMask_[index] = clamp01(harmonicMask_[index] + boundedDelta);

        const double energy = static_cast<double>(magnitudes_[index])
                            * magnitudes_[index];
        weightedMaskMotion += energy * std::abs(static_cast<double>(boundedDelta));
        finalHarmonicEnergy += energy
            * static_cast<double>(harmonicMask_[index]);
        finalNoiseEnergy += energy
            * static_cast<double>(1.0f - harmonicMask_[index]);
    }

    const double classifiedEnergy = finalHarmonicEnergy + finalNoiseEnergy;
    const float harmonicityTarget = classifiedEnergy > 1.0e-14
        ? clamp01(static_cast<float>(finalHarmonicEnergy / classifiedEnergy))
        : 0.0f;
    const float noisePathTarget = classifiedEnergy > 1.0e-14
        ? clamp01(static_cast<float>(finalNoiseEnergy / classifiedEnergy))
        : 1.0f;
    const float maskMotion = classifiedEnergy > 1.0e-14
        ? clamp01(static_cast<float>(weightedMaskMotion / classifiedEnergy) * 12.0f)
        : 0.0f;
    const float maskStabilityTarget = 1.0f - maskMotion;
    const float maskStabilityCoefficient = maskStabilityTarget > smoothedMaskStability_
        ? metricAttackCoefficient_
        : metricReleaseCoefficient_;
    smoothedMaskStability_ += maskStabilityCoefficient
        * (maskStabilityTarget - smoothedMaskStability_);

    if (noisePathTarget >= profile_.noiseDominanceThreshold
        && spectralFlux < 0.50f)
    {
        noiseDominanceMs_ += frameDurationMs;
    }
    else
    {
        noiseDominanceMs_ -= 1.6f * frameDurationMs;
    }
    noiseDominanceMs_ = std::clamp(noiseDominanceMs_, 0.0f, 700.0f);
    const float noiseDominancePersistence = smoothStep(
        profile_.noiseDominanceStartMs,
        profile_.noiseDominanceFullMs,
        noiseDominanceMs_);
    const float noiseDominance = smoothStep(
        profile_.noiseDominanceThreshold,
        0.96f,
        noisePathTarget) * noiseDominancePersistence;

    // Long stable notes naturally contain more air.  Preserve a controlled
    // residual floor instead of progressively de-breathing the singer.  This
    // branch applies only when pitch evidence is strong and no competing
    // harmonic family is present.
    const bool sustainedMusicalState = context.trackingState == TrackingState::stable
                                    || context.trackingState == TrackingState::transition;
    const float longNoteAir = sustainedMusicalState
        ? smoothStep(0.35f, 2.20f, context.noteAgeSeconds)
            * periodicEvidence
            * (1.0f - smoothedPolyphony_)
        : 0.0f;

    const float reductionAmount = clamp01(context.breathReduction);
    const float softNoiseEvidence = smoothStep(0.04f, 0.60f, noisePathTarget);
    const float breathEvidence = std::max(
        breathProtection_ * persistence,
        0.80f * smoothStep(0.18f, 0.72f, smoothedBreathiness_));
    float reductionDrive = clamp01(
        softNoiseEvidence * (0.38f + 0.62f * breathEvidence)
        * (1.0f - transientEvidence));

    // Noise-dominant safety mode (>~80%): attenuate a sustained breath bed,
    // but never try to turn it into a pitched signal.  Polyphonic/bleed frames
    // are preserved rather than mistaken for removable noise; their pitch
    // correction authority is reduced through spectralReliability below.
    const float dominanceDrive = noiseDominance
        * (0.30f + 0.70f * breathEvidence)
        * (1.0f - transientEvidence);
    reductionDrive = std::max(reductionDrive, dominanceDrive);
    reductionDrive *= 1.0f - 0.62f * smoothedPolyphony_;
    reductionDrive *= 1.0f - 0.35f * longNoteAir;

    const float maximumReductionDb = profile_.maximumNoiseReductionDb
        * reductionAmount
        * (1.0f - 0.45f * longNoteAir);
    const float targetReductionDb = maximumReductionDb * reductionDrive;
    float targetNoiseGain = std::pow(10.0f, -targetReductionDb / 20.0f);

    const bool transientRestore = transientEvidence > 0.38f;
    if (transientRestore)
        targetNoiseGain = std::max(targetNoiseGain, 0.92f);

    // If almost everything is classified as noise and periodic evidence is
    // weak, do not gate the whole signal.  The controller will move toward the
    // aligned dry path instead; retaining at least 82% prevents breath holes.
    if (noiseDominance > 0.55f && periodicEvidence < 0.38f)
        targetNoiseGain = std::max(targetNoiseGain, 0.82f);

    const float noiseGainCoefficient = targetNoiseGain < smoothedNoiseGain_
        ? noiseReductionAttackCoefficient_
        : (transientRestore ? transientNoiseRestoreCoefficient_
                            : noiseReductionReleaseCoefficient_);
    smoothedNoiseGain_ += noiseGainCoefficient
        * (targetNoiseGain - smoothedNoiseGain_);
    smoothedNoiseGain_ = std::clamp(smoothedNoiseGain_, 0.20f, 1.0f);
    currentNoiseReductionDb_ = std::max(0.0f,
        -20.0f * std::log10(std::max(1.0e-6f, smoothedNoiseGain_)));

    const float harmonicCoefficient = harmonicityTarget > smoothedHarmonicity_
        ? metricAttackCoefficient_
        : metricReleaseCoefficient_;
    smoothedHarmonicity_ += harmonicCoefficient
        * (harmonicityTarget - smoothedHarmonicity_);

    const float noiseCoefficient = noisePathTarget > smoothedNoisePathAmount_
        ? metricAttackCoefficient_
        : metricReleaseCoefficient_;
    smoothedNoisePathAmount_ += noiseCoefficient
        * (noisePathTarget - smoothedNoisePathAmount_);

    const float familyReliability = periodicEvidence
        * (0.30f + 0.70f * harmonicityTarget)
        * (1.0f - 0.82f * smoothedPolyphony_);
    const float dominanceReliability = 1.0f
        - 0.64f * noiseDominance;
    const float reliabilityTarget = clamp01(
        (0.16f + 0.84f * familyReliability)
        * (0.50f + 0.50f * smoothedMaskStability_)
        * dominanceReliability);
    const float reliabilityCoefficient = reliabilityTarget
        > smoothedSpectralReliability_
        ? reliabilityAttackCoefficient_
        : reliabilityReleaseCoefficient_;
    smoothedSpectralReliability_ += reliabilityCoefficient
        * (reliabilityTarget - smoothedSpectralReliability_);
}

void ModernPitchEngine::SpectralVoiceShifter::processFrame(
    std::int64_t frameEndSample,
    const TransitionManager::Command& transition,
    float formantPreservation,
    const HarmonicNoiseContext& harmonicNoiseContext,
    bool forcePhaseReset) noexcept
{
    const std::int64_t frameStartSample = frameEndSample - frameSize_ + 1;
    for (int index = 0; index < frameSize_; ++index)
    {
        const float input = readInputSample(frameStartSample + index);
        fftBuffer_[static_cast<std::size_t>(index)] = Complex(
            input * window_[static_cast<std::size_t>(index)], 0.0f);
    }

    fft(fftBuffer_, false);

    const int positiveBins = frameSize_ / 2;
    double positiveFlux = 0.0;
    double magnitudeSum = 0.0;
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        const Complex value = fftBuffer_[static_cast<std::size_t>(bin)];
        const float magnitude = std::abs(value);
        magnitudes_[static_cast<std::size_t>(bin)] = magnitude;
        analysisPhases_[static_cast<std::size_t>(bin)] =
            std::atan2(value.imag(), value.real());
        positiveFlux += std::max(
            0.0f,
            magnitude - previousMagnitudes_[static_cast<std::size_t>(bin)]);
        magnitudeSum += magnitude;
    }

    if (!envelopeInitialised_
        || ++envelopeFrameCounter_ >= envelopeUpdateInterval_)
    {
        envelopeFrameCounter_ = 0;
        calculateEnvelope(positiveBins);
    }
    calculatePeakRegions(positiveBins);

    const float spectralFlux = magnitudeSum > 1.0e-12
        ? static_cast<float>(positiveFlux / magnitudeSum)
        : 0.0f;
    const bool resetAnalysis = forcePhaseReset
                            || phaseResetPending_
                            || !analysisPhaseInitialised_;
    phaseResetPending_ = false;

    const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)
                                    / static_cast<double>(frameSize_);
    const double binFromPhaseScale = static_cast<double>(frameSize_)
                                   / (twoPi * static_cast<double>(hopSize_));
    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const double analysisPhase =
            analysisPhases_[static_cast<std::size_t>(sourceBin)];
        double trueSourceBin = static_cast<double>(sourceBin);
        if (!resetAnalysis)
        {
            const double expectedAdvance = expectedPhaseScale
                                         * static_cast<double>(sourceBin);
            const double phaseDeviation = wrapPhase(
                analysisPhase
                - static_cast<double>(previousAnalysisPhases_[
                    static_cast<std::size_t>(sourceBin)])
                - expectedAdvance);
            trueSourceBin += phaseDeviation * binFromPhaseScale;
        }

        const float binReliability = clamp01(
            0.40f * smoothedSpectralReliability_
            + 0.35f * smoothedHarmonicity_
            + 0.25f * harmonicNoiseContext.consensus);
        const double maximumBinDrift = 0.16
            + 0.34 * static_cast<double>(binReliability);
        trueSourceBins_[static_cast<std::size_t>(sourceBin)] = std::clamp(
            trueSourceBin,
            static_cast<double>(sourceBin) - maximumBinDrift,
            static_cast<double>(sourceBin) + maximumBinDrift);
    }

    updateHarmonicNoiseAnalysis(positiveBins,
                                spectralFlux,
                                harmonicNoiseContext);

    const int positiveBinCount = positiveBins + 1;
    const double correctionCents = transition.dualSynthesis
        ? transition.primaryCents
            + static_cast<double>(clamp01(transition.blend))
                * (transition.secondaryCents - transition.primaryCents)
        : transition.primaryCents;

    neumaton::outputv3::AnalysisFrameView analysis;
    analysis.analysedSpectrum = { fftBuffer_.data(), frameSize_ };
    analysis.magnitudes = { magnitudes_.data(), positiveBinCount };
    analysis.analysisPhases = { analysisPhases_.data(), positiveBinCount };
    analysis.previousAnalysisPhases = {
        previousAnalysisPhases_.data(), positiveBinCount };
    analysis.trueSourceBins = { trueSourceBins_.data(), positiveBinCount };
    analysis.harmonicMask = { harmonicMask_.data(), positiveBinCount };
    analysis.spectralEnvelope = { spectralEnvelope_.data(), positiveBinCount };
    analysis.nearestPeak = { nearestPeak_.data(), positiveBinCount };
    analysis.peakBins = {
        peakBins_.empty() ? nullptr : peakBins_.data(),
        static_cast<int>(peakBins_.size()) };
    analysis.sampleRate = sampleRate_;
    analysis.frameSize = frameSize_;
    analysis.hopSize = hopSize_;
    analysis.positiveBinCount = positiveBinCount;
    analysis.frameEndSample = frameEndSample;
    analysis.detectedPitchHz = harmonicNoiseContext.detectedPitchHz;
    analysis.confidence = harmonicNoiseContext.confidence;
    analysis.voicing = harmonicNoiseContext.voicing;
    analysis.consensus = harmonicNoiseContext.consensus;
    analysis.onsetStrength = harmonicNoiseContext.onsetStrength;
    analysis.breathiness = smoothedBreathiness_;
    analysis.harmonicity = smoothedHarmonicity_;
    analysis.polyphony = smoothedPolyphony_;
    analysis.spectralReliability = smoothedSpectralReliability_;
    analysis.maskStability = smoothedMaskStability_;
    analysis.phaseReset = resetAnalysis;

    neumaton::outputv3::CorrectionTrajectoryFrame trajectory;
    trajectory.previousCorrectionCents = trajectoryInitialised_
        ? previousCorrectionCents_
        : correctionCents;
    trajectory.correctionCents = correctionCents;
    trajectory.previousTargetPitchHz = trajectoryInitialised_
        ? previousTargetPitchHz_
        : harmonicNoiseContext.targetPitchHz;
    trajectory.targetPitchHz = harmonicNoiseContext.targetPitchHz;
    trajectory.targetRevision = harmonicNoiseContext.targetRevision;
    trajectory.targetValid = harmonicNoiseContext.targetPitchHz > 0.0f
        && harmonicNoiseContext.trackingState != TrackingState::unvoiced
        && harmonicNoiseContext.trackingState != TrackingState::release;
    trajectory.forceReset = resetAnalysis;

    const auto ridgeFrame = ridgeLedger_.processFrame(analysis, trajectory);
    ridgeDiagnostics_ = ridgeLedger_.getDiagnostics();
    outputRenderer_.renderAndCommitFrame(analysis,
                                         trajectory,
                                         ridgeFrame,
                                         formantPreservation,
                                         frameEndSample);
    publishRendererDiagnostics();

    previousCorrectionCents_ = correctionCents;
    previousTargetPitchHz_ = harmonicNoiseContext.targetPitchHz;
    trajectoryInitialised_ = true;

    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        previousMagnitudes_[static_cast<std::size_t>(bin)] =
            magnitudes_[static_cast<std::size_t>(bin)];
        previousAnalysisPhases_[static_cast<std::size_t>(bin)] =
            analysisPhases_[static_cast<std::size_t>(bin)];
    }
    analysisPhaseInitialised_ = true;
}

void ModernPitchEngine::SpectralVoiceShifter::publishRendererDiagnostics() noexcept
{
    const auto& output = outputRenderer_.getDiagnostics();
    const float collision = clamp01(output.destinationCollisionEnergyRatio);
    const float ola = clamp01(output.overlapAddCoherence);
    const float phase = clamp01(output.phaseFieldCoherence);
    const float temporal = clamp01(output.temporalSpectrumDistance);
    const float gainNeed = clamp01(std::abs(output.requestedEnergyGainDb) / 6.0f);
    const float reconstruction = clamp01(
        0.42f * temporal + 0.38f * collision + 0.20f * gainNeed);

    outputSourceCorrespondence_ = 100.0f * clamp01(output.assignedEnergyRatio);
    outputTargetCoherence_ = 100.0f * ola;
    outputPhysicalHarmonicFit_ = 100.0f * phase;
    outputLedgerHealth_ = 100.0f * (1.0f - collision);
    outputPhaseCoherence_ = 100.0f * (0.58f * ola + 0.42f * phase);
    outputReconstructionNeed_ = 100.0f * reconstruction;
    outputMeterValid_ = output.frameValid ? 1.0f : 0.0f;

    outputSourceMirrorFit_ = outputSourceCorrespondence_;
    outputDoubleFamilyRisk_ = 100.0f * collision;
    outputLedgerDeficit_ = 100.0f * collision;
    outputMemoryReliability_ = 100.0f * ola;
    outputPreIfftConsensus_ = 50.0f * (ola + phase);
    outputSelectiveReconstructionNeed_ = outputReconstructionNeed_;
}

float ModernPitchEngine::SpectralVoiceShifter::processSample(
    float inputSample,
    const TransitionManager::Command& transition,
    float desiredWetMix,
    float formantPreservation,
    const HarmonicNoiseContext& harmonicNoiseContext,
    bool forcePhaseReset) noexcept
{
    inputSample = sanitiseAudioSample(inputSample);
    if (frameSize_ <= 0 || inputRing_.empty())
        return inputSample;

    static_cast<void>(desiredWetMix);
    bypassStatePrimed_ = false;

    const std::int64_t currentSample = inputSampleCounter_;
    const int inputIndex = static_cast<int>(currentSample & inputRingMask_);
    inputRing_[static_cast<std::size_t>(inputIndex)] = inputSample;
    if (forcePhaseReset)
        phaseResetPending_ = true;

    const float formantTarget = clamp01(formantPreservation);
    const float formantCoefficient = formantTarget < smoothedFormantPreservation_
        ? formantReductionCoefficient_
        : formantRecoveryCoefficient_;
    smoothedFormantPreservation_ += formantCoefficient
        * (formantTarget - smoothedFormantPreservation_);

    if (((currentSample + 1) % hopSize_) == 0)
    {
        processFrame(currentSample,
                     transition,
                     smoothedFormantPreservation_,
                     harmonicNoiseContext,
                     forcePhaseReset);
    }

    const float output = sanitiseAudioSample(
        outputRenderer_.consumeSample(currentSample));
    ++inputSampleCounter_;
    return output;
}

float ModernPitchEngine::SpectralVoiceShifter::processBypassedSample(
    float inputSample) noexcept
{
    inputSample = sanitiseAudioSample(inputSample);
    if (frameSize_ <= 0 || inputRing_.empty())
        return inputSample;

    const std::int64_t currentSample = inputSampleCounter_;
    const int inputIndex = static_cast<int>(currentSample & inputRingMask_);
    inputRing_[static_cast<std::size_t>(inputIndex)] = inputSample;
    outputRenderer_.discardSample(currentSample);

    const float delayedDry = readInputSample(currentSample - frameSize_);
    ++inputSampleCounter_;

    if (!bypassStatePrimed_)
    {
        analysisPhaseInitialised_ = false;
        phaseResetPending_ = true;
        envelopeInitialised_ = false;
        envelopeFrameCounter_ = 0;
        ridgeLedger_.reset();
        outputRenderer_.reset();
        ridgeDiagnostics_ = {};
        previousCorrectionCents_ = 0.0;
        previousTargetPitchHz_ = 0.0f;
        trajectoryInitialised_ = false;
        smoothedBreathiness_ = 0.0f;
        smoothedHarmonicity_ = 1.0f;
        smoothedNoisePathAmount_ = 0.0f;
        smoothedNoiseGain_ = 1.0f;
        currentNoiseReductionDb_ = 0.0f;
        smoothedPolyphony_ = 0.0f;
        smoothedSpectralReliability_ = 1.0f;
        smoothedMaskStability_ = 1.0f;
        breathProtection_ = 0.0f;
        breathPersistenceMs_ = 0.0f;
        noiseDominanceMs_ = 0.0f;
        bypassStatePrimed_ = true;
    }

    return delayedDry;
}
