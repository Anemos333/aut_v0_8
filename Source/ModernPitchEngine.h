#pragma once

#include <JuceHeader.h>
#include "Tempo.h"
#include "SingleWetSpectralRenderer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
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

    enum class TrackingState : int
    {
        unvoiced = 0,
        attack,
        acquire,
        stable,
        transition,
        release
    };

    struct Parameters
    {
        // LINKED_ONLY_PRODUCT_PATH_V1: stereo channels share one detector,
        // ownership state and correction trajectory; rendering remains per-channel.
        // LEAN_PARAMETERS_V1: only fields with an active V1 consumer remain.
        float amount = 1.0f;
        float retuneTimeMs = 8.0f;
        float transitionTimeMs = 35.0f;
        float humanize = 0.20f;
        float formantPreservation = 0.90f;
        float detectorSensitivity = 0.70f;
        float maximumCorrectionSemitones = 12.0f;
        float minimumPitchHz = 45.0f;
        float maximumPitchHz = 1600.0f;

        // Analysis-only voice evidence supplied by LivePitchProcessor. These
        // fields classify note body vs breath; they never scale Amount or mix
        // an alternate signal path.
        bool voiceEvidenceValid = false;
        float voiceHarmonicity = 0.0f;
        float voiceBreathiness = 0.0f;
        float voiceBodyEnergy = 0.0f;
        float voiceSpectralReliability = 0.0f;
        float voiceEventStrength = 0.0f;
        float voiceFormantStability = 0.0f;
        // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7: analysis-only. This value
        // permits lower-family inspection; it never reduces correction depth.
        float voiceLowerFamilyEvidence = 0.0f;

        bool scaleLock = false;
        float lockHysteresis = 24.0f;
        float vibratoPreserve = 0.0f;

        CreativeTempo::Settings tempo;
    };

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
        float outputTemporalStability = 0.0f;
        float outputTargetJumpCents = 0.0f;
        float outputCorrectionVelocityCentsPerSecond = 0.0f;
        float outputOctaveConflict = 0.0f;
        float outputTransitionStress = 0.0f;
        float outputSourceMirrorFit = 0.0f;
        float outputDoubleFamilyRisk = 0.0f;
        float outputLedgerDeficit = 0.0f;
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
                 const CreativeTempo::HostPosition& hostTempoPosition,
                 std::uint64_t scaleGeneration = 0);

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
    struct PitchObservation
    {
        // frequencyHz is the continuity/identity coordinate. It may be
        // smoothed by the tracker so target ownership remains stable.
        float frequencyHz = 0.0f;

        // LIVE_CORRECTION_COORDINATE_V5: latest accepted live F0 in the same
        // register, used only by the fully rigid Scale Lock endpoint. This
        // prevents continuity smoothing from becoming audible pitch residual.
        float correctionFrequencyHz = 0.0f;
        float confidence = 0.0f;
        float periodicity = 0.0f;
        float voicing = 0.0f;
        float consensus = 0.0f;
        float onsetStrength = 0.0f;
        int detectorSupport = 0;
        int octaveState = 0;
        int pendingOctaveObservations = 0;
        bool valid = false;
        // MEASUREMENT_CONTINUUM_V1: a finite period was physically measured,
        // but detector evidence may still be too weak to call it a trusted F0.
        // This is analysis information only and never direct renderer authority.
        bool measurementAvailable = false;
        bool onset = false;
        bool audioPresent = false;
    };

    class BiquadLowPass
    {
    public:
        void prepare(double sampleRate, double cutoffHz,
                     double q = 0.7071067811865476) noexcept;
        void reset() noexcept;
        [[nodiscard]] float process(float input) noexcept;

    private:
        double b0_ = 1.0;
        double b1_ = 0.0;
        double b2_ = 0.0;
        double a1_ = 0.0;
        double a2_ = 0.0;
        double z1_ = 0.0;
        double z2_ = 0.0;
    };

    class MultiRatePitchTracker
    {
    public:
        MultiRatePitchTracker() noexcept;
        ~MultiRatePitchTracker();

        MultiRatePitchTracker(const MultiRatePitchTracker&) = delete;
        MultiRatePitchTracker& operator=(const MultiRatePitchTracker&) = delete;

        void prepare(double sampleRate) noexcept;
        void reset() noexcept;
        void setRange(float minimumPitchHz, float maximumPitchHz) noexcept;
        void setSensitivity(float sensitivity) noexcept;
        void setVoiceAuthorityContext(bool valid,
                                      float harmonicity,
                                      float breathiness,
                                      float bodyEnergy,
                                      float spectralReliability,
                                      float eventStrength,
                                      float formantStability,
                                      float lowerFamilyEvidence) noexcept;
        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }
        // TRANSITION_WAKES_DETECTOR_NOT_OUTPUT_V1: analysis-only watchdog.
        void setTransitionWake(bool enabled) noexcept { transitionWake_ = enabled; }
        void setReacquisitionAnchor(float frequencyHz) noexcept;
        void clearReacquisitionAnchor() noexcept { reacquisitionAnchorHz_ = 0.0f; }
        bool processSample(float inputSample, PitchObservation& observation) noexcept;
        [[nodiscard]] static constexpr int hopSize() noexcept { return detectorHop; }

    private:
        class AnalysisWorker;

        static constexpr int ringSize = 1024;
        static constexpr int ringMask = ringSize - 1;
        static constexpr int maxAnalysisSize = 512;
        static constexpr int standardAnalysisSize = 256;
        static constexpr int detectorHop = 32;
        static constexpr int detectorPathCount = 4;
        static constexpr int maxConsensusHypotheses = 20;
        static constexpr int decoderBeamWidth = 6;

        struct PitchCandidate
        {
            float frequencyHz = 0.0f;
            float confidence = 0.0f;
            float periodicity = 0.0f;
            // VOICE_AWARE_F0_FRONTEND_V1: evidence about whether the measured
            // period belongs to a coherent vocal source rather than breath,
            // formant ringing or background noise. Analysis only.
            float harmonicFamily = -1.0f;
            float aperiodicity = -1.0f;
            // PATH_ROLE_SPLIT_V1: orthogonal confidence that this period was
            // measured from a coherent tonal vocal source rather than a formant,
            // breath, hiss or background-noise structure. Analysis only.
            float tonalCleanliness = -1.0f;
            int pathIndex = -1;
            int ageInHops = 1000;
            bool valid = false;
        };

        struct CandidateSlot
        {
            PitchCandidate candidate;
            int ageInHops = 1000;
        };

        struct AnalysisWorkspace
        {
            std::array<float, maxAnalysisSize> frame {};
            // Detector-only inverse-filtered residual. The audible signal never
            // enters this buffer and the renderer never reads it.
            std::array<float, maxAnalysisSize> voiceResidualFrame {};
            std::array<float, maxAnalysisSize> difference {};
            // RESIDUAL_HANN_LAZY_MEMOIZATION_V1: scratch for one analyse() call.
            // The first unique residual line fills this lazily in golden loop
            // order; later unique lines reuse the exact double coefficients.
            std::array<double, maxAnalysisSize> residualHannWindow {};
        };

        struct ConsensusHypothesis
        {
            float frequencyHz = 0.0f;
            float confidence = 0.0f;
            float periodicity = 0.0f;
            float harmonicFamily = 1.0f;
            float tonalCleanliness = 1.0f;
            float consensus = 0.0f;
            float evidenceScore = -1000.0f;
            int supportCount = 0;
            int cleanSupportCount = 0;
            int directSupportCount = 0;
            std::uint8_t supportMask = 0;
            std::uint8_t freshSupportMask = 0;
            bool valid = false;
        };

        struct DecoderState
        {
            double logFrequency = 0.0;
            float score = -1000.0f;
            int ageInHops = 0;
            int octaveIndex = 0;
            bool valid = false;
        };

        struct DecoderDecision
        {
            PitchCandidate candidate;
            float consensus = 0.0f;
            int supportCount = 0;
            int directSupportCount = 0;
            std::uint8_t freshSupportMask = 0;
            int decoderOctaveIndex = 0;
            // AUTHORITATIVE_DIRECT_FAST_PATH_V6: this is not confidence. It
            // means current detector geometry has already demonstrated an
            // unambiguous physical coordinate. directSupportCount records the
            // number of native strong path measurements agreeing with it.
            bool authoritativeDirect = false;
            bool valid = false;
        };

        static_assert((ringSize & (ringSize - 1)) == 0,
                      "Pitch tracker ring size must be a power of two");

        // OBSERVATION_MEMORY_SEPARATION_V1: clears detector hypotheses only.
        // It never touches ScaleQuantizer, CorrectionState or renderer state.
        void clearObservationMemory(bool clearAnalysisBuffers) noexcept;
        void push(std::array<float, ringSize>& ring,
                  int& writePosition,
                  int& availableSamples,
                  float sample) noexcept;
        // CONTINUOUS_F0_MEASUREMENT_V1: measurement core composed only from
        // already validated detector primitives. A finite physical coordinate is
        // published independently of confidence/cleanliness authority.
        [[nodiscard]] PitchCandidate measureCoordinate(
            const std::array<float, ringSize>& ring,
            int writePosition,
            int availableSamples,
            double effectiveSampleRate,
            float minimumFrequency,
            float maximumFrequency,
            int analysisLength,
            AnalysisWorkspace& workspace) noexcept;

        [[nodiscard]] PitchCandidate analyse(
            const std::array<float, ringSize>& ring,
            int writePosition,
            int availableSamples,
            double effectiveSampleRate,
            float minimumFrequency,
            float maximumFrequency,
            int analysisLength,
            AnalysisWorkspace& workspace) noexcept;
        [[nodiscard]] int collectFreshCandidates(
            std::array<PitchCandidate, detectorPathCount>& candidates) const noexcept;
        [[nodiscard]] int buildConsensusHypotheses(
            const std::array<PitchCandidate, detectorPathCount>& candidates,
            int candidateCount,
            std::array<ConsensusHypothesis, maxConsensusHypotheses>& hypotheses) const noexcept;
        [[nodiscard]] DecoderDecision decodeCandidate(bool onsetPending) noexcept;
        // PATH_ROLE_SPLIT_V1: decimated paths no longer cast equivalent votes.
        // Pitch authority says how useful a path is for locating F0; cleanliness
        // authority says how useful it is for deciding whether that F0 belongs to
        // tonal voice rather than aperiodic/formant/background material.
        [[nodiscard]] float pathPitchAuthority(int pathIndex, float frequencyHz) const noexcept;
        // Family evidence and coordinate ownership are deliberately distinct.
        // Direct-band boundaries reuse the existing 230/460/900 Hz path limits.
        [[nodiscard]] float pathCoordinateAuthority(int pathIndex, float frequencyHz) const noexcept;
        [[nodiscard]] float pathCleanlinessAuthority(int pathIndex, float frequencyHz) const noexcept;
        [[nodiscard]] float candidateBaseScore(const PitchCandidate& candidate) const noexcept;
        [[nodiscard]] float voiceBodyAuthorityV67() const noexcept;
        [[nodiscard]] bool voiceAllowsLowerFamilyV67() const noexcept;
        [[nodiscard]] static float centsDistance(float frequencyA,
                                                 float frequencyB) noexcept;
        [[nodiscard]] static bool isOctaveLikeTransition(float fromFrequency,
                                                         float toFrequency,
                                                         int& octaveDelta,
                                                         float& residualCents) noexcept;
        [[nodiscard]] bool confirmOctaveTransition(DecoderDecision& decision,
                                                   bool onsetPending) noexcept;
        void updateDecoderBeam(
            const std::array<ConsensusHypothesis, maxConsensusHypotheses>& hypotheses,
            int hypothesisCount,
            bool onsetPending) noexcept;

        double sampleRate_ = 48000.0;
        float minimumPitchHz_ = 45.0f;
        float maximumPitchHz_ = 1600.0f;
        float sensitivity_ = 0.70f;
        bool rescueMode_ = false;
        bool presenceMode_ = false;
        bool presenceSinceLastHop_ = false;
        bool transitionWake_ = false;
        // True after a physical input discontinuity or watchdog falsification.
        // While true, musical note-body state may not be re-injected as an F0
        // anchor. A fresh measured F0 clears it.
        bool observationContinuityBroken_ = false;

        // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7: previous-block causal
        // supervision only. Coordinate measurements still come exclusively
        // from the detector paths below.
        bool voiceAuthorityContextValid_ = false;
        float voiceAuthorityHarmonicity_ = 0.0f;
        float voiceAuthorityBreathiness_ = 0.0f;
        float voiceAuthorityBodyEnergy_ = 0.0f;
        float voiceAuthoritySpectralReliability_ = 0.0f;
        float voiceAuthorityFormantStability_ = 0.0f;
        float voiceAuthorityLowerFamilyEvidence_ = 0.0f;

        std::array<float, ringSize> fullRateRing_ {};
        std::array<float, ringSize> halfRateRing_ {};
        std::array<float, ringSize> quarterRateRing_ {};
        std::array<float, ringSize> eighthRateRing_ {};
        int fullRateWritePosition_ = 0;
        int halfRateWritePosition_ = 0;
        int quarterRateWritePosition_ = 0;
        int eighthRateWritePosition_ = 0;
        int fullRateAvailableSamples_ = 0;
        int halfRateAvailableSamples_ = 0;
        int quarterRateAvailableSamples_ = 0;
        int eighthRateAvailableSamples_ = 0;
        int halfRateDecimationCounter_ = 0;
        int quarterRateDecimationCounter_ = 0;
        int eighthRateDecimationCounter_ = 0;
        int hopCounter_ = 0;
        int analysisHopCounter_ = 0;

        BiquadLowPass halfRateAntiAlias_;
        BiquadLowPass quarterRateAntiAlias_;
        BiquadLowPass eighthRateAntiAlias_;
        float previousInput_ = 0.0f;
        float previousDcOutput_ = 0.0f;
        float dcBlockCoefficient_ = 0.995f;
        float fastEnergy_ = 0.0f;
        float slowEnergy_ = 0.0f;
        // Lower-envelope estimate used only to rank detector evidence in
        // ordinary room/live noise. It never gates or attenuates audio.
        float noiseFloorEnergy_ = 1.0e-6f;
        float fastEnergyCoefficient_ = 0.0f;
        float slowEnergyCoefficient_ = 0.0f;
        float onsetEnvelope_ = 0.0f;
        int onsetCooldownSamples_ = 0;
        bool onsetPending_ = false;

        CandidateSlot fullRateCandidate_;
        CandidateSlot halfRateCandidate_;
        CandidateSlot quarterRateCandidate_;
        CandidateSlot eighthRateCandidate_;
        // Main/audio-thread workspace. PARKED_LOW_RATE_WORKER_V1 owns a second
        // private workspace and never shares scratch arithmetic with this one.
        AnalysisWorkspace analysisWorkspace_ {};
        std::unique_ptr<AnalysisWorker> analysisWorker_;
        std::array<DecoderState, decoderBeamWidth> decoderBeam_ {};
        float trackedPitchHz_ = 0.0f;
        float reacquisitionAnchorHz_ = 0.0f;
        float trackedConfidence_ = 0.0f;
        float trackedPeriodicity_ = 0.0f;
        float trackedConsensus_ = 0.0f;
        int trackedSupportCount_ = 0;
        int invalidHopCount_ = 0;
        int octaveState_ = 0;
        int pendingOctaveDelta_ = 0;
        int pendingOctaveCount_ = 0;
        float pendingOctaveFrequencyHz_ = 0.0f;
        float committedOctaveFrequencyHz_ = 0.0f;
        int octaveCommitGuardHops_ = 0;
    };

    class ScaleQuantizer
    {
    public:
        void reset() noexcept;
        bool setScale(const double* ratios, int ratioCount,
                      double rootFrequency,
                      std::uint64_t generation = 0) noexcept;
        [[nodiscard]] float minimumStepCents() const noexcept { return minStepCents_; }
        [[nodiscard]] double nearestTargetLog2(double inputLog2) const noexcept;
        [[nodiscard]] double adjacentTargetLog2(double currentTargetLog2,
                                                int direction) const noexcept;

    private:
        [[nodiscard]] static std::uint64_t hashScale(const double* ratios,
                                                     int count,
                                                     double root) noexcept;
        std::array<double, maxScaleRatios> logRatios_ {};
        int ratioCount_ = 1;
        double rootLog2_ = 0.0;
        std::uint64_t hash_ = 0;
        std::uint64_t generation_ = 0;
        float minStepCents_ = 1200.0f;
    };

    struct CorrectionState
    {
        bool targetValid = false;
        bool pitchCentreValid = false;
        double targetLog2 = 0.0;
        double pitchCentreLog2 = 0.0;
        double desiredCents = 0.0;
        double currentCents = 0.0;
        double velocityCentsPerSecond = 0.0;
        double responseMs = 8.0;
        double lastTargetJumpCents = 0.0;
        std::uint64_t revision = 0;
        int stableObservations = 0;
        int invalidObservations = 0;
        int stableBodyObservations = 0;
        int breathEvidenceSamples = 0;
        int uncertainSamples = 0;
        int stateAgeSamples = 0;
        int pitchStaleSamples = 0;
        bool noteBodyLatched = false;
        float noteBodyConfidence = 0.0f;
        double transportPeriodHz = 0.0;
        // LOCAL_TRAJECTORY_V1: cents per detector hop, used only to predict
        // continuous within-note vocal motion. Large innovations are frozen;
        // confirmed target changes rebase transport explicitly.
        double transportVelocityCentsPerHop = 0.0;
        double transportChallengerLog2 = 0.0;
        int transportChallengerHops = 0;

        // RENDERER_STABLE_HOP_AUTHORITY_V1: analysis/controller history keeps
        // every detector hop. This flag only decides whether the current hop's
        // controller command may replace the last command heard by the renderer.
        bool rendererAcceptCurrentHop = true;
        bool rendererCommandValid = false;
        double rendererCommandCents = 0.0;

        // LATENT_SCALE_CANDIDATE_V2: a detector coordinate that points outside
        // the currently owned scale cell is analysis only until the same exact
        // destination degree persists.  While pending it has zero audible
        // authority over target, transport and correction.
        bool latentTargetValid = false;
        double latentTargetLog2 = 0.0;
        int latentTargetHops = 0;

        // SCALE_OWNS_IDENTITY_V2: detector observations may nominate a new
        // cell, but only bounded same-side geometric persistence may present
        // that challenger to ScaleQuantizer. Confidence/consensus never enter
        // this accumulator; user Hold remains a separate later authority.
        int identityChallengerDirection = 0;
        double identityChallengerEvidence = 0.0;

        // CONSERVATIVE_F0_RESCUE_V1: short memory contains detector-derived
        // F0 only. Predicted coordinates are never fed back into the tracker.
        std::array<double, 10> recentRealPitchLog2 {};
        int recentRealPitchCount = 0;
        int rescueQualificationHops = 0;
        bool rescuePredictionActive = false;
        int rescuePredictionHops = 0;
        int rescueDirection = 0;
        bool rescueTargetShifted = false;
        double rescueBaseSourceLog2 = 0.0;
        double rescueSourceLog2 = 0.0;
        double rescueBaseTargetLog2 = 0.0;
        double rescueBaseDesiredCents = 0.0;
        double rescueSlopeCentsPerHop = 0.0;

        TrackingState trackingState = TrackingState::unvoiced;
    };

    [[nodiscard]] static float clamp01(float value) noexcept;
    [[nodiscard]] static double safeLog2(double value) noexcept;
    [[nodiscard]] static int latencyForMode(LatencyMode mode) noexcept;
    [[nodiscard]] float adaptiveHysteresis(const Parameters& parameters,
                                           const ScaleQuantizer& quantizer,
                                           const PitchObservation& observation) const noexcept;
    [[nodiscard]] double responseTimeMs(const Parameters& parameters,
                                        bool targetChanged,
                                        double targetJumpCents) const noexcept;
    void updateCorrectionState(CorrectionState& state,
                               ScaleQuantizer& quantizer,
                               const PitchObservation& observation,
                               const Parameters& parameters) noexcept;
    [[nodiscard]] bool advanceConservativeF0Rescue(
        CorrectionState& state,
        ScaleQuantizer& quantizer,
        const PitchObservation& observation,
        const Parameters& parameters,
        bool bodyLikeFrame) noexcept;
    [[nodiscard]] double advanceCorrection(CorrectionState& state) noexcept;
    [[nodiscard]] double selectRendererCorrection(
        CorrectionState& state,
        double controllerCents) noexcept;
    void publishMetering(
        const PitchObservation& observation,
        const CorrectionState& state,
        double audibleCents,
        const CreativeTempo::Metering& tempoMeter) noexcept;

    double sampleRate_ = 48000.0;
    int channelCount_ = 1;
    int latencySamples_ = 256;
    LatencyMode latencyMode_ = LatencyMode::live;

    MultiRatePitchTracker linkedTracker_;
    ScaleQuantizer linkedQuantizer_;
    std::array<SingleWetSpectralRenderer, maxSupportedChannels> wetRenderers_ {};
    CreativeTempo::Controller tempoController_;
    CorrectionState linkedCorrection_;
    PitchObservation latestObservation_ {};
    double audibleCorrectionCents_ = 0.0;
    std::int64_t sustainedSamples_ = 0;

    std::atomic<std::uint32_t> meterSequence_ { 0 };
    std::atomic<float> meterPitchHz_ { 0.0f };
    std::atomic<float> meterTargetHz_ { 0.0f };
    std::atomic<float> meterConfidence_ { 0.0f };
    std::atomic<float> meterVoicing_ { 0.0f };
    std::atomic<float> meterPeriodicity_ { 0.0f };
    std::atomic<float> meterConsensus_ { 0.0f };
    std::atomic<float> meterCorrectionCents_ { 0.0f };
    std::atomic<float> meterCorrectionVelocity_ { 0.0f };
    std::atomic<float> meterTargetJumpCents_ { 0.0f };
    std::atomic<float> meterSustainedSeconds_ { 0.0f };

    std::atomic<int> meterDetectorSupport_ { 0 };
    std::atomic<int> meterOctaveState_ { 0 };
    std::atomic<int> meterPendingOctave_ { 0 };
    std::atomic<int> meterTrackingState_ { static_cast<int>(TrackingState::unvoiced) };
    std::atomic<float> meterTempoBpm_ { 120.0f };
    std::atomic<float> meterTempoGridPhase_ { 0.0f };
    std::atomic<float> meterTempoGlideTimeMs_ { 0.0f };
    std::atomic<bool> meterTempoActive_ { false };
    std::atomic<bool> meterTempoWaiting_ { false };
    std::atomic<bool> meterTempoHostSync_ { false };
    std::atomic<int> meterTempoMode_ { static_cast<int>(CreativeTempo::Mode::off) };
};
