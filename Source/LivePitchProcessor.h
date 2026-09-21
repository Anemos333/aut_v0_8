#pragma once

#include "ModernPitchEngine.h"
#include "VoiceEvidenceAnalyzer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

// Drop-in-oriented adapter for projects that already use the original
// LivePitchProcessor interface. All three release engines are prepared before
// the audio callback starts; a mode change is then a lock-free publication of
// the already prepared engine plus a bounded state reset on the audio thread.
//
// VoiceEvidenceAnalyzer is deliberately analysis-only. It may condition the
// clean engine's detector/target/formant controls, but it never renders audio,
// never supplies a dry signal and never scales Amount/correction authority.
class LivePitchProcessor final
{
public:
    using LatencyMode = ModernPitchEngine::LatencyMode;
    using StereoMode = ModernPitchEngine::StereoMode;
    using Metering = ModernPitchEngine::Metering;
    using VoiceEvidence = VoiceEvidenceAnalyzer::Evidence;

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
        channelCount_ = std::clamp(numberOfChannels, 1,
                                   ModernPitchEngine::maxSupportedChannels);
        for (int modeIndex = 0; modeIndex < engineCount; ++modeIndex)
        {
            modernEngines_[static_cast<std::size_t>(modeIndex)].prepare(
                sampleRate_, maximumBlockSize_, channelCount_,
                static_cast<LatencyMode>(modeIndex));

            resetRequested_[static_cast<std::size_t>(modeIndex)].store(
                false, std::memory_order_relaxed);
        }

