
#include "ScaleLockPitchEngine.h"

void ScaleLockPitchEngine::prepare(double sampleRate,
                                   int maximumExpectedSamplesPerBlock,
                                   int numberOfChannels,
                                   LatencyMode latencyMode)
{
    latencyMode_ = latencyMode;
    engine_.prepare(sampleRate, maximumExpectedSamplesPerBlock, numberOfChannels, latencyMode);
}

void ScaleLockPitchEngine::reset() noexcept
{
    engine_.reset();
}

ModernPitchEngine::Parameters ScaleLockPitchEngine::makeHardLockParameters(const Parameters& input) const noexcept
{
    Parameters p = input;

    // This engine is meaningful only as the Scale Lock path.
    p.scaleLock = true;
    p.hardLockActive = true;

    // Internal default: hard enough to feel millimetric, not maxed out.
    // Expose lockStrictness to the UI later if desired.
    p.lockStrictness = std::clamp(p.lockStrictness <= 0.0f ? 0.85f : p.lockStrictness, 0.0f, 1.0f);

    // Keep the user's musical intent, but avoid soft autotune behaviour when this
    // path is selected. The actual smoothing still happens inside the controller.
    const float hard = p.lockStrictness;
    p.amount = std::clamp(p.amount, 0.0f, 1.0f);
    p.retuneTimeMs = std::max(0.25f, p.retuneTimeMs * (1.0f - 0.55f * hard));
    p.transitionTimeMs = std::max(3.0f, p.transitionTimeMs * (1.0f - 0.45f * hard));

    // Do not kill expressivity completely: let ScaleLock::Processor preserve
    // vibrato using periodicity/stability/breathiness gates.
    p.vibratoPreserve = std::clamp(p.vibratoPreserve, 0.0f, 1.0f);

    // Keep all three modern latency modes; no special handling for old Slow/High latency.
    p.latencyMode = static_cast<int>(latencyMode_);

    return p;
}

void ScaleLockPitchEngine::process(juce::AudioBuffer<float>& buffer,
                                   const double* scaleRatios,
                                   int numberOfScaleRatios,
                                   double rootFrequency,
                                   const Parameters& parameters)
{
    const Parameters hard = makeHardLockParameters(parameters);
    engine_.process(buffer, scaleRatios, numberOfScaleRatios, rootFrequency, hard);
}

void ScaleLockPitchEngine::process(juce::AudioBuffer<float>& buffer,
                                   const double* scaleRatios,
                                   int numberOfScaleRatios,
                                   double rootFrequency,
                                   const Parameters& parameters,
                                   const CreativeTempo::HostPosition& hostTempoPosition)
{
    const Parameters hard = makeHardLockParameters(parameters);
    engine_.process(buffer, scaleRatios, numberOfScaleRatios, rootFrequency, hard, hostTempoPosition);
}

void ScaleLockPitchEngine::processBypassed(juce::AudioBuffer<float>& buffer)
{
    engine_.processBypassed(buffer);
}

int ScaleLockPitchEngine::getLatencySamples() const noexcept
{
    return engine_.getLatencySamples();
}

ScaleLockPitchEngine::LatencyMode ScaleLockPitchEngine::getLatencyMode() const noexcept
{
    return latencyMode_;
}

ScaleLockPitchEngine::Metering ScaleLockPitchEngine::getMetering() const noexcept
{
    return engine_.getMetering();
}
