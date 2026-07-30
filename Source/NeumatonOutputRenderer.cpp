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
    outputRingMask_ = spec_.outputRingSize - 1;

    const auto frameCount = static_cast<std::size_t>(spec_.frameSize);
    const auto binCount = static_cast<std::size_t>(spec_.positiveBinCount);

    spectrum_.assign(frameCount, Complex {});
    previousSpectrum_.assign(frameCount, Complex {});
    timeFrame_.assign(frameCount, Complex {});
    outputAccumulationRing_.assign(static_cast<std::size_t>(spec_.outputRingSize), 0.0f);
    synthesisWindow_.assign(frameCount, 0.0f);
    fftBitReversal_.assign(frameCount, 0);
    fftTwiddles_.assign(static_cast<std::size_t>(spec_.frameSize / 2), Complex {});

    ownership_.assign(binCount, BinOwnership::unclassified);
    freeSynthesisPhase_.assign(binCount, 0.0);
    freeSynthesisPhaseValid_.assign(binCount, std::uint8_t { 0 });
    destinationDepositedEnergy_.assign(binCount, 0.0f);

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
    std::fill(freeSynthesisPhase_.begin(), freeSynthesisPhase_.end(), 0.0);
    std::fill(freeSynthesisPhaseValid_.begin(), freeSynthesisPhaseValid_.end(), std::uint8_t { 0 });
    std::fill(destinationDepositedEnergy_.begin(), destinationDepositedEnergy_.end(), 0.0f);
    diagnostics_ = {};
    lastCorrectionRatio_ = 1.0;
    correctionRatioValid_ = false;
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
    std::fill(destinationDepositedEnergy_.begin(),
              destinationDepositedEnergy_.end(),
              0.0f);

    classifyForDiagnostics(analysis);
    buildFullSpectrum(analysis, trajectory, ledger, formantPreservation);
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
    const auto safeIndex = static_cast<std::size_t>(index);
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

void NeumatonOutputRenderer::classifyForDiagnostics(
    const AnalysisFrameView& analysis) noexcept
{
    const int usableBins = std::min({ analysis.positiveBinCount,
                                      analysis.magnitudes.size(),
                                      static_cast<int>(ownership_.size()) });
    const float binWidthHz = static_cast<float>(
        analysis.sampleRate / static_cast<double>(std::max(1, analysis.frameSize)));

    for (int bin = 0; bin < usableBins; ++bin)
    {
        const float harmonic = bin < analysis.harmonicMask.size()
            ? clamp01(analysis.harmonicMask[bin])
            : 0.0f;
        const float frequencyHz = static_cast<float>(bin) * binWidthHz;
        const float eventEvidence = clamp01(analysis.onsetStrength)
            * (0.25f + 0.75f * (1.0f - harmonic));
        const float airEvidence = (1.0f - harmonic)
            * clamp01(analysis.breathiness)
            * clamp01((frequencyHz - 1800.0f) / 6200.0f);

        if (harmonic >= 0.45f)
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::ridge;
        else if (eventEvidence >= 0.38f)
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::event;
        else if (airEvidence >= 0.16f)
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::air;
        else
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::unclassified;
    }
}

void NeumatonOutputRenderer::buildFullSpectrum(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory,
    const RidgeLedgerFrameView& ledger,
    float formantPreservation) noexcept
{
    const int usableBins = std::min({ analysis.positiveBinCount,
                                      analysis.magnitudes.size(),
                                      static_cast<int>(ownership_.size()) });
    if (usableBins <= 1)
        return;

    const double ratio = correctionRatio(analysis, trajectory);
    const bool resetPhase = analysis.phaseReset
        || trajectory.forceReset
        || analysis.onsetStrength >= 0.58f;

    for (int sourceBin = 0; sourceBin < usableBins; ++sourceBin)
    {
        const float magnitude = std::max(0.0f, analysis.magnitudes[sourceBin]);
        if (!(magnitude > magnitudeFloor))
            continue;

        const double sourceBinPosition = sourcePosition(analysis, sourceBin);
        const double targetPosition = sourceBinPosition * ratio;
        if (targetPosition < 0.0
            || targetPosition > static_cast<double>(usableBins - 1))
        {
            continue;
        }

        const int peakBin = nearestPeakForBin(analysis, sourceBin);
        const double phase = synthesisPhase(analysis,
                                            ledger,
                                            sourceBin,
                                            peakBin,
                                            targetPosition,
                                            resetPhase);
        const double slope = localPhaseSlope(analysis,
                                             ledger,
                                             sourceBin,
                                             peakBin,
                                             ratio);
        const float harmonicEvidence = sourceBin < analysis.harmonicMask.size()
            ? clamp01(analysis.harmonicMask[sourceBin])
            : 0.0f;
        const float gain = formantGain(analysis,
                                       sourceBinPosition,
                                       targetPosition,
                                       formantPreservation,
                                       harmonicEvidence);
        const float targetMagnitude = magnitude * gain;
        depositMappedBin(sourceBin,
                         targetPosition,
                         targetMagnitude,
                         phase,
                         slope);
    }


}

