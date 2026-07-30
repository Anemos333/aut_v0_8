#pragma once

#include "NeumatonOutputTypes.h"

#include <complex>
#include <cstdint>
#include <vector>

namespace neumaton::outputv3
{

// Scale-cage renderer: every analysed component follows one musical ratio.
class NeumatonOutputRenderer final
{
public:
    NeumatonOutputRenderer() = default;

    void prepare(const OutputPrepareSpec& spec);
    void reset() noexcept;

    [[nodiscard]] OutputSpectrumView inspectFrame(
        const AnalysisFrameView& analysis,
        const CorrectionTrajectoryFrame& trajectory,
        const RidgeLedgerFrameView& ledger,
        float formantPreservation) noexcept;

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

    void classifyForDiagnostics(const AnalysisFrameView& analysis) noexcept;
    void buildFullSpectrum(const AnalysisFrameView& analysis,
                           const CorrectionTrajectoryFrame& trajectory,
                           const RidgeLedgerFrameView& ledger,
                           float formantPreservation) noexcept;
    void depositMappedBin(int sourceBin,
                          double targetPosition,
                          float magnitude,
                          double phase,
                          double phaseSlope) noexcept;
    void completeConjugateSymmetry() noexcept;
    void commitCurrentSpectrum(std::int64_t frameEndSample) noexcept;
    void updateDiagnostics(const AnalysisFrameView& analysis) noexcept;
    void fft(std::vector<Complex>& data, bool inverse) noexcept;

    [[nodiscard]] double correctionRatio(
        const AnalysisFrameView& analysis,
        const CorrectionTrajectoryFrame& trajectory) noexcept;
    [[nodiscard]] double sourcePosition(const AnalysisFrameView& analysis,
                                        int sourceBin) const noexcept;
    [[nodiscard]] int nearestPeakForBin(const AnalysisFrameView& analysis,
                                        int sourceBin) const noexcept;
    [[nodiscard]] int trackForPeak(const RidgeLedgerFrameView& ledger,
                                   int peakBin) const noexcept;
    [[nodiscard]] double synthesisPhase(const AnalysisFrameView& analysis,
                                        const RidgeLedgerFrameView& ledger,
                                        int sourceBin,
                                        int peakBin,
                                        double targetPosition,
                                        bool resetPhase) noexcept;
    [[nodiscard]] double localPhaseSlope(const AnalysisFrameView& analysis,
                                         const RidgeLedgerFrameView& ledger,
                                         int sourceBin,
                                         int peakBin,
                                         double ratio) const noexcept;
    [[nodiscard]] float formantGain(const AnalysisFrameView& analysis,
                                    double sourceBin,
                                    double targetBin,
                                    float amount,
                                    float harmonicEvidence) const noexcept;
    [[nodiscard]] static float interpolate(const ConstArrayView<float>& values,
                                           double position,
                                           float fallback) noexcept;
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
    std::vector<double> freeSynthesisPhase_;
    std::vector<std::uint8_t> freeSynthesisPhaseValid_;
    std::vector<float> destinationDepositedEnergy_;

    OutputRendererDiagnostics diagnostics_ {};
    double lastCorrectionRatio_ = 1.0;
    bool correctionRatioValid_ = false;
    bool previousSpectrumValid_ = false;
};

} // namespace neumaton::outputv3
