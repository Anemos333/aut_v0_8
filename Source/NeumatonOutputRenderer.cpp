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
constexpr float magnitudeFloor = 1.0e-12f;

[[nodiscard]] bool trackIsUsable(const RidgeState& track) noexcept
{
    return track.lifeState == RidgeLifeState::active
        || track.lifeState == RidgeLifeState::coasting;
}

[[nodiscard]] float smoothStep(float edge0, float edge1, float value) noexcept
{
    if (edge1 <= edge0)
        return value >= edge1 ? 1.0f : 0.0f;
    const float x = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
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

    const auto frameCount = static_cast<std::size_t>(spec_.frameSize);
    const auto binCount = static_cast<std::size_t>(spec_.positiveBinCount);
    const auto ridgeCount = static_cast<std::size_t>(spec_.maximumRidges);

    spectrum_.assign(frameCount, Complex {});
    previousSpectrum_.assign(frameCount, Complex {});
    timeFrame_.assign(frameCount, Complex {});
    outputAccumulationRing_.assign(static_cast<std::size_t>(spec_.outputRingSize), 0.0f);
    synthesisWindow_.assign(frameCount, 0.0f);
    fftBitReversal_.assign(frameCount, 0);
    fftTwiddles_.assign(static_cast<std::size_t>(spec_.frameSize / 2), Complex {});

    ownership_.assign(binCount, BinOwnership::unclassified);
    ridgeSourceEnergy_.assign(ridgeCount, 0.0f);
    ridgePredictedEnergy_.assign(ridgeCount, 0.0f);
    ridgeNormalisationGain_.assign(ridgeCount, 1.0f);

    destinationRidgeComplex_.assign(binCount, Complex {});
    destinationRidgeEnergy_.assign(binCount, 0.0f);
    destinationRidgeAnchor_.assign(binCount, Complex {});
    destinationRidgeAnchorEnergy_.assign(binCount, 0.0f);
    destinationRidgeReliability_.assign(binCount, 0.0f);
    destinationRidgeOwner_.assign(binCount, 0);
    destinationRidgeContributionCount_.assign(binCount, 0);

    destinationOwnerToken_.assign(binCount, 0);
    destinationEnergy_.assign(binCount, 0.0f);
    destinationCollisionEnergy_.assign(binCount, 0.0f);

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
    std::fill(destinationRidgeComplex_.begin(), destinationRidgeComplex_.end(), Complex {});
    std::fill(destinationRidgeEnergy_.begin(), destinationRidgeEnergy_.end(), 0.0f);
    std::fill(destinationRidgeAnchor_.begin(), destinationRidgeAnchor_.end(), Complex {});
    std::fill(destinationRidgeAnchorEnergy_.begin(), destinationRidgeAnchorEnergy_.end(), 0.0f);
    std::fill(destinationRidgeReliability_.begin(), destinationRidgeReliability_.end(), 0.0f);
    std::fill(destinationRidgeOwner_.begin(), destinationRidgeOwner_.end(), 0);
    std::fill(destinationRidgeContributionCount_.begin(), destinationRidgeContributionCount_.end(), 0);
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

int NeumatonOutputRenderer::trackForSourceBin(
    const AnalysisFrameView& analysis,
    const RidgeLedgerFrameView& ledger,
    int sourceBin) const noexcept
{
    if (sourceBin < 0 || sourceBin >= analysis.positiveBinCount)
        return -1;

    const auto usableTrack = [&ledger](int trackIndex) noexcept -> bool
    {
        return trackIndex >= 0
            && trackIndex < ledger.tracks.size()
            && trackIsUsable(ledger.tracks[trackIndex]);
    };

    const int direct = sourceBin < ledger.sourceBinTrackIndices.size()
        ? ledger.sourceBinTrackIndices[sourceBin]
        : -1;
    if (usableTrack(direct))
        return direct;

    const float harmonic = sourceBin < analysis.harmonicMask.size()
        ? clamp01(analysis.harmonicMask[sourceBin])
        : 0.0f;
    if (harmonic < 0.28f)
        return -1;

    const int ridgeRadius = spec_.frameSize <= 128 ? 1
                          : spec_.frameSize <= 256 ? 2
                          : 3;
    if (sourceBin < analysis.nearestPeak.size())
    {
        const int peak = analysis.nearestPeak[sourceBin];
        if (peak >= 0
            && std::abs(sourceBin - peak) <= ridgeRadius
            && peak < ledger.sourceBinTrackIndices.size())
        {
            const int peakTrack = ledger.sourceBinTrackIndices[peak];
            if (usableTrack(peakTrack))
                return peakTrack;
        }
    }

    if (harmonic >= 0.62f)
    {
        for (int trackIndex = 0; trackIndex < ledger.tracks.size(); ++trackIndex)
        {
            const auto& track = ledger.tracks[trackIndex];
            if (trackIsUsable(track)
                && track.lastPeakBin >= 0
                && std::abs(sourceBin - track.lastPeakBin) <= ridgeRadius)
            {
                return trackIndex;
            }
        }
    }

    return -1;
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
        if (trackForSourceBin(analysis, ledger, bin) >= 0)
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
        const int trackIndex = trackForSourceBin(analysis, ledger, sourceBin);
        if (trackIndex < 0
            || trackIndex >= ledger.tracks.size()
            || trackIndex >= static_cast<int>(ridgeSourceEnergy_.size()))
        {
            continue;
        }

        const auto& track = ledger.tracks[trackIndex];
        const float magnitude = std::max(0.0f, analysis.magnitudes[sourceBin]);
        if (!trackIsUsable(track) || magnitude <= magnitudeFloor)
            continue;

        const double sourcePosition = sourceBin < analysis.trueSourceBins.size()
            && std::isfinite(analysis.trueSourceBins[sourceBin])
            ? analysis.trueSourceBins[sourceBin]
            : static_cast<double>(sourceBin);
        const double targetPosition = sourcePosition * ridgeRatio(track, trajectory);
        if (targetPosition < 0.0
            || targetPosition > static_cast<double>(usableBins - 1))
        {
            continue;
        }

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
                std::sqrt(sourceEnergy / predictedEnergy), 0.55f, 1.82f);
        }
    }
}