void NeumatonOutputRenderer::depositMappedBin(int /*sourceBin*/,
                                               double targetPosition,
                                               float magnitude,
                                               double phase,
                                               double phaseSlope) noexcept
{
    if (!(magnitude > magnitudeFloor) || !std::isfinite(targetPosition))
        return;

    const int centre = static_cast<int>(std::floor(targetPosition));
    const double fraction = targetPosition - static_cast<double>(centre);
    const double oneMinus = 1.0 - fraction;

    // Smooth, non-negative four-bin transport. L2 normalisation preserves the
    // source-bin energy without the two-bin weight exchange that produced the
    // audible amplitude modulation in the former renderer.
    float weights[4] {
        static_cast<float>((oneMinus * oneMinus * oneMinus) / 6.0),
        static_cast<float>((3.0 * fraction * fraction * fraction
                          - 6.0 * fraction * fraction + 4.0) / 6.0),
        static_cast<float>((-3.0 * fraction * fraction * fraction
                          + 3.0 * fraction * fraction
                          + 3.0 * fraction + 1.0) / 6.0),
        static_cast<float>((fraction * fraction * fraction) / 6.0)
    };
    const int bins[4] { centre - 1, centre, centre + 1, centre + 2 };

    float weightEnergy = 0.0f;
    for (int index = 0; index < 4; ++index)
    {
        if (bins[index] >= 0
            && bins[index] < static_cast<int>(destinationDepositedEnergy_.size()))
        {
            weightEnergy += weights[index] * weights[index];
        }
    }
    if (!(weightEnergy > 1.0e-12f))
        return;

    const float inverseNorm = 1.0f / std::sqrt(weightEnergy);
    for (int index = 0; index < 4; ++index)
    {
        const int destinationBin = bins[index];
        if (destinationBin < 0
            || destinationBin >= static_cast<int>(destinationDepositedEnergy_.size()))
        {
            continue;
        }

        const float weight = weights[index] * inverseNorm;
        const double binPhase = phase
            + phaseSlope * (static_cast<double>(destinationBin) - targetPosition);
        const float amplitude = magnitude * weight;
        const Complex contribution(
            amplitude * static_cast<float>(std::cos(binPhase)),
            amplitude * static_cast<float>(std::sin(binPhase)));
        spectrum_[static_cast<std::size_t>(destinationBin)] += contribution;
        destinationDepositedEnergy_[static_cast<std::size_t>(destinationBin)]
            += amplitude * amplitude;
    }
}

double NeumatonOutputRenderer::correctionRatio(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory) noexcept
{
    double ratio = lastCorrectionRatio_;
    bool newRatioValid = false;

    if (std::isfinite(trajectory.correctionCents)
        && (trajectory.targetValid || std::abs(trajectory.correctionCents) > 1.0e-8))
    {
        ratio = std::exp2(trajectory.correctionCents / 1200.0);
        newRatioValid = true;
    }
    else if (trajectory.targetPitchHz > 0.0f
             && analysis.detectedPitchHz > 0.0f
             && std::isfinite(trajectory.targetPitchHz)
             && std::isfinite(analysis.detectedPitchHz))
    {
        ratio = static_cast<double>(trajectory.targetPitchHz)
              / static_cast<double>(analysis.detectedPitchHz);
        newRatioValid = true;
    }

    ratio = std::clamp(ratio, 0.25, 4.0);
    if (newRatioValid)
    {
        lastCorrectionRatio_ = ratio;
        correctionRatioValid_ = true;
    }
    else if (!correctionRatioValid_)
    {
        lastCorrectionRatio_ = 1.0;
        ratio = 1.0;
    }

    return ratio;
}

double NeumatonOutputRenderer::sourcePosition(
    const AnalysisFrameView& analysis,
    int sourceBin) const noexcept
{
    if (sourceBin >= 0
        && sourceBin < analysis.trueSourceBins.size()
        && std::isfinite(analysis.trueSourceBins[sourceBin])
        && analysis.trueSourceBins[sourceBin] >= 0.0)
    {
        return analysis.trueSourceBins[sourceBin];
    }
    return static_cast<double>(std::max(0, sourceBin));
}

