#include <JuceHeader.h>
#include "../Source/LivePitchProcessor.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>

namespace
{
float oldPluginSanitise(float value) noexcept
{
    if (!std::isfinite(value) || std::fpclassify(value) == FP_SUBNORMAL)
        return 0.0f;
    return std::clamp(value, -32.0f, 32.0f);
}

bool sameBits(float a, float b) noexcept
{
    std::uint32_t aa = 0, bb = 0;
    std::memcpy(&aa, &a, sizeof(a));
    std::memcpy(&bb, &b, sizeof(b));
    return aa == bb;
}
}

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;

    std::array<double, 12> scale {};
    for (int i = 0; i < 12; ++i)
        scale[static_cast<std::size_t>(i)] = std::exp2(static_cast<double>(i) / 12.0);

    LivePitchProcessor raw;
    LivePitchProcessor presanitised;
    raw.prepare(sampleRate, blockSize, 1, ModernPitchEngine::LatencyMode::live);
    presanitised.prepare(sampleRate, blockSize, 1, ModernPitchEngine::LatencyMode::live);

    raw.setAdvancedParameters(35.0f, 0.25f, 0.90f, 0.70f,
                              12.0f, 45.0f, 1600.0f,
                              ModernPitchEngine::StereoMode::linkedMidSide);
    presanitised.setAdvancedParameters(35.0f, 0.25f, 0.90f, 0.70f,
                                       12.0f, 45.0f, 1600.0f,
                                       ModernPitchEngine::StereoMode::linkedMidSide);
    raw.setScaleLockParameters(true, 24.0f, 0.20f);
    presanitised.setScaleLockParameters(true, 24.0f, 0.20f);

    juce::AudioBuffer<float> a(1, blockSize);
    juce::AudioBuffer<float> b(1, blockSize);

    double phase = 0.0;
    for (int block = 0; block < 900; ++block)
    {
        float* ap = a.getWritePointer(0);
        float* bp = b.getWritePointer(0);

        for (int i = 0; i < blockSize; ++i)
        {
            const std::int64_t n = static_cast<std::int64_t>(block) * blockSize + i;
            phase += 2.0 * 3.14159265358979323846
                * (220.0 + 12.0 * std::sin(2.0 * 3.14159265358979323846 * 0.73
                  * static_cast<double>(n) / sampleRate))
                / sampleRate;
            if (phase > 2.0 * 3.14159265358979323846)
                phase -= 2.0 * 3.14159265358979323846;

            float x = static_cast<float>(0.17 * std::sin(phase)
                                       + 0.045 * std::sin(2.0 * phase));
            if ((n % 997) == 0) x = std::numeric_limits<float>::quiet_NaN();
            if ((n % 1231) == 0) x = std::numeric_limits<float>::infinity();
            if ((n % 1429) == 0) x = -std::numeric_limits<float>::infinity();
            if ((n % 811) == 0) x = std::numeric_limits<float>::denorm_min();
            if ((n % 1877) == 0) x = 64.0f;
            if ((n % 1999) == 0) x = -64.0f;

            ap[i] = x;
            bp[i] = oldPluginSanitise(x);
        }

        raw.process(a, scale.data(), static_cast<int>(scale.size()), 440.0, 8.0f, 1.0f);
        presanitised.process(b, scale.data(), static_cast<int>(scale.size()), 440.0, 8.0f, 1.0f);

        for (int i = 0; i < blockSize; ++i)
        {
            if (!sameBits(a.getSample(0, i), b.getSample(0, i)))
            {
                std::cerr << "PLUGIN_INPUT_SANITIZE_EQUIVALENCE=FAIL"
                          << " block=" << block << " sample=" << i
                          << " raw=" << a.getSample(0, i)
                          << " old=" << b.getSample(0, i) << "\n";
                return 2;
            }
        }

        const auto ma = raw.getMetering();
        const auto mb = presanitised.getMetering();
        if (!sameBits(ma.detectedPitchHz, mb.detectedPitchHz)
            || !sameBits(ma.targetPitchHz, mb.targetPitchHz)
            || !sameBits(ma.correctionCents, mb.correctionCents)
            || !sameBits(ma.confidence, mb.confidence)
            || !sameBits(ma.consensus, mb.consensus))
        {
            std::cerr << "PLUGIN_INPUT_SANITIZE_METER_EQUIVALENCE=FAIL block="
                      << block << "\n";
            return 3;
        }
    }

    std::cout << "PLUGIN_INPUT_SANITIZE_EQUIVALENCE=PASS\n";
    return 0;
}
