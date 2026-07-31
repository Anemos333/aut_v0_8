#include "NeumatonOutputRenderer.h"
#include "NeumatonRidgeLedger.h"

#include <atomic>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

namespace
{
std::atomic<unsigned long long> guardedAllocations { 0 };
thread_local bool guardActive = false;
struct Guard { Guard() { guardActive = true; } ~Guard() { guardActive = false; } };
void countAllocation() noexcept { if (guardActive) ++guardedAllocations; }
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
}

void* operator new(std::size_t size)
{
    countAllocation();
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    countAllocation();
    if (void* memory = std::malloc(size)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main()
{
    using namespace neumaton::outputv3;
    constexpr int frameSize = 256;
    constexpr int hopSize = 64;
    constexpr int positiveBins = frameSize / 2 + 1;
    constexpr double sampleRate = 48000.0;

    OutputPrepareSpec spec;
    spec.sampleRate = sampleRate;
    spec.frameSize = frameSize;
    spec.hopSize = hopSize;
    spec.positiveBinCount = positiveBins;
    spec.outputRingSize = 2048;
    spec.maximumRidges = 96;
    spec.maximumObservations = positiveBins;

    NeumatonRidgeLedger ledger;
    NeumatonOutputRenderer renderer;
    ledger.prepare(spec);
    renderer.prepare(spec);

    std::vector<std::complex<float>> analysed(frameSize);
    std::vector<float> magnitudes(positiveBins);
    std::vector<float> phases(positiveBins);
    std::vector<float> previousPhases(positiveBins);
    std::vector<double> trueBins(positiveBins);
    std::vector<float> harmonicMask(positiveBins);
    std::vector<float> envelope(positiveBins, 1.0f);
    std::vector<int> nearestPeak(positiveBins);
    std::vector<int> peaks;
    peaks.reserve(16);

    // Fundamental scale-cage invariant: ownership may change reconstruction,
    // but neither tonal nor aperiodic energy may remain at its source frequency.
    const auto verifyCompleteTransport = [&](int sourceBin,
                                             float harmonicEvidence,
                                             float breathiness) -> bool
    {
        renderer.reset();
        std::fill(analysed.begin(), analysed.end(), std::complex<float> {});
        std::fill(magnitudes.begin(), magnitudes.end(), 0.0f);
        std::fill(phases.begin(), phases.end(), 0.0f);
        std::fill(previousPhases.begin(), previousPhases.end(), 0.0f);
        std::fill(harmonicMask.begin(), harmonicMask.end(), 0.0f);
        std::fill(envelope.begin(), envelope.end(), 1.0f);
        peaks.clear();

        for (int bin = 0; bin < positiveBins; ++bin)
        {
            trueBins[static_cast<std::size_t>(bin)] = static_cast<double>(bin);
            nearestPeak[static_cast<std::size_t>(bin)] = sourceBin;
        }

        analysed[static_cast<std::size_t>(sourceBin)] = { 1.0f, 0.0f };
        magnitudes[static_cast<std::size_t>(sourceBin)] = 1.0f;
        harmonicMask[static_cast<std::size_t>(sourceBin)] = harmonicEvidence;
        peaks.push_back(sourceBin);

        AnalysisFrameView analysis;
        analysis.analysedSpectrum = { analysed.data(), frameSize };
        analysis.magnitudes = { magnitudes.data(), positiveBins };
        analysis.analysisPhases = { phases.data(), positiveBins };
        analysis.previousAnalysisPhases = { previousPhases.data(), positiveBins };
        analysis.trueSourceBins = { trueBins.data(), positiveBins };
        analysis.harmonicMask = { harmonicMask.data(), positiveBins };
        analysis.spectralEnvelope = { envelope.data(), positiveBins };
        analysis.nearestPeak = { nearestPeak.data(), positiveBins };
        analysis.peakBins = { peaks.data(), static_cast<int>(peaks.size()) };
        analysis.sampleRate = sampleRate;
        analysis.frameSize = frameSize;
        analysis.hopSize = hopSize;
        analysis.positiveBinCount = positiveBins;
        analysis.frameEndSample = frameSize - 1;
        analysis.detectedPitchHz = static_cast<float>(sourceBin * sampleRate / frameSize);
        analysis.confidence = harmonicEvidence > 0.5f ? 0.98f : 0.05f;
        analysis.voicing = harmonicEvidence > 0.5f ? 0.98f : 0.03f;
        analysis.consensus = harmonicEvidence > 0.5f ? 0.96f : 0.02f;
        analysis.harmonicity = harmonicEvidence;
        analysis.breathiness = breathiness;
        analysis.onsetStrength = breathiness > 0.5f ? 0.85f : 0.0f;
        analysis.spectralReliability = harmonicEvidence > 0.5f ? 0.96f : 0.08f;
        analysis.maskStability = 0.95f;
        analysis.phaseReset = true;

        constexpr double correctionCents = 500.0;
        const double ratio = std::exp2(correctionCents / 1200.0);
        CorrectionTrajectoryFrame trajectory;
        trajectory.previousCorrectionCents = correctionCents;
        trajectory.correctionCents = correctionCents;
        trajectory.previousTargetPitchHz = static_cast<float>(
            analysis.detectedPitchHz * ratio);
        trajectory.targetPitchHz = trajectory.previousTargetPitchHz;
        trajectory.targetRevision = 1;
        trajectory.targetValid = true;
        trajectory.forceReset = true;

        RidgeLedgerFrameView noTracks;
        OutputSpectrumView preview;
        {
            Guard guard;
            preview = renderer.inspectFrame(analysis, trajectory, noTracks, 0.0f);
        }

        const double targetPosition = static_cast<double>(sourceBin) * ratio;
        const int supportFirst = static_cast<int>(std::floor(targetPosition)) - 1;
        const int supportLast = static_cast<int>(std::floor(targetPosition)) + 2;
        double totalEnergy = 0.0;
        double supportEnergy = 0.0;
        double outsideEnergy = 0.0;
        for (int bin = 0; bin < positiveBins; ++bin)
        {
            const double energy = std::norm(preview.spectrum[bin]);
            totalEnergy += energy;
            if (bin >= supportFirst && bin <= supportLast)
                supportEnergy += energy;
            else
                outsideEnergy += energy;
        }

        const double sourceEnergy = std::norm(preview.spectrum[sourceBin]);
        return totalEnergy > 0.25
            && supportEnergy > 0.25
            && sourceEnergy < 1.0e-12
            && outsideEnergy < 1.0e-10;
    };

    const bool tonalTransportComplete = verifyCompleteTransport(20, 1.0f, 0.0f);
    const bool breathTransportComplete = verifyCompleteTransport(70, 0.0f, 1.0f);
    if (!tonalTransportComplete) return 7;
    if (!breathTransportComplete) return 8;

    renderer.reset();
    double outputEnergy = 0.0;
    int validFrames = 0;
    int activeTracks = 0;
    float worstGainDb = 0.0f;

    for (int frame = 0; frame < 80; ++frame)
    {
        std::fill(analysed.begin(), analysed.end(), std::complex<float> {});
        std::fill(magnitudes.begin(), magnitudes.end(), 0.0f);
        std::fill(phases.begin(), phases.end(), 0.0f);
        std::fill(trueBins.begin(), trueBins.end(), 0.0);
        std::fill(harmonicMask.begin(), harmonicMask.end(), 0.05f);
        peaks.clear();

        const double f0 = 220.0 * std::exp2(
            12.0 * std::sin(twoPi * 5.0 * frame * hopSize / sampleRate) / 1200.0);
        for (int harmonic = 1; harmonic <= 8; ++harmonic)
        {
            const double frequency = f0 * harmonic;
            const double binPosition = frequency * frameSize / sampleRate;
            const int bin = static_cast<int>(std::lround(binPosition));
            if (bin <= 1 || bin >= positiveBins - 1) continue;
            const float amplitude = static_cast<float>(1.0 / harmonic);
            const double phase = twoPi * frequency * frame * hopSize / sampleRate
                               + 0.13 * harmonic;
            const std::complex<float> value(
                amplitude * static_cast<float>(std::cos(phase)),
                amplitude * static_cast<float>(std::sin(phase)));
            analysed[static_cast<std::size_t>(bin)] += value;
            magnitudes[static_cast<std::size_t>(bin)] = std::abs(
                analysed[static_cast<std::size_t>(bin)]);
            phases[static_cast<std::size_t>(bin)] = std::arg(
                analysed[static_cast<std::size_t>(bin)]);
            trueBins[static_cast<std::size_t>(bin)] = binPosition;
            harmonicMask[static_cast<std::size_t>(bin)] = 1.0f;
            envelope[static_cast<std::size_t>(bin)] = 1.0f + 0.08f * harmonic;
            peaks.push_back(bin);
        }

        for (int bin = 0; bin < positiveBins; ++bin)
        {
            int best = peaks.empty() ? 0 : peaks.front();
            int bestDistance = 9999;
            for (int peak : peaks)
            {
                const int distance = std::abs(bin - peak);
                if (distance < bestDistance) { bestDistance = distance; best = peak; }
            }
            nearestPeak[static_cast<std::size_t>(bin)] = best;
            if (trueBins[static_cast<std::size_t>(bin)] == 0.0)
                trueBins[static_cast<std::size_t>(bin)] = static_cast<double>(bin);
        }

        AnalysisFrameView analysis;
        analysis.analysedSpectrum = { analysed.data(), frameSize };
        analysis.magnitudes = { magnitudes.data(), positiveBins };
        analysis.analysisPhases = { phases.data(), positiveBins };
        analysis.previousAnalysisPhases = { previousPhases.data(), positiveBins };
        analysis.trueSourceBins = { trueBins.data(), positiveBins };
        analysis.harmonicMask = { harmonicMask.data(), positiveBins };
        analysis.spectralEnvelope = { envelope.data(), positiveBins };
        analysis.nearestPeak = { nearestPeak.data(), positiveBins };
        analysis.peakBins = { peaks.data(), static_cast<int>(peaks.size()) };
        analysis.sampleRate = sampleRate;
        analysis.frameSize = frameSize;
        analysis.hopSize = hopSize;
        analysis.positiveBinCount = positiveBins;
        analysis.frameEndSample = frame * hopSize + frameSize - 1;
        analysis.detectedPitchHz = static_cast<float>(f0);
        analysis.confidence = 0.98f;
        analysis.voicing = 0.98f;
        analysis.consensus = 0.95f;
        analysis.harmonicity = 0.96f;
        analysis.spectralReliability = 0.96f;
        analysis.maskStability = 0.95f;
        analysis.phaseReset = frame == 0;

        CorrectionTrajectoryFrame trajectory;
        trajectory.previousCorrectionCents = 200.0;
        trajectory.correctionCents = 200.0;
        trajectory.previousTargetPitchHz = static_cast<float>(
            f0 * std::exp2(200.0 / 1200.0));
        trajectory.targetPitchHz = trajectory.previousTargetPitchHz;
        trajectory.targetRevision = 1;
        trajectory.targetValid = true;
        trajectory.forceReset = frame == 0;

        RidgeLedgerFrameView ridgeFrame;
        {
            Guard guard;
            ridgeFrame = ledger.processFrame(analysis, trajectory);
            renderer.renderAndCommitFrame(analysis,
                                          trajectory,
                                          ridgeFrame,
                                          0.9f,
                                          analysis.frameEndSample);
        }

        const auto& diagnostics = renderer.getDiagnostics();
        validFrames += diagnostics.frameValid ? 1 : 0;
        activeTracks = std::max(activeTracks, ridgeFrame.activeTrackCount);
        worstGainDb = std::max(worstGainDb,
                               std::abs(diagnostics.requestedEnergyGainDb));

        const std::int64_t firstOutputSample = analysis.frameEndSample + 1;
        for (int sample = 0; sample < hopSize; ++sample)
        {
            const float value = renderer.consumeSample(firstOutputSample + sample);
            if (!std::isfinite(value)) return 2;
            outputEnergy += static_cast<double>(value) * value;
        }
        previousPhases = phases;
    }

    std::cout << "allocations=" << guardedAllocations.load() << '\n'
              << "tonal_transport_complete=" << tonalTransportComplete << '\n'
              << "breath_transport_complete=" << breathTransportComplete << '\n'
              << "valid_frames=" << validFrames << '\n'
              << "active_tracks=" << activeTracks << '\n'
              << "output_energy=" << outputEnergy << '\n'
              << "worst_requested_gain_db=" << worstGainDb << '\n';

    if (guardedAllocations.load() != 0) return 3;
    if (validFrames < 60 || activeTracks < 2) return 4;
    if (!(outputEnergy > 1.0e-5)) return 5;
    if (!(worstGainDb < 12.0f)) return 6;
    return 0;
}
