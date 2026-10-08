#pragma once

struct FactoryPreset
{
    const char* name;
    const char* group;
    const char* description;

    int processingMode; // 1 Studio/512, 2 Live/256, 3 Low Latency/128

    float speedMs;
    float amount;   // 0..100 narrows the allowed pitch window
    float humanize; // 0..100 adds room around the target

    bool analogMode;
    float outVolumeDb;
};

namespace FactoryPresets
{
    int getNumPresets() noexcept;
    const FactoryPreset& getPreset (int index) noexcept;
}
