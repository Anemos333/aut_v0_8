#include "CustomScalePresets.h"

#include <algorithm>
#include <cmath>

int CustomScalePresets::getNumPresets() const noexcept
{
    return static_cast<int> (presets_.size());
}

const CustomScale& CustomScalePresets::getPreset (int index) const
{
    return presets_[static_cast<std::size_t> (index)];
}

int CustomScalePresets::findPresetIndexByStableId (const juce::String& stableId) const noexcept
{
    if (stableId.isEmpty())
        return -1;

    for (std::size_t i = 0; i < presets_.size(); ++i)
        if (presets_[i].stableId == stableId)
            return static_cast<int> (i);

    return -1;
}

juce::String CustomScalePresets::makeLocalStableId()
{
    return "user.scale." + juce::Uuid().toString();
}

std::vector<double> CustomScalePresets::normaliseRatios (
    const std::vector<double>& ratios,
    double equaveRatio)
{
    if (! std::isfinite (equaveRatio) || equaveRatio <= 1.0)
        equaveRatio = 2.0;

    const double logEquave = std::log2 (equaveRatio);
    std::vector<double> result;
    result.reserve (std::min<std::size_t> (ratios.size() + 1, 96));
    result.push_back (1.0);

    for (double ratio : ratios)
    {
        if (result.size() >= 96)
            break;
        if (! std::isfinite (ratio) || ratio <= 0.0)
            continue;

        double phase = std::fmod (std::log2 (ratio), logEquave);
        if (phase < 0.0)
            phase += logEquave;

        const double folded = std::exp2 (phase);
        if (std::isfinite (folded) && folded >= 1.0
            && folded < equaveRatio - 1.0e-10)
            result.push_back (folded);
    }

    std::sort (result.begin(), result.end());
    const auto last = std::unique (result.begin(), result.end(),
        [] (double a, double b)
        {
            return std::abs (a - b) < 1.0e-8;
        });
    result.erase (last, result.end());
    return result;
}

bool CustomScalePresets::addPreset (const juce::String& name,
                                    const std::vector<double>& ratios)
{
    return addPreset (name, ratios, 2.0);
}

bool CustomScalePresets::addPreset (const juce::String& name,
                                    const std::vector<double>& ratios,
                                    double equaveRatio,
                                    const juce::String& stableId,
                                    const juce::String& sourcePackId)
{
    if (static_cast<int> (presets_.size()) >= maxPresets || name.trim().isEmpty())
        return false;

    const auto requestedId = stableId.trim();
    if (requestedId.isNotEmpty() && findPresetIndexByStableId (requestedId) >= 0)
        return false;

    auto normalised = normaliseRatios (ratios, equaveRatio);
    if (normalised.size() < 3 || normalised.size() > 96)
        return false;

    CustomScale scale;
    scale.stableId = requestedId.isNotEmpty() ? requestedId : makeLocalStableId();
    scale.sourcePackId = sourcePackId.trim();
    scale.name = name.trim();
    scale.equaveRatio = std::isfinite (equaveRatio) && equaveRatio > 1.0
        ? equaveRatio : 2.0;
    scale.ratios = std::move (normalised);

    presets_.push_back (std::move (scale));
    return true;
}

bool CustomScalePresets::removePreset (int index)
{
    if (index < 0 || index >= static_cast<int> (presets_.size()))
        return false;

    presets_.erase (presets_.begin() + index);
    return true;
}

juce::ValueTree CustomScalePresets::toValueTree() const
{
    juce::ValueTree tree ("CustomScales");
    tree.setProperty ("schemaVersion", schemaVersion, nullptr);

    for (const auto& preset : presets_)
    {
        juce::ValueTree scaleTree ("Scale");
        scaleTree.setProperty ("stableId", preset.stableId, nullptr);
        scaleTree.setProperty ("sourcePackId", preset.sourcePackId, nullptr);
        scaleTree.setProperty ("name", preset.name, nullptr);
        scaleTree.setProperty ("category", preset.category, nullptr);
        scaleTree.setProperty ("equaveRatio", preset.equaveRatio, nullptr);

        juce::String ratioString;
        for (std::size_t i = 0; i < preset.ratios.size(); ++i)
        {
            if (i != 0)
                ratioString += ",";
            ratioString += juce::String (preset.ratios[i], 12);
        }
        scaleTree.setProperty ("ratios", ratioString, nullptr);
        tree.addChild (scaleTree, -1, nullptr);
    }

    return tree;
}

