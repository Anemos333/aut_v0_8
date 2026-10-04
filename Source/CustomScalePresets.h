#pragma once

#include <JuceHeader.h>

#include "ShareablePack.h"

#include <vector>

struct CustomScale
{
    juce::String stableId;
    juce::String sourcePackId;
    juce::String name;
    juce::String category { "User" };
    double equaveRatio = 2.0;
    std::vector<double> ratios; // sorted [1.0, equaveRatio), unison included
};

class CustomScalePresets
{
public:
    CustomScalePresets() = default;

    // V1 local-editor policy. The pack schema itself is intentionally not limited to 7.
    static constexpr int maxPresets = 7;
    static constexpr int schemaVersion = 2;

    int getNumPresets() const noexcept;
    const CustomScale& getPreset (int index) const;
    int findPresetIndexByStableId (const juce::String& stableId) const noexcept;

    bool addPreset (const juce::String& name,
                    const std::vector<double>& ratios);

    bool addPreset (const juce::String& name,
                    const std::vector<double>& ratios,
                    double equaveRatio,
                    const juce::String& stableId = {},
                    const juce::String& sourcePackId = {});

    bool removePreset (int index);

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree& tree);

    // Backend-only V1 pack support. GUI import/export is intentionally deferred.
    [[nodiscard]] neumaton::sharing::PackDocument makeScalePack (
        const neumaton::sharing::PackManifest& manifest) const;

    // Imports as many scale entries as fit in the current local V1 editor bank.
    // Stable ids and source pack identity survive the round trip, so this API can
    // later be reused by the full pack manager without changing the scale model.
    int importScalePack (const neumaton::sharing::PackDocument& pack);

private:
    static std::vector<double> normaliseRatios (
        const std::vector<double>& ratios,
        double equaveRatio);

    static juce::String makeLocalStableId();

    std::vector<CustomScale> presets_;
};
