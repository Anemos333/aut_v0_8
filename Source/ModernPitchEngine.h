#pragma once

#include <JuceHeader.h>
#include "Tempo.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

class ModernPitchEngine final
{
public:
    static constexpr int maxSupportedChannels = 8;
    static constexpr int maxScaleRatios = 96;

    enum class LatencyMode : int
    {
        ultraLive = 0,
        live = 1,
        quality = 2
    };

    enum class StereoMode : int
    {
        linkedMidSide = 0,
        dualMono = 1
    };

    enum class TrackingState : int
    {
        unvoiced = 0,
        attack,
        acquire,
        stable,
        transition,
        release
    };

    // Compatibility-facing parameter block. Only the controls described below
    // influence the clean engine:
    //   amount            -> tolerated output error in cents;
    //   retuneTimeMs      -> delay after a note attack before correction begins;
    //   humanize          -> pitch movement still considered the same note;
    //   formantPreservation -> strength of LPC envelope reconstruction.
    // The remaining fields stay in the API so the current processor/editor do
    // not need a parallel migration. They never reduce correction authority.
    struct Parameters
    {
        float amount = 1.0f;
        float retuneTimeMs = 8.0f;
        float transitionTimeMs = 35.0f;
        float preserveVibrato = 0.70f;
        float humanize = 0.20f;
        float formantPreservation = 0.90f;
        float transientProtection = 0.85f;
        float detectorSensitivity = 0.70f;
        float maximumCorrectionSemitones = 12.0f;
        float minimumPitchHz = 45.0f;
        float maximumPitchHz = 1600.0f;
        StereoMode stereoMode = StereoMode::linkedMidSide;
        float breathReduction = 0.50f;

        bool scaleLock = false;
        float lockHysteresis = 24.0f;
        float vibratoPreserve = 0.0f;
        int scaleSize = 12;
        float minScaleStepCents = 100.0f;
        int latencyMode = 1;
        float lockStrictness = 0.0f;
        bool hardLockActive = false;

        CreativeTempo::Settings tempo;
    };

    // The editor currently reads the historical metering surface. The clean
    // engine publishes the meaningful subset and leaves obsolete diagnostics at
    // neutral values; none of these meters feeds the audio path.
    struct Metering
    {
        float detectedPitchHz = 0.0f;
        float targetPitchHz = 0.0f;
        float confidence = 0.0f;
        float voicing = 0.0f;
        float breathiness = 0.0f;
        float harmonicity = 0.0f;
        float noisePath = 0.0f;
        float noiseReductionDb = 0.0f;
        float polyphony = 0.0f;
        float spectralReliability = 0.0f;
        float maskStability = 1.0f;
        float sustainedNoteSeconds = 0.0f;
        float consensus = 0.0f;
        float correctionCents = 0.0f;
        float wetMix = 1.0f;
        float transitionBlend = 0.0f;

        float outputSourceCorrespondence = 0.0f;
        float outputTargetCoherence = 0.0f;
        float outputPhysicalHarmonicFit = 0.0f;
        float outputLedgerHealth = 0.0f;
        float outputPhaseCoherence = 0.0f;
        float outputReconstructionNeed = 0.0f;
        float outputMeterValid = 0.0f;
        float outputTemporalStability = 0.0f;
        float outputTargetJumpCents = 0.0f;
        float outputCorrectionVelocityCentsPerSecond = 0.0f;
        float outputOctaveConflict = 0.0f;
        float outputTransitionStress = 0.0f;
        float outputSourceMirrorFit = 0.0f;
        float outputDoubleFamilyRisk = 0.0f;
        float outputLedgerDeficit = 0.0f;
        float outputMemoryReliability = 0.0f;
        float outputPreIfftConsensus = 0.0f;
        float outputSelectiveReconstructionNeed = 0.0f;

        int shadowRidgeObservationCount = 0;
        int shadowRidgeActiveCount = 0;
        int shadowRidgeBirthCount = 0;
        int shadowRidgeCoastCount = 0;
        int shadowRidgeDeathCount = 0;
        int shadowRidgeIdentitySwitchCount = 0;
        float shadowRidgePredictionErrorRadians = 0.0f;
        float shadowRidgeReliability = 0.0f;
        float shadowRidgeResolvedBinCoverage = 0.0f;
        bool shadowRidgeValid = false;

        bool dualSynthesisActive = false;
        int detectorSupport = 0;
        int octaveState = 0;
        int pendingOctaveObservations = 0;
        TrackingState state = TrackingState::unvoiced;

        float tempoBpm = 120.0f;
        float tempoGridPhase = 0.0f;
        float tempoGlideTimeMs = 0.0f;
        bool tempoActive = false;
        bool tempoWaitingForGrid = false;
        bool tempoHostSyncValid = false;
        CreativeTempo::Mode tempoMode = CreativeTempo::Mode::off;
    };

