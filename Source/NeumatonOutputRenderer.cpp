#include "NeumatonOutputRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace neumaton::outputv3
{
namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr double groupDelayPrior = 1.35 * pi;
constexpr float magnitudeFloor = 1.0e-12f;

[[nodiscard]] bool trackIsUsable(const RidgeState& track) noexcept
{
    return track.lifeState == RidgeLifeState::active
        || track.lifeState == RidgeLifeState::coasting;
}
} // namespace

void NeumatonOutputRenderer::prepare(const OutputPrepareSpec& requestedSpec)
{
    spec_ = requestedSpec;
    spec_.sampleRate = std::isfinite(spec_.sampleRate)
        ? std::max(8000.0, spec_.sampleRate)
        : 48000.0;
    spec_.frameSize = std::max(64, nextPowerOfTwo(spec_.frameSize));
    spec_.hopSize = std::clamp(spec_.hopSize, 1, spec_.frameSize);
    spec_.positiveBinCount = std::clamp(spec_.positiveBinCount,
                                        2,
                                        spec_.frameSize / 2 + 1);
    spec_.outputRingSize = nextPowerOfTwo(std::max(spec_.frameSize * 2,
                                                   spec_.outputRingSize));
    spec_.maximumRidges = std::clamp(spec_.maximumRidges, 1, 512);
    outputRingMask_ = spec_.outputRingSize - 1;

    spectrum_.assign(static_cast<std::size_t>(spec_.frameSize), Complex {});
    previousSpectrum_.assign(static_cast<std::size_t>(spec_.frameSize), Complex {});
    timeFrame_.assign(static_cast<std::size_t>(spec_.frameSize), Complex {});
    outputAccumulationRing_.assign(static_cast<std::size_t>(spec_.outputRingSize), 0.0f);
    synthesisWindow_.assign(static_cast<std::size_t>(spec_.frameSize), 0.0f);
    fftBitReversal_.assign(static_cast<std::size_t>(spec_.frameSize), 0);
    fftTwiddles_.assign(static_cast<std::size_t>(spec_.frameSize / 2), Complex {});

    ownership_.assign(static_cast<std::size_t>(spec_.positiveBinCount),
                      BinOwnership::unclassified);
    ridgeSourceEnergy_.assign(static_cast<std::size_t>(spec_.maximumRidges), 0.0f);
    ridgePredictedEnergy_.assign(static_cast<std::size_t>(spec_.maximumRidges), 0.0f);
    ridgeNormalisationGain_.assign(static_cast<std::size_t>(spec_.maximumRidges), 1.0f);
    destinationOwnerToken_.assign(static_cast<std::size_t>(spec_.positiveBinCount), 0);
    destinationEnergy_.assign(static_cast<std::size_t>(spec_.positiveBinCount), 0.0f);
    destinationCollisionEnergy_.assign(static_cast<std::size_t>(spec_.positiveBinCount), 0.0f);

    for (int index = 0; index < spec_.frameSize; ++index)
    {
        const double periodicHann = 0.5 - 0.5 * std::cos(
            twoPi * static_cast<double>(index)
            / static_cast<double>(spec_.frameSize));
        synthesisWindow_[static_cast<std::size_t>(index)] = static_cast<float>(
            std::sqrt(std::max(0.0, periodicHann)));
    }

    double overlapNormalisation = 0.0;
    const int overlapCount = std::max(1, spec_.frameSize / spec_.hopSize);
    for (int overlap = 0; overlap < overlapCount; ++overlap)
    {
        const int index = (overlap * spec_.hopSize) % spec_.frameSize;
        const double value = synthesisWindow_[static_cast<std::size_t>(index)];
        overlapNormalisation += value * value;
    }
    synthesisGain_ = static_cast<float>(1.0
        / std::max(1.0e-9, overlapNormalisation));

    int fftBits = 0;
    while ((1 << fftBits) < spec_.frameSize)
        ++fftBits;
    for (int index = 0; index < spec_.frameSize; ++index)
    {
        unsigned value = static_cast<unsigned>(index);
        unsigned reversed = 0;
        for (int bit = 0; bit < fftBits; ++bit)
        {
            reversed = (reversed << 1u) | (value & 1u);
            value >>= 1u;
        }
        fftBitReversal_[static_cast<std::size_t>(index)] = static_cast<int>(reversed);
    }

    for (int index = 0; index < spec_.frameSize / 2; ++index)
    {
        const double angle = -twoPi * static_cast<double>(index)
                           / static_cast<double>(spec_.frameSize);
        fftTwiddles_[static_cast<std::size_t>(index)] = Complex(
            static_cast<float>(std::cos(angle)),
            static_cast<float>(std::sin(angle)));
    }

    reset();
}