int NeumatonOutputRenderer::nearestPeakForBin(
    const AnalysisFrameView& analysis,
    int sourceBin) const noexcept
{
    if (sourceBin >= 0 && sourceBin < analysis.nearestPeak.size())
    {
        const int peak = analysis.nearestPeak[sourceBin];
        if (peak >= 0 && peak < analysis.positiveBinCount)
            return peak;
    }
    return sourceBin;
}

int NeumatonOutputRenderer::trackForPeak(
    const RidgeLedgerFrameView& ledger,
    int peakBin) const noexcept
{
    if (peakBin < 0 || peakBin >= ledger.sourceBinTrackIndices.size())
        return -1;
    const int trackIndex = ledger.sourceBinTrackIndices[peakBin];
    if (trackIndex < 0 || trackIndex >= ledger.tracks.size())
        return -1;
    return trackIsUsable(ledger.tracks[trackIndex]) ? trackIndex : -1;
}

double NeumatonOutputRenderer::synthesisPhase(
    const AnalysisFrameView& analysis,
    const RidgeLedgerFrameView& ledger,
    int sourceBin,
    int peakBin,
    double targetPosition,
    bool resetPhase) noexcept
{
    const int safeSource = std::clamp(sourceBin,
                                      0,
                                      std::max(0, analysis.analysisPhases.size() - 1));
    const int safePeak = std::clamp(peakBin,
                                    0,
                                    std::max(0, analysis.analysisPhases.size() - 1));
    const double sourcePhase = analysis.analysisPhases.empty()
        ? 0.0
        : static_cast<double>(analysis.analysisPhases[safeSource]);
    const double peakPhase = analysis.analysisPhases.empty()
        ? sourcePhase
        : static_cast<double>(analysis.analysisPhases[safePeak]);
    const double relativePhase = wrapPhase(sourcePhase - peakPhase);

    const float harmonicEvidence = sourceBin < analysis.harmonicMask.size()
        ? clamp01(analysis.harmonicMask[sourceBin])
        : 0.0f;
    const int trackIndex = harmonicEvidence >= 0.20f
        ? trackForPeak(ledger, peakBin)
        : -1;

    if (trackIndex >= 0 && !resetPhase)
        return wrapPhase(ledger.tracks[trackIndex].targetPhase + relativePhase);

    if (harmonicEvidence < 0.20f)
{
    const auto aperiodicIndex = static_cast<std::size_t>(std::clamp(
      sourceBin,
      0,
      static_cast<int>(freeSynthesisPhaseValid_.size()) - 1));
    freeSynthesisPhaseValid_[aperiodicIndex] = std::uint8_t { 0 };
    return wrapPhase(sourcePhase);
}

    const auto phaseIndex = static_cast<std::size_t>(std::clamp(
        sourceBin,
        0,
        static_cast<int>(freeSynthesisPhase_.size()) - 1));
    if (resetPhase || freeSynthesisPhaseValid_[phaseIndex] == 0u)
    {
        freeSynthesisPhase_[phaseIndex] = sourcePhase;
        freeSynthesisPhaseValid_[phaseIndex] = 1u;
    }
    else
    {
        const double advance = twoPi
            * static_cast<double>(spec_.hopSize)
            * targetPosition
            / static_cast<double>(spec_.frameSize);
        freeSynthesisPhase_[phaseIndex] = wrapPhase(
            freeSynthesisPhase_[phaseIndex] + advance);
    }
    return freeSynthesisPhase_[phaseIndex];
}

double NeumatonOutputRenderer::localPhaseSlope(
    const AnalysisFrameView& analysis,
    const RidgeLedgerFrameView& ledger,
    int sourceBin,
    int peakBin,
    double ratio) const noexcept
{
    const int trackIndex = trackForPeak(ledger, peakBin);
    if (trackIndex >= 0)
    {
        return static_cast<double>(
            ledger.tracks[trackIndex].groupDelaySlopeRadiansPerBin)
            / std::max(0.25, ratio);
    }

    if (analysis.analysisPhases.size() < 3)
        return 0.0;
    const int centre = std::clamp(sourceBin,
                                  1,
                                  analysis.analysisPhases.size() - 2);
    const double left = analysis.analysisPhases[centre - 1];
    const double right = analysis.analysisPhases[centre + 1];
    return 0.5 * wrapPhase(right - left) / std::max(0.25, ratio);
}

