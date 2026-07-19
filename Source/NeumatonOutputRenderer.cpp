#include "NeumatonOutputRenderer.h"

#include <algorithm>
#include <cmath>

namespace neumaton::outputv3
{
namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;

[[nodiscard]] float clamp01(float value) noexcept
{
    return std::clamp(value, 0.0f, 1.0f);
}
} // namespace

void NeumatonOutputRenderer::prepare(const OutputPrepareSpec& requestedSpec)
{
    spec_ = requestedSpec;
    spec_.sampleRate = std::isfinite(spec_.sampleRate)
        ? std::max(8000.0, spec_.sampleRate)
        : 48000.0;
    spec_.frameSize = std::max(64, spec_.frameSize);
    spec_.hopSize = std::clamp(spec_.hopSize, 1, spec_.frameSize);
    spec_.positiveBinCount = std::clamp(spec_.positiveBinCount,
                                        2,
                                        spec_.frameSize / 2 + 1);
    spec_.outputRingSize = std::max(spec_.frameSize * 2,
                                    spec_.outputRingSize);

    spectrum_.assign(static_cast<std::size_t>(spec_.frameSize), {});
    ifftFrame_.assign(static_cast<std::size_t>(spec_.frameSize), {});
    outputAccumulationRing_.assign(static_cast<std::size_t>(spec_.outputRingSize), 0.0f);
    synthesisWindow_.assign(static_cast<std::size_t>(spec_.frameSize), 0.0f);
    ownership_.assign(static_cast<std::size_t>(spec_.positiveBinCount),
                      BinOwnership::unclassified);

    for (int index = 0; index < spec_.frameSize; ++index)
    {
        const double periodicHann = 0.5 - 0.5 * std::cos(
            twoPi * static_cast<double>(index)
            / static_cast<double>(spec_.frameSize));
        synthesisWindow_[static_cast<std::size_t>(index)] = static_cast<float>(
            std::sqrt(std::max(0.0, periodicHann)));
    }

    reset();
}

void NeumatonOutputRenderer::reset() noexcept
{
    std::fill(spectrum_.begin(), spectrum_.end(), std::complex<float> {});
    std::fill(ifftFrame_.begin(), ifftFrame_.end(), std::complex<float> {});
    std::fill(outputAccumulationRing_.begin(), outputAccumulationRing_.end(), 0.0f);
    std::fill(ownership_.begin(), ownership_.end(), BinOwnership::unclassified);
    diagnostics_ = {};
}

OutputSpectrumView NeumatonOutputRenderer::inspectFrame(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory,
    const RidgeLedgerFrameView& ledger) noexcept
{
    if (spectrum_.empty() || ownership_.empty())
        return {};

    std::fill(spectrum_.begin(), spectrum_.end(), std::complex<float> {});
    std::fill(ownership_.begin(), ownership_.end(), BinOwnership::unclassified);

    classifyOwnership(analysis, ledger);
    buildDiagnosticSpectrum(analysis, trajectory, ledger);
    updateDiagnostics(analysis);

    return {
        { spectrum_.data(), static_cast<int>(spectrum_.size()) },
        { ownership_.data(), static_cast<int>(ownership_.size()) }
    };
}

void NeumatonOutputRenderer::classifyOwnership(
    const AnalysisFrameView& analysis,
    const RidgeLedgerFrameView& ledger) noexcept
{
    const int usableBins = std::min({ analysis.positiveBinCount,
                                      static_cast<int>(ownership_.size()),
                                      analysis.magnitudes.size() });

    for (int bin = 0; bin < usableBins; ++bin)
    {
        const int trackIndex = bin < ledger.sourceBinTrackIndices.size()
            ? ledger.sourceBinTrackIndices[bin]
            : -1;
        if (trackIndex >= 0 && trackIndex < ledger.tracks.size())
        {
            const auto& track = ledger.tracks[trackIndex];
            if (track.lifeState == RidgeLifeState::active
                || track.lifeState == RidgeLifeState::coasting)
            {
                ownership_[static_cast<std::size_t>(bin)] = BinOwnership::ridge;
                continue;
            }
        }

        const float harmonic = bin < analysis.harmonicMask.size()
            ? clamp01(analysis.harmonicMask[bin])
            : 0.0f;
        const float eventEvidence = clamp01(analysis.onsetStrength)
            * (0.35f + 0.65f * (1.0f - harmonic));
        const float frequencyHz = static_cast<float>(bin)
            * static_cast<float>(analysis.sampleRate
                / static_cast<double>(std::max(1, analysis.frameSize)));
        const float airEvidence = (1.0f - harmonic)
            * clamp01(analysis.breathiness)
            * clamp01((frequencyHz - 2400.0f) / 5200.0f);

        if (eventEvidence > 0.48f)
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::event;
        else if (airEvidence > 0.22f)
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::air;
        else
            ownership_[static_cast<std::size_t>(bin)] = BinOwnership::unclassified;
    }
}