void NeumatonOutputRenderer::depositRidge(int destinationBin,
                                          Complex value,
                                          int ownerToken,
                                          float reliability) noexcept
{
    if (destinationBin < 0
        || destinationBin >= static_cast<int>(destinationRidgeComplex_.size()))
    {
        return;
    }

    const auto index = static_cast<std::size_t>(destinationBin);
    const float energy = std::norm(value);
    if (!(energy > 0.0f) || !std::isfinite(energy))
        return;

    if (destinationOwnerToken_[index] == 0)
        destinationOwnerToken_[index] = ownerToken;
    else if (destinationOwnerToken_[index] != ownerToken)
        destinationCollisionEnergy_[index] += energy;

    if (destinationRidgeOwner_[index] == 0)
        destinationRidgeOwner_[index] = ownerToken;
    else if (destinationRidgeOwner_[index] != ownerToken)
        destinationCollisionEnergy_[index] += energy;

    destinationEnergy_[index] += energy;
    destinationRidgeComplex_[index] += value;
    destinationRidgeEnergy_[index] += energy;
    destinationRidgeReliability_[index] = std::max(
        destinationRidgeReliability_[index], clamp01(reliability));
    ++destinationRidgeContributionCount_[index];

    if (energy > destinationRidgeAnchorEnergy_[index])
    {
        destinationRidgeAnchorEnergy_[index] = energy;
        destinationRidgeAnchor_[index] = value;
    }
}

void NeumatonOutputRenderer::depositResidual(int destinationBin,
                                             Complex value,
                                             int ownerToken) noexcept
{
    if (destinationBin < 0
        || destinationBin >= static_cast<int>(ownership_.size()))
    {
        return;
    }

    const auto index = static_cast<std::size_t>(destinationBin);
    const float energy = std::norm(value);
    if (!(energy > 0.0f) || !std::isfinite(energy))
        return;

    if (destinationOwnerToken_[index] == 0)
        destinationOwnerToken_[index] = ownerToken;
    else if (destinationOwnerToken_[index] != ownerToken)
        destinationCollisionEnergy_[index] += energy;

    destinationEnergy_[index] += energy;
    spectrum_[index] += value;
}

