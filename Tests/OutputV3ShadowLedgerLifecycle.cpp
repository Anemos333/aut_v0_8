#include "NeumatonRidgeLedger.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>

namespace
{
std::atomic<std::uint64_t> guardedAllocations { 0 };
thread_local bool allocationGuardActive = false;
constexpr double twoPi = 6.283185307179586476925286766559;

struct Guard final
{
    Guard() noexcept { allocationGuardActive = true; }
    ~Guard() { allocationGuardActive = false; }
};

void countAllocation() noexcept
{
    if (allocationGuardActive)
        guardedAllocations.fetch_add(1u, std::memory_order_relaxed);
}

struct SyntheticFrame final
{
    static constexpr int frameSize = 256;
    static constexpr int positiveBins = 129;
    static constexpr int hopSize = 64;
    static constexpr double sampleRate = 48000.0;

    std::vector<std::complex<float>> spectrum = std::vector<std::complex<float>>(frameSize);
    std::vector<float> magnitudes = std::vector<float>(positiveBins, 0.0f);
    std::vector<float> phases = std::vector<float>(positiveBins, 0.0f);
    std::vector<float> previousPhases = std::vector<float>(positiveBins, 0.0f);
    std::vector<double> trueBins = std::vector<double>(positiveBins, 0.0);
    std::vector<float> harmonicMask = std::vector<float>(positiveBins, 0.0f);
    std::vector<float> envelope = std::vector<float>(positiveBins, 1.0f);
    std::vector<int> nearestPeak = std::vector<int>(positiveBins, 0);
    std::vector<int> peaks;
    std::int64_t frameEnd = frameSize - 1;
    double phaseA = 0.0;
    double phaseB = 0.7;

    SyntheticFrame()
    {
        for (int bin = 0; bin < positiveBins; ++bin)
            trueBins[static_cast<std::size_t>(bin)] = static_cast<double>(bin);
        peaks.reserve(8);
    }

    neumaton::outputv3::AnalysisFrameView make(bool includeA,
                                                bool includeB,
                                                float binA,
                                                float binB,
                                                bool onset = false)
    {
        previousPhases = phases;
        std::fill(spectrum.begin(), spectrum.end(), std::complex<float> {});
        std::fill(magnitudes.begin(), magnitudes.end(), 1.0e-7f);
        std::fill(harmonicMask.begin(), harmonicMask.end(), 0.0f);
        peaks.clear();

        const auto place = [&](float binPosition,
                               float amplitude,
                               double phase,
                               bool enabled)
        {
            if (!enabled)
                return;
            const int bin = std::clamp(static_cast<int>(std::lround(binPosition)),
                                       2, positiveBins - 3);
            peaks.push_back(bin);
            trueBins[static_cast<std::size_t>(bin)] = binPosition;
            magnitudes[static_cast<std::size_t>(bin)] = amplitude;
            magnitudes[static_cast<std::size_t>(bin - 1)] = 0.20f * amplitude;
            magnitudes[static_cast<std::size_t>(bin + 1)] = 0.18f * amplitude;
            phases[static_cast<std::size_t>(bin)] = static_cast<float>(phase);
            phases[static_cast<std::size_t>(bin - 1)] = static_cast<float>(phase - 0.20);
            phases[static_cast<std::size_t>(bin + 1)] = static_cast<float>(phase + 0.20);
            harmonicMask[static_cast<std::size_t>(bin)] = 1.0f;
            harmonicMask[static_cast<std::size_t>(bin - 1)] = 0.92f;
            harmonicMask[static_cast<std::size_t>(bin + 1)] = 0.92f;
        };

        place(binA, 1.0f, phaseA, includeA);
        place(binB, 0.82f, phaseB, includeB);
        std::sort(peaks.begin(), peaks.end());

        for (int bin = 0; bin < positiveBins; ++bin)
        {
            int best = peaks.empty() ? 0 : peaks.front();
            for (const int peak : peaks)
            {
                if (std::abs(peak - bin) < std::abs(best - bin))
                    best = peak;
            }
            nearestPeak[static_cast<std::size_t>(bin)] = best;
        }

        neumaton::outputv3::AnalysisFrameView view;
        view.analysedSpectrum = { spectrum.data(), frameSize };
        view.magnitudes = { magnitudes.data(), positiveBins };
        view.analysisPhases = { phases.data(), positiveBins };
        view.previousAnalysisPhases = { previousPhases.data(), positiveBins };
        view.trueSourceBins = { trueBins.data(), positiveBins };
        view.harmonicMask = { harmonicMask.data(), positiveBins };
        view.spectralEnvelope = { envelope.data(), positiveBins };
        view.nearestPeak = { nearestPeak.data(), positiveBins };
        view.peakBins = { peaks.empty() ? nullptr : peaks.data(),
                          static_cast<int>(peaks.size()) };
        view.sampleRate = sampleRate;
        view.frameSize = frameSize;
        view.hopSize = hopSize;
        view.positiveBinCount = positiveBins;
        view.frameEndSample = frameEnd;
        view.detectedPitchHz = 187.5f;
        view.confidence = 0.95f;
        view.voicing = 0.96f;
        view.consensus = 0.94f;
        view.onsetStrength = onset ? 0.8f : 0.0f;
        view.harmonicity = 0.95f;
        view.spectralReliability = 0.96f;
        view.maskStability = 0.95f;
        return view;
    }

