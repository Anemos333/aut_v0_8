#pragma once

#include <JuceHeader.h>

#include "ShareablePack.h"

#include <limits>
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

    // The original editor exposed seven slots. Community Pack V1 removes that
    // library limit. Keep the symbol only as source compatibility for the menu;
    // it now behaves as an effectively unbounded vector policy.
    static constexpr int maxPresets = std::numeric_limits<int>::max();
    static constexpr int schemaVersion = 3;

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

    [[nodiscard]] neumaton::sharing::PackDocument makeScalePack (
        const neumaton::sharing::PackManifest& manifest) const;

    int importScalePack (const neumaton::sharing::PackDocument& pack);

private:
    static std::vector<double> normaliseRatios (
        const std::vector<double>& ratios,
        double equaveRatio);

    static juce::String makeLocalStableId();

    std::vector<CustomScale> presets_;
};
