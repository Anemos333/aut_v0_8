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
        ambiguousUpperHz_ = 0.0;
        ambiguousLowerHz_ = 0.0;
        ambiguousMidpointHz_ = 0.0;
        ambiguityWidthCents_ = 0.0;
        lowerExitObservations_ = 0;
    }

    [[nodiscard]] double process(const pitch::PitchResult& observation,
                                 const pitch::ScaleQuantizer& quantizer,
                                 float amount,
                                 float humanize,
                                 float speedMs,
                                 float boundaryStability = 0.5f) noexcept
    {
        if (observation.state == pitch::TrackingState::stable
            && observation.hasStable
            && std::isfinite(observation.stableHz)
            && observation.stableHz > 0.0f)
        {
            auto target = quantizer.quantize(observation.stableHz,
                                             amount, humanize,
                                             boundaryStability);
            if (target.valid)
            {
                // The upper legal degree owns the microscopic tie region.
                // Retain it across slight downward jitter; leaving the region
                // must be supported by two fresh, clearly lower measurements.
                if (target.ambiguousBoundary)
                {
                    ambiguousUpperHz_ = target.targetHz;
                    ambiguousLowerHz_ = target.lowerTargetHz;
                    ambiguousMidpointHz_ = target.boundaryMidpointHz;
                    ambiguityWidthCents_ = target.boundaryHalfWidthCents;
                    lowerExitObservations_ = 0;
                }
                else if (ambiguousUpperHz_ > 0.0)
                {
                    const auto sameNote = [](double a, double b) noexcept
                    {
                        return a > 0.0 && b > 0.0
                            && std::abs(1200.0 * std::log2(a / b)) < 0.1;
                    };
                    const auto legalCheck = quantizer.quantize(
                        ambiguousUpperHz_, 1.0f, 0.0f, boundaryStability);
                    if (!legalCheck.valid
                        || !sameNote(legalCheck.targetHz, ambiguousUpperHz_))
                    {
                        ambiguousUpperHz_ = 0.0; // Scale or root changed.
                    }
                    else if (sameNote(target.targetHz, ambiguousLowerHz_))
                    {
                        const double gap = std::abs(1200.0
                            * std::log2(ambiguousUpperHz_ / ambiguousLowerHz_));
                        const double exitCents = std::min(0.25 * gap,
                            std::max(ambiguityWidthCents_ + 3.0, 5.0));
                        const bool distinctlyLower = observation.stableHz
                            < ambiguousMidpointHz_ * std::exp2(-exitCents / 1200.0);
                        if (!distinctlyLower)
                            lowerExitObservations_ = 0;
                        else if (observation.newMeasurement)
                            ++lowerExitObservations_;

                        if (lowerExitObservations_ < 2)
                        {
                            target.targetHz = ambiguousUpperHz_;
                            target.correctionCents = 1200.0
                                * std::log2(target.targetHz / observation.stableHz);
                        }
                        else
                        {
                            ambiguousUpperHz_ = 0.0;
                            lowerExitObservations_ = 0;
                        }
                    }
                    else
                    {
                        // Normal note changes keep their original response.
                        ambiguousUpperHz_ = 0.0;
                        lowerExitObservations_ = 0;
                    }
                }

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
    double ambiguousUpperHz_ = 0.0;
    double ambiguousLowerHz_ = 0.0;
    double ambiguousMidpointHz_ = 0.0;
    double ambiguityWidthCents_ = 0.0;
    int lowerExitObservations_ = 0;
};

} // namespace neumaton::render
