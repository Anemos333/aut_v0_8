#pragma once

#include "NeumatonOutputTypes.h"

#include <complex>
#include <vector>

namespace neumaton::outputv3
{

class NeumatonOutputRenderer final
{
public:
    NeumatonOutputRenderer() = default;

    // Allocates the future spectrum, IFFT frame and OLA ring. The current
    // scaffold never commits them to audio; it is compiled only to stabilise the
    // boundary before the observed tracker is validated.
    void prepare(const OutputPrepareSpec& spec);
    void reset() noexcept;

    // Builds a non-audible ownership/spectrum preview. The returned spectrum is
    // diagnostic only and must not be consumed by ModernPitchEngine yet.
    [[nodiscard]] OutputSpectrumView inspectFrame(
        const AnalysisFrameView& analysis,
        const CorrectionTrajectoryFrame& trajectory,
        const RidgeLedgerFrameView& ledger) noexcept;

    [[nodiscard]] const OutputRendererDiagnostics& getDiagnostics() const noexcept
    {
        return diagnostics_;
    }

    [[nodiscard]] static constexpr bool isAudioPathEnabled() noexcept
    {
        return false;
    }

private:
    void classifyOwnership(const AnalysisFrameView& analysis,
                           const RidgeLedgerFrameView& ledger) noexcept;
    void buildDiagnosticSpectrum(const AnalysisFrameView& analysis,
                                 const CorrectionTrajectoryFrame& trajectory,
                                 const RidgeLedgerFrameView& ledger) noexcept;
    void updateDiagnostics(const AnalysisFrameView& analysis) noexcept;

    [[nodiscard]] static double wrapPhase(double phase) noexcept;

    OutputPrepareSpec spec_ {};
    std::vector<std::complex<float>> spectrum_;
    std::vector<std::complex<float>> ifftFrame_;
    std::vector<float> outputAccumulationRing_;
    std::vector<float> synthesisWindow_;
    std::vector<BinOwnership> ownership_;
    OutputRendererDiagnostics diagnostics_ {};
};

} // namespace neumaton::outputv3
