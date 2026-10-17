#include "Preset.h"
#include <algorithm>

namespace
{
    // Preserve the twelve program slots used by saved sessions/host programs.
    // Settings change only when a factory program is explicitly recalled.
    constexpr FactoryPreset presets[] =
    {
        { "Studio Gentle", "Studio", "A soft pull toward the scale with room for pitch drift.",
          1, 25.0f, 80.0f, 55.0f,5.0f,25.0f, true, -1.2f },
        { "Slow Drift", "Studio", "Slower correction and a wider pitch window for flexible phrases.",
          1, 45.0f, 55.0f, 75.0f,25.0f,0.0f, false, -1.2f },
        { "Clean Correction", "Studio", "A quick response with a small pitch window around each scale target.",
          1, 5.0f, 75.0f, 35.0f,30.0f,0.0f, false, -0.3f },
        { "Full Correction", "Studio", "Immediate correction to the scale target with zero pitch window.",
          1, 0.0f, 100.0f, 0.0f,0.0f,0.0f, false, -1.0f },

        { "Live Anchor", "Live", "A balanced live response with moderate room around the target.",
          2, 9.5f, 65.0f, 45.0f,15.3f,0.0, false, -1.5f },
        { "Live Drift", "Live", "A slower live response with room for expressive pitch movement.",
          2, 19.3f, 70.0f, 70.0f,50.0f,75.3f, true, -1.8f },
        { "Stage Tight", "Live", "Fast live correction with a narrow pitch window.",
          2, 5.0f, 95.0f, 25.0f,75.0f,25.5f, true, -0.9f },
        { "Emergency Tight", "Live", "Low Latency mode with a very fast response and a tiny pitch window.",
          3, 5.0f, 100.0f, 3.0f,80.0f,0.0f, false, -1.5f },

        { "Robot Rite", "Lab", "Immediate exact-target correction with Analog Texture.",
          1, 0.0f, 100.0f, 0.0f,0.0f,100.0f, true, -2.0f },
        { "Slow Orbit", "Lab", "A long slide toward each scale target with room for drift.",
          1, 45.0f, 75.0f, 65.0f,0.0f,0.0f, false, -1.2f },
        { "Liquid Steps", "Lab", "A gradual slide all the way to each scale target with Analog Texture.",
          1, 50.0f, 100.0f, 0.0f,100.0f,48.3f, true, -2.0f },
        { "Open Window", "Lab", "Slow retuning and a broad pitch window in Low Latency mode.",
          3, 50.0f, 25.0f, 100.0f,5.0f,0.0f, false, -1.5f }
    };
}

namespace FactoryPresets
{
    int getNumPresets() noexcept
    {
        return static_cast<int> (sizeof (presets) / sizeof (presets[0]));
    }

    const FactoryPreset& getPreset (int index) noexcept
    {
        index = std::clamp (index, 0, getNumPresets() - 1);
        return presets[index];
    }
}
