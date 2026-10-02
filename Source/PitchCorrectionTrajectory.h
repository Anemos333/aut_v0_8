#pragma once

#include "PitchCore.h"

#include <algorithm>
#include <cmath>

namespace neumaton::render
{

// Controller between stable F0/quantization and the one-path renderer.
// Transition never relaxes correction because of uncertainty: once a Stable
// transport exists, the last desired correction remains authoritative until a
// new Stable observation replaces it.
class PitchCorrectionTrajectory final
{
public:
    void prepare(double sampleRate,
                 double maximumCorrectionCents = 1200.0) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate)
                                                : 48000.0;
        maximumCorrectionCents_ = std::clamp(
            std::abs(maximumCorrectionCents), 0.0, 4800.0);
        reset();
    }

    void reset() noexcept
    {
        desiredCorrectionCents_ = 0.0;
        currentCorrectionCents_ = 0.0;
        targetPitchHz_ = 0.0;
        hasStableTransport_ = false;
    }

    [[nodiscard]] double process(const pitch::PitchResult& observation,
                                 const pitch::ScaleQuantizer& quantizer,
                                 float amount,
                                 float humanize,
                                 float speedMs) noexcept
    {
        if (observation.state == pitch::TrackingState::stable
            && observation.hasStable
            && std::isfinite(observation.stableHz)
            && observation.stableHz > 0.0f)
        {
            const auto target = quantizer.quantize(observation.stableHz,
                                                   amount,
                                                   humanize);
            if (target.valid)
            {
                const double required = target.correctionCents;
                const double window = std::max(0.0, target.liveWindowCents);
                const double residual = std::copysign(
                    std::min(std::abs(required), window), required);

                desiredCorrectionCents_ = std::clamp(
                    required - residual,
                    -maximumCorrectionCents_,
                    maximumCorrectionCents_);
                targetPitchHz_ = target.targetHz;
                hasStableTransport_ = true;
            }
        }
        else if (!hasStableTransport_)
        {
            // Before the first physically available Stable F0, the one path is
            // an identity transport at the declared latency. Once Stable has
            // existed, Acquire/Transition cannot use uncertainty to request
            // correction=0.
            desiredCorrectionCents_ = 0.0;
            targetPitchHz_ = 0.0;
        }

        const double safeSpeedMs = std::isfinite(speedMs)
            ? std::clamp(static_cast<double>(speedMs), 0.0, 500.0)
            : 50.0;

        if (safeSpeedMs <= 0.0)
        {
            currentCorrectionCents_ = desiredCorrectionCents_;
        }
        else
        {
            // Interpret Speed as approximately the time to reach 95% of the
            // newly requested correction. This is trajectory only, never a
            // confidence/authority weighting.
            const double samplesTo95 = std::max(
                1.0, 0.001 * safeSpeedMs * sampleRate_);
            const double coefficient = 1.0
                - std::exp(std::log(0.05) / samplesTo95);
            currentCorrectionCents_ += coefficient
                * (desiredCorrectionCents_ - currentCorrectionCents_);
        }

        if (!std::isfinite(currentCorrectionCents_))
            currentCorrectionCents_ = 0.0;

        return currentCorrectionCents_;
    }

    [[nodiscard]] double desiredCorrectionCents() const noexcept
    {
        return desiredCorrectionCents_;
    }

    [[nodiscard]] double currentCorrectionCents() const noexcept
    {
        return currentCorrectionCents_;
    }

    [[nodiscard]] double targetPitchHz() const noexcept
    {
        return targetPitchHz_;
    }

    [[nodiscard]] bool hasStableTransport() const noexcept
    {
        return hasStableTransport_;
    }

private:
    double sampleRate_ = 48000.0;
    double maximumCorrectionCents_ = 1200.0;
    double desiredCorrectionCents_ = 0.0;
    double currentCorrectionCents_ = 0.0;
    double targetPitchHz_ = 0.0;
    bool hasStableTransport_ = false;
};

} // namespace neumaton::render
