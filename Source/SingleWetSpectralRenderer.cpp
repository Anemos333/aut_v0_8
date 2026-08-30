#include "SingleWetSpectralRenderer.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace {
constexpr double pi=3.1415926535897932384626433832795, twoPi=2.0*pi;
int nextPowerOfTwo(int v) noexcept { int r=1; while(r<v) r<<=1; return r; }
float clamp01(float v) noexcept { return std::clamp(std::isfinite(v)?v:0.0f,0.0f,1.0f); }
float sanitiseAudioSample(float v) noexcept { if(!std::isfinite(v)||std::fpclassify(v)==FP_SUBNORMAL) return 0.0f; return std::clamp(v,-32.0f,32.0f); }
float smoothStep(float a,float b,float v) noexcept { if(b<=a) return v>=b?1.0f:0.0f; const float x=std::clamp((v-a)/(b-a),0.0f,1.0f); return x*x*(3.0f-2.0f*x); }
double sanitiseCorrectionCents(double c) noexcept { return std::clamp(std::isfinite(c) ? c : 0.0, -2400.0, 2400.0); }
}

void SingleWetSpectralRenderer::prepare(double sampleRate,
                                                        int frameSize)
{
    sampleRate_ = std::max(8000.0, sampleRate);
    frameSize_ = std::max(64, nextPowerOfTwo(frameSize));
    hopSize_ = std::max(1, frameSize_ / 4);

    const int inputRingSize = nextPowerOfTwo(frameSize_ * 4);
    inputRing_.assign(static_cast<std::size_t>(inputRingSize), 0.0f);
    inputRingMask_ = inputRingSize - 1;

    const int outputRingSize = nextPowerOfTwo(frameSize_ * 8);
    outputRingMask_ = outputRingSize - 1;

    window_.resize(static_cast<std::size_t>(frameSize_));
    for (int index = 0; index < frameSize_; ++index)
    {
        const double periodicHann = 0.5 - 0.5 * std::cos(
            twoPi * static_cast<double>(index)
            / static_cast<double>(frameSize_));
        window_[static_cast<std::size_t>(index)] = static_cast<float>(
            std::sqrt(std::max(0.0, periodicHann)));
    }

    // Precompute the FFT permutation and roots. The old implementation rebuilt
    // both for every forward and inverse transform.
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

    sineTable_.resize(static_cast<std::size_t>(sineTableSize + 1));
    for (int index = 0; index <= sineTableSize; ++index)
    {
        sineTable_[static_cast<std::size_t>(index)] = static_cast<float>(
            std::sin(twoPi * static_cast<double>(index)
                     / static_cast<double>(sineTableSize)));
    }

    formantGainTable_.resize(static_cast<std::size_t>(
        (formantLevelCount + 1) * (formantRatioTableSize + 1)));
    for (int level = 0; level <= formantLevelCount; ++level)
    {
        const double amount = static_cast<double>(level)
                            / static_cast<double>(formantLevelCount);
        for (int ratioIndex = 0; ratioIndex <= formantRatioTableSize; ++ratioIndex)
        {
            const double ratio = 0.56 + (1.78 - 0.56)
                * static_cast<double>(ratioIndex)
                / static_cast<double>(formantRatioTableSize);
            formantGainTable_[static_cast<std::size_t>(
                level * (formantRatioTableSize + 1) + ratioIndex)] =
                static_cast<float>(std::pow(ratio, amount));
        }
    }

    // sqrt-Hann with 75% overlap has a constant sum of squared windows.
    double overlapNormalisation = 0.0;
    const int overlapCount = std::max(1, frameSize_ / hopSize_);
    for (int overlap = 0; overlap < overlapCount; ++overlap)
    {
        const int index = (overlap * hopSize_) % frameSize_;
        const double value = window_[static_cast<std::size_t>(index)];
        overlapNormalisation += value * value;
    }
    synthesisGain_ = static_cast<float>(1.0
        / std::max(1.0e-9, overlapNormalisation));
    envelopeUpdateInterval_ = 2;

    const int positiveBinCount = frameSize_ / 2 + 1;
    fftBuffer_.assign(static_cast<std::size_t>(frameSize_), Complex {});
    magnitudes_.assign(static_cast<std::size_t>(positiveBinCount), 0.0f);
    analysisPhases_.assign(static_cast<std::size_t>(positiveBinCount), 0.0f);
    previousMagnitudes_.assign(static_cast<std::size_t>(positiveBinCount), 0.0f);
    previousAnalysisPhases_.assign(static_cast<std::size_t>(positiveBinCount), 0.0f);
    trueSourceBins_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);
    propagatedPhases_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);
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

    layer_.spectrum.assign(static_cast<std::size_t>(frameSize_), Complex {});
    layer_.synthesisPhases.assign(static_cast<std::size_t>(positiveBinCount), 0.0);
    layer_.outputAccumulationRing.assign(static_cast<std::size_t>(outputRingSize), 0.0f);
    layer_.phaseInitialised = false;

    // FULL_SPECTRUM_SINGLE_TRANSPORT_V1
    // Quality, Live and Experimental share one reconstruction law. Frame
    // size may change latency/resolution, but classification is never a
    // licence to route spectral energy through a different reconstruction.
    profile_ = AnalysisProfile {};

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

    noiseReductionAttackCoefficient_ = frameCoefficient(28.0f);
    noiseReductionReleaseCoefficient_ = frameCoefficient(180.0f);
    transientNoiseRestoreCoefficient_ = frameCoefficient(6.0f);
    reset();
}

