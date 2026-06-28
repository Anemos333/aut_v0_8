#include "Preset.h"
#include <algorithm>

namespace
{
    constexpr FactoryPreset presets[] =
    {
        { "Studio Gentle",       "Studio", 1, 80.0f, 45.0f, 55.0f, false, 24.0f, 60.0f, 0, 2, 35.0f, 100.0f, true,  false,  -1.2f },
        { "Natural Vibrato",     "Studio", 1, 120.0f, 55.0f, 75.0f, false, 30.0f, 85.0f, 0, 2, 35.0f, 100.0f, true,  false,  -1.2f },
        { "Clean Correction",    "Studio", 1, 45.0f, 75.0f, 35.0f, true,  22.0f, 45.0f, 0, 2, 35.0f, 100.0f, true,  false,  -0.3f },
        { "Hard Tune Studio",    "Studio", 1, 12.0f, 100.0f, 8.0f, true,  12.0f, 10.0f, 0, 2, 35.0f, 100.0f, true,  false, -1.0f },

        { "Live Anchor",         "Live",   2, 55.0f, 65.0f, 45.0f, true,  28.0f, 55.0f, 0, 2, 35.0f, 100.0f, true,  false,  -1.5f },
        { "Live Natural",        "Live",   2, 90.0f, 50.0f, 70.0f, false, 30.0f, 80.0f, 0, 2, 35.0f, 100.0f, true,  false,  -1.8f },
        { "Backing Track Lock",  "Live",   2, 25.0f, 85.0f, 25.0f, true,  18.0f, 30.0f, 0, 2, 35.0f, 100.0f, true,  false,  -0.9f },
        { "Emergency Tight",     "Live",   3, 8.0f,  100.0f, 5.0f, true,  8.0f,  5.0f,  0, 2, 35.0f, 100.0f, true,  false, -1.5f },

        { "Robot Rite",          "Lab",    1, 0.0f,  100.0f, 0.0f, true,  4.0f,  0.0f,  0, 2, 35.0f, 100.0f, true,  true,  -2.0f },
        { "Tempo Glide",         "Lab",    1, 45.0f, 90.0f, 20.0f, true,  16.0f, 20.0f, 1, 2, 45.0f, 100.0f, true,  false,  -1.2f },
        { "Glide Lock",          "Lab",    1, 25.0f, 100.0f, 8.0f, true,  10.0f, 5.0f,  2, 2, 35.0f, 100.0f, true,  false, -1.0f },
        { "Experimental Window", "Lab",    3, 4.0f,  100.0f, 5.0f, true,  6.0f,  0.0f,  0, 2, 35.0f, 100.0f, true,  false, -1.5f }
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
