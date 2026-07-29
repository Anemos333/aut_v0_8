#include "NeumatonRidgeLedger.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

[[nodiscard]] float safeMagnitude(const AnalysisFrameView& analysis, int bin) noexcept
{
    if (bin < 0 || bin >= analysis.magnitudes.size())
        return 0.0f;
    const float value = analysis.magnitudes[bin];
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}
} // namespace

void NeumatonRidgeLedger::prepare(const OutputPrepareSpec& requestedSpec)
{
    spec_ = requestedSpec;
    spec_.sampleRate = std::isfinite(spec_.sampleRate)
        ? std::max(8000.0, spec_.sampleRate)
        : 48000.0;
    spec_.frameSize = std::max(64, spec_.frameSize);
    spec_.hopSize = std::clamp(spec_.hopSize, 1, spec_.frameSize);
    spec_.positiveBinCount = std::max(2, spec_.positiveBinCount);
    spec_.maximumRidges = std::clamp(spec_.maximumRidges, 1, 512);
    spec_.maximumObservations = std::clamp(spec_.maximumObservations, 1, 1024);

    tracks_.assign(static_cast<std::size_t>(spec_.maximumRidges), RidgeState {});
    observations_.assign(static_cast<std::size_t>(spec_.maximumObservations),
                         RidgeObservation {});
    observationMatched_.assign(static_cast<std::size_t>(spec_.maximumObservations), 0u);
    sourceBinTrackIndices_.assign(static_cast<std::size_t>(spec_.positiveBinCount), -1);
    previousBinTrackIds_.assign(static_cast<std::size_t>(spec_.positiveBinCount), 0u);
    reset();
}

void NeumatonRidgeLedger::reset() noexcept
{
    std::fill(tracks_.begin(), tracks_.end(), RidgeState {});
    std::fill(observations_.begin(), observations_.end(), RidgeObservation {});
    std::fill(observationMatched_.begin(), observationMatched_.end(), 0u);
    std::fill(sourceBinTrackIndices_.begin(), sourceBinTrackIndices_.end(), -1);
    std::fill(previousBinTrackIds_.begin(), previousBinTrackIds_.end(), 0u);
    observationCount_ = 0;
    phasePredictionErrorSum_ = 0.0;
    phasePredictionErrorCount_ = 0;
    nextTrackId_ = 1;
    diagnostics_ = {};
}

RidgeLedgerFrameView NeumatonRidgeLedger::processFrame(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory) noexcept
{
    if (tracks_.empty() || observations_.empty() || sourceBinTrackIndices_.empty())
        return {};

    clearFrameState();
    extractObservations(analysis);

    if (analysis.phaseReset || trajectory.forceReset)
    {
        for (auto& track : tracks_)
        {
            if (track.lifeState != RidgeLifeState::inactive)
            {
                track.targetPhase = 0.0;
                track.previousTargetFrequencyHz = 0.0f;
                track.missedFrames = maximumCoastFrames;
            }
        }
    }

    continueExistingTracks(analysis, trajectory);
    createReliableBirths(analysis, trajectory);
    coastUnmatchedTracks(trajectory);
    buildSourceBinOwnership(analysis);
    updateDiagnostics(analysis);

    return {
        { tracks_.data(), static_cast<int>(tracks_.size()) },
        { sourceBinTrackIndices_.data(), static_cast<int>(sourceBinTrackIndices_.size()) },
        diagnostics_.activeTrackCount
    };
}

void NeumatonRidgeLedger::clearFrameState() noexcept
{
    const int rememberedBins = std::min(static_cast<int>(sourceBinTrackIndices_.size()),
                                        static_cast<int>(previousBinTrackIds_.size()));
    for (int bin = 0; bin < rememberedBins; ++bin)
    {
        const int trackIndex = sourceBinTrackIndices_[static_cast<std::size_t>(bin)];
        previousBinTrackIds_[static_cast<std::size_t>(bin)] =
            trackIndex >= 0 && trackIndex < static_cast<int>(tracks_.size())
                ? tracks_[static_cast<std::size_t>(trackIndex)].id
                : 0u;
    }

    observationCount_ = 0;
    phasePredictionErrorSum_ = 0.0;
    phasePredictionErrorCount_ = 0;
    std::fill(observationMatched_.begin(), observationMatched_.end(), 0u);
    std::fill(sourceBinTrackIndices_.begin(), sourceBinTrackIndices_.end(), -1);

    diagnostics_ = {};
    for (auto& track : tracks_)
        track.matchedThisFrame = false;
}

