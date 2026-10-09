#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace neumaton::pitch
{

enum class LatencyMode : std::uint8_t
{
    lowLatency128 = 0,
    live256,
    studio512
};

enum class TrackingState : std::uint8_t
{
    acquire = 0,
    stable,
    transition
};

struct PitchResult
{
    TrackingState state = TrackingState::acquire;
    float stableHz = 0.0f;
    float measuredHz = 0.0f;
    float confidence = 0.0f;
    float periodicity = 0.0f;
    bool hasStable = false;
    bool newMeasurement = false;
    bool stableChanged = false;
    bool gateOpen = true;
    bool octaveAmbiguous = false;
};

// Measurement only. It never produces audio, a gain, a mix value or any
// downstream descriptor. Its complete public result is one boolean: measure
// the current signal, or do not measure it.
class MeasurementGate final
{
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] bool processSample(float sample) noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return open_; }

private:
    static constexpr int ringSize = 256;
    static constexpr int ringMask = ringSize - 1;
    static constexpr int evaluationHop = 32;

    std::array<float, ringSize> ring_ {};
    double sampleRate_ = 48000.0;
    int write_ = 0;
    int available_ = 0;
    int hop_ = 0;
    bool open_ = true;

    void evaluate() noexcept;
};

// One tuner-like detector: estimate -> validate -> refine. There are no
// parallel pitch trackers and no confidence-weighted voting paths.
class FundamentalDetector final
{
public:
    void prepare(double sampleRate,
                 LatencyMode mode,
                 float minimumPitchHz = 45.0f,
                 float maximumPitchHz = 1600.0f) noexcept;
    void reset() noexcept;

    [[nodiscard]] PitchResult processSample(float sample, bool gateOpen) noexcept;
    [[nodiscard]] PitchResult latest() const noexcept { return latest_; }

private:
    static constexpr int ringSize = 16384;
    static constexpr int ringMask = ringSize - 1;
    static constexpr int coarseFrameMax = 1024;
    static constexpr int coarseLagMax = 1023;

    struct Candidate
    {
        float hz = 0.0f;
        float confidence = 0.0f;
        float periodicity = 0.0f;
        float periodSamples = 0.0f;
        float halfPeriodScore = -1.0f;
        float doublePeriodScore = -1.0f;
        bool valid = false;
        bool octaveAmbiguous = false;
    };

    enum class OctaveAdvice : std::uint8_t
    {
        none = 0,
        rejectUpperCandidate,
        rejectLowerCandidate,
        candidateSupported
    };

    struct SensorDecision
    {
        bool holdTransition = false;
        bool rejectCandidate = false;
        bool emergencyRan = false;
    };

    std::array<float, ringSize> ring_ {};
    std::array<float, coarseFrameMax> coarse_ {};
    std::array<float, coarseLagMax + 1> difference_ {};
    std::array<float, coarseLagMax + 1> cmndf_ {};

    double sampleRate_ = 48000.0;
    LatencyMode mode_ = LatencyMode::live256;
    float minimumPitchHz_ = 45.0f;
    float maximumPitchHz_ = 1600.0f;
    int write_ = 0;
    int available_ = 0;
    int analysisHop_ = 64;
    int analysisCounter_ = 0;
    int decimation_ = 4;

    TrackingState state_ = TrackingState::acquire;
    float stableHz_ = 0.0f;
    float pendingHz_ = 0.0f;
    int pendingConfirmations_ = 0;
    PitchResult latest_ {};

    float previousSample_ = 0.0f;
    double fastEnergy_ = 0.0;
    double slowEnergy_ = 0.0;
    double differenceEnergy_ = 0.0;
    double fastEnergyCoefficient_ = 0.0;
    double slowEnergyCoefficient_ = 0.0;

    [[nodiscard]] float readAgo(int samplesAgo) const noexcept;
    [[nodiscard]] Candidate estimateGlobal() noexcept;
    [[nodiscard]] Candidate trackLocal(float centreHz) noexcept;
    [[nodiscard]] float periodScore(double periodSamples,
                                    int cycles,
                                    int sampleStride = 1) const noexcept;
    [[nodiscard]] Candidate refineCandidate(float coarseHz,
                                            float coarseConfidence) noexcept;
    [[nodiscard]] static float centsDistance(float a, float b) noexcept;
    [[nodiscard]] static bool octaveLike(float a, float b) noexcept;

    [[nodiscard]] OctaveAdvice octaveSensor(const Candidate& candidate) const noexcept;
    [[nodiscard]] bool emergencyOctaveCheck(const Candidate& candidate) const noexcept;
    [[nodiscard]] SensorDecision queryNatureSensors(const Candidate& candidate) const noexcept;

    void acceptStable(const Candidate& candidate) noexcept;
    void enterTransition() noexcept;
    void handleCandidate(const Candidate& candidate) noexcept;
    void publish(bool gateOpen, const Candidate* candidate,
                 bool stableChanged, bool newMeasurement) noexcept;
};

// Quantizer has no detector authority. It receives an already-stable F0 and
// chooses the mathematically nearest scale centre. Equaves other than 2:1 are
// native rather than special-cased.
class ScaleQuantizer final
{
public:
    static constexpr int maxDegrees = 96;

    struct Target
    {
        double targetHz = 0.0;
        double correctionCents = 0.0;
        double liveWindowCents = 0.0;
        bool valid = false;
        // Adjacent legal pitches are almost equidistant; the upper owns the tie.
        bool ambiguousBoundary = false;
        double lowerTargetHz = 0.0;
        double boundaryMidpointHz = 0.0;
        double boundaryHalfWidthCents = 0.0;
    };

    bool setScale(const double* ratios,
                  int count,
                  double referenceHz,
                  double equaveRatio = 2.0) noexcept;

    [[nodiscard]] Target quantize(double fundamentalHz,
                                  float amount,
                                  float humanize,
                                  float boundaryStability = 0.5f) const noexcept;

    [[nodiscard]] double equaveRatio() const noexcept { return equaveRatio_; }
    [[nodiscard]] int size() const noexcept { return count_; }

private:
    std::array<double, maxDegrees> positions_ {};
    int count_ = 0;
    double referenceHz_ = 440.0;
    double equaveRatio_ = 2.0;
    double logEquave_ = 0.6931471805599453094;
    double minimumStepCents_ = 100.0;
};

class PitchCore final
{
public:
    void prepare(double sampleRate,
                 LatencyMode mode,
                 float minimumPitchHz = 45.0f,
                 float maximumPitchHz = 1600.0f) noexcept;
    void reset() noexcept;

    [[nodiscard]] PitchResult processSample(float sample) noexcept;
    [[nodiscard]] PitchResult latest() const noexcept { return detector_.latest(); }

private:
    MeasurementGate gate_;
    FundamentalDetector detector_;
};

[[nodiscard]] constexpr int declaredLatencySamples(LatencyMode mode) noexcept
{
    switch (mode)
    {
        case LatencyMode::lowLatency128: return 128;
        case LatencyMode::live256:       return 256;
        case LatencyMode::studio512:     return 512;
    }
    return 256;
}

} // namespace neumaton::pitch
