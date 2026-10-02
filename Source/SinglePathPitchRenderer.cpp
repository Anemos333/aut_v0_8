#include "SinglePathPitchRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace neumaton::render
{
namespace
{
[[nodiscard]] float finiteSample(float x) noexcept
{
    if (!std::isfinite(x) || std::fpclassify(x) == FP_SUBNORMAL)
        return 0.0f;
    return std::clamp(x, -16.0f, 16.0f);
}

[[nodiscard]] int nextPowerOfTwo(int x) noexcept
{
    int result = 1;
    while (result < x)
        result <<= 1;
    return result;
}
}

void SinglePathPitchRenderer::prepare(double sampleRate,
                                      int channels,
                                      int latencySamples)
{
    sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate)
                                            : 48000.0;
    channels_ = std::clamp(channels, 1, maxChannels);
    latencySamples_ = std::clamp(latencySamples, 32, 4096);

    // History capacity is deliberately larger than the declared latency.
    // Capacity is not latency: at zero correction the single read head remains
    // exactly latencySamples_ behind the writer and the output is bit-identical
    // to an integer delay. The extra history exists only so a pitch-shift splice
    // can inspect older candidates without allocating in the audio callback.
    ringSize_ = nextPowerOfTwo(std::max(4096, latencySamples_ * 8));
    ringMask_ = ringSize_ - 1;
    for (auto& ring : ring_)
        ring.assign(static_cast<std::size_t>(ringSize_), 0.0f);

    reset();
}

void SinglePathPitchRenderer::reset() noexcept
{
    for (auto& ring : ring_)
        std::fill(ring.begin(), ring.end(), 0.0f);
    for (auto& history : outputHistory_)
        history.fill(0.0f);

    historyWrite_ = 0;
    historyAvailable_ = 0;
    writeSample_ = 0;
    readPosition_ = -static_cast<double>(latencySamples_);
    primed_ = false;
    spliceCount_ = 0;
    lastSpliceMismatch_ = 0.0f;
}

float SinglePathPitchRenderer::readInteger(int channel,
                                           std::int64_t absolutePosition) const noexcept
{
    if (absolutePosition < 0 || absolutePosition >= writeSample_)
        return 0.0f;
    if (writeSample_ - absolutePosition > ringSize_ - 4)
        return 0.0f;

    const auto index = static_cast<std::size_t>(absolutePosition & ringMask_);
    return ring_[static_cast<std::size_t>(channel)][index];
}

float SinglePathPitchRenderer::readInterpolated(int channel,
                                                double absolutePosition) const noexcept
{
    if (!std::isfinite(absolutePosition))
        return 0.0f;

    const auto i0 = static_cast<std::int64_t>(std::floor(absolutePosition));
    const double fraction = absolutePosition - static_cast<double>(i0);

    // Four-point cubic interpolation keeps the one-read-head architecture but
    // avoids the avoidable high-frequency thinning of linear interpolation.
    const double xm1 = readInteger(channel, i0 - 1);
    const double x0  = readInteger(channel, i0);
    const double x1  = readInteger(channel, i0 + 1);
    const double x2  = readInteger(channel, i0 + 2);

    const double c0 = x0;
    const double c1 = 0.5 * (x1 - xm1);
    const double c2 = xm1 - 2.5 * x0 + 2.0 * x1 - 0.5 * x2;
    const double c3 = 0.5 * (x2 - xm1) + 1.5 * (x0 - x1);

    return finiteSample(static_cast<float>(
        ((c3 * fraction + c2) * fraction + c1) * fraction + c0));
}

double SinglePathPitchRenderer::spliceScore(double candidateReadPosition) const noexcept
{
    if (historyAvailable_ < historySize)
        return 0.0;

    double error = 0.0;
    double reference = 1.0e-9;

    // This is measurement only. We compare the recent emitted waveform with
    // the waveform immediately preceding each possible new read position.
    // There is no overlap, no crossfade and no second rendered signal.
    for (int k = 0; k < historySize; ++k)
    {
        const int historyIndex = (historyWrite_ - 1 - k + historySize)
                               % historySize;
        const double sourcePosition = candidateReadPosition
                                    - 1.0 - static_cast<double>(k);

        for (int channel = 0; channel < channels_; ++channel)
        {
            const double emitted = outputHistory_[static_cast<std::size_t>(channel)]
                                                 [static_cast<std::size_t>(historyIndex)];
            const double source = readInterpolated(channel, sourcePosition);
            const double d = source - emitted;
            error += d * d;
            reference += emitted * emitted;
        }
    }

    return error / reference;
}

