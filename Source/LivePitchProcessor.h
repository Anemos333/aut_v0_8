#pragma once

#include <JuceHeader.h>
#include "PitchEngineV1.h"
#include "Tempo.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

// Thin JUCE/block adapter around the rebuilt V1 engine.
// Exactly one prepared engine is processed at a time; mode changes only select
// which 128/256/512-sample engine owns the single audible path.
class LivePitchProcessor final
{
public:
    static constexpr int maxSupportedChannels = 2;
    static constexpr int maxScaleRatios = neumaton::pitch::ScaleQuantizer::maxDegrees;

    enum class LatencyMode : int
    {
        ultraLive = 0, // 128 samples
        live = 1,      // 256 samples
        quality = 2    // 512 samples / Studio
    };

    enum class StereoMode : int
    {
        linkedMidSide = 0,
        dualMono = 1
    };

    // Compatibility vocabulary for the current UI only. The rebuilt detector
    // itself has exactly Acquire / Stable / Transition.
    enum class TrackingState : int
    {
        unvoiced = 0,
        attack,
        acquire,
        stable,
        transition,
        release
    };

    // UI compatibility surface. Only fields backed by the new engine are
    // populated with active data; legacy diagnostics remain zero and have no
    // path back into the DSP.
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

        std::uint32_t targetRevisionDiagnosticSerial = 0;
        float targetRevisionBeforeHz = 0.0f;
        float targetRevisionAfterHz = 0.0f;
        float targetRevisionJumpCents = 0.0f;
        bool targetRevisionFromStable = false;
        bool targetRevisionVoiceEvidenceValid = false;
        bool targetRevisionTerminalTailVeto = false;
        bool targetRevisionBodyPresent = false;
        bool targetRevisionMusicalOnset = false;
        bool targetRevisionLiveIdentityBreak = false;
        bool targetRevisionDetectorScaleCommit = false;
        bool targetRevisionDeepCentreExit = false;
        bool targetRevisionPersistentBoundaryExit = false;
        bool targetRevisionTerminalStructure = false;
        bool targetRevisionSameTailSide = false;
        bool targetRevisionOutsideStableCore = false;
        float targetRevisionVoiceBodyEnergy = 0.0f;
        float targetRevisionVoiceHarmonicity = 0.0f;
        float targetRevisionVoiceSpectralReliability = 0.0f;
        float targetRevisionVoiceBreathiness = 0.0f;
        float targetRevisionVoiceEventStrength = 0.0f;
        float targetRevisionCorrectionBeforeCents = 0.0f;
        float targetRevisionCorrectionAfterCents = 0.0f;
        float targetRevisionCorrectionDeltaCents = 0.0f;