void NeumatonOutputRenderer::buildDiagnosticSpectrum(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory,
    const RidgeLedgerFrameView& ledger) noexcept
{
    const int usableBins = std::min({ analysis.positiveBinCount,
                                      static_cast<int>(ownership_.size()),
                                      analysis.magnitudes.size() });
    const double ratio = trajectory.targetValid
        && std::isfinite(trajectory.correctionCents)
        ? std::clamp(std::exp2(trajectory.correctionCents / 1200.0), 0.25, 4.0)
        : 1.0;

    for (int sourceBin = 0; sourceBin < usableBins; ++sourceBin)
    {
        const float magnitude = std::max(0.0f, analysis.magnitudes[sourceBin]);
        if (magnitude <= 1.0e-12f)
            continue;

        const auto owner = ownership_[static_cast<std::size_t>(sourceBin)];
        const int trackIndex = sourceBin < ledger.sourceBinTrackIndices.size()
            ? ledger.sourceBinTrackIndices[sourceBin]
            : -1;

        if (owner == BinOwnership::ridge
            && trackIndex >= 0
            && trackIndex < ledger.tracks.size())
        {
            const auto& track = ledger.tracks[trackIndex];
            const double sourcePosition = sourceBin < analysis.trueSourceBins.size()
                && std::isfinite(analysis.trueSourceBins[sourceBin])
                ? analysis.trueSourceBins[sourceBin]
                : static_cast<double>(sourceBin);
            const double targetPosition = sourcePosition * ratio;
            if (targetPosition < 0.0
                || targetPosition > static_cast<double>(usableBins - 1))
                continue;

            const double phase = wrapPhase(track.targetPhase
                + static_cast<double>(track.groupDelaySlopeRadiansPerBin)
                    * (static_cast<double>(sourceBin - track.lastPeakBin)));
            const std::complex<float> polar(
                magnitude * static_cast<float>(std::cos(phase)),
                magnitude * static_cast<float>(std::sin(phase)));

            const int lowerBin = static_cast<int>(std::floor(targetPosition));
            const int upperBin = lowerBin + 1;
            const float upperWeight = static_cast<float>(targetPosition
                - static_cast<double>(lowerBin));
            const float lowerWeight = 1.0f - upperWeight;

            if (lowerBin >= 0 && lowerBin < usableBins)
                spectrum_[static_cast<std::size_t>(lowerBin)] += polar * lowerWeight;
            if (upperBin >= 0 && upperBin < usableBins)
                spectrum_[static_cast<std::size_t>(upperBin)] += polar * upperWeight;
            continue;
        }

        if (sourceBin < analysis.analysedSpectrum.size())
        {
            spectrum_[static_cast<std::size_t>(sourceBin)] +=
                analysis.analysedSpectrum[sourceBin];
        }
        else
        {
            const float phase = sourceBin < analysis.analysisPhases.size()
                ? analysis.analysisPhases[sourceBin]
                : 0.0f;
            spectrum_[static_cast<std::size_t>(sourceBin)] += std::complex<float>(
                magnitude * std::cos(phase),
                magnitude * std::sin(phase));
        }
    }

    if (!spectrum_.empty())
        spectrum_[0] = { spectrum_[0].real(), 0.0f };

    const int nyquistBin = spec_.frameSize / 2;
    if (nyquistBin >= 0 && nyquistBin < static_cast<int>(spectrum_.size()))
        spectrum_[static_cast<std::size_t>(nyquistBin)] = {
            spectrum_[static_cast<std::size_t>(nyquistBin)].real(), 0.0f
        };

    const int mirrorLimit = std::min(usableBins - 1, nyquistBin);
    for (int bin = 1; bin < mirrorLimit; ++bin)
    {
        const int mirrorBin = spec_.frameSize - bin;
        if (mirrorBin >= 0 && mirrorBin < static_cast<int>(spectrum_.size()))
            spectrum_[static_cast<std::size_t>(mirrorBin)] =
                std::conj(spectrum_[static_cast<std::size_t>(bin)]);
    }
}

void NeumatonOutputRenderer::updateDiagnostics(
    const AnalysisFrameView& analysis) noexcept
{
    double totalEnergy = 0.0;
    double ridgeEnergy = 0.0;
    double eventEnergy = 0.0;
    double airEnergy = 0.0;
    double unclassifiedEnergy = 0.0;

    const int usableBins = std::min({ analysis.positiveBinCount,
                                      static_cast<int>(ownership_.size()),
                                      analysis.magnitudes.size() });
    for (int bin = 0; bin < usableBins; ++bin)
    {
        const double magnitude = std::max(0.0f, analysis.magnitudes[bin]);
        const double energy = magnitude * magnitude;
        totalEnergy += energy;

        switch (ownership_[static_cast<std::size_t>(bin)])
        {
            case BinOwnership::ridge:        ridgeEnergy += energy; break;
            case BinOwnership::event:        eventEnergy += energy; break;
            case BinOwnership::air:          airEnergy += energy; break;
            case BinOwnership::unclassified: unclassifiedEnergy += energy; break;
        }
    }

    const double inverseTotal = totalEnergy > 1.0e-18 ? 1.0 / totalEnergy : 0.0;
    diagnostics_.ridgeEnergyRatio = static_cast<float>(ridgeEnergy * inverseTotal);
    diagnostics_.eventEnergyRatio = static_cast<float>(eventEnergy * inverseTotal);
    diagnostics_.airEnergyRatio = static_cast<float>(airEnergy * inverseTotal);
    diagnostics_.unclassifiedEnergyRatio = static_cast<float>(
        unclassifiedEnergy * inverseTotal);
    diagnostics_.overlapAddCoherence = 0.0f;
    diagnostics_.requestedEnergyGainDb = 0.0f;
}

double NeumatonOutputRenderer::wrapPhase(double phase) noexcept
{
    if (!std::isfinite(phase))
        return 0.0;
    phase -= twoPi * std::nearbyint(phase / twoPi);
    return phase;
}

} // namespace neumaton::outputv3