void NeumatonOutputRenderer::reset() noexcept
{
    std::fill(spectrum_.begin(), spectrum_.end(), Complex {});
    std::fill(previousSpectrum_.begin(), previousSpectrum_.end(), Complex {});
    std::fill(timeFrame_.begin(), timeFrame_.end(), Complex {});
    std::fill(outputAccumulationRing_.begin(), outputAccumulationRing_.end(), 0.0f);
    std::fill(ownership_.begin(), ownership_.end(), BinOwnership::unclassified);
    std::fill(ridgeSourceEnergy_.begin(), ridgeSourceEnergy_.end(), 0.0f);
    std::fill(ridgePredictedEnergy_.begin(), ridgePredictedEnergy_.end(), 0.0f);
    std::fill(ridgeNormalisationGain_.begin(), ridgeNormalisationGain_.end(), 1.0f);
    std::fill(destinationOwnerToken_.begin(), destinationOwnerToken_.end(), 0);
    std::fill(destinationEnergy_.begin(), destinationEnergy_.end(), 0.0f);
    std::fill(destinationCollisionEnergy_.begin(), destinationCollisionEnergy_.end(), 0.0f);
    diagnostics_ = {};
    previousSpectrumValid_ = false;
}

OutputSpectrumView NeumatonOutputRenderer::inspectFrame(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory,
    const RidgeLedgerFrameView& ledger,
    float formantPreservation) noexcept
{
    if (spectrum_.empty() || ownership_.empty())
        return {};

    std::copy(spectrum_.begin(), spectrum_.end(), previousSpectrum_.begin());
    std::fill(spectrum_.begin(), spectrum_.end(), Complex {});
    std::fill(ownership_.begin(), ownership_.end(), BinOwnership::unclassified);
    std::fill(destinationOwnerToken_.begin(), destinationOwnerToken_.end(), 0);
    std::fill(destinationEnergy_.begin(), destinationEnergy_.end(), 0.0f);
    std::fill(destinationCollisionEnergy_.begin(), destinationCollisionEnergy_.end(), 0.0f);

    classifyOwnership(analysis, ledger);
    calculateRidgeNormalisation(analysis, trajectory, ledger, formantPreservation);
    buildSpectrum(analysis, trajectory, ledger, formantPreservation);
    completeConjugateSymmetry();
    updateDiagnostics(analysis);
    previousSpectrumValid_ = true;

    return {
        { spectrum_.data(), static_cast<int>(spectrum_.size()) },
        { ownership_.data(), static_cast<int>(ownership_.size()) }
    };
}

void NeumatonOutputRenderer::renderAndCommitFrame(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory,
    const RidgeLedgerFrameView& ledger,
    float formantPreservation,
    std::int64_t frameEndSample) noexcept
{
    static_cast<void>(inspectFrame(analysis,
                                   trajectory,
                                   ledger,
                                   formantPreservation));
    commitCurrentSpectrum(frameEndSample);
}

float NeumatonOutputRenderer::consumeSample(std::int64_t absoluteSample) noexcept
{
    if (outputAccumulationRing_.empty())
        return 0.0f;
    const int index = static_cast<int>(absoluteSample
        & static_cast<std::int64_t>(outputRingMask_));
    const std::size_t safeIndex = static_cast<std::size_t>(index);
    const float value = outputAccumulationRing_[safeIndex];
    outputAccumulationRing_[safeIndex] = 0.0f;
    if (!std::isfinite(value) || std::fpclassify(value) == FP_SUBNORMAL)
        return 0.0f;
    return value * synthesisGain_;
}

