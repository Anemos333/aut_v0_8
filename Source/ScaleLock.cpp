#include "ScaleLock.h"

namespace ScaleLock
{
    void Processor::reset()
    {
        std::fill(delayBufferCents.begin(), delayBufferCents.end(), 0.0);
        delayWriteIndex = 0;
        delayTargetValid = false;
        delayTargetLog2 = 0.0;
        heldDelaySamples = 0.0;
        smoothedDelaySamples = 0.0;
    }

    int Processor::wrapDelayIndex(int index) noexcept
    {
        index %= delayBufferSize;
        return index < 0 ? index + delayBufferSize : index;
    }

    double Processor::stableHash01(double value) noexcept
    {
        // Deterministic, cheap, stable for a given target. No <random> in audio.
        const double s = std::sin(value * 127.1 + 311.7) * 43758.5453123;
        return s - std::floor(s);
    }

    double Processor::calculateHysteresis(const Parameters& params) const
    {
        // Quality = neutral. Live and UltraLive reduce hysteresis moderately.
        // Avoid the old very aggressive 0.33 collapse for dense scales.
        double modeFactor = 1.0;
        if (params.latencyMode <= 0)
            modeFactor = 0.55;       // UltraLive / Experimental
        else if (params.latencyMode == 1)
            modeFactor = 0.78;       // Live

        const double tempoFactor = params.tempoLockActive ? 1.25 : 1.0;

        const double confidence = std::clamp(static_cast<double>(params.confidence), 0.0, 1.0);
        const double confidenceFactor = 0.55 + 0.45 * confidence;

        double minStep = static_cast<double>(params.minScaleStepCents);
        if (! std::isfinite(minStep) || minStep <= 0.0)
        {
            const int safeSize = std::max(1, params.scaleSize);
            minStep = 1200.0 / static_cast<double>(safeSize);
        }
        minStep = std::clamp(minStep, 0.1, 1200.0);

        double hysteresis = static_cast<double>(params.userHysteresis)
            * modeFactor
            * tempoFactor
            * confidenceFactor;

        // Main microtonal safety rule: never allow lock hysteresis to consume
        // almost half or more of the smallest adjacent scale interval.
        const double strictness = std::clamp(static_cast<double>(params.strictness), 0.0, 1.0);
const double hardFactor = params.hardLock
    ? (0.45 - 0.17 * strictness)   // strict=1 => max 28% del passo minimo
    : 0.45;

hysteresis = std::min(hysteresis, minStep * hardFactor);
        return std::clamp(hysteresis, 0.0, 100.0);
    }

    double Processor::calculatePreservedVibrato(double vibratoComponent,
                                                double effectiveHysteresis,
                                                const Parameters& params) const
    {
        if (params.vibratoAmount <= 0.0f)
            return 0.0;

        double targetBoundarySafety = 1.0;

        if (effectiveHysteresis > 1.0e-6)
        {
            const double absVibrato = std::abs(vibratoComponent);
            const double softLimit = effectiveHysteresis * 0.8;
            const double fadeWidth = std::max(1.0e-6, effectiveHysteresis * 0.2);

            if (absVibrato > softLimit)
            {
                targetBoundarySafety = std::clamp(
                    1.0 - (absVibrato - softLimit) / fadeWidth,
                    0.0, 1.0);
            }
        }

        double baseVibrato = vibratoComponent
            * static_cast<double>(params.vibratoAmount)
            * static_cast<double>(params.stability)
            * static_cast<double>(params.periodicity)
            * targetBoundarySafety;

        baseVibrato *= (1.0 + static_cast<double>(params.humanize)
            * 0.5 * static_cast<double>(params.stability));
        baseVibrato *= static_cast<double>(params.confidence);
        baseVibrato *= (1.0 - static_cast<double>(params.breathiness));

        return baseVibrato;
    }

    double Processor::updateNoteDelaySamples(double targetLog2,
                                             double sampleRate,
                                             const Parameters& params) noexcept
    {
        if (! std::isfinite(sampleRate) || sampleRate <= 0.0)
            sampleRate = 48000.0;

        const double targetDistanceCents = delayTargetValid
            ? std::abs((targetLog2 - delayTargetLog2) * 1200.0)
            : 1000.0;

        // Re-seed only when the quantized target changes, not on every pitch wobble.
        if (! delayTargetValid || targetDistanceCents > 1.0)
        {
            delayTargetValid = true;
            delayTargetLog2 = targetLog2;

            const double random = stableHash01(targetLog2
                + static_cast<double>(params.minScaleStepCents) * 0.001);

            const double maxDelayMs = 15.0 * std::clamp(static_cast<double>(params.humanize), 0.0, 1.0);
            const double delayMs = maxDelayMs * (0.35 + 0.65 * random);

            heldDelaySamples = std::clamp(delayMs * 0.001 * sampleRate,
                                          0.0,
                                          static_cast<double>(delayBufferSize - 2));
        }

        // Smooth the delay itself so the read position slews instead of jumping.
        const double smoothingMs = 6.0;
        const double alpha = 1.0 - std::exp(-1.0 / (sampleRate * smoothingMs * 0.001));
        smoothedDelaySamples += alpha * (heldDelaySamples - smoothedDelaySamples);

        return std::clamp(smoothedDelaySamples,
                          0.0,
                          static_cast<double>(delayBufferSize - 2));
    }

    double Processor::readDelayedError(double delaySamples) const noexcept
    {
        const double clampedDelay = std::clamp(delaySamples,
                                               0.0,
                                               static_cast<double>(delayBufferSize - 2));

        const double readPosition = static_cast<double>(delayWriteIndex) - clampedDelay;
        const int baseIndex = static_cast<int>(std::floor(readPosition));
        const double frac = readPosition - static_cast<double>(baseIndex);

        const int index0 = wrapDelayIndex(baseIndex);
        const int index1 = wrapDelayIndex(baseIndex + 1);

        const double a = delayBufferCents[static_cast<std::size_t>(index0)];
        const double b = delayBufferCents[static_cast<std::size_t>(index1)];

        return a + (b - a) * frac;
    }

    ProcessResult Processor::process(double observedLog2,
                                     double targetLog2,
                                     double pitchCentreLog2,
                                     const Parameters& params,
                                     double sampleRate)
    {
        ProcessResult result;
        result.effectiveHysteresis = calculateHysteresis(params);

        const double vibratoComponent = observedLog2 - pitchCentreLog2;
        const double vibratoComponentCents = vibratoComponent * 1200.0;

        const double preservedVibratoCents = calculatePreservedVibrato(
            vibratoComponentCents, result.effectiveHysteresis, params);

        const double correctedLog2 = targetLog2 + (preservedVibratoCents / 1200.0);
        double errorCents = (correctedLog2 - observedLog2) * 1200.0;

        // Scale Lock must stay precise: humanize deadband max 0.3 cent.
        const double deadBandCents = std::min(12.0 * static_cast<double>(params.humanize), 0.3);

        if (std::abs(errorCents) <= deadBandCents)
            errorCents = 0.0;
        else
            errorCents = std::copysign(std::abs(errorCents) - deadBandCents, errorCents);

        const double delaySamples = updateNoteDelaySamples(targetLog2, sampleRate, params);

        delayBufferCents[static_cast<std::size_t>(delayWriteIndex)] = errorCents;
        const double delayedErrorCents = readDelayedError(delaySamples);
        delayWriteIndex = wrapDelayIndex(delayWriteIndex + 1);

        result.targetCorrectionCents = delayedErrorCents;
        result.preservedVibratoCents = preservedVibratoCents;
        return result;
    }
}