void SinglePathPitchRenderer::recenterReadHead(double ratio) noexcept
{
    if (!primed_ || std::abs(ratio - 1.0) < 1.0e-6)
        return;

    const double currentWrite = static_cast<double>(writeSample_);
    const double delay = currentWrite - readPosition_;

    // The read head oscillates around the declared latency. These are transport
    // bounds, not parallel delay paths. They prevent the head from reaching the
    // writer or drifting indefinitely into old audio.
    const double minimumDelay = std::max(12.0, 0.20 * latencySamples_);
    const double maximumDelay = std::max(minimumDelay + 16.0,
                                         1.80 * latencySamples_);

    if (delay >= minimumDelay && delay <= maximumDelay)
        return;

    const bool pitchUp = ratio > 1.0;
    const int firstDelay = static_cast<int>(std::ceil(
        pitchUp ? static_cast<double>(latencySamples_) : minimumDelay));
    const int lastDelay = static_cast<int>(std::floor(
        pitchUp ? maximumDelay : static_cast<double>(latencySamples_)));
    if (lastDelay <= firstDelay)
        return;

    double bestReadPosition = currentWrite
        - static_cast<double>(pitchUp ? lastDelay : firstDelay);
    double bestScore = std::numeric_limits<double>::max();
    const double span = std::max(1.0,
        static_cast<double>(lastDelay - firstDelay));

    for (int candidateDelay = firstDelay;
         candidateDelay <= lastDelay;
         ++candidateDelay)
    {
        const double candidate = currentWrite
                               - static_cast<double>(candidateDelay);
        if (candidate < 2.0 || candidate + 2.0 >= currentWrite)
            continue;

        double score = spliceScore(candidate);

        // A tiny deterministic bias chooses the point farther from the next
        // boundary when waveform matches are otherwise effectively equal.
        const double boundaryBias = pitchUp
            ? static_cast<double>(lastDelay - candidateDelay) / span
            : static_cast<double>(candidateDelay - firstDelay) / span;
        score += 0.002 * boundaryBias;

        if (score < bestScore)
        {
            bestScore = score;
            bestReadPosition = candidate;
        }
    }

    readPosition_ = bestReadPosition;
    lastSpliceMismatch_ = static_cast<float>(bestScore);
    ++spliceCount_;
}

void SinglePathPitchRenderer::processFrame(const float* input,
                                           float* output,
                                           int channels,
                                           double correctionCents) noexcept
{
    if (input == nullptr || output == nullptr)
        return;

    const int usedChannels = std::clamp(channels, 1, channels_);
    for (int channel = 0; channel < usedChannels; ++channel)
    {
        ring_[static_cast<std::size_t>(channel)]
             [static_cast<std::size_t>(writeSample_ & ringMask_)]
            = finiteSample(input[channel]);
    }

    if (!primed_ && writeSample_ >= latencySamples_)
    {
        readPosition_ = static_cast<double>(writeSample_ - latencySamples_);
        primed_ = true;
    }

    const double safeCents = std::isfinite(correctionCents)
        ? std::clamp(correctionCents, -1200.0, 1200.0)
        : 0.0;
    const double ratio = std::exp2(safeCents / 1200.0);

    if (!primed_)
    {
        for (int channel = 0; channel < usedChannels; ++channel)
            output[channel] = 0.0f;
    }
    else
    {
        recenterReadHead(ratio);
        for (int channel = 0; channel < usedChannels; ++channel)
            output[channel] = readInterpolated(channel, readPosition_);
        readPosition_ += ratio;
    }

    for (int channel = 0; channel < usedChannels; ++channel)
    {
        outputHistory_[static_cast<std::size_t>(channel)]
                      [static_cast<std::size_t>(historyWrite_)]
            = finiteSample(output[channel]);
    }
    historyWrite_ = (historyWrite_ + 1) % historySize;
    historyAvailable_ = std::min(historyAvailable_ + 1, historySize);
    ++writeSample_;
}

} // namespace neumaton::render
