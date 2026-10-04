#pragma once

#include <JuceHeader.h>

#include <memory>

namespace neumaton::sharing
{
inline constexpr int currentPackSchemaVersion = 2;
inline constexpr const char* communityPackExtension = ".ecpk";
inline constexpr const char* communityPackWildcard = "*.ecpk";

struct PackManifest
{
    juce::String stableId;
    juce::String name;
    juce::String author;
    juce::String version { "1.0.0" };
    juce::String description;
    int schemaVersion = currentPackSchemaVersion;

    [[nodiscard]] bool isValid() const noexcept
    {
        return stableId.trim().isNotEmpty()
            && name.trim().isNotEmpty()
            && author.trim().isNotEmpty()
            && schemaVersion > 0
            && schemaVersion <= currentPackSchemaVersion;
    }

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

// Ergasterion Community Pack. The V1 container is deliberately plain XML under
// the .ecpk extension: inspectable, versioned and easy to migrate. A later binary
// or signed transport can wrap the same logical ValueTree schema.
struct PackDocument
{
    PackManifest manifest;
    juce::ValueTree scales { "Scales" };
    juce::ValueTree presets { "Presets" };
    juce::ValueTree scenes { "Scenes" };

    [[nodiscard]] bool isValid() const noexcept
    {
        return manifest.isValid()
            && scales.isValid()
            && presets.isValid()
            && scenes.isValid();
    }

    [[nodiscard]] juce::ValueTree toValueTree() const
    {
        juce::ValueTree root ("ErgasterionCommunityPack");
        root.setProperty ("schemaVersion", currentPackSchemaVersion, nullptr);
        root.addChild (manifest.toValueTree(), -1, nullptr);
        root.addChild (scales.createCopy(), -1, nullptr);
        root.addChild (presets.createCopy(), -1, nullptr);
        root.addChild (scenes.createCopy(), -1, nullptr);
        return root;
    }

    [[nodiscard]] static PackDocument fromValueTree (const juce::ValueTree& root)
    {
        PackDocument result;
        if (! root.isValid())
            return result;

        // Accept the provisional V1 root too: no public pack files existed yet,
        // but retaining the parser costs nothing and keeps development builds safe.
        const bool knownRoot = root.getType() == juce::Identifier ("ErgasterionCommunityPack")
            || root.getType() == juce::Identifier ("NeumatonPack");
        if (! knownRoot)
            return result;

        const int rootSchema = static_cast<int> (
            root.getProperty ("schemaVersion", 1));
        if (rootSchema <= 0 || rootSchema > currentPackSchemaVersion)
        {
            result.manifest.schemaVersion = rootSchema;
            return result;
        }

        result.manifest = PackManifest::fromValueTree (
            root.getChildWithName ("Manifest"));

        const auto scaleTree = root.getChildWithName ("Scales");
        if (scaleTree.isValid())
            result.scales = scaleTree.createCopy();

        const auto presetTree = root.getChildWithName ("Presets");
        if (presetTree.isValid())
            result.presets = presetTree.createCopy();

        const auto sceneTree = root.getChildWithName ("Scenes");
        if (sceneTree.isValid())
            result.scenes = sceneTree.createCopy();

        // Schema 1 had no Scenes node. Treat it as an empty collection.
        if (! result.scenes.isValid())
            result.scenes = juce::ValueTree ("Scenes");

        return result;
    }

    [[nodiscard]] bool writeToFile (juce::File target) const
    {
        if (! isValid())
            return false;

        if (! target.hasFileExtension (communityPackExtension))
            target = target.withFileExtension (communityPackExtension);

        const auto xml = toValueTree().createXml();
        if (xml == nullptr)
            return false;

        return target.replaceWithText (xml->toString());
    }

    [[nodiscard]] static PackDocument readFromFile (const juce::File& file)
    {
        PackDocument result;
        if (! file.existsAsFile() || ! file.hasFileExtension (communityPackExtension))
            return result;

        const auto text = file.loadFileAsString();
        if (text.isEmpty())
            return result;

        const auto xml = juce::XmlDocument::parse (text);
        if (xml == nullptr)
            return result;

        return fromValueTree (juce::ValueTree::fromXml (*xml));
    }
};
} // namespace neumaton::sharing