        voiceEvidenceAnalyzer_.prepare(sampleRate_, maximumBlockSize_, channelCount_);
        voiceEvidencePrimed_ = false;
        hostTransportHistoryValid_ = false;
        hostPpqHistoryValid_ = false;
        expectedNextHostSample_ = 0;
        expectedNextHostPpq_ = 0.0;
        activeModeIndex_.store(toModeIndex(latencyMode),
                               std::memory_order_release);
        prepared_.store(true, std::memory_order_release);
    }

    void reset() noexcept
    {
        for (auto& engine : modernEngines_)
            engine.reset();

        for (auto& request : resetRequested_)
            request.store(false, std::memory_order_relaxed);

        voiceEvidenceAnalyzer_.reset();
        voiceEvidencePrimed_ = false;
        hostTransportHistoryValid_ = false;
        hostPpqHistoryValid_ = false;
        expectedNextHostSample_ = 0;
        expectedNextHostPpq_ = 0.0;
    }

    // Safe from the message thread while audio is running. No prepare(), heap
    // allocation, mutex or object mutation is performed on the published DSP.
    void setLatencyModeNonRealtime(LatencyMode mode) noexcept
    {
        const int modeIndex = toModeIndex(mode);
        if (modeIndex == activeModeIndex_.load(std::memory_order_acquire))
            return;

        // Only one ModernPitchEngine is audible/processed at a time. We do not
        // pre-render or crossfade parallel engines: that would violate the
        // single-audio-path contract. The selected prepared instance is reset
        // once at the block boundary; mode-switch continuity remains a release
        // validation item, but no legacy or secondary renderer is introduced.
        resetRequested_[static_cast<std::size_t>(modeIndex)].store(
            true, std::memory_order_release);
        activeModeIndex_.store(modeIndex, std::memory_order_release);
    }

    void setAdvancedParameters(float transitionMs,
                               float preserveVibrato,
                               float humanize,
                               float formantPreservation,
                               float transientProtection,
                               float detectorSensitivity,
                               float maximumCorrectionSemitones,
                               float minimumPitchHz,
                               float maximumPitchHz,
                               StereoMode stereoMode,
                               float breathReduction = 0.50f) noexcept
    {
        parameters_.transitionTimeMs = transitionMs;
        parameters_.preserveVibrato = preserveVibrato;
        parameters_.humanize = humanize;
        parameters_.formantPreservation = formantPreservation;
        parameters_.transientProtection = transientProtection;
        parameters_.detectorSensitivity = detectorSensitivity;

        parameters_.maximumCorrectionSemitones = std::clamp(
            maximumCorrectionSemitones, 0.0f, 48.0f);

        parameters_.minimumPitchHz = minimumPitchHz;
        parameters_.maximumPitchHz = maximumPitchHz;
        parameters_.stereoMode = stereoMode;
        parameters_.breathReduction = std::clamp(breathReduction, 0.0f, 1.0f);
    }

    void setTempoSettings(const CreativeTempo::Settings& settings) noexcept
    {
        parameters_.tempo = settings;
    }

    void setScaleLockParameters(bool scaleLock,
                                float lockHysteresis,
                                float vibratoPreserve) noexcept
    {
        parameters_.scaleLock = scaleLock;
        parameters_.lockHysteresis = std::clamp(lockHysteresis, 0.0f, 80.0f);
        parameters_.vibratoPreserve = std::clamp(vibratoPreserve, 0.0f, 1.0f);
        parameters_.preserveVibrato = parameters_.vibratoPreserve;

        // HOLD_SINGLE_OWNER_V1: the visible Hold radius is passed literally.
        // No derived strictness or hidden hard-lock state exists downstream.
    }

    void setTempoHostPosition(const CreativeTempo::HostPosition& position) noexcept
    {
        // TRANSPORT_REPLAY_DETERMINISM_V1
        // A non-looping seek/restart is a physical observation discontinuity.
        // Detector, correction, voice-evidence and renderer state from the old
        // timeline must not seed the new playback. This is transport hygiene,
        // never a confidence/F0 gate and never runs during contiguous playback.
        bool transportDiscontinuity = false;
        if (position.isPlaying && !position.isLooping)
        {
            if (hostTransportHistoryValid_ && position.hasTimeInSamples)
            {
                const auto error = std::llabs(position.timeInSamples
                                              - expectedNextHostSample_);
                const auto tolerance = static_cast<std::int64_t>(
                    std::max(4, std::max(1, position.numberOfSamples) * 2));
                transportDiscontinuity = error > tolerance;
            }
            else if (!position.hasTimeInSamples
                     && hostPpqHistoryValid_
                     && position.hasPpq
                     && position.hasBpm
                     && std::isfinite(position.ppqAtBlockStart)
                     && std::isfinite(position.bpm)
                     && position.bpm > 1.0)
            {
                const double expectedTravel = position.bpm
                    / (60.0 * std::max(8000.0, sampleRate_))
                    * static_cast<double>(std::max(1, position.numberOfSamples));
                const double tolerance = std::max(0.01, expectedTravel * 3.0);
                transportDiscontinuity = std::abs(position.ppqAtBlockStart
                                                  - expectedNextHostPpq_) > tolerance;
            }
        }

        if (transportDiscontinuity)
            reset();

        tempoHostPosition_ = position;

        if (position.isPlaying && position.hasTimeInSamples)
        {
            expectedNextHostSample_ = position.timeInSamples
                + static_cast<std::int64_t>(std::max(0, position.numberOfSamples));
            hostTransportHistoryValid_ = true;
        }

        if (position.isPlaying
            && position.hasPpq
            && position.hasBpm
            && std::isfinite(position.ppqAtBlockStart)
            && std::isfinite(position.bpm)
            && position.bpm > 1.0)
        {
            expectedNextHostPpq_ = position.ppqAtBlockStart
                + position.bpm / (60.0 * std::max(8000.0, sampleRate_))
                    * static_cast<double>(std::max(0, position.numberOfSamples));
            hostPpqHistoryValid_ = true;
        }
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const double* scaleRatios,
                 int numberOfScaleRatios,
                 double rootFrequency,
                 float speedMs,
                 float amount)
    {
        parameters_.retuneTimeMs = speedMs;
        parameters_.amount = amount;

        // Use only evidence published before this host block. The analyzer
        // may inspect the current input to prepare the next block, but it cannot
        // use future samples from this block to classify its first sample.
        const auto evidence = voiceEvidenceAnalyzer_.getLatest();
        const bool evidenceValid = voiceEvidencePrimed_;
        static_cast<void>(analyseEvidence(buffer));
        voiceEvidencePrimed_ = true;
        const auto conditioned = conditionedParameters(evidence, evidenceValid);

        activeModernEngine().process(buffer,
                                     scaleRatios,
                                     numberOfScaleRatios,
                                     rootFrequency,
                                     conditioned,
                                     tempoHostPosition_);
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

        parameters_.retuneTimeMs = speedMs;
        parameters_.amount = amount;

        float* channels[] { data };
        juce::AudioBuffer<float> view(channels, 1, numberOfSamples);
        const auto evidence = voiceEvidenceAnalyzer_.getLatest();
        const bool evidenceValid = voiceEvidencePrimed_;
        static_cast<void>(analyseEvidence(view));
        voiceEvidencePrimed_ = true;
        const auto conditioned = conditionedParameters(evidence, evidenceValid);

        activeModernEngine().process(view,
                                     scaleRatios.empty() ? nullptr : scaleRatios.data(),
                                     static_cast<int>(scaleRatios.size()),
                                     rootFrequency,
                                     conditioned);
    }

    void processBypassed(juce::AudioBuffer<float>& buffer)
    {
        // Evidence continues to advance in bypass so air/event/formant state
        // does not restart from zero. It still does not render any audio.
        static_cast<void>(analyseEvidence(buffer));
        voiceEvidencePrimed_ = true;
        activeModernEngine().processBypassed(buffer);
    }

    [[nodiscard]] int getLatencySamples() const noexcept
    {
        return activeModernEngineConst().getLatencySamples();
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

    [[nodiscard]] VoiceEvidence getVoiceEvidence() const noexcept
    {
        return voiceEvidenceAnalyzer_.getLatest();
    }

    [[nodiscard]] Metering getMetering() const noexcept
    {
        Metering result = activeModernEngineConst().getMetering();
        const VoiceEvidence evidence = voiceEvidenceAnalyzer_.getLatest();

        // Publish the richer legacy-derived analysis without resurrecting its
        // rendering logic. wetMix is intentionally fixed at unity because the
        // clean engine has one authoritative output path.
        result.harmonicity = evidence.harmonicity;
        result.breathiness = evidence.breathiness;
        result.noisePath = evidence.breathiness;
        result.polyphony = evidence.polyphonyRisk;
        result.spectralReliability = evidence.spectralReliability;
        result.maskStability = evidence.formantStability;
        result.wetMix = 1.0f;
        return result;
    }

private:
    static constexpr int engineCount = 3;

    [[nodiscard]] static int toModeIndex(LatencyMode mode) noexcept
    {
        return std::clamp(static_cast<int>(mode), 0, engineCount - 1);
    }

    ModernPitchEngine& activeModernEngine() noexcept
    {
        const int index = activeModeIndex_.load(std::memory_order_acquire);
        auto& resetRequest = resetRequested_[static_cast<std::size_t>(index)];
        auto& engine = modernEngines_[static_cast<std::size_t>(index)];

        if (resetRequest.exchange(false, std::memory_order_acq_rel))
            engine.reset();

        return engine;
    }

    [[nodiscard]] const ModernPitchEngine& activeModernEngineConst() const noexcept
    {
        const int index = activeModeIndex_.load(std::memory_order_acquire);
        return modernEngines_[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] VoiceEvidence analyseEvidence(
        const juce::AudioBuffer<float>& buffer) noexcept
    {
        const Metering meter = activeModernEngineConst().getMetering();
        VoiceEvidenceAnalyzer::Context context;
        context.detectedPitchHz = meter.detectedPitchHz;
        context.confidence = meter.confidence;
        context.periodicity = meter.harmonicity;
        context.consensus = meter.consensus;
        context.onsetStrength = meter.state == ModernPitchEngine::TrackingState::attack
            ? 1.0f : 0.0f;
        context.detectorSupport = meter.detectorSupport;
        return voiceEvidenceAnalyzer_.analyse(buffer, context);
    }

    [[nodiscard]] ModernPitchEngine::Parameters conditionedParameters(
        const VoiceEvidence& evidence,
        bool evidenceValid) const noexcept
    {
        ModernPitchEngine::Parameters conditioned = parameters_;

        // Sensor output is supervision data only. No user-authoritative audio
        // or correction parameter is multiplied by confidence, breathiness or
        // state evidence here. In particular Amount, Formant, Transient,
        // Vibrato and Breath Reduction keep the exact values selected in the UI.
        conditioned.voiceEvidenceValid = evidenceValid;
        conditioned.voiceHarmonicity = std::clamp(evidence.harmonicity, 0.0f, 1.0f);
        conditioned.voiceBreathiness = std::clamp(evidence.breathiness, 0.0f, 1.0f);
        conditioned.voiceBodyEnergy = std::clamp(evidence.voicedBodyEnergy, 0.0f, 1.0f);
        conditioned.voiceSpectralReliability = std::clamp(
            evidence.spectralReliability, 0.0f, 1.0f);
        conditioned.voiceEventStrength = std::clamp(evidence.eventStrength, 0.0f, 1.0f);
        conditioned.voiceFormantStability = std::clamp(
            evidence.formantStability, 0.0f, 1.0f);
        conditioned.voiceLowerFamilyEvidence = std::clamp(
            evidence.lowerFamilyEvidence, 0.0f, 1.0f);
        return conditioned;
    }


    std::array<ModernPitchEngine, engineCount> modernEngines_;
    std::array<std::atomic<bool>, engineCount> resetRequested_ {};
    std::atomic<int> activeModeIndex_ { static_cast<int>(LatencyMode::live) };
    std::atomic<bool> prepared_ { false };
    ModernPitchEngine::Parameters parameters_;
    VoiceEvidenceAnalyzer voiceEvidenceAnalyzer_;
    bool voiceEvidencePrimed_ = false;
    double sampleRate_ = 0.0;
    int maximumBlockSize_ = 0;
    int channelCount_ = 1;
    CreativeTempo::HostPosition tempoHostPosition_;
    bool hostTransportHistoryValid_ = false;
    bool hostPpqHistoryValid_ = false;
    std::int64_t expectedNextHostSample_ = 0;
    double expectedNextHostPpq_ = 0.0;
};
