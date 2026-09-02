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
    prefixSum_.assign(static_cast<std::size_t>(positiveBinCount + 1), 0.0);

    layer_.spectrum.assign(static_cast<std::size_t>(frameSize_), Complex {});
    layer_.synthesisPhases.assign(static_cast<std::size_t>(positiveBinCount), 0.0);
    layer_.outputAccumulationRing.assign(static_cast<std::size_t>(outputRingSize), 0.0f);
    layer_.phaseInitialised = false;

    // FULL_SPECTRUM_SINGLE_TRANSPORT_V1
    // STABLE_SINGLE_LATTICE_TRANSPORT_V3
    // PURE_SINGLE_TRANSPORT_V4
    // MINIMAL_RENDERER_V5
    // One analysis FFT -> one spectral transport -> one IFFT/OLA. Voice-state
    // analysis is upstream and has no runtime hook into this renderer.
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
    reset();
}

void SingleWetSpectralRenderer::clearLayerOutput(SynthesisLayer& layer) noexcept { std::fill(layer.outputAccumulationRing.begin(),layer.outputAccumulationRing.end(),0.0f); }
void SingleWetSpectralRenderer::reset() noexcept
{
    std::fill(inputRing_.begin(), inputRing_.end(), 0.0f);
    std::fill(fftBuffer_.begin(), fftBuffer_.end(), Complex {});
    std::fill(magnitudes_.begin(), magnitudes_.end(), 0.0f);
    std::fill(analysisPhases_.begin(), analysisPhases_.end(), 0.0f);
    std::fill(previousMagnitudes_.begin(), previousMagnitudes_.end(), 0.0f);
    std::fill(previousAnalysisPhases_.begin(), previousAnalysisPhases_.end(), 0.0f);
    std::fill(trueSourceBins_.begin(), trueSourceBins_.end(), 0.0);
    std::fill(propagatedPhases_.begin(), propagatedPhases_.end(), 0.0);
    std::fill(logMagnitudes_.begin(), logMagnitudes_.end(), 0.0f);
    std::fill(rawSpectralEnvelope_.begin(), rawSpectralEnvelope_.end(), 1.0f);
    std::fill(spectralEnvelope_.begin(), spectralEnvelope_.end(), 1.0f);
    std::fill(prefixSum_.begin(), prefixSum_.end(), 0.0);
    std::fill(layer_.spectrum.begin(), layer_.spectrum.end(), Complex {});
    std::fill(layer_.synthesisPhases.begin(), layer_.synthesisPhases.end(), 0.0);
    clearLayerOutput(layer_);
    layer_.phaseInitialised = false;
    inputSampleCounter_ = 0;
    analysisPhaseInitialised_ = false;
    phaseResetPending_ = false;
    envelopeInitialised_ = false;
    envelopeFrameCounter_ = 0;
    smoothedFormantPreservation_ = 0.0f;
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

void SingleWetSpectralRenderer::synthesiseLayer(
    SynthesisLayer& layer,
    std::int64_t frameEndSample,
    double correctionCents,
    float formantPreservation,
    bool resetPhases,
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

        // PURE_SINGLE_TRANSPORT_V4
        // Detector, voiced, breath, harmonicity and reliability state are
        // observers/supervisors only. They cannot select a second phase law,
        // attenuate spectral pieces or pull reconstruction toward dry analysis.
        // Every bin follows the one propagated transport phase.

        // STABLE_SINGLE_LATTICE_TRANSPORT_V3
        // Magnitudes keep one stable FFT geometry. trueSourceBins_ is an
        // instantaneous-frequency / phase-velocity estimate and belongs in the
        // synthesis phase integrator above; using it again as magnitude geometry
        // makes neighbouring leakage bins jump independently and fragments the
        // reconstructed voice. Every bin is transported once from the common
        // analysis lattice through the exact same correction ratio.
        const double targetPosition = static_cast<double>(sourceBin) * safeRatio;
        if (targetPosition > static_cast<double>(positiveBins) + 1.0)
            continue;

        const double outputPhase = propagatedPhases_[sourceIndex];

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

        // Formant is an explicit user control. No detector/classifier state
        // is allowed to scale the reconstructed spectrum.
        const float outputMagnitude = magnitude
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
    std::int64_t frameEndSample,
    double correctionCents,
    float formantPreservation) noexcept
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
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        const Complex value = fftBuffer_[static_cast<std::size_t>(bin)];
        magnitudes_[static_cast<std::size_t>(bin)] = std::abs(value);
        analysisPhases_[static_cast<std::size_t>(bin)] =
            std::atan2(value.imag(), value.real());
    }

    if (!envelopeInitialised_
        || ++envelopeFrameCounter_ >= envelopeUpdateInterval_)
    {
        envelopeFrameCounter_ = 0;
        calculateEnvelope(positiveBins);
    }

    const bool resetAnalysis = phaseResetPending_ || !analysisPhaseInitialised_;
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
        trueSourceBins_[static_cast<std::size_t>(sourceBin)] = trueSourceBin;
    }

    synthesiseLayer(layer_, frameEndSample, correctionCents,
                    formantPreservation, resetAnalysis, positiveBins);
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

float SingleWetSpectralRenderer::processSample(
    float inputSample,
    double correctionCents,
    float formantPreservation) noexcept
{
    inputSample = sanitiseAudioSample(inputSample);
    if (frameSize_ <= 0 || inputRing_.empty())
        return inputSample;
    const std::int64_t currentSample = inputSampleCounter_;
    inputRing_[static_cast<std::size_t>(currentSample & inputRingMask_)] = inputSample;
    const float target = clamp01(formantPreservation);
    const float coefficient = target < smoothedFormantPreservation_
        ? formantReductionCoefficient_ : formantRecoveryCoefficient_;
    smoothedFormantPreservation_ += coefficient
        * (target - smoothedFormantPreservation_);
    if (((currentSample + 1) % hopSize_) == 0)
        processFrame(currentSample, correctionCents, smoothedFormantPreservation_);
    const float shifted = consumeLayerOutput(layer_, currentSample);
    ++inputSampleCounter_;
    return sanitiseAudioSample(shifted);
}
float SingleWetSpectralRenderer::processBypassedSample(float inputSample) noexcept
{
    inputSample=sanitiseAudioSample(inputSample); if(frameSize_<=0||inputRing_.empty()) return inputSample;
    const std::int64_t currentSample=inputSampleCounter_; inputRing_[static_cast<std::size_t>(currentSample & inputRingMask_)]=inputSample;
    const float delayed=readInputSample(currentSample-frameSize_); if(!layer_.outputAccumulationRing.empty()) layer_.outputAccumulationRing[static_cast<std::size_t>(currentSample & outputRingMask_)]=0.0f;
    ++inputSampleCounter_; return sanitiseAudioSample(delayed);
}
