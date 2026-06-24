#pragma once

#include <cmath>
#include <algorithm>
#include <vector>

namespace ScaleLock
{
    struct Parameters
    {
        float userHysteresis = 24.0f; // 0-80 cents
        float vibratoAmount = 0.0f;   // 0-1
        float humanize = 0.20f;       // 0-1
        int latencyMode = 1;          // 0=UltraLive, 1=Live, 2=Quality
        
        // External context
        float confidence = 1.0f;
        float breathiness = 0.0f;
        bool tempoLockActive = false;
        int scaleSize = 12;           // Number of notes in scale
        
        float stability = 1.0f;       // From engine
        float periodicity = 1.0f;     // From engine (voicing/harmonicity)
    };

    struct ProcessResult
    {
        double targetCorrectionCents = 0.0;
        double preservedVibratoCents = 0.0;
        double effectiveHysteresis = 0.0;
    };

    class Processor
    {
    public:
        Processor() = default;

        ProcessResult process(
            double observedLog2, 
            double targetLog2, 
            double pitchCentreLog2,
            const Parameters& params,
            double sampleRate);
            
        void reset();

        double calculateHysteresis(const Parameters& params) const;

    private:
        std::vector<double> delayBufferCents;
        int delayIndex = 0;

        double calculatePreservedVibrato(double vibratoComponent, double effectiveHysteresis, const Parameters& params) const;
    };
}
