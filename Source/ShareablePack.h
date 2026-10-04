#pragma once

#include <JuceHeader.h>

namespace neumaton::sharing
{
inline constexpr int currentPackSchemaVersion = 1;

struct PackManifest
{
    juce::String stableId;
    juce::String name;
    juce::String author;
    juce::String version { "1.0.0" };
    juce::String description;
    int schemaVersion = currentPackSchemaVersion;

    [[nodiscard]] juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree ("Manifest");
        tree.setProperty ("schemaVersion", schemaVersion, nullptr);
        tree.setProperty ("stableId", stableId, nullptr);
        tree.setProperty ("name", name, nullptr);
        tree.setProperty ("author", author, nullptr);
        tree.setProperty ("version", version, nullptr);
        tree.setProperty ("description", description, nullptr);
        return tree;
    }

    [[nodiscard]] static PackManifest fromValueTree (const juce::ValueTree& tree)
    {
        PackManifest result;
        if (! tree.isValid())
            return result;

        result.schemaVersion = static_cast<int> (
            tree.getProperty ("schemaVersion", currentPackSchemaVersion));
        result.stableId = tree.getProperty ("stableId").toString();
        result.name = tree.getProperty ("name").toString();
        result.author = tree.getProperty ("author").toString();
        result.version = tree.getProperty ("version", "1.0.0").toString();
        result.description = tree.getProperty ("description").toString();
        return result;
    }
};

// Common top-level schema for future packs containing scales, presets or both.
// V1 only exposes the scale-library backend; preset import/export can attach to
// the already-versioned Presets child without changing manifest identity.
struct PackDocument
{
    PackManifest manifest;
    juce::ValueTree scales { "Scales" };
    juce::ValueTree presets { "Presets" };

    [[nodiscard]] juce::ValueTree toValueTree() const
    {
        juce::ValueTree root ("NeumatonPack");
        root.setProperty ("schemaVersion", currentPackSchemaVersion, nullptr);
        root.addChild (manifest.toValueTree(), -1, nullptr);
        root.addChild (scales.createCopy(), -1, nullptr);
        root.addChild (presets.createCopy(), -1, nullptr);
        return root;
    }

    [[nodiscard]] static PackDocument fromValueTree (const juce::ValueTree& root)
    {
        PackDocument result;
        if (! root.isValid() || root.getType() != juce::Identifier ("NeumatonPack"))
            return result;

        result.manifest = PackManifest::fromValueTree (
            root.getChildWithName ("Manifest"));

        const auto scaleTree = root.getChildWithName ("Scales");
        if (scaleTree.isValid())
            result.scales = scaleTree.createCopy();

        const auto presetTree = root.getChildWithName ("Presets");
        if (presetTree.isValid())
            result.presets = presetTree.createCopy();

        return result;
    }
};
} // namespace neumaton::sharing