void NeumatonRidgeLedger::extractObservations(
    const AnalysisFrameView& analysis) noexcept
{
    if (analysis.peakBins.empty() || analysis.magnitudes.empty())
        return;

    const double safeSampleRate = std::isfinite(analysis.sampleRate)
        ? std::max(8000.0, analysis.sampleRate)
        : spec_.sampleRate;
    const int safeFrameSize = std::max(1, analysis.frameSize);
    const float binWidthHz = static_cast<float>(safeSampleRate
        / static_cast<double>(safeFrameSize));
    const int usableBins = std::min({ analysis.positiveBinCount,
                                      analysis.magnitudes.size(),
                                      spec_.positiveBinCount });

    for (int peakIndex = 0;
         peakIndex < analysis.peakBins.size()
             && observationCount_ < static_cast<int>(observations_.size());
         ++peakIndex)
    {
        const int peakBin = analysis.peakBins[peakIndex];
        if (peakBin <= 0 || peakBin >= usableBins - 1)
            continue;

        const float amplitude = safeMagnitude(analysis, peakBin);
        if (amplitude <= 1.0e-10f)
            continue;

        const double trueBin = peakBin < analysis.trueSourceBins.size()
            && std::isfinite(analysis.trueSourceBins[peakBin])
            ? analysis.trueSourceBins[peakBin]
            : static_cast<double>(peakBin);
        const float frequencyHz = static_cast<float>(std::max(0.0, trueBin)
            * static_cast<double>(binWidthHz));

        float neighbourSum = 0.0f;
        int neighbourCount = 0;
        for (int offset = -3; offset <= 3; ++offset)
        {
            if (offset == 0)
                continue;
            const int bin = peakBin + offset;
            if (bin <= 0 || bin >= usableBins - 1)
                continue;
            neighbourSum += safeMagnitude(analysis, bin);
            ++neighbourCount;
        }
        const float neighbourMean = neighbourCount > 0
            ? neighbourSum / static_cast<float>(neighbourCount)
            : amplitude;
        const float localSnr = amplitude / std::max(1.0e-10f, neighbourMean);
        const float snrReliability = clamp01((localSnr - 1.0f) / 4.0f);

        int widthBins = 1;
        const float halfHeight = 0.5f * amplitude;
        for (int offset = 1; offset <= 4; ++offset)
        {
            if (peakBin - offset > 0
                && safeMagnitude(analysis, peakBin - offset) >= halfHeight)
                ++widthBins;
            if (peakBin + offset < usableBins - 1
                && safeMagnitude(analysis, peakBin + offset) >= halfHeight)
                ++widthBins;
        }

        const float driftBins = static_cast<float>(std::abs(trueBin
            - static_cast<double>(peakBin)));
        const float phaseCoherence = 1.0f - clamp01(driftBins / 0.80f);
        const float harmonicPrior = peakBin < analysis.harmonicMask.size()
            ? clamp01(analysis.harmonicMask[peakBin])
            : 0.0f;
        const float eventProbability = clamp01(
            0.70f * clamp01(analysis.onsetStrength)
            + 0.30f * (1.0f - harmonicPrior));
        const float reliability = clamp01(
            0.30f * snrReliability
            + 0.25f * phaseCoherence
            + 0.25f * harmonicPrior
            + 0.20f * clamp01(analysis.spectralReliability))
            * (1.0f - 0.72f * eventProbability);

        RidgeObservation observation;
        observation.peakBin = peakBin;
        observation.sourceFrequencyHz = frequencyHz;
        observation.amplitude = amplitude;
        observation.localWidthBins = static_cast<float>(widthBins);
        observation.localSnr = localSnr;
        observation.sourcePhase = peakBin < analysis.analysisPhases.size()
            ? analysis.analysisPhases[peakBin]
            : 0.0f;
        if (peakBin > 0 && peakBin + 1 < analysis.analysisPhases.size())
        {
            const double phaseDelta = wrapPhase(
                static_cast<double>(analysis.analysisPhases[peakBin + 1])
                - static_cast<double>(analysis.analysisPhases[peakBin - 1]));
            observation.groupDelaySlopeRadiansPerBin =
                static_cast<float>(0.5 * phaseDelta);
        }
        observation.phaseCoherence = phaseCoherence;
        observation.harmonicPrior = harmonicPrior;
        observation.eventProbability = eventProbability;
        observation.reliability = reliability;
        observations_[static_cast<std::size_t>(observationCount_++)] = observation;
    }
}

