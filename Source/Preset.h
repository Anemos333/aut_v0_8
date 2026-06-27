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

    bool scaleLock;
    float lockHysteresis;
    float vibratoPreserve;

    int tempoMode;       // 0 Off, 1 Tempo Glide, 2 Glide Lock
    int tempoDivision;   // 0..4
    float tempoGlidePct;
    float tempoLockStrength;
    bool tempoSmartOnset;

    bool analogMode;
    float outVolumeDb;
};

namespace FactoryPresets
{
    int getNumPresets() noexcept;
    const FactoryPreset& getPreset (int index) noexcept;
}
