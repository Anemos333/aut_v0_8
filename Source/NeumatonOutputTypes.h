#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>

namespace neumaton::outputv3
{

template <typename T>
class ConstArrayView final
{
public:
    constexpr ConstArrayView() noexcept = default;
    constexpr ConstArrayView(const T* data, int size) noexcept
        : data_(data), size_(size > 0 ? size : 0)
    {
    }

    [[nodiscard]] constexpr const T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr int size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr const T& operator[](int index) const noexcept
    {
        return data_[static_cast<std::size_t>(index)];
    }

private:
    const T* data_ = nullptr;
    int size_ = 0;
};

template <typename T>
class ArrayView final
{
public:
    constexpr ArrayView() noexcept = default;
    constexpr ArrayView(T* data, int size) noexcept
        : data_(data), size_(size > 0 ? size : 0)
    {
    }

    [[nodiscard]] constexpr T* data() const noexcept { return data_; }
    [[nodiscard]] constexpr int size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr T& operator[](int index) const noexcept
    {
        return data_[static_cast<std::size_t>(index)];
    }

private:
    T* data_ = nullptr;
    int size_ = 0;
};

enum class BinOwnership : std::uint8_t
{
    unclassified = 0,
    ridge,
    event,
    air
};

enum class RidgeLifeState : std::uint8_t
{
    inactive = 0,
    pending,
    active,
    coasting
};

struct OutputPrepareSpec
{
    double sampleRate = 48000.0;
    int frameSize = 256;
    int hopSize = 64;
    int positiveBinCount = 129; // DC through Nyquist, inclusive.
    int outputRingSize = 2048;
    int maximumRidges = 96;
    int maximumObservations = 128;
};

struct AnalysisFrameView
{
    ConstArrayView<std::complex<float>> analysedSpectrum;
    ConstArrayView<float> magnitudes;
    ConstArrayView<float> analysisPhases;
    ConstArrayView<float> previousAnalysisPhases;
    ConstArrayView<double> trueSourceBins;
    ConstArrayView<float> harmonicMask;
    ConstArrayView<float> spectralEnvelope;
    ConstArrayView<int> nearestPeak;
    ConstArrayView<int> peakBins;

    double sampleRate = 48000.0;
    int frameSize = 256;
    int hopSize = 64;
    int positiveBinCount = 129;
    std::int64_t frameEndSample = 0;

    float detectedPitchHz = 0.0f;
    float confidence = 0.0f;
    float voicing = 0.0f;
    float consensus = 0.0f;
    float onsetStrength = 0.0f;
    float breathiness = 0.0f;
    float harmonicity = 0.0f;
    float polyphony = 0.0f;
    float spectralReliability = 0.0f;
    float maskStability = 0.0f;
    bool phaseReset = false;
};

struct CorrectionTrajectoryFrame
{
    // These are the musical trajectory endpoints for the current synthesis hop.
    // The ledger integrates them directly; it must not infer a new target note.
    double previousCorrectionCents = 0.0;
    double correctionCents = 0.0;
    float previousTargetPitchHz = 0.0f;
    float targetPitchHz = 0.0f;
    std::uint64_t targetRevision = 0;
    bool targetValid = false;
    bool forceReset = false;
};

struct RidgeObservation
{
    int peakBin = -1;
    float sourceFrequencyHz = 0.0f;
    float amplitude = 0.0f;
    float localWidthBins = 0.0f;
    float localSnr = 0.0f;
    float sourcePhase = 0.0f;
    float phaseCoherence = 0.0f;
    float harmonicPrior = 0.0f;
    float eventProbability = 0.0f;
    float reliability = 0.0f;
};

struct RidgeState
{
    std::uint32_t id = 0;
    RidgeLifeState lifeState = RidgeLifeState::inactive;

    float sourceFrequencyHz = 0.0f;
    float previousSourceFrequencyHz = 0.0f;
    float targetFrequencyHz = 0.0f;
    float previousTargetFrequencyHz = 0.0f;

    double targetPhase = 0.0;
    float groupDelaySlopeRadiansPerBin = 0.0f;
    float amplitude = 0.0f;
    float reliability = 0.0f;

    int ageFrames = 0;
    int missedFrames = 0;
    int lastPeakBin = -1;
    bool matchedThisFrame = false;
};

struct RidgeLedgerFrameView
{
    ConstArrayView<RidgeState> tracks;
    ConstArrayView<int> sourceBinTrackIndices;
    int activeTrackCount = 0;
};

struct OutputSpectrumView
{
    ConstArrayView<std::complex<float>> spectrum;
    ConstArrayView<BinOwnership> ownership;
};

struct RidgeLedgerDiagnostics
{
    int observationCount = 0;
    int activeTrackCount = 0;
    int bornTrackCount = 0;
    int coastTrackCount = 0;
    int deadTrackCount = 0;
    int identitySwitchCount = 0;
    float meanPredictionErrorRadians = 0.0f;
    float meanReliability = 0.0f;
};

struct OutputRendererDiagnostics
{
    float overlapAddCoherence = 0.0f;
    float ridgeEnergyRatio = 0.0f;
    float eventEnergyRatio = 0.0f;
    float airEnergyRatio = 0.0f;
    float unclassifiedEnergyRatio = 0.0f;
    float requestedEnergyGainDb = 0.0f;
};

} // namespace neumaton::outputv3
