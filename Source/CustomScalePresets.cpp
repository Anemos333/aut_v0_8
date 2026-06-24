#include "CustomScalePresets.h"
#include <algorithm>

CustomScalePresets::CustomScalePresets() {}

int CustomScalePresets::getNumPresets() const
{
    return static_cast<int> (presets_.size());
}

const CustomScale& CustomScalePresets::getPreset (int index) const
{
    return presets_[static_cast<size_t> (index)];
}

bool CustomScalePresets::addPreset (const juce::String& name, const std::vector<double>& ratios)
{
    if (static_cast<int> (presets_.size()) >= maxPresets)
        return false;
    if (name.isEmpty())
        return false;

    CustomScale cs;
    cs.name = name;
    cs.ratios.reserve (std::min<std::size_t> (ratios.size() + 1, 33));
    cs.ratios.push_back (1.0); // invariant: unison is always part of a scale

    for (double ratio : ratios)
    {
        if (! std::isfinite (ratio) || ratio <= 0.0)
            continue;

        // O(1) mathematically safe octave folding to [1.0, 2.0)
        double l = std::log2(ratio);
        double folded = std::exp2(l - std::floor(l));

        // Precision edge-case guard
        if (folded >= 2.0) folded = 1.0; 

        cs.ratios.push_back (folded);
    }

    std::sort (cs.ratios.begin(), cs.ratios.end());
    auto last = std::unique (cs.ratios.begin(), cs.ratios.end(),
        [] (double a, double b) { return std::abs (a - b) < 1.0e-8; });
    cs.ratios.erase (last, cs.ratios.end());

    if (cs.ratios.size() < 3 || cs.ratios.size() > 33)
        return false;

    presets_.push_back (std::move (cs));
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

    for (const auto& preset : presets_)
    {
        juce::ValueTree scaleTree ("Scale");
        scaleTree.setProperty ("name", preset.name, nullptr);

        juce::String ratioStr;
        for (size_t i = 0; i < preset.ratios.size(); ++i)
        {
            if (i > 0) ratioStr += ",";
            ratioStr += juce::String (preset.ratios[i], 10);
        }
        scaleTree.setProperty ("ratios", ratioStr, nullptr);

        tree.addChild (scaleTree, -1, nullptr);
    }

    return tree;
}

void CustomScalePresets::fromValueTree (const juce::ValueTree& tree)
{
    presets_.clear();

    if (! tree.isValid() || tree.getType() != juce::Identifier ("CustomScales"))
        return;

    for (int i = 0; i < tree.getNumChildren() && i < maxPresets; ++i)
    {
        auto scaleTree = tree.getChild (i);
        if (scaleTree.getType() != juce::Identifier ("Scale"))
            continue;

        CustomScale cs;
        cs.name = scaleTree.getProperty ("name").toString();

        juce::String ratioStr = scaleTree.getProperty ("ratios").toString();
        juce::StringArray tokens;
        tokens.addTokens (ratioStr, ",", "");

        for (const auto& token : tokens)
        {
            double val = token.getDoubleValue();
            if (val >= 1.0 && val < 2.0)
                cs.ratios.push_back (val);
        }

        if (cs.ratios.size() >= 3 && cs.ratios.size() <= 33 && cs.name.isNotEmpty())
        {
            std::sort (cs.ratios.begin(), cs.ratios.end());
            presets_.push_back (std::move (cs));
        }
    }
}