        float tempoBpm = 120.0f;
        float tempoGridPhase = 0.0f;
        float tempoGlideTimeMs = 0.0f;
        bool tempoActive = false;
        bool tempoWaitingForGrid = false;
        bool tempoHostSyncValid = false;
        CreativeTempo::Mode tempoMode = CreativeTempo::Mode::off;
    };

    void prepare(double sampleRate, int maximumExpectedSamplesPerBlock)
    {
        prepare(sampleRate, maximumExpectedSamplesPerBlock, 1, getLatencyMode());
    }

    void prepare(double sampleRate,
                 int maximumExpectedSamplesPerBlock,
                 int numberOfChannels,
                 LatencyMode latencyMode)
    {
        sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate)
                                                : 48000.0;
        maximumBlockSize_ = std::max(1, maximumExpectedSamplesPerBlock);
        channelCount_ = std::clamp(numberOfChannels, 1, maxSupportedChannels);

        for (int modeIndex = 0; modeIndex < engineCount; ++modeIndex)
        {
            engines_[static_cast<std::size_t>(modeIndex)].prepare(
                sampleRate_, channelCount_, toPitchMode(static_cast<LatencyMode>(modeIndex)),
                minimumPitchHz_, maximumPitchHz_);
            resetRequested_[static_cast<std::size_t>(modeIndex)].store(
                false, std::memory_order_relaxed);
        }

        activeModeIndex_.store(toModeIndex(latencyMode), std::memory_order_release);
        hostTransportHistoryValid_ = false;
        expectedNextHostSample_ = 0;
        sustainedStableSamples_ = 0;
        prepared_.store(true, std::memory_order_release);
    }

    void reset() noexcept
    {
        for (auto& engine : engines_)
            engine.reset();
        for (auto& request : resetRequested_)
            request.store(false, std::memory_order_relaxed);
        hostTransportHistoryValid_ = false;
        expectedNextHostSample_ = 0;
        sustainedStableSamples_ = 0;
    }

    void setLatencyModeNonRealtime(LatencyMode mode) noexcept
    {
        const int modeIndex = toModeIndex(mode);
        if (modeIndex == activeModeIndex_.load(std::memory_order_acquire))
            return;

        resetRequested_[static_cast<std::size_t>(modeIndex)].store(
            true, std::memory_order_release);
        activeModeIndex_.store(modeIndex, std::memory_order_release);
    }

    // Compatibility setter. Only Humanize and pitch-range values belong to the
    // rebuilt V1 contract today; formant/sensitivity/stereo arguments cannot
    // gain hidden authority over PitchCore or the renderer.
    void setAdvancedParameters(float /*transitionMs*/,
                               float humanize,
                               float /*formantPreservation*/,
                               float /*detectorSensitivity*/,
                               float /*maximumCorrectionSemitones*/,
                               float minimumPitchHz,
                               float maximumPitchHz,
                               StereoMode /*stereoMode*/) noexcept
    {
        humanize_ = std::clamp(humanize, 0.0f, 1.0f);
        minimumPitchHz_ = std::clamp(minimumPitchHz, 25.0f, 500.0f);
        maximumPitchHz_ = std::clamp(maximumPitchHz,
                                     minimumPitchHz_ + 20.0f,
                                     4000.0f);
    }

    // Creative-tempo and old Scale-Lock sub-controls are intentionally not
    // allowed to modify V1 audio until they are rebuilt against the new
    // trajectory contract. Values are retained only for UI/state continuity.
    void setTempoSettings(const CreativeTempo::Settings& settings) noexcept
    {
        tempoSettings_ = settings;
    }

    void setScaleLockParameters(bool scaleLock,
                                float lockHysteresis,
                                float vibratoPreserve) noexcept
    {
        scaleLock_ = scaleLock;
        lockHysteresis_ = std::clamp(lockHysteresis, 0.0f, 80.0f);
        vibratoPreserve_ = std::clamp(vibratoPreserve, 0.0f, 1.0f);
    }

    void setTempoHostPosition(const CreativeTempo::HostPosition& position) noexcept
    {
        bool discontinuity = false;
        if (position.isPlaying && position.hasTimeInSamples && hostTransportHistoryValid_)
        {
            const auto error = std::llabs(position.timeInSamples - expectedNextHostSample_);
            const auto tolerance = static_cast<std::int64_t>(
                std::max(4, std::max(1, position.numberOfSamples) * 2));
            discontinuity = error > tolerance;
        }

        if (discontinuity)
            reset();

        tempoHostPosition_ = position;
        if (position.isPlaying && position.hasTimeInSamples)
        {
            expectedNextHostSample_ = position.timeInSamples
                + static_cast<std::int64_t>(std::max(0, position.numberOfSamples));
            hostTransportHistoryValid_ = true;
        }
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const double* scaleRatios,
                 int numberOfScaleRatios,
                 double rootFrequency,
                 float speedMs,
                 float amount)
    {
        lastSpeedMs_ = std::isfinite(speedMs) ? std::clamp(speedMs, 0.0f, 500.0f) : 50.0f;
        lastAmount_ = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 1.0f;

        auto& engine = activeEngine();
        static_cast<void>(engine.setScale(scaleRatios,
                                          numberOfScaleRatios,
                                          rootFrequency,
                                          2.0));

        const int channels = std::min({ buffer.getNumChannels(),
                                        channelCount_,
                                        maxSupportedChannels });
        const int samples = buffer.getNumSamples();
        if (channels <= 0 || samples <= 0)
            return;

        std::array<float, maxSupportedChannels> input {};
        std::array<float, maxSupportedChannels> output {};
        for (int sample = 0; sample < samples; ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
                input[static_cast<std::size_t>(channel)] = buffer.getSample(channel, sample);

            engine.processFrame(input.data(), output.data(), channels,
                                lastSpeedMs_, lastAmount_, humanize_);

            for (int channel = 0; channel < channels; ++channel)
                buffer.setSample(channel, sample, output[static_cast<std::size_t>(channel)]);

            updateStableDuration(engine.metering().pitch.state);
        }
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const std::vector<double>& scaleRatios,
                 double rootFrequency,
                 float speedMs,
                 float amount)
    {
        process(buffer,
                scaleRatios.empty() ? nullptr : scaleRatios.data(),
                static_cast<int>(scaleRatios.size()),
                rootFrequency,
                speedMs,
                amount);
    }

    void process(float* data,
                 int numberOfSamples,
                 const std::vector<double>& scaleRatios,
                 double rootFrequency,
                 float speedMs,
                 float amount)
    {
        if (data == nullptr || numberOfSamples <= 0)
            return;

        lastSpeedMs_ = std::isfinite(speedMs) ? std::clamp(speedMs, 0.0f, 500.0f) : 50.0f;
        lastAmount_ = std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 1.0f;

        auto& engine = activeEngine();
        static_cast<void>(engine.setScale(scaleRatios.empty() ? nullptr : scaleRatios.data(),
                                          static_cast<int>(scaleRatios.size()),
                                          rootFrequency,
                                          2.0));
        for (int sample = 0; sample < numberOfSamples; ++sample)
        {
            const float input = data[sample];
            float output = 0.0f;
            engine.processFrame(&input, &output, 1,
                                lastSpeedMs_, lastAmount_, humanize_);
            data[sample] = output;
            updateStableDuration(engine.metering().pitch.state);
        }
    }

    void processBypassed(juce::AudioBuffer<float>& buffer)
    {
        auto& engine = activeEngine();
        const int channels = std::min({ buffer.getNumChannels(),
                                        channelCount_,
                                        maxSupportedChannels });
        const int samples = buffer.getNumSamples();
        if (channels <= 0 || samples <= 0)
            return;

        std::array<float, maxSupportedChannels> input {};
        std::array<float, maxSupportedChannels> output {};
        for (int sample = 0; sample < samples; ++sample)
        {
            for (int channel = 0; channel < channels; ++channel)
                input[static_cast<std::size_t>(channel)] = buffer.getSample(channel, sample);

            engine.processBypassedFrame(input.data(), output.data(), channels,
                                        lastSpeedMs_, lastAmount_, humanize_);

            for (int channel = 0; channel < channels; ++channel)
                buffer.setSample(channel, sample, output[static_cast<std::size_t>(channel)]);

            updateStableDuration(engine.metering().pitch.state);
        }
    }

    [[nodiscard]] int getLatencySamples() const noexcept
    {
        return activeEngineConst().latencySamples();
    }

    [[nodiscard]] LatencyMode getLatencyMode() const noexcept
    {
        return static_cast<LatencyMode>(
            activeModeIndex_.load(std::memory_order_acquire));
    }

    [[nodiscard]] float getDetectedPitchHz() const noexcept
    {
        return getMetering().detectedPitchHz;
    }

    [[nodiscard]] float getDetectionConfidence() const noexcept
    {
        return getMetering().confidence;
    }

    [[nodiscard]] Metering getMetering() const noexcept
    {
        const auto source = activeEngineConst().metering();
        Metering result;
        result.detectedPitchHz = source.pitch.hasStable
            ? source.pitch.stableHz
            : source.pitch.measuredHz;
        result.targetPitchHz = static_cast<float>(source.targetPitchHz);
        result.confidence = source.pitch.confidence;
        result.voicing = source.pitch.state == neumaton::pitch::TrackingState::stable
            ? 1.0f
            : source.pitch.periodicity;
        result.harmonicity = source.pitch.periodicity;
        result.spectralReliability = source.pitch.confidence;
        result.maskStability = 1.0f;
        // UI compatibility only: the old "Consensus" meter displays the one
        // detector's periodicity. No consensus mechanism exists in V1.
        result.consensus = source.pitch.periodicity;
        result.correctionCents = static_cast<float>(source.correctionCents);
        result.wetMix = 1.0f;
        result.detectorSupport = source.pitch.newMeasurement ? 1 : 0;
        result.octaveState = source.pitch.octaveAmbiguous ? 1 : 0;
        result.sustainedNoteSeconds = static_cast<float>(
            static_cast<double>(sustainedStableSamples_) / std::max(8000.0, sampleRate_));
        result.state = toUiState(source.pitch);

        // Tempo UI is retained as state only until its trajectory semantics are
        // explicitly rebuilt. It has no audible authority in this engine.
        result.tempoBpm = tempoHostPosition_.hasBpm
            ? static_cast<float>(tempoHostPosition_.bpm)
            : static_cast<float>(tempoSettings_.fallbackBpm);
        result.tempoMode = tempoSettings_.mode;
        result.tempoHostSyncValid = tempoHostPosition_.hasBpm;
        result.tempoActive = false;
        result.tempoWaitingForGrid = false;
        return result;
    }

