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

        const float h = parameters_.lockHysteresis / 80.0f;
        const float hysteresisStrictness = h * h * (3.0f - 2.0f * h); // smoothstep

        const int modeIndex = activeModeIndex_.load(std::memory_order_acquire);
        const float liveBoost = modeIndex == static_cast<int>(LatencyMode::live)
            ? 0.03f : 0.0f;
        const float experimentalBoost = modeIndex == static_cast<int>(LatencyMode::ultraLive)
            ? 0.12f : 0.0f;

        parameters_.lockStrictness = scaleLock
            ? std::clamp(hysteresisStrictness + liveBoost + experimentalBoost,
                         0.0f, 1.0f)
            : 0.0f;

        parameters_.hardLockActive = scaleLock;
    }

    void setTempoHostPosition(const CreativeTempo::HostPosition& position) noexcept
    {
        tempoHostPosition_ = position;
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
        updateScaleLockContext(scaleRatios, numberOfScaleRatios);

        const auto evidence = analyseEvidence(buffer);
        const auto conditioned = conditionedParameters(evidence);

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
        updateScaleLockContext(scaleRatios.empty() ? nullptr : scaleRatios.data(),
                               static_cast<int>(scaleRatios.size()));

        float* channels[] { data };
        juce::AudioBuffer<float> view(channels, 1, numberOfSamples);
        const auto evidence = analyseEvidence(view);
        const auto conditioned = conditionedParameters(evidence);

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
        const VoiceEvidence& evidence) const noexcept
    {
        ModernPitchEngine::Parameters conditioned = parameters_;

        const float reliability = std::clamp(evidence.spectralReliability, 0.0f, 1.0f);
        const float breathiness = std::clamp(evidence.breathiness, 0.0f, 1.0f);
        const float event = std::clamp(evidence.eventStrength, 0.0f, 1.0f);
        const float harmonicity = std::clamp(evidence.harmonicity, 0.0f, 1.0f);
        const float formantStability = std::clamp(evidence.formantStability, 0.0f, 1.0f);
        const float secondHarmonic = std::clamp(evidence.secondHarmonicDominance, 0.0f, 1.0f);
        const float polyphonyRisk = std::clamp(evidence.polyphonyRisk, 0.0f, 1.0f);

        // Detector evidence may make the tracker more permissive around
        // breathy/second-harmonic-dominant material, but it never lowers
        // Amount or scales the requested correction destination.
        conditioned.detectorSensitivity = std::clamp(
            conditioned.detectorSensitivity
                + 0.08f * breathiness
                + 0.06f * secondHarmonic
                + 0.03f * (1.0f - reliability),
            0.0f, 1.0f);

        // Formant and breath controls remain user-authoritative: zero stays
        // zero; the detectors only decide when/how strongly the requested
        // processing should engage on the same transported signal.
        conditioned.formantPreservation = std::clamp(
            conditioned.formantPreservation
                * (0.62f + 0.38f * formantStability)
                * (1.0f - 0.30f * event),
            0.0f, 1.0f);
        conditioned.breathReduction = std::clamp(
            conditioned.breathReduction * (0.10f + 0.90f * breathiness),
            0.0f, 1.0f);
        conditioned.transientProtection = std::clamp(
            conditioned.transientProtection * (0.45f + 0.55f * event),
            0.0f, 1.0f);

        const float vibratoEvidence = std::clamp(
            0.25f + 0.75f * harmonicity * formantStability,
            0.0f, 1.0f);
        conditioned.preserveVibrato = std::clamp(
            conditioned.preserveVibrato * vibratoEvidence, 0.0f, 1.0f);
        conditioned.vibratoPreserve = std::clamp(
            conditioned.vibratoPreserve * vibratoEvidence, 0.0f, 1.0f);

        if (conditioned.scaleLock)
        {
            const float identityGuard = std::clamp(
                0.78f
                + 0.18f * reliability
                + 0.18f * secondHarmonic
                + 0.22f * polyphonyRisk,
                0.55f, 1.35f);
            conditioned.lockHysteresis = std::clamp(
                conditioned.lockHysteresis * identityGuard,
                0.0f, 80.0f);
            conditioned.lockStrictness = std::clamp(
                conditioned.lockStrictness
                    * (0.88f + 0.12f * reliability)
                    + 0.08f * secondHarmonic,
                0.0f, 1.0f);
        }

        return conditioned;
    }

    [[nodiscard]] static float calculateMinScaleStepCents(
        const double* scaleRatios,
        int numberOfScaleRatios) noexcept
    {
        std::array<double, ModernPitchEngine::maxScaleRatios + 1> cents {};
        int count = 0;

        cents[static_cast<std::size_t>(count++)] = 0.0; // unisono sempre presente

        if (scaleRatios != nullptr && numberOfScaleRatios > 0)
        {
            const int safeCount = std::min(numberOfScaleRatios,
                                           ModernPitchEngine::maxScaleRatios);

            for (int i = 0; i < safeCount
                 && count < static_cast<int>(cents.size()); ++i)
            {
                const double ratio = scaleRatios[i];
                if (!std::isfinite(ratio) || ratio <= 0.0)
                    continue;

                const double logRatio = std::log2(ratio);
                double folded = 1200.0 * (logRatio - std::floor(logRatio));

                if (folded < 0.0)
                    folded += 1200.0;
                if (folded >= 1199.9999)
                    folded = 0.0;

                cents[static_cast<std::size_t>(count++)] = folded;
            }
        }

        std::sort(cents.begin(), cents.begin() + count);

        int uniqueCount = 0;
        for (int i = 0; i < count; ++i)
        {
            const double value = cents[static_cast<std::size_t>(i)];
            if (uniqueCount == 0
                || std::abs(value
                    - cents[static_cast<std::size_t>(uniqueCount - 1)]) > 1.0e-4)
            {
                cents[static_cast<std::size_t>(uniqueCount++)] = value;
            }
        }

        if (uniqueCount <= 1)
            return 1200.0f;

        double minStep = 1200.0;
        for (int i = 1; i < uniqueCount; ++i)
            minStep = std::min(minStep,
                               cents[static_cast<std::size_t>(i)]
                               - cents[static_cast<std::size_t>(i - 1)]);

        const double wrapStep = 1200.0
            - cents[static_cast<std::size_t>(uniqueCount - 1)]
            + cents[0];
        minStep = std::min(minStep, wrapStep);

        return static_cast<float>(std::clamp(minStep, 0.1, 1200.0));
    }

    void updateScaleLockContext(const double* scaleRatios,
                                int numberOfScaleRatios) noexcept
    {
        const int safeCount = (scaleRatios != nullptr && numberOfScaleRatios > 0)
            ? std::min(numberOfScaleRatios, ModernPitchEngine::maxScaleRatios)
            : 1;

        parameters_.scaleSize = safeCount;
        parameters_.minScaleStepCents = calculateMinScaleStepCents(
            scaleRatios, safeCount);
        parameters_.latencyMode = activeModeIndex_.load(std::memory_order_acquire);
    }

    std::array<ModernPitchEngine, engineCount> modernEngines_;
    std::array<std::atomic<bool>, engineCount> resetRequested_ {};
    std::atomic<int> activeModeIndex_ { static_cast<int>(LatencyMode::live) };
    std::atomic<bool> prepared_ { false };
    ModernPitchEngine::Parameters parameters_;
    VoiceEvidenceAnalyzer voiceEvidenceAnalyzer_;
    double sampleRate_ = 0.0;
    int maximumBlockSize_ = 0;
    int channelCount_ = 1;
    CreativeTempo::HostPosition tempoHostPosition_;
};