void NeumatonOutputRenderer::discardSample(std::int64_t absoluteSample) noexcept
{
    if (outputAccumulationRing_.empty())
        return;
    const int index = static_cast<int>(absoluteSample
        & static_cast<std::int64_t>(outputRingMask_));
    outputAccumulationRing_[static_cast<std::size_t>(index)] = 0.0f;
}

void NeumatonOutputRenderer::classifyOwnership(
    const AnalysisFrameView& analysis,
    const RidgeLedgerFrameView& ledger) noexcept
{
    const int usableBins = std::min({ analysis.positiveBinCount,
                                      analysis.magnitudes.size(),
                                      static_cast<int>(ownership_.size()) });
    const float binWidthHz = static_cast<float>(analysis.sampleRate
        / static_cast<double>(std::max(1, analysis.frameSize)));

    for (int bin = 0; bin < usableBins; ++bin)
    {
        const int trackIndex = bin < ledger.sourceBinTrackIndices.size()
            ? ledger.sourceBinTrackIndices[bin]
            : -1;
        if (trackIndex >= 0 && trackIndex < ledger.tracks.size()
            && trackIsUsable(ledger.tracks[trackIndex]))
        {
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::ridge;
            continue;
        }

        const float harmonic = bin < analysis.harmonicMask.size()
            ? clamp01(analysis.harmonicMask[bin])
            : 0.0f;
        const float frequencyHz = static_cast<float>(bin) * binWidthHz;
        const float eventEvidence = clamp01(analysis.onsetStrength)
            * (0.30f + 0.70f * (1.0f - harmonic));
        const float airEvidence = (1.0f - harmonic)
            * clamp01(analysis.breathiness)
            * clamp01((frequencyHz - 2200.0f) / 5200.0f);

        if (eventEvidence > 0.42f)
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::event;
        else if (airEvidence > 0.18f)
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::air;
        else
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::unclassified;
    }
}

void NeumatonOutputRenderer::calculateRidgeNormalisation(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory,
    const RidgeLedgerFrameView& ledger,
    float formantPreservation) noexcept
{
    std::fill(ridgeSourceEnergy_.begin(), ridgeSourceEnergy_.end(), 0.0f);
    std::fill(ridgePredictedEnergy_.begin(), ridgePredictedEnergy_.end(), 0.0f);
    std::fill(ridgeNormalisationGain_.begin(), ridgeNormalisationGain_.end(), 1.0f);

    const int usableBins = std::min({ analysis.positiveBinCount,
                                      analysis.magnitudes.size(),
                                      static_cast<int>(ownership_.size()) });

    for (int sourceBin = 1; sourceBin < usableBins; ++sourceBin)
    {
        if (ownership_[static_cast<std::size_t>(sourceBin)] != BinOwnership::ridge)
            continue;
        const int trackIndex = sourceBin < ledger.sourceBinTrackIndices.size()
            ? ledger.sourceBinTrackIndices[sourceBin]
            : -1;
        if (trackIndex < 0
            || trackIndex >= ledger.tracks.size()
            || trackIndex >= static_cast<int>(ridgeSourceEnergy_.size()))
            continue;

        const auto& track = ledger.tracks[trackIndex];
        if (!trackIsUsable(track))
            continue;

        const float magnitude = std::max(0.0f, analysis.magnitudes[sourceBin]);
        if (magnitude <= magnitudeFloor)
            continue;

        const double sourcePosition = sourceBin < analysis.trueSourceBins.size()
            && std::isfinite(analysis.trueSourceBins[sourceBin])
            ? analysis.trueSourceBins[sourceBin]
            : static_cast<double>(sourceBin);
        const double targetPosition = sourcePosition * ridgeRatio(track, trajectory);
        if (targetPosition < 0.0
            || targetPosition > static_cast<double>(usableBins - 1))
            continue;

        const float shapeGain = formantGain(analysis,
                                            sourcePosition,
                                            targetPosition,
                                            formantPreservation);
        const float sourceEnergy = magnitude * magnitude;
        const float targetMagnitude = magnitude * shapeGain;
        ridgeSourceEnergy_[static_cast<std::size_t>(trackIndex)] += sourceEnergy;
        ridgePredictedEnergy_[static_cast<std::size_t>(trackIndex)] +=
            targetMagnitude * targetMagnitude;
    }

    const int usableTracks = std::min(ledger.tracks.size(),
                                      static_cast<int>(ridgeNormalisationGain_.size()));
    for (int trackIndex = 0; trackIndex < usableTracks; ++trackIndex)
    {
        const float sourceEnergy = ridgeSourceEnergy_[static_cast<std::size_t>(trackIndex)];
        const float predictedEnergy = ridgePredictedEnergy_[static_cast<std::size_t>(trackIndex)];
        if (sourceEnergy > 1.0e-18f && predictedEnergy > 1.0e-18f)
        {
            ridgeNormalisationGain_[static_cast<std::size_t>(trackIndex)] = std::clamp(
                std::sqrt(sourceEnergy / predictedEnergy), 0.50f, 2.0f);
        }
    }
}