void NeumatonRidgeLedger::continueExistingTracks(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory) noexcept
{
    static_cast<void>(analysis);

    for (auto& track : tracks_)
    {
        if (track.lifeState == RidgeLifeState::inactive
            || track.lifeState == RidgeLifeState::pending)
            continue;

        const int observationIndex = findBestObservationForTrack(track);
        if (observationIndex < 0)
            continue;

        observationMatched_[static_cast<std::size_t>(observationIndex)] = 1u;
        updateMatchedTrack(track,
                           observations_[static_cast<std::size_t>(observationIndex)],
                           trajectory,
                           false);
    }
}

void NeumatonRidgeLedger::createReliableBirths(
    const AnalysisFrameView& analysis,
    const CorrectionTrajectoryFrame& trajectory) noexcept
{
    static_cast<void>(analysis);

    for (int observationIndex = 0; observationIndex < observationCount_; ++observationIndex)
    {
        if (observationMatched_[static_cast<std::size_t>(observationIndex)] != 0u)
            continue;

        const auto& observation = observations_[static_cast<std::size_t>(observationIndex)];
        if (observation.reliability < 0.64f || observation.eventProbability > 0.55f)
            continue;

        const int slot = findInactiveTrackSlot();
        if (slot < 0)
            break;

        auto& track = tracks_[static_cast<std::size_t>(slot)];
        track = {};
        track.id = nextTrackId_++;
        if (nextTrackId_ == 0u)
            nextTrackId_ = 1u;
        track.lifeState = RidgeLifeState::active;
        updateMatchedTrack(track, observation, trajectory, true);
        observationMatched_[static_cast<std::size_t>(observationIndex)] = 1u;
        ++diagnostics_.bornTrackCount;
    }
}

void NeumatonRidgeLedger::coastUnmatchedTracks(
    const CorrectionTrajectoryFrame& trajectory) noexcept
{
    const double ratio = correctionRatio(trajectory);

    for (auto& track : tracks_)
    {
        if (track.lifeState == RidgeLifeState::inactive
            || track.lifeState == RidgeLifeState::pending
            || track.matchedThisFrame)
            continue;

        ++track.missedFrames;
        ++track.ageFrames;
        track.lifeState = RidgeLifeState::coasting;
        track.reliability *= 0.78f;

        const float targetFrequencyHz = static_cast<float>(
            std::max(0.0, static_cast<double>(track.sourceFrequencyHz) * ratio));
        track.previousSourcePhase = track.sourcePhase;
        track.sourcePhase = static_cast<float>(wrapPhase(
            static_cast<double>(track.sourcePhase)
            + twoPi * static_cast<double>(spec_.hopSize)
                * static_cast<double>(track.sourceFrequencyHz)
                / std::max(1.0, spec_.sampleRate)));
        advanceTargetPhase(track, targetFrequencyHz);
        ++diagnostics_.coastTrackCount;

        if (track.missedFrames > maximumCoastFrames || track.reliability < 0.08f)
        {
            track = {};
            ++diagnostics_.deadTrackCount;
        }
    }
}

void NeumatonRidgeLedger::buildSourceBinOwnership(
    const AnalysisFrameView& analysis) noexcept
{
    const int usableBins = std::min({ analysis.positiveBinCount,
                                      static_cast<int>(sourceBinTrackIndices_.size()),
                                      analysis.nearestPeak.size() });

    for (int bin = 0; bin < usableBins; ++bin)
    {
        const int ownerPeak = analysis.nearestPeak[bin];
        int bestTrack = -1;
        int bestDistance = std::numeric_limits<int>::max();

        for (int trackIndex = 0; trackIndex < static_cast<int>(tracks_.size()); ++trackIndex)
        {
            const auto& track = tracks_[static_cast<std::size_t>(trackIndex)];
            if (track.lifeState == RidgeLifeState::inactive
                || track.lifeState == RidgeLifeState::pending
                || track.lastPeakBin < 0)
                continue;

            const int distance = std::abs(track.lastPeakBin - ownerPeak);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestTrack = trackIndex;
            }
        }

        if (bestDistance <= 2)
            sourceBinTrackIndices_[static_cast<std::size_t>(bin)] = bestTrack;
    }
}

