#include "ScaleLock.h"

namespace ScaleLock
{
    void Processor::reset()
    {
        std::fill(delayBufferCents.begin(), delayBufferCents.end(), 0.0);
        delayIndex = 0;
    }

    double Processor::calculateHysteresis(const Parameters& params) const
    {
        double modeFactor = 1.0;
        if (params.latencyMode == 2) modeFactor = 1.0; // Quality
        else if (params.latencyMode == 1) modeFactor = 0.66; // Live
        else modeFactor = 0.33; // UltraLive/Experimental

        double tempoFactor = params.tempoLockActive ? 1.5 : 1.0; 
        
        double scaleDensityFactor = 1.0;
        if (params.scaleSize > 12) {
            scaleDensityFactor = 12.0 / static_cast<double>(params.scaleSize);
        }

        const double confidence = std::clamp (static_cast<double> (params.confidence), 0.0, 1.0);

// Non far collassare l'isteresi quando il detector è incerto.
// A bassa confidence manteniamo almeno il 55% dell'isteresi richiesta.
const double confidenceFactor = 0.55 + 0.45 * confidence;

double hysteresis = static_cast<double> (params.userHysteresis)
                  * modeFactor
                  * tempoFactor
                  * scaleDensityFactor
                  * confidenceFactor;
        
        if (params.tempoLockActive) {
            hysteresis += 15.0; 
        }

        return std::clamp(hysteresis, 0.0, 100.0);
    }

    double Processor::calculatePreservedVibrato(double vibratoComponent, double effectiveHysteresis, const Parameters& params) const
    {
        if (params.vibratoAmount <= 0.0f)
            return 0.0;

        double targetBoundarySafety = 1.0;

if (effectiveHysteresis > 1.0e-6)
{
    const double absVibrato = std::abs (vibratoComponent);
    const double softLimit = effectiveHysteresis * 0.8;
    const double fadeWidth = std::max (1.0e-6, effectiveHysteresis * 0.2);

    if (absVibrato > softLimit)
        targetBoundarySafety = std::clamp (
            1.0 - (absVibrato - softLimit) / fadeWidth,
            0.0, 1.0);
}

        double baseVibrato = vibratoComponent 
                           * static_cast<double>(params.vibratoAmount)
                           * static_cast<double>(params.stability)
                           * static_cast<double>(params.periodicity)
                           * targetBoundarySafety;
                           
        // Humanize Vibrato Pass
        baseVibrato *= (1.0 + static_cast<double>(params.humanize) * 0.5 * static_cast<double>(params.stability));
        
        baseVibrato *= static_cast<double>(params.confidence);
        baseVibrato *= (1.0 - static_cast<double>(params.breathiness));

        return baseVibrato;
    }

    ProcessResult Processor::process(
        double observedLog2, 
        double targetLog2, 
        double pitchCentreLog2,
        const Parameters& params,
        double sampleRate)
    {
        ProcessResult result;
        result.effectiveHysteresis = calculateHysteresis(params);

        double vibratoComponent = observedLog2 - pitchCentreLog2;
        double vibratoComponentCents = vibratoComponent * 1200.0;
        
        double preservedVibratoCents = calculatePreservedVibrato(vibratoComponentCents, result.effectiveHysteresis, params);
        
        double correctedLog2 = targetLog2 + (preservedVibratoCents / 1200.0);
        double errorCents = (correctedLog2 - observedLog2) * 1200.0;

        // Humanize Pitch Window: 0% = 0 cents, 50% = 6 cents, 100% = 12 cents
        double deadBandCents = 12.0 * static_cast<double>(params.humanize);
        
        if (std::abs(errorCents) <= deadBandCents) {
            errorCents = 0.0;
        } else {
            errorCents = std::copysign(std::abs(errorCents) - deadBandCents, errorCents);
        }

        // Humanize Timing: 0% = 0ms, 100% = up to 15ms
        // Introduciamo un po' di pseudo-randomicità se humanize è alto (usiamo observedLog2 come seed "rumoroso" veloce)
        double randomFactor = std::abs(std::sin(observedLog2 * 10000.0)); 
        double maxDelayMs = 15.0 * static_cast<double>(params.humanize);
        double delayMs = maxDelayMs * (0.5 + 0.5 * randomFactor * static_cast<double>(params.humanize));
        
        int targetDelaySamples = static_cast<int>(delayMs * 0.001 * sampleRate);
        targetDelaySamples = std::clamp(targetDelaySamples, 0, 1024);
        
        delayBufferCents[static_cast<std::size_t>(delayIndex)] = errorCents;
        int readIndex = (delayIndex - targetDelaySamples + 1024) % 1024;
        double delayedErrorCents = delayBufferCents[readIndex];
        
        delayIndex = (delayIndex + 1) % 1024;

        result.targetCorrectionCents = delayedErrorCents;
        result.preservedVibratoCents = preservedVibratoCents;

        return result;
    }
}