    void advance(float binA, float binB)
    {
        const double binWidth = sampleRate / static_cast<double>(frameSize);
        phaseA = std::remainder(phaseA + twoPi * static_cast<double>(hopSize)
            * static_cast<double>(binA) * binWidth / sampleRate, twoPi);
        phaseB = std::remainder(phaseB + twoPi * static_cast<double>(hopSize)
            * static_cast<double>(binB) * binWidth / sampleRate, twoPi);
        frameEnd += hopSize;
    }
};

std::uint32_t trackIdNear(const neumaton::outputv3::RidgeLedgerFrameView& view,
                          int peakBin)
{
    for (int index = 0; index < view.tracks.size(); ++index)
    {
        const auto& track = view.tracks[index];
        if (track.lifeState != neumaton::outputv3::RidgeLifeState::inactive
            && std::abs(track.lastPeakBin - peakBin) <= 1)
            return track.id;
    }
    return 0u;
}
} // namespace

void* operator new(std::size_t size)
{
    countAllocation();
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    countAllocation();
    if (void* memory = std::malloc(size))
        return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

int main()
{
    using namespace neumaton::outputv3;

    OutputPrepareSpec spec;
    spec.sampleRate = SyntheticFrame::sampleRate;
    spec.frameSize = SyntheticFrame::frameSize;
    spec.hopSize = SyntheticFrame::hopSize;
    spec.positiveBinCount = SyntheticFrame::positiveBins;
    spec.maximumRidges = 24;
    spec.maximumObservations = 32;

    NeumatonRidgeLedger ledger;
    ledger.prepare(spec);

    CorrectionTrajectoryFrame trajectory;
    trajectory.previousCorrectionCents = 100.0;
    trajectory.correctionCents = 100.0;
    trajectory.previousTargetPitchHz = 198.4f;
    trajectory.targetPitchHz = 198.4f;
    trajectory.targetRevision = 1;
    trajectory.targetValid = true;

    SyntheticFrame frame;
    std::uint32_t idA = 0u;
    std::uint32_t idB = 0u;
    int totalDeaths = 0;
    int totalSwitches = 0;
    float maximumPredictionError = 0.0f;
    float maximumCoverage = 0.0f;

    for (int hop = 0; hop < 12; ++hop)
    {
        const float binA = 10.0f + 0.012f * static_cast<float>(hop);
        const float binB = 17.0f - 0.010f * static_cast<float>(hop);
        const bool includeA = hop != 6 && hop != 7;
        const auto analysis = frame.make(includeA, true, binA, binB, false);
        RidgeLedgerFrameView view;
        {
            Guard guard;
            view = ledger.processFrame(analysis, trajectory);
        }

        const auto diagnostics = ledger.getDiagnostics();
        totalDeaths += diagnostics.deadTrackCount;
        totalSwitches += diagnostics.identitySwitchCount;
        maximumPredictionError = std::max(maximumPredictionError,
                                          diagnostics.meanPredictionErrorRadians);
        maximumCoverage = std::max(maximumCoverage,
                                   diagnostics.resolvedBinCoverage);

        const std::uint32_t currentB = trackIdNear(view, 17);
        if (hop == 0)
        {
            idA = trackIdNear(view, 10);
            idB = currentB;
            if (idA == 0u || idB == 0u || idA == idB)
                return 10;
        }
        else if (currentB != idB)
        {
            return 11;
        }

        if (includeA && hop > 0 && trackIdNear(view, 10) != idA)
            return 12;

        frame.advance(binA, binB);
    }

    for (int hop = 0; hop < 6; ++hop)
    {
        const auto analysis = frame.make(false, false, 10.0f, 17.0f);
        {
            Guard guard;
            static_cast<void>(ledger.processFrame(analysis, trajectory));
        }
        totalDeaths += ledger.getDiagnostics().deadTrackCount;
        frame.advance(10.0f, 17.0f);
    }

    if (ledger.getDiagnostics().activeTrackCount != 0)
        return 13;
    if (totalDeaths < 2)
        return 14;
    if (totalSwitches != 0)
        return 15;
    if (maximumPredictionError > 0.75f)
        return 16;
    if (maximumCoverage < 0.90f)
        return 17;
    if (guardedAllocations.load(std::memory_order_relaxed) != 0u)
        return 18;

    std::cout << "shadow lifecycle ok"
              << " deaths=" << totalDeaths
              << " max_prediction_error=" << maximumPredictionError
              << " max_coverage=" << maximumCoverage << '\n';
    return 0;
}