    ModernPitchEngine() = default;

    void prepare(double sampleRate,
                 int maximumExpectedSamplesPerBlock,
                 int numberOfChannels,
                 LatencyMode latencyMode);

    void reset() noexcept;

    void process(juce::AudioBuffer<float>& buffer,
                 const double* scaleRatios,
                 int numberOfScaleRatios,
                 double rootFrequency,
                 const Parameters& parameters);

    void process(juce::AudioBuffer<float>& buffer,
                 const double* scaleRatios,
                 int numberOfScaleRatios,
                 double rootFrequency,
                 const Parameters& parameters,
                 const CreativeTempo::HostPosition& hostTempoPosition);

    void process(juce::AudioBuffer<float>& buffer,
                 const std::vector<double>& scaleRatios,
                 double rootFrequency,
                 const Parameters& parameters);

    void process(float* monoData,
                 int numberOfSamples,
                 const std::vector<double>& scaleRatios,
                 double rootFrequency,
                 const Parameters& parameters);

    void processBypassed(juce::AudioBuffer<float>& buffer);

    [[nodiscard]] int getLatencySamples() const noexcept { return latencySamples_; }
    [[nodiscard]] LatencyMode getLatencyMode() const noexcept { return latencyMode_; }
    [[nodiscard]] Metering getMetering() const noexcept;

private:
    static constexpr int detectorRingSize = 4096;
    static constexpr int maximumLpcOrder = 12;
    static constexpr int transportRingSize = 16384;

    struct PitchObservation
    {
        float frequencyHz = 0.0f;
        float confidence = 0.0f;
        float periodicity = 0.0f;
        float voicing = 0.0f;
        float consensus = 0.0f;
        float onsetStrength = 0.0f;
        int detectorSupport = 0;
        int octaveState = 0;
        int pendingOctaveObservations = 0;
        bool valid = false;
        bool onset = false;
    };

    class PitchTracker
    {
    public:
        void prepare(double sampleRate, LatencyMode mode);
        void reset() noexcept;
        [[nodiscard]] bool push(float sample,
                                float minimumPitchHz,
                                float maximumPitchHz,
                                float sensitivity,
                                PitchObservation& result) noexcept;

    private:
        [[nodiscard]] PitchObservation analyse(float minimumPitchHz,
                                               float maximumPitchHz,
                                               float sensitivity) noexcept;
        [[nodiscard]] float correlationAt(int lag, int stride) const noexcept;
        [[nodiscard]] float sampleFromNewest(int age) const noexcept;

        std::array<float, detectorRingSize> ring_ {};
        std::array<float, detectorRingSize> scratch_ {};
        int writeIndex_ = 0;
        int filled_ = 0;
        int downsampleCounter_ = 0;
        float downsampleAccumulator_ = 0.0f;
        int samplesSinceAnalysis_ = 0;

        double sampleRate_ = 48000.0;
        double detectorSampleRate_ = 24000.0;
        int analysisWindow_ = 1536;
        int analysisHop_ = 48;

        float shortEnergy_ = 0.0f;
        float longEnergy_ = 0.0f;
        float previousFrequencyHz_ = 0.0f;
        int pendingOctaveDirection_ = 0;
        int pendingOctaveCount_ = 0;
    };

    class ScaleQuantizer
    {
    public:
        void reset() noexcept;
        void setScale(const double* ratios,
                      int ratioCount,
                      double rootFrequency) noexcept;

        [[nodiscard]] double chooseTarget(double detectedPitchHz,
                                          float humanize,
                                          float minimumScaleStepCents,
                                          bool onset,
                                          float detectorConfidence,
                                          int& pendingOctaveObservations,
                                          int& octaveState) noexcept;

        [[nodiscard]] double currentTargetHz() const noexcept
        {
            return targetValid_ ? currentTargetHz_ : 0.0;
        }

    private:
        [[nodiscard]] double nearestScaleFrequency(double frequencyHz) const noexcept;
        [[nodiscard]] static double centsBetween(double a, double b) noexcept;

        std::array<double, maxScaleRatios> ratios_ {};
        int ratioCount_ = 1;
        double rootFrequency_ = 440.0;

        bool targetValid_ = false;
        double currentTargetHz_ = 0.0;
        double previousDetectedHz_ = 0.0;

        bool pendingTargetValid_ = false;
        double pendingTargetHz_ = 0.0;
        int pendingTargetCount_ = 0;
    };

    struct TransportPlan
    {
        double delayA = 0.0;
        double delayB = 0.0;
        float gainA = 1.0f;
        float gainB = 0.0f;
    };

    class TransportClock
    {
    public:
        void prepare(int reportedLatencySamples) noexcept;
        void reset() noexcept;
        [[nodiscard]] TransportPlan next(double ratio) noexcept;

    private:
        double phase_ = 0.5;
        int minimumDelay_ = 8;
        int rangeSamples_ = 240;
    };