void NeumatonOutputRenderer::buildSpectrum(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory,
    const RidgeLedgerFrameView& ledger,
    float formantPreservation) noexcept
{
    std::fill(destinationRidgeComplex_.begin(), destinationRidgeComplex_.end(), Complex {});
    std::fill(destinationRidgeEnergy_.begin(), destinationRidgeEnergy_.end(), 0.0f);
    std::fill(destinationRidgeAnchor_.begin(), destinationRidgeAnchor_.end(), Complex {});
    std::fill(destinationRidgeAnchorEnergy_.begin(), destinationRidgeAnchorEnergy_.end(), 0.0f);
    std::fill(destinationRidgeReliability_.begin(), destinationRidgeReliability_.end(), 0.0f);
    std::fill(destinationRidgeOwner_.begin(), destinationRidgeOwner_.end(), 0);
    std::fill(destinationRidgeContributionCount_.begin(), destinationRidgeContributionCount_.end(), 0);

    const int usableBins = std::min({ analysis.positiveBinCount,
                                      analysis.magnitudes.size(),
                                      static_cast<int>(ownership_.size()) });

    for (int sourceBin = 0; sourceBin < usableBins; ++sourceBin)
    {
        const float magnitude = std::max(0.0f, analysis.magnitudes[sourceBin]);
        if (magnitude <= magnitudeFloor)
            continue;

        const auto owner = ownership_[static_cast<std::size_t>(sourceBin)];
        const int trackIndex = owner == BinOwnership::ridge
            ? trackForSourceBin(analysis, ledger, sourceBin)
            : -1;

        if (trackIndex >= 0
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
                    const double centrePhase = ridgePhaseAt(analysis,
                                                            track,
                                                            sourcePosition,
                                                            targetPosition,
                                                            ratio);
                    const double slope = regularisedSlope(analysis, track, ratio);
                    const float shapeGain = formantGain(analysis,
                                                        sourcePosition,
                                                        targetPosition,
                                                        formantPreservation);
                    const float localGain = ridgeNormalisationGain_[
                        static_cast<std::size_t>(trackIndex)];
                    const float targetMagnitude = magnitude * shapeGain * localGain;

                    const int lowerBin = static_cast<int>(std::floor(targetPosition));
                    const int upperBin = lowerBin + 1;
                    const float fraction = static_cast<float>(targetPosition
                        - static_cast<double>(lowerBin));
                    const float lowerWeight = std::cos(0.5f * static_cast<float>(pi) * fraction);
                    const float upperWeight = std::sin(0.5f * static_cast<float>(pi) * fraction);
                    const int ownerToken = static_cast<int>(track.id == 0u ? 1u : track.id);
                    const float reliability = clamp01(track.reliability
                        * (0.55f + 0.45f * clamp01(analysis.spectralReliability)));

                    const auto polarAt = [centrePhase, slope, targetPosition, targetMagnitude](
                        int destinationBin,
                        float weight) noexcept -> Complex
                    {
                        if (!(weight > 0.0f))
                            return {};
                        const double phase = centrePhase
                            + slope * (static_cast<double>(destinationBin) - targetPosition);
                        const float amplitude = targetMagnitude * weight;
                        return Complex(amplitude * static_cast<float>(std::cos(phase)),
                                       amplitude * static_cast<float>(std::sin(phase)));
                    };

                    depositRidge(lowerBin,
                                 polarAt(lowerBin, lowerWeight),
                                 ownerToken,
                                 reliability);
                    depositRidge(upperBin,
                                 polarAt(upperBin, upperWeight),
                                 ownerToken,
                                 reliability);
                    continue;
                }

                // A tonal bin that moves outside the representable band is not
                // reintroduced uncorrected. Its ownership remains tonal and the
                // out-of-band energy is intentionally discarded.
                continue;
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
        depositResidual(sourceBin, identity, token);
    }

    finaliseRidgeDestinations(analysis);
}

