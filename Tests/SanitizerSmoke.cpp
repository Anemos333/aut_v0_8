#include "../Source/ModernPitchEngine.h"
#include "../Source/LivePitchProcessor.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>

int main()
{
    std::array<double, 12> scale {};
    for (int i = 0; i < 12; ++i)
        scale[static_cast<std::size_t>(i)] = std::exp2(static_cast<double>(i) / 12.0);

    std::mt19937 random(0x6142u);
    std::uniform_real_distribution<float> sample(-1.0f, 1.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const std::array<double, 8> rates { 22050.0, 32000.0, 44100.0, 48000.0,
                                        88200.0, 96000.0, 176400.0, 192000.0 };
    const std::array<int, 11> blocks { 1, 2, 3, 7, 31, 64, 127, 257, 512, 1024, 4096 };

    for (double rate : rates)
    {
        for (int mode = 0; mode < 3; ++mode)
        {
            ModernPitchEngine engine;
            engine.prepare(rate, 4096, 2, static_cast<ModernPitchEngine::LatencyMode>(mode));
            ModernPitchEngine::Parameters p;
            p.minimumPitchHz = 35.0f;
            p.maximumPitchHz = 2000.0f;
            std::array<float, 4096> left {};
            std::array<float, 4096> right {};
            std::array<float*, 2> ptr { left.data(), right.data() };

            for (int iteration = 0; iteration < 22; ++iteration)
            {
                const int count = blocks[static_cast<std::size_t>(iteration % blocks.size())];
                for (int i = 0; i < count; ++i)
                {
                    left[static_cast<std::size_t>(i)] = sample(random);
                    right[static_cast<std::size_t>(i)] = sample(random);
                }
                if (iteration == 3) left[0] = std::numeric_limits<float>::quiet_NaN();
                if (iteration == 5) right[0] = std::numeric_limits<float>::infinity();
                if (iteration == 7) left[0] = std::numeric_limits<float>::denorm_min();
                if (iteration == 9) right[0] = 32.0f;

                p.amount = (iteration % 37 == 0) ? std::numeric_limits<float>::quiet_NaN() : unit(random);
                p.retuneTimeMs = 500.0f * unit(random);
                p.transitionTimeMs = 300.0f * unit(random);
                p.detectorSensitivity = unit(random);
                p.formantPreservation = unit(random);
                p.breathReduction = unit(random);
                juce::AudioBuffer<float> buffer(ptr.data(), 2, count);
                engine.process(buffer, scale.data(), static_cast<int>(scale.size()), 261.625565, p);
                for (int i = 0; i < count; ++i)
                {
                    if (!std::isfinite(left[static_cast<std::size_t>(i)])
                        || !std::isfinite(right[static_cast<std::size_t>(i)]))
                    {
                        std::cerr << "non-finite output\n";
                        return 2;
                    }
                }
                if ((iteration % 53) == 0)
                    engine.reset();
            }
        }
    }

    LivePitchProcessor wrapper;
    wrapper.prepare(48000.0, 4096, 2, ModernPitchEngine::LatencyMode::live);
    std::array<float, 17> left {};
    std::array<float, 17> right {};
    std::array<float*, 2> ptr { left.data(), right.data() };
    juce::AudioBuffer<float> block(ptr.data(), 2, 17);
    for (int i = 0; i < 120; ++i)
    {
        wrapper.setLatencyModeNonRealtime(static_cast<ModernPitchEngine::LatencyMode>(i % 3));
        wrapper.process(block, scale.data(), static_cast<int>(scale.size()), 440.0, 8.0f, 1.0f);
    }

    std::cout << "Sanitizer smoke passed.\n";
    return 0;
}
