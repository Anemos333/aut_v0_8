#include "NeumatonOutputRenderer.h"
#include "NeumatonRidgeLedger.h"

#include <array>
#include <complex>
#include <cstdint>

int main()
{
    using namespace neumaton::outputv3;

    constexpr int frameSize = 256;
    constexpr int positiveBinCount = frameSize / 2 + 1;

    OutputPrepareSpec spec;
    spec.sampleRate = 48000.0;
    spec.frameSize = frameSize;
    spec.hopSize = frameSize / 4;
    spec.positiveBinCount = positiveBinCount;
    spec.outputRingSize = frameSize * 8;
    spec.maximumRidges = 32;
    spec.maximumObservations = 64;

    NeumatonRidgeLedger ledger;
    NeumatonOutputRenderer renderer;
    ledger.prepare(spec);
    renderer.prepare(spec);

    std::array<std::complex<float>, frameSize> spectrum {};
    std::array<float, positiveBinCount> magnitudes {};
    std::array<float, positiveBinCount> phases {};
    std::array<float, positiveBinCount> previousPhases {};
    std::array<double, positiveBinCount> trueBins {};
    std::array<float, positiveBinCount> harmonicMask {};
    std::array<float, positiveBinCount> envelope {};
    std::array<int, positiveBinCount> nearestPeak {};
    std::array<int, 3> peaks { 2, 4, 6 };

    for (int bin = 0; bin < positiveBinCount; ++bin)
    {
        trueBins[static_cast<std::size_t>(bin)] = static_cast<double>(bin);
        envelope[static_cast<std::size_t>(bin)] = 1.0f;
        nearestPeak[static_cast<std::size_t>(bin)] = bin < 3 ? 2 : (bin < 5 ? 4 : 6);
    }

    magnitudes[2] = 1.0f;
    magnitudes[4] = 0.75f;
    magnitudes[6] = 0.55f;
    harmonicMask[2] = 1.0f;
    harmonicMask[4] = 0.95f;
    harmonicMask[6] = 0.90f;
    spectrum[2] = { 1.0f, 0.0f };
    spectrum[4] = { 0.75f, 0.0f };
    spectrum[6] = { 0.55f, 0.0f };

    AnalysisFrameView analysis;
    analysis.analysedSpectrum = { spectrum.data(), frameSize };
    analysis.magnitudes = { magnitudes.data(), positiveBinCount };
    analysis.analysisPhases = { phases.data(), positiveBinCount };
    analysis.previousAnalysisPhases = { previousPhases.data(), positiveBinCount };
    analysis.trueSourceBins = { trueBins.data(), positiveBinCount };
    analysis.harmonicMask = { harmonicMask.data(), positiveBinCount };
    analysis.spectralEnvelope = { envelope.data(), positiveBinCount };
    analysis.nearestPeak = { nearestPeak.data(), positiveBinCount };
    analysis.peakBins = { peaks.data(), static_cast<int>(peaks.size()) };
    analysis.sampleRate = spec.sampleRate;
    analysis.frameSize = frameSize;
    analysis.hopSize = spec.hopSize;
    analysis.positiveBinCount = positiveBinCount;
    analysis.detectedPitchHz = 375.0f;
    analysis.confidence = 0.95f;
    analysis.voicing = 0.95f;
    analysis.consensus = 0.90f;
    analysis.harmonicity = 0.90f;
    analysis.spectralReliability = 0.90f;
    analysis.maskStability = 0.90f;

    CorrectionTrajectoryFrame trajectory;
    trajectory.previousCorrectionCents = 0.0;
    trajectory.correctionCents = 100.0;
    trajectory.previousTargetPitchHz = 375.0f;
    trajectory.targetPitchHz = 397.290f;
    trajectory.targetRevision = 1;
    trajectory.targetValid = true;

    const auto ridgeFrame = ledger.processFrame(analysis, trajectory);
    const auto preview = renderer.inspectFrame(analysis, trajectory, ridgeFrame);

    if (NeumatonOutputRenderer::isAudioPathEnabled())
        return 1;
    if (preview.spectrum.size() != frameSize)
        return 2;
    if (preview.ownership.size() != positiveBinCount)
        return 3;
    if (ledger.getDiagnostics().observationCount != 3)
        return 4;

    ledger.reset();
    renderer.reset();
    return 0;
}