void NeumatonRidgeLedger::updateDiagnostics(
    const AnalysisFrameView& analysis) noexcept
{
    double reliabilitySum = 0.0;
    int activeCount = 0;

    for (const auto& track : tracks_)
    {
        if (track.lifeState == RidgeLifeState::inactive
            || track.lifeState == RidgeLifeState::pending)
            continue;
        reliabilitySum += track.reliability;
        ++activeCount;
    }

    const int usableBins = std::min({ analysis.positiveBinCount,
                                      analysis.magnitudes.size(),
                                      static_cast<int>(sourceBinTrackIndices_.size()) });
    float maximumMagnitude = 0.0f;
    for (int bin = 1; bin < usableBins; ++bin)
        maximumMagnitude = std::max(maximumMagnitude, safeMagnitude(analysis, bin));

    const float energyGate = maximumMagnitude * 1.0e-4f;
    int eligibleBins = 0;
    int resolvedBins = 0;
    for (int bin = 1; bin < usableBins; ++bin)
    {
        if (safeMagnitude(analysis, bin) <= energyGate)
            continue;
        ++eligibleBins;
        if (sourceBinTrackIndices_[static_cast<std::size_t>(bin)] >= 0)
            ++resolvedBins;
    }

    diagnostics_.observationCount = observationCount_;
    diagnostics_.activeTrackCount = activeCount;
    diagnostics_.meanPredictionErrorRadians = phasePredictionErrorCount_ > 0
        ? static_cast<float>(phasePredictionErrorSum_
            / static_cast<double>(phasePredictionErrorCount_))
        : 0.0f;
    diagnostics_.meanReliability = activeCount > 0
        ? static_cast<float>(reliabilitySum / static_cast<double>(activeCount))
        : 0.0f;
    diagnostics_.resolvedBinCoverage = eligibleBins > 0
        ? static_cast<float>(resolvedBins) / static_cast<float>(eligibleBins)
        : 0.0f;
    diagnostics_.frameValid = observationCount_ > 0 && usableBins > 2;
}

int NeumatonRidgeLedger::findBestObservationForTrack(
    const RidgeState& track) const noexcept
{
    int bestObservation = -1;
    float bestCost = std::numeric_limits<float>::infinity();

    for (int observationIndex = 0; observationIndex < observationCount_; ++observationIndex)
    {
        if (observationMatched_[static_cast<std::size_t>(observationIndex)] != 0u)
            continue;

        const auto& observation = observations_[static_cast<std::size_t>(observationIndex)];
        const float distanceCents = centsDistance(track.sourceFrequencyHz,
                                                  observation.sourceFrequencyHz);
        const float toleranceCents = 48.0f + 92.0f * (1.0f - track.reliability)
            + 18.0f * static_cast<float>(track.missedFrames);
        if (distanceCents > toleranceCents)
            continue;

        const float amplitudeCost = std::abs(std::log(
            std::max(1.0e-9f, observation.amplitude)
            / std::max(1.0e-9f, track.amplitude)));
        const float phaseCost = 1.0f - observation.phaseCoherence;
        const float eventCost = observation.eventProbability;
        const float cost = 0.62f * (distanceCents / std::max(1.0f, toleranceCents))
            + 0.18f * std::min(1.0f, amplitudeCost)
            + 0.12f * phaseCost
            + 0.08f * eventCost;

        if (cost < bestCost)
        {
            bestCost = cost;
            bestObservation = observationIndex;
        }
    }

    return bestCost <= 0.92f ? bestObservation : -1;
}

int NeumatonRidgeLedger::findInactiveTrackSlot() const noexcept
{
    for (int index = 0; index < static_cast<int>(tracks_.size()); ++index)
    {
        if (tracks_[static_cast<std::size_t>(index)].lifeState
            == RidgeLifeState::inactive)
            return index;
    }
    return -1;
}