void NeumatonOutputRenderer::buildSpectrum(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory,
    const RidgeLedgerFrameView& ledger,
    float formantPreservation) noexcept
{
    const int usableBins = std::min({ analysis.positiveBinCount,
                                      analysis.magnitudes.size(),
                                      static_cast<int>(ownership_.size()) });

    const auto commit = [this, usableBins](int bin,
                                           Complex value,
                                           int ownerToken) noexcept
    {
        if (bin < 0 || bin >= usableBins)
            return;
        const std::size_t index = static_cast<std::size_t>(bin);
        const float energy = std::norm(value);
        if (destinationOwnerToken_[index] == 0)
            destinationOwnerToken_[index] = ownerToken;
        else if (destinationOwnerToken_[index] != ownerToken)
            destinationCollisionEnergy_[index] += energy;
        destinationEnergy_[index] += energy;
        spectrum_[index] += value;
    };

    for (int sourceBin = 0; sourceBin < usableBins; ++sourceBin)
    {
        const float magnitude = std::max(0.0f, analysis.magnitudes[sourceBin]);
        if (magnitude <= magnitudeFloor)
            continue;

        const auto owner = ownership_[static_cast<std::size_t>(sourceBin)];
        const int trackIndex = sourceBin < ledger.sourceBinTrackIndices.size()
            ? ledger.sourceBinTrackIndices[sourceBin]
            : -1;

        if (owner == BinOwnership::ridge
            && trackIndex >= 0
            && trackIndex < ledger.tracks.size()
            && trackIndex < static_cast<int>(ridgeNormalisationGain_.size()))
        {
            const auto& track = ledger.tracks[trackIndex];
            if (trackIsUsable(track))
            {
                const double ratio = ridgeRatio(track, trajectory);
                const double sourcePosition = sourceBin < analysis.trueSourceBins.size()
                    && std::isfinite(analysis.trueSourceBins[sourceBin])
                    ? analysis.trueSourceBins[sourceBin]
                    : static_cast<double>(sourceBin);
                const double targetPosition = sourcePosition * ratio;
                if (targetPosition >= 0.0
                    && targetPosition <= static_cast<double>(usableBins - 1))
                {
                    const double ridgeSourceBin = static_cast<double>(track.sourceFrequencyHz)
                        * static_cast<double>(analysis.frameSize)
                        / std::max(1.0, analysis.sampleRate);
                    const double ridgeTargetBin = static_cast<double>(track.targetFrequencyHz)
                        * static_cast<double>(analysis.frameSize)
                        / std::max(1.0, analysis.sampleRate);
                    const double measuredSlope = unwrapNear(
                        static_cast<double>(track.groupDelaySlopeRadiansPerBin),
                        groupDelayPrior);
                    const double targetSlope = 0.82 * measuredSlope
                        / std::max(0.25, ratio)
                        + 0.18 * groupDelayPrior;
                    const double phase = wrapPhase(track.targetPhase
                        + targetSlope * (targetPosition - ridgeTargetBin)
                        + 0.04 * measuredSlope * (sourcePosition - ridgeSourceBin));

                    const float shapeGain = formantGain(analysis,
                                                        sourcePosition,
                                                        targetPosition,
                                                        formantPreservation);
                    const float localGain = ridgeNormalisationGain_[
                        static_cast<std::size_t>(trackIndex)];
                    const float targetMagnitude = magnitude * shapeGain * localGain;
                    const Complex polar(targetMagnitude * static_cast<float>(std::cos(phase)),
                                        targetMagnitude * static_cast<float>(std::sin(phase)));

                    const int lowerBin = static_cast<int>(std::floor(targetPosition));
                    const int upperBin = lowerBin + 1;
                    const float fraction = static_cast<float>(targetPosition
                        - static_cast<double>(lowerBin));
                    const float lowerWeight = std::cos(0.5f * static_cast<float>(pi) * fraction);
                    const float upperWeight = std::sin(0.5f * static_cast<float>(pi) * fraction);
                    const int ownerToken = static_cast<int>(track.id == 0u ? 1u : track.id);
                    commit(lowerBin, polar * lowerWeight, ownerToken);
                    commit(upperBin, polar * upperWeight, ownerToken);
                    continue;
                }
            }
        }

        Complex identity {};
        if (sourceBin < analysis.analysedSpectrum.size())
            identity = analysis.analysedSpectrum[sourceBin];
        else
        {
            const float phase = sourceBin < analysis.analysisPhases.size()
                ? analysis.analysisPhases[sourceBin]
                : 0.0f;
            identity = Complex(magnitude * std::cos(phase),
                               magnitude * std::sin(phase));
        }

        int token = -3;
        if (owner == BinOwnership::event)
            token = -1;
        else if (owner == BinOwnership::air)
            token = -2;
        commit(sourceBin, identity, token);
    }
}

