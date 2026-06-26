#pragma once

#include "ModernPitchEngine.h"

#include <algorithm>
#include <array>
#include <cmath>

// ScaleLockPitchEngine
// -----------------------------------------------------------------------------
// Thin alternative engine for Scale Lock mode.
// It deliberately reuses ModernPitchEngine instead of copying its full DSP code.
// The hard-lock behaviour is activated by ModernPitchEngine::Parameters fields
// added in README_modifiche_ScaleLockPitchEngine.md.
//
// Rationale:
// - same latency modes and same spectral engine as ModernPitchEngine;
// - no second 180 KB implementation to maintain;
// - only the musical decision policy changes when Scale Lock is active.
class ScaleLockPitchEngine final
{
public:
    using LatencyMode = ModernPitchEngine::LatencyMode;
    using StereoMode = ModernPitchEngine::StereoMode;
    using TrackingState = ModernPitchEngine::TrackingState;
    using Parameters = ModernPitchEngine::Parameters;
    using Metering = ModernPitchEngine::Metering;

    ScaleLockPitchEngine() = default;

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

    void processBypassed(juce::AudioBuffer<float>& buffer);

    [[nodiscard]] int getLatencySamples() const noexcept;
    [[nodiscard]] LatencyMode getLatencyMode() const noexcept;
    [[nodiscard]] Metering getMetering() const noexcept;

private:
    [[nodiscard]] Parameters makeHardLockParameters(const Parameters& input) const noexcept;

    ModernPitchEngine engine_;
    LatencyMode latencyMode_ = LatencyMode::live;
};

