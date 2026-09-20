#include "SingleWetSpectralRenderer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double sampleRate = 48000.0;

std::uint64_t hashFloat(std::uint64_t hash, float value)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    for (int byte = 0; byte < 4; ++byte)
    {
        hash ^= static_cast<std::uint8_t>((bits >> (8 * byte)) & 0xffu);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t runCase(int frameSize)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);

    std::uint64_t hash = 1469598103934665603ull;
    constexpr int samples = 96000;
    for (int sample = 0; sample < samples; ++sample)
    {
        const double t = static_cast<double>(sample) / sampleRate;
        const double envelope = sample < 24000 ? 0.16
            : sample < 48000 ? 0.035
            : sample < 72000 ? 0.23 : 0.09;
        const double source =
              0.55 * std::sin(2.0 * pi * 223.7 * t + 0.13)
            + 0.27 * std::sin(2.0 * pi * 447.4 * t + 0.71)
            + 0.13 * std::sin(2.0 * pi * 731.2 * t + 1.17)
            + 0.05 * std::sin(2.0 * pi * 1193.0 * t + 2.01);
        const float input = static_cast<float>(envelope * source);

        const double correction = sample < 32000 ? -73.25
            : sample < 64000 ? 137.60 : -41.75;
        const float formant = sample < 50000 ? 0.92f : 0.37f;
        const float output = renderer.processSample(input, correction, formant);
        hash = hashFloat(hash, output);
    }
    return hash;
}
}

int main()
{
    const auto hash256 = runCase(256);
    const auto hash512 = runCase(512);
    std::cout << std::hex
              << "RENDERER_TRANSPARENCY_HASH_256=" << hash256 << "\n"
              << "RENDERER_TRANSPARENCY_HASH_512=" << hash512 << "\n";
    return 0;
}