void NeumatonOutputRenderer::completeConjugateSymmetry() noexcept
{
    if (spectrum_.empty())
        return;

    spectrum_[0] = Complex(spectrum_[0].real(), 0.0f);
    const int nyquistBin = spec_.frameSize / 2;
    spectrum_[static_cast<std::size_t>(nyquistBin)] = Complex(
        spectrum_[static_cast<std::size_t>(nyquistBin)].real(), 0.0f);
    for (int bin = 1; bin < nyquistBin; ++bin)
    {
        spectrum_[static_cast<std::size_t>(spec_.frameSize - bin)] =
            std::conj(spectrum_[static_cast<std::size_t>(bin)]);
    }
}

void NeumatonOutputRenderer::commitCurrentSpectrum(
    std::int64_t frameEndSample) noexcept
{
    std::copy(spectrum_.begin(), spectrum_.end(), timeFrame_.begin());
    fft(timeFrame_, true);

    const std::int64_t outputStartSample = frameEndSample + 1;
    for (int index = 0; index < spec_.frameSize; ++index)
    {
        const float output = timeFrame_[static_cast<std::size_t>(index)].real()
                           * synthesisWindow_[static_cast<std::size_t>(index)];
        if (!std::isfinite(output))
            continue;
        const int ringIndex = static_cast<int>((outputStartSample + index)
            & static_cast<std::int64_t>(outputRingMask_));
        outputAccumulationRing_[static_cast<std::size_t>(ringIndex)] += output;
    }
}

