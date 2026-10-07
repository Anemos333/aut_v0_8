#pragma once

#include <JuceHeader.h>
#include "PitchEngineV1.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
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

    // Only quantities produced by the active V1 path belong here.
    struct Metering
    {
        float detectedPitchHz = 0.0f;
        float targetPitchHz = 0.0f;
        float confidence = 0.0f;
        float periodicity = 0.0f;
        float correctionCents = 0.0f;
        float sustainedNoteSeconds = 0.0f;

        int targetDegreeIndex = -1;
        int targetDegreeCount = 0;

        bool gateOpen = false;
        bool newMeasurement = false;
        bool octaveAmbiguous = false;
        std::uint64_t rendererSplices = 0;
        TrackingState state = TrackingState::unvoiced;
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
                sampleRate_, channelCount_,
                toPitchMode(static_cast<LatencyMode>(modeIndex)),
                minimumPitchHz_, maximumPitchHz_);
            resetRequested_[static_cast<std::size_t>(modeIndex)].store(
                false, std::memory_order_relaxed);
        }

        activeModeIndex_.store(toModeIndex(latencyMode), std::memory_order_release);
        sustainedStableSamples_ = 0;
    }

    void reset() noexcept
    {
        for (auto& engine : engines_)
            engine.reset();
        for (auto& request : resetRequested_)
            request.store(false, std::memory_order_relaxed);
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

    // Kept as the small processor-facing setup API. Only Humanize and the pitch
    // range are meaningful in V1; the remaining arguments are intentionally
    // ignored instead of reviving legacy detector/renderer authority.
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

    void process(juce::AudioBuffer<float>& buffer,
                 const double* scaleRatios,
                 int numberOfScaleRatios,
                 double rootFrequency,
                 float speedMs,
                 float amount,
                 double equaveRatio = 2.0)
    {
        lastSpeedMs_ = std::isfinite(speedMs)
            ? std::clamp(speedMs, 0.0f, 500.0f) : 50.0f;
        lastAmount_ = std::isfinite(amount)
            ? std::clamp(amount, 0.0f, 1.0f) : 1.0f;

        auto& engine = activeEngine();
        static_cast<void>(engine.setScale(scaleRatios,
                                          numberOfScaleRatios,
                                          rootFrequency,
                                          sanitiseEquave(equaveRatio)));

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
                input[static_cast<std::size_t>(channel)] =
                    buffer.getSample(channel, sample);

            engine.processFrame(input.data(), output.data(), channels,
                                lastSpeedMs_, lastAmount_, humanize_);

            for (int channel = 0; channel < channels; ++channel)
                buffer.setSample(channel, sample,
                                 output[static_cast<std::size_t>(channel)]);

            updateStableDuration(engine.metering().pitch.state);
        }
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const std::vector<double>& scaleRatios,
                 double rootFrequency,
                 float speedMs,
                 float amount,
                 double equaveRatio = 2.0)
    {
        process(buffer,
                scaleRatios.empty() ? nullptr : scaleRatios.data(),
                static_cast<int>(scaleRatios.size()),
                rootFrequency,
                speedMs,
                amount,
                equaveRatio);
    }

    void process(float* data,
                 int numberOfSamples,
                 const std::vector<double>& scaleRatios,
                 double rootFrequency,
                 float speedMs,
                 float amount,
                 double equaveRatio = 2.0)
    {
        if (data == nullptr || numberOfSamples <= 0)
            return;

        lastSpeedMs_ = std::isfinite(speedMs)
            ? std::clamp(speedMs, 0.0f, 500.0f) : 50.0f;
        lastAmount_ = std::isfinite(amount)
            ? std::clamp(amount, 0.0f, 1.0f) : 1.0f;

        auto& engine = activeEngine();
        static_cast<void>(engine.setScale(
            scaleRatios.empty() ? nullptr : scaleRatios.data(),
            static_cast<int>(scaleRatios.size()),
            rootFrequency,
            sanitiseEquave(equaveRatio)));

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
                input[static_cast<std::size_t>(channel)] =
                    buffer.getSample(channel, sample);

            engine.processBypassedFrame(input.data(), output.data(), channels,
                                        lastSpeedMs_, lastAmount_, humanize_);

            for (int channel = 0; channel < channels; ++channel)
                buffer.setSample(channel, sample,
                                 output[static_cast<std::size_t>(channel)]);

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
        result.periodicity = source.pitch.periodicity;
        result.correctionCents = static_cast<float>(source.correctionCents);
        result.targetDegreeIndex = source.targetDegreeIndex;
        result.targetDegreeCount = source.targetDegreeCount;
        result.gateOpen = source.pitch.gateOpen;
        result.newMeasurement = source.pitch.newMeasurement;
        result.octaveAmbiguous = source.pitch.octaveAmbiguous;
        result.rendererSplices = source.rendererSplices;
        result.sustainedNoteSeconds = static_cast<float>(
            static_cast<double>(sustainedStableSamples_)
            / std::max(8000.0, sampleRate_));
        result.state = toUiState(source.pitch);
        return result;
    }

private:
    static constexpr int engineCount = 3;

    [[nodiscard]] static double sanitiseEquave(double equave) noexcept
    {
        return std::isfinite(equave) && equave > 1.0 ? equave : 2.0;
    }

    [[nodiscard]] static int toModeIndex(LatencyMode mode) noexcept
    {
        return std::clamp(static_cast<int>(mode), 0, engineCount - 1);
    }

    [[nodiscard]] static neumaton::pitch::LatencyMode toPitchMode(
        LatencyMode mode) noexcept
    {
        switch (mode)
        {
            case LatencyMode::ultraLive:
                return neumaton::pitch::LatencyMode::lowLatency128;
            case LatencyMode::live:
                return neumaton::pitch::LatencyMode::live256;
            case LatencyMode::quality:
                return neumaton::pitch::LatencyMode::studio512;
        }
        return neumaton::pitch::LatencyMode::live256;
    }

    [[nodiscard]] static TrackingState toUiState(
        const neumaton::pitch::PitchResult& pitch) noexcept
    {
        switch (pitch.state)
        {
            case neumaton::pitch::TrackingState::acquire:
                return pitch.gateOpen ? TrackingState::acquire
                                      : TrackingState::unvoiced;
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

    double sampleRate_ = 48000.0;
    int maximumBlockSize_ = 512;
    int channelCount_ = 1;
    float humanize_ = 0.20f;
    float minimumPitchHz_ = 45.0f;
    float maximumPitchHz_ = 1600.0f;
    float lastSpeedMs_ = 50.0f;
    float lastAmount_ = 1.0f;
    std::uint64_t sustainedStableSamples_ = 0;
};
