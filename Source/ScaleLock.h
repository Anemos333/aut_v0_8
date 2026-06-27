#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ScaleLock
{
    struct Parameters
    {
        float userHysteresis = 24.0f; // 0-80 cents
        float vibratoAmount = 0.0f;   // 0-1
        float humanize = 0.20f;       // 0-1
        int latencyMode = 1;    
        float strictness = 0.0f; // 0..1, usato solo dall'hard Scale Lock
        bool hardLock = false;// 0=UltraLive, 1=Live, 2=Quality
        
            // External context
        float confidence = 1.0f;
        float breathiness = 0.0f;
        bool tempoLockActive = false;
        int scaleSize = 12;           // fallback only
        float minScaleStepCents = 100.0f;
        float stability = 1.0f;
        float periodicity = 1.0f;
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

        ProcessResult process(double observedLog2,
                              double targetLog2,
                              double pitchCentreLog2,
                              const Parameters& params,
                              double sampleRate);

        void reset();
        double calculateHysteresis(const Parameters& params) const;

    private:
        static constexpr int delayBufferSize = 2048;

        std::vector<double> delayBufferCents = std::vector<double>(
            static_cast<std::size_t>(delayBufferSize), 0.0);

        int delayWriteIndex = 0;
        bool delayTargetValid = false;
        double delayTargetLog2 = 0.0;
        double heldDelaySamples = 0.0;
        double smoothedDelaySamples = 0.0;

        static int wrapDelayIndex(int index) noexcept;
        static double stableHash01(double value) noexcept;
        static double sanitisedMinStepCents(const Parameters& params) noexcept;
        static double smoothStep01(double x) noexcept;

        double updateNoteDelaySamples(double targetLog2,
                                      double sampleRate,
                                      const Parameters& params) noexcept;

        double readDelayedError(double delaySamples) const noexcept;

        double calculatePreservedVibrato(double vibratoComponent,
                                         double effectiveHysteresis,
                                         const Parameters& params) const;
    };
}
