#pragma once

#include "ModernPitchEngine.h"
#include "ScaleLockPitchEngine.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

// Drop-in-oriented adapter for projects that already use the original
// LivePitchProcessor interface. All three release engines are prepared before
// the audio callback starts; a mode change is then a lock-free publication of
// the already prepared engine plus a bounded state reset on the audio thread.
class LivePitchProcessor final
{
public:
    using LatencyMode = ModernPitchEngine::LatencyMode;
    using StereoMode = ModernPitchEngine::StereoMode;
    using Metering = ModernPitchEngine::Metering;

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

scaleLockEngines_[static_cast<std::size_t>(modeIndex)].prepare(
    sampleRate_, maximumBlockSize_, channelCount_,
    static_cast<LatencyMode>(modeIndex));
            resetRequested_[static_cast<std::size_t>(modeIndex)].store(
                false, std::memory_order_relaxed);
        }

        activeModeIndex_.store(toModeIndex(latencyMode),
                               std::memory_order_release);
        prepared_.store(true, std::memory_order_release);
    }

    void reset() noexcept
    {
        for (auto& engine : modernEngines_)
    engine.reset();

for (auto& engine : scaleLockEngines_)
    engine.reset();
        for (auto& request : resetRequested_)
            request.store(false, std::memory_order_relaxed);
    }

    // Safe from the message thread while audio is running. No prepare(), heap
    // allocation, mutex or object mutation is performed on the published DSP.
    void setLatencyModeNonRealtime(LatencyMode mode) noexcept
    {
        const int modeIndex = toModeIndex(mode);
        if (modeIndex == activeModeIndex_.load(std::memory_order_acquire))
            return;

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
        parameters_.maximumCorrectionSemitones = maximumCorrectionSemitones;
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
                            float vibratoPreserve,
                            float lockStrictness = 0.0f,
                            bool hardLock = false) noexcept
{
    parameters_.scaleLock = scaleLock;
    parameters_.lockHysteresis = lockHysteresis;
    parameters_.vibratoPreserve = vibratoPreserve;
    parameters_.lockStrictness = std::clamp(lockStrictness, 0.0f, 1.0f);
    parameters_.hardLockActive = hardLock;
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

        auto& engine = activeEngine();
        if (useScaleLockEngine())
{
    activeScaleLockEngine().process(buffer,
                                    scaleRatios,
                                    numberOfScaleRatios,
                                    rootFrequency,
                                    parameters_,
                                    tempoHostPosition_);
}
else
{
    activeModernEngine().process(buffer,
                                 scaleRatios,
                                 numberOfScaleRatios,
                                 rootFrequency,
                                 parameters_,
                                 tempoHostPosition_);
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
        parameters_.retuneTimeMs = speedMs;
        parameters_.amount = amount;
         
        updateScaleLockContext(scaleRatios.empty() ? nullptr : scaleRatios.data(),
                       static_cast<int>(scaleRatios.size()));


        float* channels[] { data };
        juce::AudioBuffer<float> view(channels, 1, numberOfSamples);
        if (useScaleLockEngine())
{
    activeScaleLockEngine().process(view,
                                    scaleRatios.empty() ? nullptr : scaleRatios.data(),
                                    static_cast<int>(scaleRatios.size()),
                                    rootFrequency,
                                    parameters_);
}
else
{
    activeModernEngine().process(view,
                                 scaleRatios.empty() ? nullptr : scaleRatios.data(),
                                 static_cast<int>(scaleRatios.size()),
                                 rootFrequency,
                                 parameters_);
}
    }

    void processBypassed(juce::AudioBuffer<float>& buffer)
    {
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

[[nodiscard]] Metering getMetering() const noexcept
{
    return useScaleLockEngine()
        ? activeScaleLockEngineConst().getMetering()
        : activeModernEngineConst().getMetering();
}

private:
    static constexpr int engineCount = 3;

    [[nodiscard]] static int toModeIndex(LatencyMode mode) noexcept
    {
        return std::clamp(static_cast<int>(mode), 0, engineCount - 1);
    }

    [[nodiscard]] bool useScaleLockEngine() const noexcept
{
    return parameters_.scaleLock && parameters_.hardLockActive && parameters_.lockStrictness > 0.001f;
}

ModernPitchEngine& activeModernEngine() noexcept
{
    const int index = activeModeIndex_.load(std::memory_order_acquire);
    auto& resetRequest = resetRequested_[static_cast<std::size_t>(index)];
    auto& engine = modernEngines_[static_cast<std::size_t>(index)];

    if (resetRequest.exchange(false, std::memory_order_acq_rel))
    {
        engine.reset();
        scaleLockEngines_[static_cast<std::size_t>(index)].reset();
    }

    return engine;
}
ScaleLockPitchEngine& activeScaleLockEngine() noexcept
{
    const int index = activeModeIndex_.load(std::memory_order_acquire);
    auto& resetRequest = resetRequested_[static_cast<std::size_t>(index)];
    auto& engine = scaleLockEngines_[static_cast<std::size_t>(index)];

    if (resetRequest.exchange(false, std::memory_order_acq_rel))
    {
        modernEngines_[static_cast<std::size_t>(index)].reset();
        engine.reset();
    }

    return engine;
}

   [[nodiscard]] const ModernPitchEngine& activeModernEngineConst() const noexcept
{
    const int index = activeModeIndex_.load(std::memory_order_acquire);
    return modernEngines_[static_cast<std::size_t>(index)];
}

[[nodiscard]] const ScaleLockPitchEngine& activeScaleLockEngineConst() const noexcept
{
    const int index = activeModeIndex_.load(std::memory_order_acquire);
    return scaleLockEngines_[static_cast<std::size_t>(index)];
}
[[nodiscard]] static float calculateMinScaleStepCents(const double* scaleRatios,
                                                      int numberOfScaleRatios) noexcept
{
    std::array<double, ModernPitchEngine::maxScaleRatios + 1> cents {};
    int count = 0;

    cents[static_cast<std::size_t>(count++)] = 0.0; // unisono sempre presente

    if (scaleRatios != nullptr && numberOfScaleRatios > 0)
    {
        const int safeCount = std::min(numberOfScaleRatios,
                                       ModernPitchEngine::maxScaleRatios);

        for (int i = 0; i < safeCount && count < static_cast<int>(cents.size()); ++i)
        {
            const double ratio = scaleRatios[i];
            if (! std::isfinite(ratio) || ratio <= 0.0)
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
            || std::abs(value - cents[static_cast<std::size_t>(uniqueCount - 1)]) > 1.0e-4)
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

void updateScaleLockContext(const double* scaleRatios, int numberOfScaleRatios) noexcept
{
    const int safeCount = (scaleRatios != nullptr && numberOfScaleRatios > 0)
        ? std::min(numberOfScaleRatios, ModernPitchEngine::maxScaleRatios)
        : 1;

    parameters_.scaleSize = safeCount;
    parameters_.minScaleStepCents = calculateMinScaleStepCents(scaleRatios, safeCount);
    parameters_.latencyMode = activeModeIndex_.load(std::memory_order_acquire);
   
}



    std::array<ModernPitchEngine, engineCount> modernEngines_;
 std::array<ScaleLockPitchEngine, engineCount> scaleLockEngines_;
    std::array<std::atomic<bool>, engineCount> resetRequested_ {};
    std::atomic<int> activeModeIndex_ { static_cast<int>(LatencyMode::live) };
    std::atomic<bool> prepared_ { false };
    ModernPitchEngine::Parameters parameters_;
    double sampleRate_ = 0.0;
    int maximumBlockSize_ = 0;
    int channelCount_ = 1;
    CreativeTempo::HostPosition tempoHostPosition_;
};
