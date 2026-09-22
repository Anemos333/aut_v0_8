#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

class F0PeriodicGateV1 final
{
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate) : 48000.0;
        windowSamples_ = std::clamp(
            static_cast<int>(std::lround(0.0090 * sampleRate_)),
            96,
            maxWindowSamples);
        hopSamples_ = std::max(8, static_cast<int>(std::lround(sampleRate_ / 1500.0)));
        reset();
    }

    void reset() noexcept
    {
        ring_.fill(0.0f);
        writePosition_ = 0;
        availableSamples_ = 0;
        hopCounter_ = 0;
        measure_ = true;
    }

    [[nodiscard]] bool processSample(float inputSample) noexcept
    {
        if (!std::isfinite(inputSample) || std::fpclassify(inputSample) == FP_SUBNORMAL)
            inputSample = 0.0f;

        ring_[static_cast<std::size_t>(writePosition_)] = inputSample;
        writePosition_ = (writePosition_ + 1) & ringMask;
        availableSamples_ = std::min(availableSamples_ + 1, maxWindowSamples);

        if (++hopCounter_ < hopSamples_)
            return measure_;
        hopCounter_ = 0;

        if (availableSamples_ < windowSamples_)
        {
            measure_ = true;
            return measure_;
        }

        double energy = 0.0;
        double deltaEnergy = 0.0;
        double lagOneNumerator = 0.0;
        double lagOneEnergyA = 0.0;
        double lagOneEnergyB = 0.0;
        int zeroCrossings = 0;

        const int start = (writePosition_ - windowSamples_ + ringSize) & ringMask;
        float previous = ring_[static_cast<std::size_t>(start)];
        energy += static_cast<double>(previous) * previous;

        for (int index = 1; index < windowSamples_; ++index)
        {
            const float current = ring_[static_cast<std::size_t>((start + index) & ringMask)];
            energy += static_cast<double>(current) * current;

            const double delta = static_cast<double>(current) - previous;
            deltaEnergy += delta * delta;

            if ((current >= 0.0f) != (previous >= 0.0f))
                ++zeroCrossings;

            lagOneNumerator += static_cast<double>(previous) * current;
            lagOneEnergyA += static_cast<double>(previous) * previous;
            lagOneEnergyB += static_cast<double>(current) * current;
            previous = current;
        }

        const double rms = std::sqrt(energy / static_cast<double>(windowSamples_));
        if (rms < 1.0e-6)
        {
            measure_ = false;
            return measure_;
        }

        const double zcr = static_cast<double>(zeroCrossings)
                         / static_cast<double>(std::max(1, windowSamples_ - 1));
        const double roughness = std::sqrt(
            deltaEnergy / static_cast<double>(std::max(1, windowSamples_ - 1)))
            / std::max(1.0e-12, rms);
        const double lagOneDenominator = std::sqrt(
            std::max(1.0e-24, lagOneEnergyA * lagOneEnergyB));
        const double lagOneCorrelation = lagOneNumerator / lagOneDenominator;

        // Reject only an extreme aperiodic/high-frequency signature. Ordinary
        // white/coloured noise is deliberately allowed to pass when ambiguous;
        // the detector validator, not this gate, owns stable-F0 truth.
        const bool certainAperiodic = (zcr > 0.60
                                    && roughness > 1.48
                                    && lagOneCorrelation < -0.10)
                                || (zcr > 0.48
                                    && roughness > 1.45
                                    && lagOneCorrelation < 0.05);
        measure_ = !certainAperiodic;
        return measure_;
    }

    [[nodiscard]] bool shouldMeasure() const noexcept { return measure_; }

private:
    static constexpr int ringSize = 2048;
    static constexpr int ringMask = ringSize - 1;
    static constexpr int maxWindowSamples = 1024;
    static_assert((ringSize & (ringSize - 1)) == 0, "ring size must be a power of two");

    std::array<float, ringSize> ring_ {};
    double sampleRate_ = 48000.0;
    int windowSamples_ = 432;
    int hopSamples_ = 32;
    int writePosition_ = 0;
    int availableSamples_ = 0;
    int hopCounter_ = 0;
    bool measure_ = true;
};