void NeumatonRidgeLedger::updateMatchedTrack(
    RidgeState& track,
    const RidgeObservation& observation,
    const CorrectionTrajectoryFrame& trajectory,
    bool newlyBorn) noexcept
{
    const float oldSourceFrequencyHz = track.sourceFrequencyHz;
    const float oldSourcePhase = track.sourcePhase;

    if (!newlyBorn && oldSourceFrequencyHz > 0.0f)
    {
        const double predictedSourcePhase = wrapPhase(
            static_cast<double>(oldSourcePhase)
            + twoPi * static_cast<double>(spec_.hopSize)
                * 0.5 * (static_cast<double>(oldSourceFrequencyHz)
                       + static_cast<double>(observation.sourceFrequencyHz))
                / std::max(1.0, spec_.sampleRate));
        phasePredictionErrorSum_ += std::abs(wrapPhase(
            static_cast<double>(observation.sourcePhase) - predictedSourcePhase));
        ++phasePredictionErrorCount_;

        if (track.missedFrames == 0
            && observation.peakBin >= 0
            && observation.peakBin < static_cast<int>(previousBinTrackIds_.size()))
        {
            const std::uint32_t previousOwnerId = previousBinTrackIds_[
                static_cast<std::size_t>(observation.peakBin)];
            if (previousOwnerId != 0u && previousOwnerId != track.id)
                ++diagnostics_.identitySwitchCount;
        }
    }

    track.previousSourceFrequencyHz = newlyBorn
        ? observation.sourceFrequencyHz
        : oldSourceFrequencyHz;
    track.sourceFrequencyHz = observation.sourceFrequencyHz;
    track.previousSourcePhase = newlyBorn ? observation.sourcePhase : oldSourcePhase;
    track.sourcePhase = observation.sourcePhase;
    track.groupDelaySlopeRadiansPerBin = newlyBorn
        ? observation.groupDelaySlopeRadiansPerBin
        : 0.72f * track.groupDelaySlopeRadiansPerBin
            + 0.28f * observation.groupDelaySlopeRadiansPerBin;
    track.lastPeakBin = observation.peakBin;
    track.amplitude = newlyBorn
        ? observation.amplitude
        : 0.70f * track.amplitude + 0.30f * observation.amplitude;
    track.reliability = newlyBorn
        ? observation.reliability
        : clamp01(0.72f * track.reliability + 0.28f * observation.reliability);
    track.missedFrames = 0;
    track.matchedThisFrame = true;
    track.lifeState = RidgeLifeState::active;
    ++track.ageFrames;

    const float targetFrequencyHz = static_cast<float>(
        std::max(0.0, static_cast<double>(track.sourceFrequencyHz)
            * correctionRatio(trajectory)));

    if (newlyBorn || track.previousTargetFrequencyHz <= 0.0f)
    {
        track.previousTargetFrequencyHz = targetFrequencyHz;
        track.targetFrequencyHz = targetFrequencyHz;
        track.targetPhase = wrapPhase(static_cast<double>(observation.sourcePhase));
    }
    else
    {
        advanceTargetPhase(track, targetFrequencyHz);
    }
}

float NeumatonRidgeLedger::centsDistance(float frequencyA,
                                         float frequencyB) noexcept
{
    if (!(frequencyA > 0.0f) || !(frequencyB > 0.0f)
        || !std::isfinite(frequencyA) || !std::isfinite(frequencyB))
        return std::numeric_limits<float>::infinity();

    return static_cast<float>(1200.0 * std::abs(std::log2(
        static_cast<double>(frequencyA) / static_cast<double>(frequencyB))));
}

double NeumatonRidgeLedger::wrapPhase(double phase) noexcept
{
    if (!std::isfinite(phase))
        return 0.0;
    phase -= twoPi * std::nearbyint(phase / twoPi);
    return phase;
}

double NeumatonRidgeLedger::correctionRatio(
    const CorrectionTrajectoryFrame& trajectory) const noexcept
{
    if (!trajectory.targetValid || !std::isfinite(trajectory.correctionCents))
        return 1.0;
    return std::clamp(std::exp2(trajectory.correctionCents / 1200.0), 0.25, 4.0);
}

void NeumatonRidgeLedger::advanceTargetPhase(RidgeState& track,
                                             float targetFrequencyHz) noexcept
{
    const float previousHz = track.targetFrequencyHz > 0.0f
        ? track.targetFrequencyHz
        : targetFrequencyHz;
    track.previousTargetFrequencyHz = previousHz;
    track.targetFrequencyHz = targetFrequencyHz;

    const double meanFrequencyHz = 0.5
        * (static_cast<double>(previousHz) + static_cast<double>(targetFrequencyHz));
    track.targetPhase = wrapPhase(track.targetPhase
        + twoPi * static_cast<double>(spec_.hopSize) * meanFrequencyHz
            / std::max(1.0, spec_.sampleRate));
}

} // namespace neumaton::outputv3