void NeumatonOutputRenderer::finaliseRidgeDestinations(
    const AnalysisFrameView& analysis) noexcept
{
    const int usableBins = std::min(analysis.positiveBinCount,
                                    static_cast<int>(destinationRidgeEnergy_.size()));

    for (int bin = 0; bin < usableBins; ++bin)
    {
        const auto index = static_cast<std::size_t>(bin);
        const float ridgeEnergy = destinationRidgeEnergy_[index];
        if (!(ridgeEnergy > magnitudeFloor * magnitudeFloor))
            continue;

        const Complex coherentSum = destinationRidgeComplex_[index];
        const float coherentMagnitude = std::abs(coherentSum);
        const float conservedMagnitude = std::sqrt(std::max(0.0f, ridgeEnergy));
        const float collisionAmount = clamp01(destinationCollisionEnergy_[index]
            / std::max(1.0e-18f, destinationEnergy_[index]));

        const Complex anchor = destinationRidgeAnchor_[index];
        const double anchorPhase = std::abs(anchor) > magnitudeFloor
            ? std::arg(anchor)
            : 0.0;
        const double coherentPhase = coherentMagnitude > magnitudeFloor
            ? std::arg(coherentSum)
            : anchorPhase;

        // Within one ridge the complex packet is preserved. When independent
        // ridges share a destination cell, magnitude comes increasingly from
        // summed energy instead of cancellation-prone complex interference.
        const float collisionBlend = smoothStep(0.02f, 0.42f, collisionAmount);
        const float ridgeMagnitude = coherentMagnitude
            + collisionBlend * (conservedMagnitude - coherentMagnitude);
        const double candidatePhase = coherentPhase
            + static_cast<double>(0.72f * collisionBlend)
                * wrapPhase(anchorPhase - coherentPhase);
        const double ridgePhase = temporallyRegularisePhase(
            analysis,
            bin,
            candidatePhase,
            collisionBlend,
            destinationRidgeReliability_[index]);
        const Complex ridgeValue(
            ridgeMagnitude * static_cast<float>(std::cos(ridgePhase)),
            ridgeMagnitude * static_cast<float>(std::sin(ridgePhase)));

        const Complex residualValue = spectrum_[index];
        const float residualEnergy = std::norm(residualValue);
        if (residualEnergy <= magnitudeFloor * magnitudeFloor)
        {
            spectrum_[index] = ridgeValue;
            continue;
        }

        // Ridge and residual are still one output object. At a destination
        // collision we conserve their joint energy and choose one phase field;
        // we never expose them as parallel dry/wet voices.
        const Complex vectorSum = residualValue + ridgeValue;
        const float vectorMagnitude = std::abs(vectorSum);
        const float jointMagnitude = std::sqrt(residualEnergy + std::norm(ridgeValue));
        const float tonalAuthority = clamp01(analysis.voicing
            * analysis.consensus
            * destinationRidgeReliability_[index]);
        const float jointBlend = clamp01(collisionBlend * (0.35f + 0.55f * tonalAuthority));
        const double vectorPhase = vectorMagnitude > magnitudeFloor
            ? std::arg(vectorSum)
            : ridgePhase;
        const double jointPhase = vectorPhase
            + static_cast<double>(jointBlend) * wrapPhase(ridgePhase - vectorPhase);
        const float finalMagnitude = vectorMagnitude
            + jointBlend * (jointMagnitude - vectorMagnitude);
        spectrum_[index] = Complex(
            finalMagnitude * static_cast<float>(std::cos(jointPhase)),
            finalMagnitude * static_cast<float>(std::sin(jointPhase)));
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

double NeumatonOutputRenderer::regularisedSlope(
    const AnalysisFrameView& analysis,
    const RidgeState& track,
    double ratio) const noexcept
{
    const double measured = wrapPhase(
        static_cast<double>(track.groupDelaySlopeRadiansPerBin));
    const float reliability = clamp01(track.reliability
        * (0.50f + 0.50f * clamp01(analysis.spectralReliability))
        * (0.60f + 0.40f * clamp01(analysis.consensus)));
    const float shortAmount = shortFrameAmount();

    // Short windows contain less trustworthy inter-bin phase geometry. We do
    // not weaken correction; we regularise only the local phase slope.
    const double slopeAuthority = static_cast<double>(
        0.42f + 0.58f * reliability * (1.0f - 0.38f * shortAmount));
    return measured * slopeAuthority / std::max(0.25, ratio);
}

double NeumatonOutputRenderer::ridgePhaseAt(
    const AnalysisFrameView& analysis,
    const RidgeState& track,
    double sourcePosition,
    double targetPosition,
    double ratio) const noexcept
{
    const double ridgeSourceBin = static_cast<double>(track.sourceFrequencyHz)
        * static_cast<double>(analysis.frameSize)
        / std::max(1.0, analysis.sampleRate);
    const double ridgeTargetBin = static_cast<double>(track.targetFrequencyHz)
        * static_cast<double>(analysis.frameSize)
        / std::max(1.0, analysis.sampleRate);
    const double slope = regularisedSlope(analysis, track, ratio);
    const double geometricRelative = slope * (targetPosition - ridgeTargetBin);

    double measuredRelative = geometricRelative;
    const int sourceBin = std::clamp(
        static_cast<int>(std::lround(sourcePosition)),
        0,
        std::max(0, analysis.analysisPhases.size() - 1));
    if (!analysis.analysisPhases.empty())
    {
        const double measuredPhase = analysis.analysisPhases[sourceBin];
        const double sourceSlopeReference = wrapPhase(
            static_cast<double>(track.groupDelaySlopeRadiansPerBin))
            * (sourcePosition - ridgeSourceBin);
        measuredRelative = unwrapNear(
            measuredPhase - static_cast<double>(track.sourcePhase),
            sourceSlopeReference);
    }

    const float detailTrust = clamp01(track.reliability
        * analysis.spectralReliability
        * (0.55f + 0.45f * analysis.consensus));
    const float detailWeight = (0.18f + 0.58f * (1.0f - shortFrameAmount()))
        * detailTrust;
    const double relative = geometricRelative
        + static_cast<double>(detailWeight)
            * wrapPhase(measuredRelative - geometricRelative);
    return wrapPhase(track.targetPhase + relative);
}

double NeumatonOutputRenderer::temporallyRegularisePhase(
    const AnalysisFrameView& analysis,
    int destinationBin,
    double candidatePhase,
    float collisionAmount,
    float reliability) const noexcept
{
    if (!previousSpectrumValid_
        || analysis.phaseReset
        || destinationBin < 0
        || destinationBin >= static_cast<int>(previousSpectrum_.size()))
    {
        return wrapPhase(candidatePhase);
    }

    const Complex previous = previousSpectrum_[static_cast<std::size_t>(destinationBin)];
    if (std::abs(previous) <= magnitudeFloor)
        return wrapPhase(candidatePhase);

    const double expectedAdvance = twoPi
        * static_cast<double>(spec_.hopSize)
        * static_cast<double>(destinationBin)
        / static_cast<double>(spec_.frameSize);
    const double predicted = std::arg(previous) + expectedAdvance;
    const float memoryWeight = std::clamp(
        shortFrameAmount()
            * (0.14f + 0.40f * clamp01(collisionAmount))
            * clamp01(reliability)
            * (0.55f + 0.45f * clamp01(analysis.consensus)),
        0.0f,
        0.58f);
    return wrapPhase(candidatePhase
        + static_cast<double>(memoryWeight) * wrapPhase(predicted - candidatePhase));
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
    return std::clamp(std::pow(ratio, safeAmount), 0.55f, 1.82f);
}

float NeumatonOutputRenderer::shortFrameAmount() const noexcept
{
    return clamp01((512.0f - static_cast<float>(spec_.frameSize)) / 384.0f);
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
