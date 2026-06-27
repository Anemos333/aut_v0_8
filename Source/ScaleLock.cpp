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

    double Processor::smoothStep01(double x) noexcept
    {
        x = std::clamp(x, 0.0, 1.0);
        return x * x * (3.0 - 2.0 * x);
    }

    double Processor::sanitisedMinStepCents(const Parameters& params) noexcept
    {
        double minStep = static_cast<double>(params.minScaleStepCents);
        if (!std::isfinite(minStep) || minStep <= 0.0)
        {
            const int safeSize = std::max(1, params.scaleSize);
            minStep = 1200.0 / static_cast<double>(safeSize);
        }

        return std::clamp(minStep, 0.1, 1200.0);
    }

    double Processor::calculateHysteresis(const Parameters& params) const
    {
        const double minStep = sanitisedMinStepCents(params);
        const double userHysteresis = std::clamp(static_cast<double>(params.userHysteresis),
                                                 0.0,
                                                 80.0);

        const double confidence = std::clamp(static_cast<double>(params.confidence), 0.0, 1.0);
        const double strictness = std::clamp(static_cast<double>(params.strictness), 0.0, 1.0);

        // In hard Scale Lock non vogliamo che una confidence non perfetta renda il
        // lock molle. La confidence resta una protezione, ma meno punitiva.
        const double confidenceFactor = params.hardLock
            ? (0.82 + 0.18 * confidence)
            : (0.55 + 0.45 * confidence);

        const double tempoFactor = params.tempoLockActive ? 1.15 : 1.0;

        // Strictness non riduce il cap geometrico: un Hysteresis alto deve restare
        // più sticky. La sicurezza microtonale è garantita dal limite < metà passo.
        const double scaleSafeCap = minStep * 0.45;

        const double strictnessAssist = params.hardLock
            ? (1.0 + 0.12 * strictness)
            : 1.0;

        double hysteresis = userHysteresis
            * confidenceFactor
            * tempoFactor
            * strictnessAssist;

        hysteresis = std::min(hysteresis, scaleSafeCap);
        return std::clamp(hysteresis, 0.0, 100.0);
    }

    double Processor::calculatePreservedVibrato(double vibratoComponent,
                                                double effectiveHysteresis,
                                                const Parameters& params) const
    {
        const double requested = std::clamp(static_cast<double>(params.vibratoAmount), 0.0, 1.0);
        if (requested <= 0.0)
            return 0.0;

        const double minStep = sanitisedMinStepCents(params);
        const double confidence = std::clamp(static_cast<double>(params.confidence), 0.0, 1.0);
        const double breathiness = std::clamp(static_cast<double>(params.breathiness), 0.0, 1.0);
        const double stability = std::clamp(static_cast<double>(params.stability), 0.0, 1.0);
        const double periodicity = std::clamp(static_cast<double>(params.periodicity), 0.0, 1.0);
        const double strictness = std::clamp(static_cast<double>(params.strictness), 0.0, 1.0);

        const double periodicTrust = smoothStep01((periodicity - 0.42) / 0.42);
        const double stableTrust = smoothStep01((stability - 0.28) / 0.50);
        const double breathTrust = 1.0 - smoothStep01((breathiness - 0.35) / 0.45);

        const double confidenceTrust = params.hardLock
            ? (0.70 + 0.30 * confidence)
            : confidence;

        // Con scale dense il vibrato preservato deve restare dentro una frazione
        // sicura del passo minimo, altrimenti ricrea il flip tra due target.
        const double maxPreservedCents = minStep * (0.18 + 0.24 * requested);
        const double strictSafety = params.hardLock
            ? (1.0 - 0.12 * strictness)
            : 1.0;

        const double clampedResidual = std::clamp(vibratoComponent,
                                                  -maxPreservedCents * strictSafety,
                                                   maxPreservedCents * strictSafety);

        double preserved = clampedResidual
            * requested
            * periodicTrust
            * stableTrust
            * breathTrust
            * confidenceTrust;

        // Humanize può ammorbidire appena il gesto, ma non deve gonfiare il vibrato
        // oltre il limite microtonale.
        preserved *= (1.0 + 0.10 * std::clamp(static_cast<double>(params.humanize), 0.0, 1.0));

        return std::clamp(preserved,
                          -maxPreservedCents * strictSafety,
                           maxPreservedCents * strictSafety);
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

