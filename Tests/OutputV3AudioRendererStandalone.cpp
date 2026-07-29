#include "NeumatonOutputRenderer.h"
#include "NeumatonRidgeLedger.h"

#include <algorithm>
#include <array>
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
constexpr double sampleRate = 48000.0;

struct CaseResult
{
    int frameSize = 0;
    int validFrames = 0;
    int maximumActiveTracks = 0;
    double outputEnergy = 0.0;
    float maximumAbsoluteSample = 0.0f;
    float worstRequestedGainDb = 0.0f;
    double meanOlaCoherence = 0.0;
    double meanCollisionRatio = 0.0;
};

CaseResult runCase(int frameSize)
{
    using namespace neumaton::outputv3;

    const int hopSize = frameSize / 4;
    const int positiveBins = frameSize / 2 + 1;

    OutputPrepareSpec spec;
    spec.sampleRate = sampleRate;
    spec.frameSize = frameSize;
    spec.hopSize = hopSize;
    spec.positiveBinCount = positiveBins;
    spec.outputRingSize = 4096;
    spec.maximumRidges = 96;
    spec.maximumObservations = positiveBins;

    NeumatonRidgeLedger ledger;
    NeumatonOutputRenderer renderer;
    ledger.prepare(spec);
    renderer.prepare(spec);

    std::vector<std::complex<float>> analysed(static_cast<std::size_t>(frameSize));
    std::vector<float> magnitudes(static_cast<std::size_t>(positiveBins));
    std::vector<float> phases(static_cast<std::size_t>(positiveBins));
    std::vector<float> previousPhases(static_cast<std::size_t>(positiveBins));
    std::vector<double> trueBins(static_cast<std::size_t>(positiveBins));
    std::vector<float> harmonicMask(static_cast<std::size_t>(positiveBins));
    std::vector<float> envelope(static_cast<std::size_t>(positiveBins), 1.0f);
    std::vector<int> nearestPeak(static_cast<std::size_t>(positiveBins));
    std::vector<int> peaks;
    peaks.reserve(static_cast<std::size_t>(positiveBins));

    CaseResult result;
    result.frameSize = frameSize;
    double olaSum = 0.0;
    double collisionSum = 0.0;
    int diagnosticFrames = 0;

    for (int frame = 0; frame < 120; ++frame)
    {
        std::fill(analysed.begin(), analysed.end(), std::complex<float> {});
        std::fill(magnitudes.begin(), magnitudes.end(), 0.0f);
        std::fill(phases.begin(), phases.end(), 0.0f);
        std::fill(trueBins.begin(), trueBins.end(), 0.0);
        std::fill(harmonicMask.begin(), harmonicMask.end(), 0.035f);
        std::fill(envelope.begin(), envelope.end(), 1.0f);
        peaks.clear();

        const double time = static_cast<double>(frame * hopSize) / sampleRate;
        const double f0 = 215.0 * std::exp2(
            (18.0 * std::sin(twoPi * 5.2 * time)
             + 7.0 * std::sin(twoPi * 0.63 * time)) / 1200.0);
        const double binWidth = sampleRate / static_cast<double>(frameSize);

        const auto addPartial = [&](double frequency,
                                    float amplitude,
                                    double phaseOffset,
                                    float harmonicEvidence)
        {
            const double binPosition = frequency * static_cast<double>(frameSize) / sampleRate;
            const int bin = static_cast<int>(std::lround(binPosition));
            if (bin <= 0 || bin >= positiveBins - 1)
                return;

            const double phase = twoPi * frequency * time + phaseOffset;
            const std::complex<float> value(
                amplitude * static_cast<float>(std::cos(phase)),
                amplitude * static_cast<float>(std::sin(phase)));
            analysed[static_cast<std::size_t>(bin)] += value;
            magnitudes[static_cast<std::size_t>(bin)] = std::abs(
                analysed[static_cast<std::size_t>(bin)]);
            phases[static_cast<std::size_t>(bin)] = std::arg(
                analysed[static_cast<std::size_t>(bin)]);
            trueBins[static_cast<std::size_t>(bin)] = binPosition;
            harmonicMask[static_cast<std::size_t>(bin)] = std::max(
                harmonicMask[static_cast<std::size_t>(bin)], harmonicEvidence);
            envelope[static_cast<std::size_t>(bin)] = 1.0f
                + 0.12f * static_cast<float>(frequency / std::max(1.0, f0));
            if (std::find(peaks.begin(), peaks.end(), bin) == peaks.end())
                peaks.push_back(bin);
        };

        for (int harmonic = 1; harmonic <= 10; ++harmonic)
        {
            const double frequency = f0 * static_cast<double>(harmonic);
            const float amplitude = static_cast<float>(1.0 / static_cast<double>(harmonic));
            addPartial(frequency, amplitude, 0.11 * harmonic, 0.98f);

            // A weaker neighbour creates destination collisions at short FFT
            // sizes without changing the intended monophonic target.
            if (harmonic >= 2 && harmonic <= 7)
            {
                addPartial(frequency + 0.72 * binWidth,
                           0.17f * amplitude,
                           0.37 + 0.09 * harmonic,
                           0.72f);
            }
        }

        // Sparse unvoiced/event energy must remain part of the same spectrum,
        // but must not become a pitch-conflicting parallel layer.
        const int eventBin = std::min(positiveBins - 2,
            std::max(2, static_cast<int>(4200.0 / binWidth)));
        if ((frame % 19) < 2)
        {
            const float eventAmplitude = 0.055f;
            const double eventPhase = 0.73 * frame;
            analysed[static_cast<std::size_t>(eventBin)] += std::complex<float>(
                eventAmplitude * static_cast<float>(std::cos(eventPhase)),
                eventAmplitude * static_cast<float>(std::sin(eventPhase)));
            magnitudes[static_cast<std::size_t>(eventBin)] = std::abs(
                analysed[static_cast<std::size_t>(eventBin)]);
            phases[static_cast<std::size_t>(eventBin)] = std::arg(
                analysed[static_cast<std::size_t>(eventBin)]);
            trueBins[static_cast<std::size_t>(eventBin)] = static_cast<double>(eventBin);
            harmonicMask[static_cast<std::size_t>(eventBin)] = 0.02f;
        }

        std::sort(peaks.begin(), peaks.end());
        for (int bin = 0; bin < positiveBins; ++bin)
        {
            int best = peaks.empty() ? 0 : peaks.front();
            int bestDistance = 9999;
            for (int peak : peaks)
            {
                const int distance = std::abs(bin - peak);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    best = peak;
                }
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
        analysis.onsetStrength = (frame % 19) < 2 ? 0.72f : 0.02f;
        analysis.breathiness = 0.12f;
        analysis.harmonicity = 0.96f;
        analysis.spectralReliability = 0.96f;
        analysis.maskStability = 0.95f;
        analysis.phaseReset = frame == 0;

        const double correctionCents = 205.0
            + 22.0 * std::sin(twoPi * 1.3 * time);
        CorrectionTrajectoryFrame trajectory;
        trajectory.previousCorrectionCents = correctionCents;
        trajectory.correctionCents = correctionCents;
        trajectory.previousTargetPitchHz = static_cast<float>(
            f0 * std::exp2(correctionCents / 1200.0));
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
                                          0.90f,
                                          analysis.frameEndSample);
        }

        const auto& diagnostics = renderer.getDiagnostics();
        result.validFrames += diagnostics.frameValid ? 1 : 0;
        result.maximumActiveTracks = std::max(
            result.maximumActiveTracks, ridgeFrame.activeTrackCount);
        result.worstRequestedGainDb = std::max(
            result.worstRequestedGainDb,
            std::abs(diagnostics.requestedEnergyGainDb));
        if (diagnostics.frameValid && frame > 2)
        {
            olaSum += diagnostics.overlapAddCoherence;
            collisionSum += diagnostics.destinationCollisionEnergyRatio;
            ++diagnosticFrames;
        }

        const std::int64_t firstOutputSample = analysis.frameEndSample + 1;
        for (int sample = 0; sample < hopSize; ++sample)
        {
            const float value = renderer.consumeSample(firstOutputSample + sample);
            if (!std::isfinite(value))
            {
                result.outputEnergy = -1.0;
                return result;
            }
            result.maximumAbsoluteSample = std::max(
                result.maximumAbsoluteSample, std::abs(value));
            result.outputEnergy += static_cast<double>(value) * value;
        }
        previousPhases = phases;
    }

    if (diagnosticFrames > 0)
    {
        result.meanOlaCoherence = olaSum / diagnosticFrames;
        result.meanCollisionRatio = collisionSum / diagnosticFrames;
    }
    return result;
}
} // namespace

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
    const std::array<int, 3> frameSizes { 128, 256, 512 };
    for (const int frameSize : frameSizes)
    {
        const CaseResult result = runCase(frameSize);
        std::cout << "frame_size=" << result.frameSize << '\n'
                  << "valid_frames=" << result.validFrames << '\n'
                  << "active_tracks=" << result.maximumActiveTracks << '\n'
                  << "output_energy=" << result.outputEnergy << '\n'
                  << "maximum_absolute_sample=" << result.maximumAbsoluteSample << '\n'
                  << "worst_requested_gain_db=" << result.worstRequestedGainDb << '\n'
                  << "mean_ola_coherence=" << result.meanOlaCoherence << '\n'
                  << "mean_collision_ratio=" << result.meanCollisionRatio << '\n';

        if (result.validFrames < 90) return 2;
        if (result.maximumActiveTracks < 2) return 3;
        if (!(result.outputEnergy > 1.0e-6)) return 4;
        if (!(result.maximumAbsoluteSample < 8.0f)) return 5;
        if (!(result.worstRequestedGainDb < 18.0f)) return 6;
        if (!(result.meanOlaCoherence >= 0.0 && result.meanOlaCoherence <= 1.0)) return 7;
        if (!(result.meanCollisionRatio >= 0.0 && result.meanCollisionRatio < 8.0)) return 8;
    }

    std::cout << "allocations=" << guardedAllocations.load() << '\n';
    if (guardedAllocations.load() != 0) return 9;
    return 0;
}
