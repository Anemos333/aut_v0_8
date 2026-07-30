#pragma once

#include "NeumatonOutputTypes.h"

#include <complex>
#include <cstdint>
#include <vector>

namespace neumaton::outputv3
{

class NeumatonOutputRenderer final
{
public:
    NeumatonOutputRenderer() = default;

    // All callback storage is allocated here.
    void prepare(const OutputPrepareSpec& spec);
    void reset() noexcept;

    // Builds the one and only output spectrum but does not commit it to audio.
    [[nodiscard]] OutputSpectrumView inspectFrame(
        const AnalysisFrameView& analysis,
        const CorrectionTrajectoryFrame& trajectory,
        const RidgeLedgerFrameView& ledger,
        float formantPreservation) noexcept;

    // Builds the same spectrum as inspectFrame(), performs one IFFT and commits
    // it to the renderer-owned overlap-add ring.
    void renderAndCommitFrame(
        const AnalysisFrameView& analysis,
        const CorrectionTrajectoryFrame& trajectory,
        const RidgeLedgerFrameView& ledger,
        float formantPreservation,
        std::int64_t frameEndSample) noexcept;

    [[nodiscard]] float consumeSample(std::int64_t absoluteSample) noexcept;
    void discardSample(std::int64_t absoluteSample) noexcept;

    [[nodiscard]] const OutputRendererDiagnostics& getDiagnostics() const noexcept
    {
        return diagnostics_;
    }

private:
    using Complex = std::complex<float>;

    void classifyOwnership(const AnalysisFrameView& analysis,
                           const RidgeLedgerFrameView& ledger) noexcept;
    void calculateRidgeNormalisation(const AnalysisFrameView& analysis,
                                     const CorrectionTrajectoryFrame& trajectory,
                                     const RidgeLedgerFrameView& ledger,
                                     float formantPreservation) noexcept;
    void buildSpectrum(const AnalysisFrameView& analysis,
                       const CorrectionTrajectoryFrame& trajectory,
                       const RidgeLedgerFrameView& ledger,
                       float formantPreservation) noexcept;
    void finaliseRidgeDestinations(const AnalysisFrameView& analysis) noexcept;
    void completeConjugateSymmetry() noexcept;
    void commitCurrentSpectrum(std::int64_t frameEndSample) noexcept;
    void updateDiagnostics(const AnalysisFrameView& analysis) noexcept;
    void fft(std::vector<Complex>& data, bool inverse) noexcept;

    void depositRidge(int destinationBin,
                      Complex value,
                      int ownerToken,
                      float reliability) noexcept;
    void depositResidual(int destinationBin,
                         Complex value,
                         int ownerToken) noexcept;

    [[nodiscard]] int trackForSourceBin(const AnalysisFrameView& analysis,
                                        const RidgeLedgerFrameView& ledger,
                                        int sourceBin) const noexcept;
    [[nodiscard]] double ridgeRatio(const RidgeState& track,
                                    const CorrectionTrajectoryFrame& trajectory) const noexcept;
    [[nodiscard]] double ridgePhaseAt(const AnalysisFrameView& analysis,
                                      const RidgeState& track,
                                      double sourcePosition,
                                      double targetPosition,
                                      double ratio) const noexcept;
    [[nodiscard]] double regularisedSlope(const AnalysisFrameView& analysis,
                                          const RidgeState& track,
                                          double ratio) const noexcept;
    [[nodiscard]] double temporallyRegularisePhase(const AnalysisFrameView& analysis,
                                                   int destinationBin,
                                                   double candidatePhase,
                                                   float collisionAmount,
                                                   float reliability) const noexcept;
    [[nodiscard]] float formantGain(const AnalysisFrameView& analysis,
                                    double sourceBin,
                                    double targetBin,
                                    float amount) const noexcept;
    [[nodiscard]] float shortFrameAmount() const noexcept;
    [[nodiscard]] static float interpolate(const ConstArrayView<float>& values,
                                           double position,
                                           float fallback) noexcept;
    [[nodiscard]] static double unwrapNear(double value, double reference) noexcept;
    [[nodiscard]] static double wrapPhase(double phase) noexcept;
    [[nodiscard]] static float clamp01(float value) noexcept;
    [[nodiscard]] static int nextPowerOfTwo(int value) noexcept;

    OutputPrepareSpec spec_ {};
    int outputRingMask_ = 0;
    float synthesisGain_ = 1.0f;

    std::vector<Complex> spectrum_;
    std::vector<Complex> previousSpectrum_;
    std::vector<Complex> timeFrame_;
    std::vector<float> outputAccumulationRing_;
    std::vector<float> synthesisWindow_;
    std::vector<int> fftBitReversal_;
    std::vector<Complex> fftTwiddles_;

    std::vector<BinOwnership> ownership_;
    std::vector<float> ridgeSourceEnergy_;
    std::vector<float> ridgePredictedEnergy_;
    std::vector<float> ridgeNormalisationGain_;

    // Destination-domain accumulators. Contributions from one ridge remain
    // complex-coherent. Contributions from different ridges are recomposed with
    // conserved energy and one phase field, rather than being allowed to create
    // a second voice or a collision-dependent flanger.
    std::vector<Complex> destinationRidgeComplex_;
    std::vector<float> destinationRidgeEnergy_;
    std::vector<Complex> destinationRidgeAnchor_;
    std::vector<float> destinationRidgeAnchorEnergy_;
    std::vector<float> destinationRidgeReliability_;
    std::vector<int> destinationRidgeOwner_;
    std::vector<int> destinationRidgeContributionCount_;

    std::vector<int> destinationOwnerToken_;
    std::vector<float> destinationEnergy_;
    std::vector<float> destinationCollisionEnergy_;

    OutputRendererDiagnostics diagnostics_ {};
    bool previousSpectrumValid_ = false;
};

} // namespace neumaton::outputv3
