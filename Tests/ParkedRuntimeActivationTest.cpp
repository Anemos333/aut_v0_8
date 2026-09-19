#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.1415926535897932384626433832795;
}

int main()
{
    ModernPitchEngine::MultiRatePitchTracker tracker;
    tracker.prepare(kSampleRate);

    if (tracker.analysisWorker_ == nullptr)
    {
        std::cerr << "PARKED_RUNTIME_WORKER_ACTIVE=FAIL\n";
        return 2;
    }

    ModernPitchEngine::PitchObservation observation;
    std::uint32_t noiseState = 0x51f15e5du;
    int detectorHops = 0;

    for (int sample = 0; sample < static_cast<int>(2.0 * kSampleRate); ++sample)
    {
        const double t = static_cast<double>(sample) / kSampleRate;
        std::uint32_t x = noiseState;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        noiseState = x;
        const float noise =
            static_cast<float>((static_cast<int>(x & 0xffffu) - 32768) / 32768.0)
            * 0.003f;
        const float input = static_cast<float>(
            0.18 * std::sin(2.0 * kPi * 220.0 * t)
          + 0.09 * std::sin(2.0 * kPi * 440.0 * t)) + noise;

        if (tracker.processSample(input, observation))
            ++detectorHops;
    }

    if (detectorHops <= 0)
    {
        std::cerr << "PARKED_RUNTIME_WORKER_ACTIVE=FAIL reason=no_hops\n";
        return 3;
    }

    std::cout << "PARKED_RUNTIME_WORKER_ACTIVE=PASS detector_hops="
              << detectorHops << "\n";
    return 0;
}