    class ChannelPath
    {
    public:
        void prepare(double sampleRate, int reportedLatencySamples);
        void reset() noexcept;
        void setLpcTarget(const std::array<float, maximumLpcOrder>& coefficients,
                          float strength) noexcept;

        [[nodiscard]] float process(float input,
                                    const TransportPlan& plan) noexcept;
        [[nodiscard]] float processBypassed(float input,
                                            int latencySamples) noexcept;

    private:
        [[nodiscard]] float interpolateResidual(double absolutePosition) const noexcept;
        static float sanitise(float value) noexcept;

        std::array<float, transportRingSize> residualRing_ {};
        std::array<float, transportRingSize> bypassRing_ {};
        std::array<float, maximumLpcOrder> inputHistory_ {};
        std::array<float, maximumLpcOrder> outputHistory_ {};
        std::array<float, maximumLpcOrder> currentLpc_ {};
        std::array<float, maximumLpcOrder> targetLpc_ {};

        std::int64_t sampleCounter_ = 0;
        float coefficientSmoothing_ = 0.01f;
    };

    void updateLpcTarget(const juce::AudioBuffer<float>& buffer,
                         int channels,
                         int samples,
                         float requestedAmount,
                         float periodicity,
                         float onsetStrength) noexcept;

    [[nodiscard]] static std::array<float, maximumLpcOrder>
        calculateLpc(const float* mono,
                     int samples) noexcept;

    void updateCorrection(const PitchObservation& observation,
                          const Parameters& parameters) noexcept;

    [[nodiscard]] double correctionForObservation(double detectedHz,
                                                  double targetHz,
                                                  const Parameters& parameters) const noexcept;

    [[nodiscard]] double currentRatioForSample() noexcept;
    void updateChannelCorrection(int channel,
                                 const PitchObservation& observation,
                                 const Parameters& parameters) noexcept;
    [[nodiscard]] double currentChannelRatioForSample(int channel) noexcept;
    [[nodiscard]] static float clamp01(float value) noexcept;
    [[nodiscard]] static double wrapToNearestOctave(double cents) noexcept;
    [[nodiscard]] static int latencyForMode(LatencyMode mode) noexcept;
    void publishMetering(const PitchObservation& observation) noexcept;

    double sampleRate_ = 48000.0;
    int maximumBlockSize_ = 512;
    int channelCount_ = 1;
    int latencySamples_ = 256;
    LatencyMode latencyMode_ = LatencyMode::live;

    PitchTracker linkedTracker_;
    std::array<PitchTracker, maxSupportedChannels> channelTrackers_ {};
    ScaleQuantizer linkedQuantizer_;
    std::array<ScaleQuantizer, maxSupportedChannels> channelQuantizers_ {};

    TransportClock linkedClock_;
    std::array<TransportClock, maxSupportedChannels> channelClocks_ {};
    std::array<ChannelPath, maxSupportedChannels> channelPaths_ {};

    std::vector<float> monoScratch_;
    std::array<float, maximumLpcOrder> currentLpcTarget_ {};

    double desiredCorrectionCents_ = 0.0;
    double currentCorrectionCents_ = 0.0;
    double correctionSmoothingCoefficient_ = 1.0;
    int speedDelaySamplesRemaining_ = 0;
    bool targetValid_ = false;
    double targetPitchHz_ = 0.0;
    std::uint64_t targetRevision_ = 0;

    // Independent channel state is used only in dual-mono mode.
    std::array<double, maxSupportedChannels> channelDesiredCorrectionCents_ {};
    std::array<double, maxSupportedChannels> channelCurrentCorrectionCents_ {};
    std::array<int, maxSupportedChannels> channelSpeedDelaySamplesRemaining_ {};
    std::array<bool, maxSupportedChannels> channelTargetValid_ {};
    std::array<double, maxSupportedChannels> channelTargetPitchHz_ {};

    PitchObservation latestObservation_ {};
    std::array<PitchObservation, maxSupportedChannels> latestChannelObservation_ {};

    std::atomic<std::uint32_t> meterSequence_ { 0 };
    std::atomic<float> meterPitchHz_ { 0.0f };
    std::atomic<float> meterTargetHz_ { 0.0f };
    std::atomic<float> meterConfidence_ { 0.0f };
    std::atomic<float> meterVoicing_ { 0.0f };
    std::atomic<float> meterPeriodicity_ { 0.0f };
    std::atomic<float> meterCorrectionCents_ { 0.0f };
    std::atomic<float> meterOnsetStrength_ { 0.0f };
    std::atomic<int> meterDetectorSupport_ { 0 };
    std::atomic<int> meterOctaveState_ { 0 };
    std::atomic<int> meterPendingOctave_ { 0 };
    std::atomic<int> meterTrackingState_ { static_cast<int>(TrackingState::unvoiced) };
};