void NeumatonOutputRenderer::updateDiagnostics(
    const AnalysisFrameView& analysis) noexcept
{
    const int usableBins = std::min({ analysis.positiveBinCount,
                                      analysis.magnitudes.size(),
                                      static_cast<int>(ownership_.size()) });
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    double assignedEnergy = 0.0;
    double ridgeEnergy = 0.0;
    double eventEnergy = 0.0;
    double airEnergy = 0.0;
    double unclassifiedEnergy = 0.0;
    double collisionEnergy = 0.0;
    double phaseCoherenceSum = 0.0;
    double phaseCoherenceWeight = 0.0;
    double temporalDistance = 0.0;
    double temporalReference = 0.0;
    double olaCoherenceSum = 0.0;
    double olaWeight = 0.0;

    for (int bin = 0; bin < usableBins; ++bin)
    {
        const double sourceMagnitude = std::max(0.0f, analysis.magnitudes[bin]);
        const double sourceEnergy = sourceMagnitude * sourceMagnitude;
        inputEnergy += sourceEnergy;
        assignedEnergy += sourceEnergy;

        switch (ownership_[static_cast<std::size_t>(bin)])
        {
            case BinOwnership::ridge: ridgeEnergy += sourceEnergy; break;
            case BinOwnership::event: eventEnergy += sourceEnergy; break;
            case BinOwnership::air: airEnergy += sourceEnergy; break;
            case BinOwnership::unclassified: unclassifiedEnergy += sourceEnergy; break;
        }

        outputEnergy += std::norm(spectrum_[static_cast<std::size_t>(bin)]);
        collisionEnergy += destinationCollisionEnergy_[static_cast<std::size_t>(bin)];

        if (bin > 0 && bin + 1 < usableBins)
        {
            const Complex left = spectrum_[static_cast<std::size_t>(bin - 1)];
            const Complex centre = spectrum_[static_cast<std::size_t>(bin)];
            const Complex right = spectrum_[static_cast<std::size_t>(bin + 1)];
            const double weight = std::abs(centre);
            if (weight > 1.0e-9)
            {
                const double curvature = wrapPhase(std::arg(right)
                    - 2.0 * std::arg(centre) + std::arg(left));
                phaseCoherenceSum += weight * (0.5 + 0.5 * std::cos(curvature));
                phaseCoherenceWeight += weight;
            }
        }

        if (previousSpectrumValid_)
        {
            const Complex current = spectrum_[static_cast<std::size_t>(bin)];
            const Complex previous = previousSpectrum_[static_cast<std::size_t>(bin)];
            temporalDistance += std::norm(current - previous);
            temporalReference += std::norm(current) + std::norm(previous);

            const double currentMagnitude = std::abs(current);
            const double previousMagnitude = std::abs(previous);
            const double weight = currentMagnitude * previousMagnitude;
            if (weight > 1.0e-12)
            {
                const double expectedAdvance = twoPi
                    * static_cast<double>(spec_.hopSize)
                    * static_cast<double>(bin)
                    / static_cast<double>(spec_.frameSize);
                const double phaseError = wrapPhase(std::arg(current)
                    - std::arg(previous) - expectedAdvance);
                olaCoherenceSum += weight * (0.5 + 0.5 * std::cos(phaseError));
                olaWeight += weight;
            }
        }
    }

    const double inverseInput = inputEnergy > 1.0e-18 ? 1.0 / inputEnergy : 0.0;
    diagnostics_.ridgeEnergyRatio = static_cast<float>(ridgeEnergy * inverseInput);
    diagnostics_.eventEnergyRatio = static_cast<float>(eventEnergy * inverseInput);
    diagnostics_.airEnergyRatio = static_cast<float>(airEnergy * inverseInput);
    diagnostics_.unclassifiedEnergyRatio = static_cast<float>(unclassifiedEnergy * inverseInput);
    diagnostics_.assignedEnergyRatio = static_cast<float>(assignedEnergy * inverseInput);
    diagnostics_.destinationCollisionEnergyRatio = outputEnergy > 1.0e-18
        ? static_cast<float>(collisionEnergy / outputEnergy)
        : 0.0f;
    diagnostics_.phaseFieldCoherence = phaseCoherenceWeight > 1.0e-12
        ? static_cast<float>(phaseCoherenceSum / phaseCoherenceWeight)
        : 0.0f;
    diagnostics_.temporalSpectrumDistance = temporalReference > 1.0e-18
        ? static_cast<float>(std::sqrt(temporalDistance / temporalReference))
        : 0.0f;
    diagnostics_.overlapAddCoherence = olaWeight > 1.0e-18
        ? static_cast<float>(olaCoherenceSum / olaWeight)
        : 0.0f;
    diagnostics_.requestedEnergyGainDb = inputEnergy > 1.0e-18
        && outputEnergy > 1.0e-18
        ? static_cast<float>(10.0 * std::log10(inputEnergy / outputEnergy))
        : 0.0f;
    diagnostics_.frameValid = inputEnergy > 1.0e-18 && usableBins > 2;
}