private:
    static constexpr int engineCount = 3;

    [[nodiscard]] static int toModeIndex(LatencyMode mode) noexcept
    {
        return std::clamp(static_cast<int>(mode), 0, engineCount - 1);
    }

    [[nodiscard]] static neumaton::pitch::LatencyMode toPitchMode(
        LatencyMode mode) noexcept
    {
        switch (mode)
        {
            case LatencyMode::ultraLive: return neumaton::pitch::LatencyMode::lowLatency128;
            case LatencyMode::live:      return neumaton::pitch::LatencyMode::live256;
            case LatencyMode::quality:   return neumaton::pitch::LatencyMode::studio512;
        }
        return neumaton::pitch::LatencyMode::live256;
    }

    [[nodiscard]] static TrackingState toUiState(
        const neumaton::pitch::PitchResult& pitch) noexcept
    {
        switch (pitch.state)
        {
            case neumaton::pitch::TrackingState::acquire:
                return pitch.gateOpen ? TrackingState::acquire : TrackingState::unvoiced;
            case neumaton::pitch::TrackingState::stable:
                return TrackingState::stable;
            case neumaton::pitch::TrackingState::transition:
                return TrackingState::transition;
        }
        return TrackingState::unvoiced;
    }

    neumaton::PitchEngineV1& activeEngine() noexcept
    {
        const int index = activeModeIndex_.load(std::memory_order_acquire);
        auto& request = resetRequested_[static_cast<std::size_t>(index)];
        auto& engine = engines_[static_cast<std::size_t>(index)];
        if (request.exchange(false, std::memory_order_acq_rel))
        {
            engine.reset();
            sustainedStableSamples_ = 0;
        }
        return engine;
    }

    [[nodiscard]] const neumaton::PitchEngineV1& activeEngineConst() const noexcept
    {
        const int index = activeModeIndex_.load(std::memory_order_acquire);
        return engines_[static_cast<std::size_t>(index)];
    }

    void updateStableDuration(neumaton::pitch::TrackingState state) noexcept
    {
        if (state == neumaton::pitch::TrackingState::stable)
            ++sustainedStableSamples_;
        else
            sustainedStableSamples_ = 0;
    }

    std::array<neumaton::PitchEngineV1, engineCount> engines_;
    std::array<std::atomic<bool>, engineCount> resetRequested_ {};
    std::atomic<int> activeModeIndex_ { static_cast<int>(LatencyMode::live) };
    std::atomic<bool> prepared_ { false };

    double sampleRate_ = 48000.0;
    int maximumBlockSize_ = 512;
    int channelCount_ = 1;
    float humanize_ = 0.20f;
    float minimumPitchHz_ = 45.0f;
    float maximumPitchHz_ = 1600.0f;
    float lastSpeedMs_ = 50.0f;
    float lastAmount_ = 1.0f;

    bool scaleLock_ = false;
    float lockHysteresis_ = 24.0f;
    float vibratoPreserve_ = 0.0f;
    CreativeTempo::Settings tempoSettings_;
    CreativeTempo::HostPosition tempoHostPosition_;

    bool hostTransportHistoryValid_ = false;
    std::int64_t expectedNextHostSample_ = 0;
    std::uint64_t sustainedStableSamples_ = 0;
};

// Temporary source-compatibility alias for UI/processor code that still spells
// the old type name. It is not the old DSP class and carries no legacy engine.
using ModernPitchEngine = LivePitchProcessor;