void SingleWetSpectralRenderer::clearLayerOutput(SynthesisLayer& layer) noexcept { std::fill(layer.outputAccumulationRing.begin(),layer.outputAccumulationRing.end(),0.0f); }
void SingleWetSpectralRenderer::reset() noexcept
{
    std::fill(inputRing_.begin(),inputRing_.end(),0.0f); std::fill(fftBuffer_.begin(),fftBuffer_.end(),Complex{});
    std::fill(magnitudes_.begin(),magnitudes_.end(),0.0f); std::fill(analysisPhases_.begin(),analysisPhases_.end(),0.0f);
    std::fill(previousMagnitudes_.begin(),previousMagnitudes_.end(),0.0f); std::fill(previousAnalysisPhases_.begin(),previousAnalysisPhases_.end(),0.0f);
    std::fill(trueSourceBins_.begin(),trueSourceBins_.end(),0.0); std::fill(propagatedPhases_.begin(),propagatedPhases_.end(),0.0);
    std::fill(logMagnitudes_.begin(),logMagnitudes_.end(),0.0f); std::fill(rawSpectralEnvelope_.begin(),rawSpectralEnvelope_.end(),1.0f); std::fill(spectralEnvelope_.begin(),spectralEnvelope_.end(),1.0f);
    std::fill(rawHarmonicMask_.begin(),rawHarmonicMask_.end(),1.0f); std::fill(harmonicMask_.begin(),harmonicMask_.end(),1.0f); std::fill(harmonicMaskScratch_.begin(),harmonicMaskScratch_.end(),1.0f);
    std::fill(prefixSum_.begin(),prefixSum_.end(),0.0); std::fill(nearestPeak_.begin(),nearestPeak_.end(),0); peakBins_.clear();
    std::fill(layer_.spectrum.begin(),layer_.spectrum.end(),Complex{}); std::fill(layer_.synthesisPhases.begin(),layer_.synthesisPhases.end(),0.0); clearLayerOutput(layer_); layer_.phaseInitialised=false;
    inputSampleCounter_=0; analysisPhaseInitialised_=false; phaseResetPending_=false; envelopeInitialised_=false; envelopeFrameCounter_=0;
    smoothedFormantPreservation_=0.0f; smoothedBreathiness_=0.0f; smoothedHarmonicity_=1.0f; smoothedNoisePathAmount_=0.0f; smoothedNoiseGain_=1.0f;
    currentNoiseReductionDb_=0.0f; smoothedPolyphony_=0.0f; smoothedSpectralReliability_=1.0f; smoothedMaskStability_=1.0f; breathProtection_=0.0f; breathPersistenceMs_=0.0f; noiseDominanceMs_=0.0f;
}