void CustomScalePresets::fromValueTree (const juce::ValueTree& tree)
{
    presets_.clear();

    if (! tree.isValid() || tree.getType() != juce::Identifier ("CustomScales"))
        return;

    for (int i = 0; i < tree.getNumChildren()
                    && static_cast<int> (presets_.size()) < maxPresets; ++i)
    {
        const auto scaleTree = tree.getChild (i);
        if (scaleTree.getType() != juce::Identifier ("Scale"))
            continue;

        const auto name = scaleTree.getProperty ("name").toString().trim();
        if (name.isEmpty())
            continue;

        const double equaveRatio = static_cast<double> (
            scaleTree.getProperty ("equaveRatio", 2.0));

        juce::StringArray tokens;
        tokens.addTokens (scaleTree.getProperty ("ratios").toString(), ",", "");

        std::vector<double> ratios;
        ratios.reserve (static_cast<std::size_t> (tokens.size()));
        for (const auto& token : tokens)
        {
            const double value = token.getDoubleValue();
            if (std::isfinite (value) && value > 0.0)
                ratios.push_back (value);
        }

        auto normalised = normaliseRatios (ratios, equaveRatio);
        if (normalised.size() < 3 || normalised.size() > 96)
            continue;

        CustomScale scale;
        scale.stableId = scaleTree.getProperty ("stableId").toString();
        if (scale.stableId.isEmpty())
            scale.stableId = makeLocalStableId();
        if (findPresetIndexByStableId (scale.stableId) >= 0)
            scale.stableId = makeLocalStableId();
        scale.sourcePackId = scaleTree.getProperty ("sourcePackId").toString();
        scale.name = name;
        scale.category = scaleTree.getProperty ("category", "User").toString();
        scale.equaveRatio = std::isfinite (equaveRatio) && equaveRatio > 1.0
            ? equaveRatio : 2.0;
        scale.ratios = std::move (normalised);
        presets_.push_back (std::move (scale));
    }
}

neumaton::sharing::PackDocument CustomScalePresets::makeScalePack (
    const neumaton::sharing::PackManifest& manifest) const
{
    neumaton::sharing::PackDocument pack;
    pack.manifest = manifest;

    for (const auto& preset : presets_)
    {
        juce::ValueTree scale ("Scale");
        scale.setProperty ("stableId", preset.stableId, nullptr);
        scale.setProperty ("name", preset.name, nullptr);
        scale.setProperty ("category", preset.category, nullptr);
        scale.setProperty ("equaveRatio", preset.equaveRatio, nullptr);

        juce::String ratioString;
        for (std::size_t i = 0; i < preset.ratios.size(); ++i)
        {
            if (i != 0)
                ratioString += ",";
            ratioString += juce::String (preset.ratios[i], 12);
        }
        scale.setProperty ("ratios", ratioString, nullptr);
        pack.scales.addChild (scale, -1, nullptr);
    }

    return pack;
}

int CustomScalePresets::importScalePack (const neumaton::sharing::PackDocument& pack)
{
    if (pack.manifest.schemaVersion > neumaton::sharing::currentPackSchemaVersion)
        return 0;

    int imported = 0;
    for (int i = 0; i < pack.scales.getNumChildren(); ++i)
    {
        if (static_cast<int> (presets_.size()) >= maxPresets)
            break;

        const auto scale = pack.scales.getChild (i);
        if (scale.getType() != juce::Identifier ("Scale"))
            continue;

        juce::StringArray tokens;
        tokens.addTokens (scale.getProperty ("ratios").toString(), ",", "");

        std::vector<double> ratios;
        ratios.reserve (static_cast<std::size_t> (tokens.size()));
        for (const auto& token : tokens)
        {
            const double value = token.getDoubleValue();
            if (std::isfinite (value) && value > 0.0)
                ratios.push_back (value);
        }

        if (addPreset (scale.getProperty ("name").toString(),
                       ratios,
                       static_cast<double> (scale.getProperty ("equaveRatio", 2.0)),
                       scale.getProperty ("stableId").toString(),
                       pack.manifest.stableId))
        {
            auto& inserted = presets_.back();
            inserted.category = scale.getProperty ("category", "Imported").toString();
            ++imported;
        }
    }

    return imported;
}
