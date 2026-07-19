#pragma once

#include "NeumatonOutputTypes.h"

#include <cstdint>
#include <vector>

namespace neumaton::outputv3
{

class NeumatonRidgeLedger final
{
public:
    NeumatonRidgeLedger() = default;

    // Allocates all storage used by processFrame(). This is never called from
    // the audio callback.
    void prepare(const OutputPrepareSpec& spec);
    void reset() noexcept;

    // Shadow-safe entry point. It updates ridge identities and phase state but
    // has no access to an output buffer and therefore cannot alter audio.
    [[nodiscard]] RidgeLedgerFrameView processFrame(
        const AnalysisFrameView& analysis,
        const CorrectionTrajectoryFrame& trajectory) noexcept;

    [[nodiscard]] const RidgeLedgerDiagnostics& getDiagnostics() const noexcept
    {
        return diagnostics_;
    }

    [[nodiscard]] ConstArrayView<RidgeObservation> getObservations() const noexcept
    {
        return { observations_.data(), observationCount_ };
    }

private:
    static constexpr int maximumCoastFrames = 3;

    void extractObservations(const AnalysisFrameView& analysis) noexcept;
    void clearFrameState() noexcept;
    void continueExistingTracks(const AnalysisFrameView& analysis,
                                const CorrectionTrajectoryFrame& trajectory) noexcept;
    void createReliableBirths(const AnalysisFrameView& analysis,
                              const CorrectionTrajectoryFrame& trajectory) noexcept;
    void coastUnmatchedTracks(const CorrectionTrajectoryFrame& trajectory) noexcept;
    void buildSourceBinOwnership(const AnalysisFrameView& analysis) noexcept;
    void updateDiagnostics() noexcept;

    [[nodiscard]] int findBestObservationForTrack(const RidgeState& track) const noexcept;
    [[nodiscard]] int findInactiveTrackSlot() const noexcept;
    void updateMatchedTrack(RidgeState& track,
                            const RidgeObservation& observation,
                            const CorrectionTrajectoryFrame& trajectory,
                            bool newlyBorn) noexcept;

    [[nodiscard]] static float centsDistance(float frequencyA,
                                             float frequencyB) noexcept;
    [[nodiscard]] static double wrapPhase(double phase) noexcept;
    [[nodiscard]] double correctionRatio(
        const CorrectionTrajectoryFrame& trajectory) const noexcept;
    void advanceTargetPhase(RidgeState& track, float targetFrequencyHz) noexcept;

    OutputPrepareSpec spec_ {};
    std::vector<RidgeState> tracks_;
    std::vector<RidgeObservation> observations_;
    std::vector<std::uint32_t> observationMatched_;
    std::vector<int> sourceBinTrackIndices_;

    int observationCount_ = 0;
    std::uint32_t nextTrackId_ = 1;
    RidgeLedgerDiagnostics diagnostics_ {};
};

} // namespace neumaton::outputv3
