#include "../Source/SingleWetSpectralRenderer.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.1415926535897932384626433832795;

double runCase(int frameSize, double audioSeconds)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(kSampleRate, frameSize);

    const std::int64_t samples = static_cast<std::int64_t>(
        std::llround(audioSeconds * kSampleRate));
    double phase1 = 0.0;
    double phase2 = 0.0;
    double checksum = 0.0;

    const auto start = std::chrono::steady_clock::now();
    for (std::int64_t sample = 0; sample < samples; ++sample)
    {
        const double t = static_cast<double>(sample) / kSampleRate;
        const double f0 = 113.0 + 17.0 * std::sin(2.0 * kPi * 0.41 * t);
        phase1 += 2.0 * kPi * f0 / kSampleRate;
        phase2 += 2.0 * kPi * (2.03 * f0) / kSampleRate;
        if (phase1 > 2.0 * kPi) phase1 -= 2.0 * kPi;
        if (phase2 > 2.0 * kPi) phase2 -= 2.0 * kPi;

        const float input = static_cast<float>(
            0.12 * std::sin(phase1)
          + 0.055 * std::sin(phase2)
          + 0.018 * std::sin(2.0 * kPi * 3170.0 * t));

        const double correction = 340.0 * std::sin(2.0 * kPi * 0.17 * t)
                                + 95.0 * std::sin(2.0 * kPi * 0.73 * t);
        const float formant = static_cast<float>(
            0.55 + 0.40 * std::sin(2.0 * kPi * 0.11 * t));

        const float output = renderer.processSample(input, correction, formant);
        checksum += static_cast<double>(output)
                  * (1.0 + static_cast<double>(sample & 31) * 0.0001);
    }
    const auto stop = std::chrono::steady_clock::now();

    const double ms = std::chrono::duration<double, std::milli>(
        stop - start).count();
    std::cout << "RENDERER_CPU frame=" << frameSize
              << " audio_s=" << audioSeconds
              << " elapsed_ms=" << ms
              << " rt=" << ms / (audioSeconds * 1000.0)
              << " checksum=" << checksum
              << "\n";
    return checksum;
}
}

int main()
{
    volatile double sink = 0.0;
    sink += runCase(256, 20.0);
    sink += runCase(512, 20.0);
    if (!std::isfinite(static_cast<double>(sink)))
        return 2;
    return 0;
}