float NeumatonOutputRenderer::formantGain(
    const AnalysisFrameView& analysis,
    double sourceBin,
    double targetBin,
    float amount,
    float harmonicEvidence) const noexcept
{
    if (analysis.spectralEnvelope.empty())
        return 1.0f;

    const float safeAmount = clamp01(amount);
// Formant compensation belongs only to periodic vocal structure.
// Aperiodic consonants and breath still follow the scale ratio, but
// do not acquire moving envelope correction.
const float harmonicAmount = clamp01(harmonicEvidence);
const float effectiveAmount = safeAmount
    * harmonicAmount * harmonicAmount;
    if (effectiveAmount <= 1.0e-4f)
        return 1.0f;

    const float sourceEnvelope = interpolate(analysis.spectralEnvelope,
                                             sourceBin,
                                             1.0f);
    const float targetEnvelope = interpolate(analysis.spectralEnvelope,
                                             targetBin,
                                             sourceEnvelope);
    const float ratio = std::clamp(
        targetEnvelope / std::max(1.0e-8f, sourceEnvelope),
        0.25f,
        4.0f);
    return std::clamp(std::pow(ratio, effectiveAmount), 0.50f, 2.0f);
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

        switch (ownership_[static_cast<std::size_t>(bin)])
        {
            case BinOwnership::ridge: ridgeEnergy += sourceEnergy; break;
            case BinOwnership::event: eventEnergy += sourceEnergy; break;
            case BinOwnership::air: airEnergy += sourceEnergy; break;
            case BinOwnership::unclassified: unclassifiedEnergy += sourceEnergy; break;
        }

        const auto index = static_cast<std::size_t>(bin);
        const double actualEnergy = std::norm(spectrum_[index]);
        outputEnergy += actualEnergy;
        collisionEnergy += std::abs(
            static_cast<double>(destinationDepositedEnergy_[index]) - actualEnergy);

        if (bin > 0 && bin + 1 < usableBins)
        {
            const Complex left = spectrum_[static_cast<std::size_t>(bin - 1)];
            const Complex centre = spectrum_[index];
            const Complex right = spectrum_[static_cast<std::size_t>(bin + 1)];
            const double weight = std::abs(centre);
            if (weight > 1.0e-9)
            {
                const double curvature = wrapPhase(
                    std::arg(right) - 2.0 * std::arg(centre) + std::arg(left));
                phaseCoherenceSum += weight * (0.5 + 0.5 * std::cos(curvature));
                phaseCoherenceWeight += weight;
            }
        }

        if (previousSpectrumValid_)
        {
            const Complex current = spectrum_[index];
            const Complex previous = previousSpectrum_[index];
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
                const double phaseError = wrapPhase(
                    std::arg(current) - std::arg(previous) - expectedAdvance);
                olaCoherenceSum += weight * (0.5 + 0.5 * std::cos(phaseError));
                olaWeight += weight;
            }
        }
    }

    const double inverseInput = inputEnergy > 1.0e-18 ? 1.0 / inputEnergy : 0.0;
    diagnostics_.ridgeEnergyRatio = static_cast<float>(ridgeEnergy * inverseInput);
    diagnostics_.eventEnergyRatio = static_cast<float>(eventEnergy * inverseInput);
    diagnostics_.airEnergyRatio = static_cast<float>(airEnergy * inverseInput);
    diagnostics_.unclassifiedEnergyRatio = static_cast<float>(
        unclassifiedEnergy * inverseInput);
    diagnostics_.assignedEnergyRatio = inputEnergy > 1.0e-18 ? 1.0f : 0.0f;
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
    diagnostics_.formantEnvelopeError = 0.0f;
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

float NeumatonOutputRenderer::interpolate(
    const ConstArrayView<float>& values,
    double position,
    float fallback) noexcept
{
    if (values.empty() || !std::isfinite(position))
        return fallback;
    position = std::clamp(position,
                          0.0,
                          static_cast<double>(values.size() - 1));
    const int lower = static_cast<int>(std::floor(position));
    const int upper = std::min(values.size() - 1, lower + 1);
    const float fraction = static_cast<float>(
        position - static_cast<double>(lower));
    const float lowerValue = std::isfinite(values[lower])
        ? values[lower]
        : fallback;
    const float upperValue = std::isfinite(values[upper])
        ? values[upper]
        : lowerValue;
    return lowerValue + fraction * (upperValue - lowerValue);
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
