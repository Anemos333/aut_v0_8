#pragma once

#include <cstddef>

struct FactoryPreset
{
    const char* name;
    const char* group;

    int processingMode; // 0 High Latency, 1 Quality, 2 Live, 3 Experimental

    float speedMs;
    float amount;
    float humanize;

    float lockHysteresis;
    float vibratoPreserve;

    bool analogMode;
    float outVolumeDb;
};

namespace FactoryPresets
{
    int getNumPresets() noexcept;
    const FactoryPreset& getPreset (int index) noexcept;
}
