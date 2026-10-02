#include "PitchEngineV1.h"

#include <algorithm>
#include <cmath>

namespace neumaton
{

void PitchEngineV1::prepare(double sampleRate,
                            int channels,
                            pitch::LatencyMode mode,
                            float minimumPitchHz,
                            float maximumPitchHz) noexcept
{
    sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate)
                                            : 48000.0;
    channels_ = std::clamp(channels, 1, render::SinglePathPitchRenderer::maxChannels);
    mode_ = mode;

    pitchCore_.prepare(sampleRate_, mode_, minimumPitchHz, maximumPitchHz);
    trajectory_.prepare(sampleRate_, 1200.0);
    renderer_.prepare(sampleRate_, channels_, pitch::declaredLatencySamples(mode_));
    metering_ = {};
}

void PitchEngineV1::reset() noexcept
{
    pitchCore_.reset();
    trajectory_.reset();
    renderer_.reset();
    metering_ = {};
}

bool PitchEngineV1::setScale(const double* ratios,
                             int count,
                             double referenceHz,
                             double equaveRatio) noexcept
{
    return quantizer_.setScale(ratios, count, referenceHz, equaveRatio);
}

void PitchEngineV1::processFrame(const float* input,
                                 float* output,
                                 int channels,
                                 float speedMs,
                                 float amount,
                                 float humanize) noexcept
{
    if (input == nullptr || output == nullptr)
        return;

    const int usedChannels = std::clamp(channels, 1, channels_);
    double mono = 0.0;
    for (int channel = 0; channel < usedChannels; ++channel)
    {
        const float x = std::isfinite(input[channel]) ? input[channel] : 0.0f;
        mono += x;
    }
    mono /= static_cast<double>(usedChannels);

    const auto pitchResult = pitchCore_.processSample(static_cast<float>(mono));
    const double correction = trajectory_.process(
        pitchResult,
        quantizer_,
        std::clamp(amount, 0.0f, 1.0f),
        std::clamp(humanize, 0.0f, 1.0f),
        speedMs);

    renderer_.processFrame(input, output, usedChannels, correction);

    metering_.pitch = pitchResult;
    metering_.targetPitchHz = trajectory_.targetPitchHz();
    metering_.correctionCents = correction;
    metering_.rendererSplices = renderer_.spliceCount();
}

} // namespace neumaton