double SingleWetSpectralRenderer::wrapPhase(double phase) noexcept
{
    while (phase > pi)
        phase -= twoPi;
    while (phase < -pi)
        phase += twoPi;
    return phase;
}

float SingleWetSpectralRenderer::readInputSample(
    std::int64_t absoluteSample) const noexcept
{
    if (absoluteSample < 0 || inputRing_.empty())
        return 0.0f;

    const int index = static_cast<int>(absoluteSample & inputRingMask_);
    return inputRing_[static_cast<std::size_t>(index)];
}

void SingleWetSpectralRenderer::fft(std::vector<Complex>& data,
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

void SingleWetSpectralRenderer::fastSinCos(
    double phase,
    float& sine,
    float& cosine) const noexcept
{
    // synthesis phases are kept close to [-pi, pi], so a single addition is
    // sufficient before table lookup.
    double wrapped = phase;
    if (wrapped < 0.0)
        wrapped += twoPi;
    else if (wrapped >= twoPi)
        wrapped -= twoPi;

    const double tablePosition = wrapped
        * (static_cast<double>(sineTableSize) / twoPi);
    const int baseIndex = static_cast<int>(tablePosition) & (sineTableSize - 1);
    const float fraction = static_cast<float>(
        tablePosition - static_cast<double>(static_cast<int>(tablePosition)));

    const int nextIndex = baseIndex + 1;
    const float sin0 = sineTable_[static_cast<std::size_t>(baseIndex)];
    const float sin1 = sineTable_[static_cast<std::size_t>(nextIndex)];
    sine = sin0 + fraction * (sin1 - sin0);

    const double cosinePosition = tablePosition
        + static_cast<double>(sineTableSize / 4);
    const int cosineInteger = static_cast<int>(cosinePosition);
    const int cosineIndex = cosineInteger & (sineTableSize - 1);
    const float cosineFraction = static_cast<float>(
        cosinePosition - static_cast<double>(cosineInteger));
    const float cos0 = sineTable_[static_cast<std::size_t>(cosineIndex)];
    const float cos1 = sineTable_[static_cast<std::size_t>(cosineIndex + 1)];
    cosine = cos0 + cosineFraction * (cos1 - cos0);
}

float SingleWetSpectralRenderer::lookupFormantGain(
    float envelopeRatio,
    float formantAmount) const noexcept
{
    const float ratio = std::clamp(envelopeRatio, 0.56f, 1.78f);
    const float amount = clamp01(formantAmount);

    if (amount <= 1.0e-5f)
        return 1.0f;
    if (amount >= 0.99999f)
        return ratio;

    const float ratioPosition = (ratio - 0.56f)
        * static_cast<float>(formantRatioTableSize) / (1.78f - 0.56f);
    const int ratioIndex = std::clamp(static_cast<int>(ratioPosition),
                                      0,
                                      formantRatioTableSize - 1);
    const float ratioFraction = ratioPosition - static_cast<float>(ratioIndex);

    const float levelPosition = amount * static_cast<float>(formantLevelCount);
    const int levelIndex = std::clamp(static_cast<int>(levelPosition),
                                      0,
                                      formantLevelCount - 1);
    const float levelFraction = levelPosition - static_cast<float>(levelIndex);
    const int rowSize = formantRatioTableSize + 1;

    const auto sampleRow = [&](int level) noexcept
    {
        const std::size_t offset = static_cast<std::size_t>(level * rowSize + ratioIndex);
        const float a = formantGainTable_[offset];
        const float b = formantGainTable_[offset + 1];
        return a + ratioFraction * (b - a);
    };

    const float lower = sampleRow(levelIndex);
    const float upper = sampleRow(levelIndex + 1);
    return lower + levelFraction * (upper - lower);
}

void SingleWetSpectralRenderer::calculateEnvelope(
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

void SingleWetSpectralRenderer::calculatePeakRegions(
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

float SingleWetSpectralRenderer::interpolateEnvelope(
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

float SingleWetSpectralRenderer::binFrequency(int bin) const noexcept
{
    if (frameSize_ <= 0)
        return 0.0f;

    return static_cast<float>(sampleRate_
        * static_cast<double>(std::max(0, bin))
        / static_cast<double>(frameSize_));
}

float SingleWetSpectralRenderer::calculateHighBandFlatness(
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

void SingleWetSpectralRenderer::updateHarmonicNoiseAnalysis(
    int positiveBins,
    float spectralFlux,
    const Context& context) noexcept
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
    const bool reliableF0 = context.pitchAnchorFresh
                         && f0 >= 42.0f
                         && f0 <= static_cast<float>(sampleRate_ * 0.22)
                         && context.confidence >= 0.20f;
    float periodicEvidence = clamp01(
        0.42f * context.confidence
        + 0.32f * context.voicing
        + 0.26f * context.consensus);
    if (context.noteBodyLatched)
        periodicEvidence = std::max(periodicEvidence, clamp01(0.72f + 0.24f * context.noteBodyConfidence));
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
    const int smoothingRadius = 1;
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
        const float targetMask = clamp01(
            harmonicMaskScratch_[index]
            * std::clamp(breathMaskScale, 0.28f, 1.0f));

        // Low-confidence frames must not redraw the complete guidance map.
        // This map controls reconstruction care/phase locking only. It never
        // decides which spectral energy is transported to the target pitch.
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
    const bool sustainedMusicalState = context.stableMusicalBody || context.transitionBody;
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
    // are preserved rather than mistaken for removable noise. Spectral
    // reliability remains a reconstruction diagnostic, never correction authority.
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
    // weak, do not gate the whole residual; retaining at least 82% prevents
    // breath holes without introducing any dry path.
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

void SingleWetSpectralRenderer::synthesiseLayer(
    SynthesisLayer& layer,
    std::int64_t frameEndSample,
    double correctionCents,
    float formantPreservation,
    bool resetPhases,
    float phaseAnchor,
    int positiveBins) noexcept
{
    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});

    // Correction authority comes from the musical trajectory. The renderer
    // must not reinterpret register or reduce the requested interval.
    const double safeCents = sanitiseCorrectionCents(correctionCents);
    const double safeRatio = std::exp2(safeCents / 1200.0);
    const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)
                                    / static_cast<double>(frameSize_);

    const bool initialiseLayer = resetPhases || !layer.phaseInitialised;
    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const double analysisPhase =
            analysisPhases_[static_cast<std::size_t>(sourceBin)];

        if (initialiseLayer)
        {
            layer.synthesisPhases[static_cast<std::size_t>(sourceBin)] =
                analysisPhase;
        }
        else
        {
            double& synthesisPhase =
                layer.synthesisPhases[static_cast<std::size_t>(sourceBin)];
            synthesisPhase += expectedPhaseScale
                * trueSourceBins_[static_cast<std::size_t>(sourceBin)]
                * safeRatio;

            // Keep phases bounded. This improves numerical stability and makes
            // the lookup-table oscillator independent of song duration.
            synthesisPhase -= twoPi * std::nearbyint(synthesisPhase / twoPi);

            if (phaseAnchor > 0.0f)
            {
                const double phaseError = wrapPhase(
                    analysisPhase - synthesisPhase);
                synthesisPhase += static_cast<double>(phaseAnchor) * phaseError;
                synthesisPhase -= twoPi * std::nearbyint(synthesisPhase / twoPi);
            }
        }

        propagatedPhases_[static_cast<std::size_t>(sourceBin)] =
            layer.synthesisPhases[static_cast<std::size_t>(sourceBin)];
    }

    const float safeFormant = clamp01(formantPreservation);
    const float energyScale = static_cast<float>(1.0 / std::sqrt(safeRatio));

    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const std::size_t sourceIndex = static_cast<std::size_t>(sourceBin);
        const float magnitude = magnitudes_[sourceIndex];
        if (magnitude <= 1.0e-12f)
            continue;

        // The classifier is advisory only. Every spectral bin has exactly one
        // transported coordinate. Tonal/aperiodic evidence changes how strongly
        // the phase is locked and how de-breath gain is shaped, never whether a
        // fraction of the bin remains at its source pitch.
        const float phaseGuidance = clamp01(harmonicMask_[sourceIndex]);
        const float aperiodicEvidence = 1.0f - phaseGuidance;

        // Use the instantaneous-frequency estimate rather than the integer FFT
        // bin centre. This is essential in Live/Experimental, where a short FFT
        // otherwise quantises the reconstructed pitch into very coarse bins.
        const double sourcePosition = std::clamp(
            trueSourceBins_[sourceIndex],
            0.0,
            static_cast<double>(positiveBins));
        const double targetPosition = sourcePosition * safeRatio;
        if (targetPosition > static_cast<double>(positiveBins) + 1.0)
            continue;

        const int peak = nearestPeak_[sourceIndex];
        const double relativeAnalysisPhase = wrapPhase(
            static_cast<double>(analysisPhases_[sourceIndex])
            - static_cast<double>(analysisPhases_[static_cast<std::size_t>(peak)]));
        const double ownTransportPhase = initialiseLayer
            ? static_cast<double>(analysisPhases_[sourceIndex])
            : propagatedPhases_[sourceIndex];
        const double peakLockedPhase = initialiseLayer
            ? static_cast<double>(analysisPhases_[sourceIndex])
            : propagatedPhases_[static_cast<std::size_t>(peak)]
                + relativeAnalysisPhase;
        const double outputPhase = ownTransportPhase
            + static_cast<double>(phaseGuidance)
                * wrapPhase(peakLockedPhase - ownTransportPhase);

        const float sourceEnvelope = std::max(
            1.0e-8f,
            spectralEnvelope_[sourceIndex]);
        const float targetEnvelope = std::max(
            1.0e-8f,
            interpolateEnvelope(targetPosition));
        const float envelopeRatio = std::clamp(
            targetEnvelope / sourceEnvelope,
            0.56f,
            1.78f);
        const float formantGain = lookupFormantGain(envelopeRatio, safeFormant);

        const float frequencyHz = binFrequency(sourceBin);
        const float deBreathBandStrength = 0.16f
            + 0.84f * smoothStep(850.0f, 6200.0f, frequencyHz);
        const float reconstructionGain = 1.0f
            - aperiodicEvidence * deBreathBandStrength
                * (1.0f - smoothedNoiseGain_);
        const float outputMagnitude = magnitude
                                    * reconstructionGain
                                    * formantGain
                                    * energyScale;
        float phaseSine = 0.0f;
        float phaseCosine = 1.0f;
        fastSinCos(outputPhase, phaseSine, phaseCosine);
        const Complex polar(outputMagnitude * phaseCosine,
                            outputMagnitude * phaseSine);

        const int targetBin0 = static_cast<int>(std::floor(targetPosition));
        const float fraction = static_cast<float>(
            targetPosition - static_cast<double>(targetBin0));

        float lowerWeight = 1.0f - fraction;
        float upperWeight = fraction;
        const float weightPower = lowerWeight * lowerWeight
                                + upperWeight * upperWeight;
        const float weightNormalisation = weightPower > 1.0e-12f
            ? 1.0f / std::sqrt(weightPower)
            : 1.0f;
        lowerWeight *= weightNormalisation;
        upperWeight *= weightNormalisation;

        if (targetBin0 >= 0 && targetBin0 <= positiveBins)
        {
            layer.spectrum[static_cast<std::size_t>(targetBin0)] +=
                polar * lowerWeight;
        }

        const int targetBin1 = targetBin0 + 1;
        if (targetBin1 >= 0 && targetBin1 <= positiveBins)
            layer.spectrum[static_cast<std::size_t>(targetBin1)] +=
                polar * upperWeight;
    }

    layer.phaseInitialised = true;
    layer.spectrum[0] = Complex(layer.spectrum[0].real(), 0.0f);
    layer.spectrum[static_cast<std::size_t>(positiveBins)] =
        Complex(layer.spectrum[static_cast<std::size_t>(positiveBins)].real(),
                0.0f);

    for (int bin = 1; bin < positiveBins; ++bin)
    {
        layer.spectrum[static_cast<std::size_t>(frameSize_ - bin)] =
            std::conj(layer.spectrum[static_cast<std::size_t>(bin)]);
    }

    fft(layer.spectrum, true);

    const std::int64_t outputStartSample = frameEndSample + 1;
    for (int index = 0; index < frameSize_; ++index)
    {
        const float synthesisWindow = window_[static_cast<std::size_t>(index)];
        const float output = layer.spectrum[static_cast<std::size_t>(index)].real()
                           * synthesisWindow;
        const int outputIndex = static_cast<int>((outputStartSample + index)
                                                  & outputRingMask_);
        layer.outputAccumulationRing[static_cast<std::size_t>(outputIndex)] +=
            output;
    }
}