void NeumatonOutputRenderer::fft(std::vector<Complex>& data,
                                 bool inverse) noexcept
{
    const int size = std::min(spec_.frameSize, static_cast<int>(data.size()));
    if (size <= 1)
        return;

    for (int index = 0; index < size; ++index)
    {
        const int reversed = fftBitReversal_[static_cast<std::size_t>(index)];
        if (reversed > index && reversed < size)
            std::swap(data[static_cast<std::size_t>(index)],
                      data[static_cast<std::size_t>(reversed)]);
    }

    for (int length = 2; length <= size; length <<= 1)
    {
        const int halfLength = length / 2;
        const int twiddleStep = size / length;
        for (int start = 0; start < size; start += length)
        {
            for (int offset = 0; offset < halfLength; ++offset)
            {
                Complex twiddle = fftTwiddles_[static_cast<std::size_t>(
                    offset * twiddleStep)];
                if (inverse)
                    twiddle = std::conj(twiddle);
                const Complex even = data[static_cast<std::size_t>(start + offset)];
                const Complex odd = data[static_cast<std::size_t>(
                    start + offset + halfLength)] * twiddle;
                data[static_cast<std::size_t>(start + offset)] = even + odd;
                data[static_cast<std::size_t>(start + offset + halfLength)] = even - odd;
            }
        }
    }

    if (inverse)
    {
        const float inverseSize = 1.0f / static_cast<float>(size);
        for (auto& value : data)
            value *= inverseSize;
    }
}

double NeumatonOutputRenderer::ridgeRatio(
    const RidgeState& track,
    const CorrectionTrajectoryFrame& trajectory) const noexcept
{
    if (track.sourceFrequencyHz > 0.0f
        && track.targetFrequencyHz > 0.0f
        && std::isfinite(track.sourceFrequencyHz)
        && std::isfinite(track.targetFrequencyHz))
    {
        return std::clamp(static_cast<double>(track.targetFrequencyHz)
            / static_cast<double>(track.sourceFrequencyHz), 0.25, 4.0);
    }
    if (trajectory.targetValid && std::isfinite(trajectory.correctionCents))
        return std::clamp(std::exp2(trajectory.correctionCents / 1200.0), 0.25, 4.0);
    return 1.0;
}

float NeumatonOutputRenderer::formantGain(
    const AnalysisFrameView& analysis,
    double sourceBin,
    double targetBin,
    float amount) const noexcept
{
    const float safeAmount = clamp01(amount);
    if (safeAmount <= 1.0e-4f || analysis.spectralEnvelope.empty())
        return 1.0f;
    const float sourceEnvelope = interpolate(analysis.spectralEnvelope,
                                             sourceBin,
                                             1.0f);
    const float targetEnvelope = interpolate(analysis.spectralEnvelope,
                                             targetBin,
                                             sourceEnvelope);
    const float ratio = std::clamp(sourceEnvelope / std::max(1.0e-8f, targetEnvelope),
                                   0.25f,
                                   4.0f);
    return std::clamp(std::pow(ratio, safeAmount), 0.50f, 2.0f);
}

float NeumatonOutputRenderer::interpolate(
    const ConstArrayView<float>& values,
    double position,
    float fallback) noexcept
{
    if (values.empty() || !std::isfinite(position))
        return fallback;
    position = std::clamp(position, 0.0,
                          static_cast<double>(values.size() - 1));
    const int lower = static_cast<int>(std::floor(position));
    const int upper = std::min(values.size() - 1, lower + 1);
    const float fraction = static_cast<float>(position - static_cast<double>(lower));
    const float lowerValue = std::isfinite(values[lower]) ? values[lower] : fallback;
    const float upperValue = std::isfinite(values[upper]) ? values[upper] : lowerValue;
    return lowerValue + fraction * (upperValue - lowerValue);
}

double NeumatonOutputRenderer::unwrapNear(double value,
                                          double reference) noexcept
{
    if (!std::isfinite(value))
        return reference;
    return value + twoPi * std::nearbyint((reference - value) / twoPi);
}

double NeumatonOutputRenderer::wrapPhase(double phase) noexcept
{
    if (!std::isfinite(phase))
        return 0.0;
    phase -= twoPi * std::nearbyint(phase / twoPi);
    return phase;
}

float NeumatonOutputRenderer::clamp01(float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}

int NeumatonOutputRenderer::nextPowerOfTwo(int value) noexcept
{
    int result = 1;
    while (result < std::max(1, value))
        result <<= 1;
    return result;
}

} // namespace neumaton::outputv3
