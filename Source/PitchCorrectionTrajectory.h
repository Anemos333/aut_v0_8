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
        upperEntryObservations_ = 0;
        lowerOwnsBoundary_ = false;
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
                // The upper adjacent degree owns a true ambiguous midpoint.
                // Keep a two-sided, evidence-gated musical decision: crossing
                // either Schmitt boundary never switches a target by itself.
                const auto sameNote = [](double a, double b) noexcept
                {
                    return a > 0.0 && b > 0.0
                        && std::abs(1200.0 * std::log2(a / b)) < 0.1;
                };
                const double stability = std::clamp(
                    static_cast<double>(boundaryStability), 0.0, 1.0);
                const int lowerConfirmations = 3
                    + static_cast<int>(std::lround(5.0 * stability));
                const int upperConfirmations = 4
                    + static_cast<int>(std::lround(4.0 * stability));
                const auto keepTarget = [&](double targetHz) noexcept
                {
                    target.targetHz = targetHz;
                    target.correctionCents = 1200.0
                        * std::log2(targetHz / observation.stableHz);
                };
                const auto forgetBoundary = [&]() noexcept
                {
                    ambiguousUpperHz_ = 0.0;
                    ambiguousLowerHz_ = 0.0;
                    ambiguousMidpointHz_ = 0.0;
                    ambiguityWidthCents_ = 0.0;
                    lowerOwnsBoundary_ = false;
                    lowerExitObservations_ = 0;
                    upperEntryObservations_ = 0;
                };

                // Revalidate only on fresh F0 evidence, never per audio sample.
                if (ambiguousUpperHz_ > 0.0 && observation.newMeasurement)
                {
                    const auto legalCheck = quantizer.quantize(
                        ambiguousUpperHz_, 1.0f, 0.0f, boundaryStability);
                    if (!legalCheck.valid
                        || !sameNote(legalCheck.targetHz, ambiguousUpperHz_))
                        forgetBoundary();
                }

                if (target.ambiguousBoundary)
                {
                    const bool samePair = sameNote(target.targetHz, ambiguousUpperHz_)
                        && sameNote(target.lowerTargetHz, ambiguousLowerHz_);
                    if (!samePair)
                    {
                        // A new midpoint gets upper priority on first contact.
                        ambiguousUpperHz_ = target.targetHz;
                        ambiguousLowerHz_ = target.lowerTargetHz;
                        ambiguousMidpointHz_ = target.boundaryMidpointHz;
                        ambiguityWidthCents_ = target.boundaryHalfWidthCents;
                        lowerOwnsBoundary_ = false;
                        upperEntryObservations_ = 0;
                    }

                    if (lowerOwnsBoundary_)
                    {
                        if (observation.newMeasurement)
                            ++upperEntryObservations_;
                        if (upperEntryObservations_ < upperConfirmations)
                            keepTarget(ambiguousLowerHz_);
                        else
                        {
                            lowerOwnsBoundary_ = false;
                            upperEntryObservations_ = 0;
                        }
                    }
                    lowerExitObservations_ = 0;
                }
                else if (ambiguousUpperHz_ > 0.0)
                {
                    const double gapCents = std::abs(1200.0
                        * std::log2(ambiguousUpperHz_ / ambiguousLowerHz_));
                    const double exitCents = std::min(0.35 * gapCents,
                        std::max(ambiguityWidthCents_ + 3.0,
                                 (0.15 + 0.12 * stability) * gapCents));
                    const double signedMidpointCents = 1200.0
                        * std::log2(observation.stableHz / ambiguousMidpointHz_);

                    if (sameNote(target.targetHz, ambiguousLowerHz_))
                    {
                        if (lowerOwnsBoundary_)
                        {
                            // A lower-owned note must itself gather upper evidence
                            // around the old midpoint before ownership returns.
                            if (signedMidpointCents
                                >= -1.5 * ambiguityWidthCents_)
                            {
                                if (observation.newMeasurement)
                                    ++upperEntryObservations_;
                            }
                            else
                            {
                                upperEntryObservations_ = 0;
                            }
                            if (upperEntryObservations_ >= upperConfirmations)
                            {
                                lowerOwnsBoundary_ = false;
                                upperEntryObservations_ = 0;
                                keepTarget(ambiguousUpperHz_);
                            }
                        }
                        else
                        {
                            if (signedMidpointCents < -exitCents)
                            {
                                if (observation.newMeasurement)
                                    ++lowerExitObservations_;
                            }
                            else
                            {
                                lowerExitObservations_ = 0;
                            }

                            if (lowerExitObservations_ < lowerConfirmations)
                                keepTarget(ambiguousUpperHz_);
                            else
                            {
                                lowerOwnsBoundary_ = true;
                                lowerExitObservations_ = 0;
                            }
                        }
                    }
                    else if (sameNote(target.targetHz, ambiguousUpperHz_))
                    {
                        if (lowerOwnsBoundary_)
                        {
                            // A genuinely higher note has already moved well
                            // inside the upper scale cell: do not delay legato.
                            const bool deepUpper = signedMidpointCents
                                >= 0.25 * gapCents;
                            if (deepUpper)
                            {
                                lowerOwnsBoundary_ = false;
                                upperEntryObservations_ = 0;
                            }
                            else
                            {
                                if (observation.newMeasurement)
                                    ++upperEntryObservations_;
                                if (upperEntryObservations_ < upperConfirmations)
                                    keepTarget(ambiguousLowerHz_);
                                else
                                {
                                    lowerOwnsBoundary_ = false;
                                    upperEntryObservations_ = 0;
                                }
                            }
                        }
                        lowerExitObservations_ = 0;
                    }
                    else
                    {
                        // Non-adjacent/other musical changes are unaffected.
                        forgetBoundary();
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
    int upperEntryObservations_ = 0;
    bool lowerOwnsBoundary_ = false;
};

} // namespace neumaton::render