void SingleWetSpectralRenderer::processFrame(
    std::int64_t frameEndSample, double correctionCents,
    float formantPreservation, const Context& context) noexcept
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
    const bool resetAnalysis = phaseResetPending_
                            || !analysisPhaseInitialised_;
    phaseResetPending_ = false;

    const float phaseAnchor = resetAnalysis ? 0.0f
        : 0.32f * smoothStep(0.24f, 0.72f, spectralFlux);
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

        trueSourceBins_[static_cast<std::size_t>(sourceBin)] = trueSourceBin;
    }

    updateHarmonicNoiseAnalysis(positiveBins,
                                spectralFlux,
                                context);

    synthesiseLayer(layer_, frameEndSample, correctionCents, formantPreservation,
                    resetAnalysis, phaseAnchor, positiveBins);

    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        previousMagnitudes_[static_cast<std::size_t>(bin)] =
            magnitudes_[static_cast<std::size_t>(bin)];
        previousAnalysisPhases_[static_cast<std::size_t>(bin)] =
            analysisPhases_[static_cast<std::size_t>(bin)];
    }

    analysisPhaseInitialised_ = true;
}

float SingleWetSpectralRenderer::consumeLayerOutput(
    SynthesisLayer& layer,
    std::int64_t sample) noexcept
{
    const int outputIndex = static_cast<int>(sample & outputRingMask_);
    const std::size_t index = static_cast<std::size_t>(outputIndex);
    const float accumulated = layer.outputAccumulationRing[index];
    layer.outputAccumulationRing[index] = 0.0f;
    return accumulated * synthesisGain_;
}

