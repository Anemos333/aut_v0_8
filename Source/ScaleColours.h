#pragma once

#include <JuceHeader.h>
#include <vector>

namespace neumaton::scalecolours
{
using Palette = std::vector<juce::uint32>;

inline juce::Colour shade (float hue, double position)
{
    const auto t = static_cast<float> (position);
    return juce::Colour::fromHSV (hue, 0.80f - 0.45f * t, 0.62f + 0.36f * t, 1.0f);
}

// Stable fallback for factory scales and scales authored before colours existed.
inline Palette defaults (const juce::String& id, const std::vector<double>& ratios, double equave)
{
    juce::Random random (id.hashCode64());
    const float hue = random.nextFloat();
    Palette result;
    for (const auto ratio : ratios)
        result.push_back (shade (hue, juce::jlimit (0.0, 1.0, std::log (ratio) / std::log (equave))).getARGB());
    return result;
}

inline juce::String encode (const Palette& colours)
{
    juce::StringArray tokens;
    for (auto argb : colours)
        tokens.add (juce::Colour (argb).toString());
    return tokens.joinIntoString (",");
}

inline Palette decode (const juce::ValueTree& scale)
{
    Palette colours;
    for (auto token : juce::StringArray::fromTokens (scale["degreeColours"].toString(), ",", ""))
        colours.push_back (token.length() == 8 && token.containsOnly ("0123456789abcdefABCDEF")
            ? juce::Colour::fromString (token).withAlpha (1.0f).getARGB() : 0u);
    return colours;
}
}