float SingleWetSpectralRenderer::processSample(float inputSample,double correctionCents,float formantPreservation,const Context& context) noexcept
{
    inputSample=sanitiseAudioSample(inputSample); if(frameSize_<=0||inputRing_.empty()) return inputSample;
    const std::int64_t currentSample=inputSampleCounter_; inputRing_[static_cast<std::size_t>(currentSample & inputRingMask_)]=inputSample;
    const float formantTarget=clamp01(formantPreservation); const float c=formantTarget<smoothedFormantPreservation_?formantReductionCoefficient_:formantRecoveryCoefficient_;
    smoothedFormantPreservation_ += c*(formantTarget-smoothedFormantPreservation_);
    if(((currentSample+1)%hopSize_)==0) processFrame(currentSample,correctionCents,smoothedFormantPreservation_,context);
    const float shifted=consumeLayerOutput(layer_,currentSample); ++inputSampleCounter_; return sanitiseAudioSample(shifted);
}
float SingleWetSpectralRenderer::processBypassedSample(float inputSample) noexcept
{
    inputSample=sanitiseAudioSample(inputSample); if(frameSize_<=0||inputRing_.empty()) return inputSample;
    const std::int64_t currentSample=inputSampleCounter_; inputRing_[static_cast<std::size_t>(currentSample & inputRingMask_)]=inputSample;
    const float delayed=readInputSample(currentSample-frameSize_); if(!layer_.outputAccumulationRing.empty()) layer_.outputAccumulationRing[static_cast<std::size_t>(currentSample & outputRingMask_)]=0.0f;
    ++inputSampleCounter_; return sanitiseAudioSample(delayed);
}
