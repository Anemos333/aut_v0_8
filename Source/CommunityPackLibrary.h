#pragma once

#include <JuceHeader.h>

#include "ShareablePack.h"

#include <algorithm>
#include <array>
#include <vector>

namespace neumaton::community
{
inline const std::array<const char*, 9>& sharedPresetParameterIds() noexcept
{
    static const std::array<const char*, 9> ids {
        "speed",
        "amount",
        "humanize",
        "scaleLock",
        "lockHysteresis",
        "vibratoPreserve",
        "analogMode",
        "outVolume",
        "tuningReferenceHz"
    };
    return ids;
}

struct CommunityPreset
{
    juce::String stableId;
    juce::String sourcePackId;
    juce::String name;
    int processingMode = 1;

    // -1 means "not stored" for backwards compatibility with schema-2 packs.
    // New presets always persist the tonal centre explicitly so a preset/scene
    // sounds the same when applied in a fresh plugin instance.
    int tonalCenterIndex = -1;

    juce::ValueTree values { "Values" };

    [[nodiscard]] bool isValid() const noexcept
    {
        return stableId.trim().isNotEmpty()
            && name.trim().isNotEmpty()
            && values.isValid();
    }

    [[nodiscard]] juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree ("Preset");
        tree.setProperty ("stableId", stableId, nullptr);
        tree.setProperty ("sourcePackId", sourcePackId, nullptr);
        tree.setProperty ("name", name, nullptr);
        tree.setProperty ("processingMode", juce::jlimit (1, 3, processingMode), nullptr);
        if (tonalCenterIndex >= 0)
            tree.setProperty ("tonalCenterIndex", juce::jlimit (0, 11, tonalCenterIndex), nullptr);
        tree.addChild (values.createCopy(), -1, nullptr);
        return tree;
    }

    [[nodiscard]] static CommunityPreset fromValueTree (const juce::ValueTree& tree)
    {
        CommunityPreset result;
        if (! tree.isValid() || tree.getType() != juce::Identifier ("Preset"))
            return result;

        result.stableId = tree.getProperty ("stableId").toString();
        result.sourcePackId = tree.getProperty ("sourcePackId").toString();
        result.name = tree.getProperty ("name").toString().trim();
        result.processingMode = juce::jlimit (1, 3,
            static_cast<int> (tree.getProperty ("processingMode", 1)));

        if (tree.hasProperty ("tonalCenterIndex"))
            result.tonalCenterIndex = juce::jlimit (0, 11,
                static_cast<int> (tree.getProperty ("tonalCenterIndex")));

        const auto child = tree.getChildWithName ("Values");
        if (child.isValid())
            result.values = child.createCopy();

        return result;
    }

    [[nodiscard]] static CommunityPreset capture (
        const juce::String& presetName,
        juce::AudioProcessorValueTreeState& apvts,
        int mode,
        int tonalCenter)
    {
        CommunityPreset result;
        result.stableId = "user.preset." + juce::Uuid().toString();
        result.name = presetName.trim();
        result.processingMode = juce::jlimit (1, 3, mode);
        result.tonalCenterIndex = juce::jlimit (0, 11, tonalCenter);

        for (const auto* parameterId : sharedPresetParameterIds())
        {
            if (const auto* raw = apvts.getRawParameterValue (parameterId))
                result.values.setProperty (parameterId, raw->load(), nullptr);
        }

        return result;
    }

    void applyTo (juce::AudioProcessorValueTreeState& apvts) const
    {
        for (const auto* parameterId : sharedPresetParameterIds())
        {
            if (! values.hasProperty (parameterId))
                continue;

            if (auto* parameter = apvts.getParameter (parameterId))
            {
                const float plainValue = static_cast<float> (
                    static_cast<double> (values.getProperty (parameterId)));
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (
                    parameter->convertTo0to1 (plainValue));
                parameter->endChangeGesture();
            }
        }
    }
};

struct CommunityScene
{
    juce::String stableId;
    juce::String sourcePackId;
    juce::String name;
    juce::String scaleStableId;
    juce::String presetStableId;

    [[nodiscard]] bool isValid() const noexcept
    {
        return stableId.trim().isNotEmpty()
            && name.trim().isNotEmpty()
            && scaleStableId.trim().isNotEmpty()
            && presetStableId.trim().isNotEmpty();
    }

    [[nodiscard]] juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree ("Scene");
        tree.setProperty ("stableId", stableId, nullptr);
        tree.setProperty ("sourcePackId", sourcePackId, nullptr);
        tree.setProperty ("name", name, nullptr);
        tree.setProperty ("scaleStableId", scaleStableId, nullptr);
        tree.setProperty ("presetStableId", presetStableId, nullptr);
        return tree;
    }

    [[nodiscard]] static CommunityScene fromValueTree (const juce::ValueTree& tree)
    {
        CommunityScene result;
        if (! tree.isValid() || tree.getType() != juce::Identifier ("Scene"))
            return result;

        result.stableId = tree.getProperty ("stableId").toString();
        result.sourcePackId = tree.getProperty ("sourcePackId").toString();
        result.name = tree.getProperty ("name").toString().trim();
        result.scaleStableId = tree.getProperty ("scaleStableId").toString();
        result.presetStableId = tree.getProperty ("presetStableId").toString();
        return result;
    }
};

struct LoadedPack
{
    juce::File file;
    sharing::PackDocument document;
};

class CommunityPackLibrary
{
public:
    CommunityPackLibrary()
    {
        const auto base = juce::File::getSpecialLocation (
            juce::File::userApplicationDataDirectory)
                .getChildFile ("AnemosMusic")
                .getChildFile ("Neumaton");
        static_cast<void> (base.createDirectory());
        settingsFile_ = base.getChildFile ("community-library.xml");
        reloadFromDisk();
    }

    [[nodiscard]] const std::vector<CommunityPreset>& getUserPresets() const noexcept
    {
        return userPresets_;
    }

    [[nodiscard]] const std::vector<CommunityScene>& getUserScenes() const noexcept
    {
        return userScenes_;
    }

    [[nodiscard]] const std::vector<LoadedPack>& getLoadedPacks() const noexcept
    {
        return loadedPacks_;
    }

    [[nodiscard]] const juce::File& getPackDirectory() const noexcept
    {
        return packDirectory_;
    }

    // Re-read the shared Community library before presenting it. This makes a
    // newly opened plugin instance see folder/preset/scene changes written by a
    // previous instance without requiring a host restart.
    void reloadFromDisk()
    {
        loadLocalSettings();
        rescanPacks();
    }

    void setPackDirectory (const juce::File& directory)
    {
        packDirectory_ = directory.isDirectory() ? directory : juce::File();
        saveLocalSettings();
        rescanPacks();
    }

    void rescanPacks()
    {
        loadedPacks_.clear();
        if (! packDirectory_.isDirectory())
            return;

        juce::Array<juce::File> files;
        packDirectory_.findChildFiles (
            files,
            juce::File::findFiles,
            true,
            sharing::communityPackWildcard);

        for (const auto& file : files)
        {
            auto document = sharing::PackDocument::readFromFile (file);
            if (! document.isValid())
                continue;

            const auto duplicate = std::find_if (
                loadedPacks_.begin(), loadedPacks_.end(),
                [&document] (const LoadedPack& candidate)
                {
                    return candidate.document.manifest.stableId
                        == document.manifest.stableId;
                });
            if (duplicate != loadedPacks_.end())
                continue;

            loadedPacks_.push_back ({ file, std::move (document) });
        }

        std::sort (loadedPacks_.begin(), loadedPacks_.end(),
            [] (const LoadedPack& a, const LoadedPack& b)
            {
                const auto authorCompare = a.document.manifest.author
                    .compareNatural (b.document.manifest.author);
                if (authorCompare != 0)
                    return authorCompare < 0;
                return a.document.manifest.name
                    .compareNatural (b.document.manifest.name) < 0;
            });
    }

    [[nodiscard]] juce::String addCurrentPreset (
        const juce::String& name,
        juce::AudioProcessorValueTreeState& apvts,
        int processingMode,
        int tonalCenter)
    {
        auto preset = CommunityPreset::capture (
            name, apvts, processingMode, tonalCenter);
        if (! preset.isValid())
            return {};

        userPresets_.push_back (std::move (preset));
        saveLocalSettings();
        return userPresets_.back().stableId;
    }

    bool removeUserPreset (const juce::String& stableId)
    {
        const auto it = std::find_if (
            userPresets_.begin(), userPresets_.end(),
            [&stableId] (const CommunityPreset& preset)
            {
                return preset.stableId == stableId;
            });
        if (it == userPresets_.end())
            return false;

        userPresets_.erase (it);
        userScenes_.erase (
            std::remove_if (userScenes_.begin(), userScenes_.end(),
                [&stableId] (const CommunityScene& scene)
                {
                    return scene.presetStableId == stableId;
                }),
            userScenes_.end());
        saveLocalSettings();
        return true;
    }

    [[nodiscard]] juce::String addScene (
        const juce::String& name,
        const juce::String& scaleStableId,
        const juce::String& presetStableId)
    {
        CommunityScene scene;
        scene.stableId = "user.scene." + juce::Uuid().toString();
        scene.name = name.trim();
        scene.scaleStableId = scaleStableId.trim();
        scene.presetStableId = presetStableId.trim();
        if (! scene.isValid())
            return {};

        userScenes_.push_back (std::move (scene));
        saveLocalSettings();
        return userScenes_.back().stableId;
    }

    bool removeUserScene (const juce::String& stableId)
    {
        const auto oldSize = userScenes_.size();
        userScenes_.erase (
            std::remove_if (userScenes_.begin(), userScenes_.end(),
                [&stableId] (const CommunityScene& scene)
                {
                    return scene.stableId == stableId;
                }),
            userScenes_.end());
        if (userScenes_.size() == oldSize)
            return false;
        saveLocalSettings();
        return true;
    }

    [[nodiscard]] const CommunityPreset* findUserPreset (
        const juce::String& stableId) const noexcept
    {
        const auto it = std::find_if (
            userPresets_.begin(), userPresets_.end(),
            [&stableId] (const CommunityPreset& preset)
            {
                return preset.stableId == stableId;
            });
        return it != userPresets_.end() ? &*it : nullptr;
    }

    [[nodiscard]] const CommunityScene* findUserScene (
        const juce::String& stableId) const noexcept
    {
        const auto it = std::find_if (
            userScenes_.begin(), userScenes_.end(),
            [&stableId] (const CommunityScene& scene)
            {
                return scene.stableId == stableId;
            });
        return it != userScenes_.end() ? &*it : nullptr;
    }

    [[nodiscard]] static juce::ValueTree findEntryByStableId (
        const juce::ValueTree& collection,
        const juce::String& stableId)
    {
        for (int i = 0; i < collection.getNumChildren(); ++i)
        {
            const auto child = collection.getChild (i);
            if (child.getProperty ("stableId").toString() == stableId)
                return child;
        }
        return {};
    }

    [[nodiscard]] static sharing::PackManifest makeManifest (
        const juce::String& name,
        const juce::String& author,
        const juce::String& description = {})
    {
        sharing::PackManifest manifest;
        manifest.stableId = "community.pack." + juce::Uuid().toString();
        manifest.name = name.trim();
        manifest.author = author.trim();
        manifest.description = description.trim();
        return manifest;
    }

    [[nodiscard]] sharing::PackDocument buildPack (
        const sharing::PackManifest& manifest,
        const std::vector<juce::ValueTree>& availableScaleEntries,
        const juce::StringArray& selectedScaleIds,
        const juce::StringArray& selectedPresetIds,
        const juce::StringArray& selectedSceneIds) const
    {
        sharing::PackDocument pack;
        pack.manifest = manifest;

        const auto addUniqueNode = [] (juce::ValueTree& destination,
                                       const juce::ValueTree& node,
                                       const juce::String& sourcePackId)
        {
            if (! node.isValid())
                return;
            const auto id = node.getProperty ("stableId").toString();
            if (id.isEmpty()
                || CommunityPackLibrary::findEntryByStableId (destination, id).isValid())
                return;

            auto copy = node.createCopy();
            copy.setProperty ("sourcePackId", sourcePackId, nullptr);
            destination.addChild (copy, -1, nullptr);
        };

        const auto findScale = [&availableScaleEntries] (const juce::String& id)
        {
            for (const auto& entry : availableScaleEntries)
                if (entry.getProperty ("stableId").toString() == id)
                    return entry;
            return juce::ValueTree();
        };

        for (const auto& scaleId : selectedScaleIds)
            addUniqueNode (pack.scales, findScale (scaleId), manifest.stableId);

        const auto addPresetById = [this, &pack, &addUniqueNode, &manifest] (
            const juce::String& presetId)
        {
            if (const auto* preset = findUserPreset (presetId))
                addUniqueNode (pack.presets, preset->toValueTree(), manifest.stableId);
        };

        for (const auto& presetId : selectedPresetIds)
            addPresetById (presetId);

        for (const auto& sceneId : selectedSceneIds)
        {
            const auto* scene = findUserScene (sceneId);
            if (scene == nullptr)
                continue;

            addUniqueNode (pack.scenes, scene->toValueTree(), manifest.stableId);
            addUniqueNode (pack.scales, findScale (scene->scaleStableId), manifest.stableId);
            addPresetById (scene->presetStableId);
        }

        return pack;
    }

private:
    void loadLocalSettings()
    {
        userPresets_.clear();
        userScenes_.clear();
        packDirectory_ = juce::File();

        if (! settingsFile_.existsAsFile())
            return;

        const auto xml = juce::XmlDocument::parse (settingsFile_.loadFileAsString());
        if (xml == nullptr)
            return;

        const auto root = juce::ValueTree::fromXml (*xml);
        if (! root.isValid() || root.getType() != juce::Identifier ("CommunityLibrary"))
            return;

        const auto directoryText = root.getProperty ("packDirectory").toString();
        if (directoryText.isNotEmpty())
        {
            const juce::File candidate (directoryText);
            if (candidate.isDirectory())
                packDirectory_ = candidate;
        }

        const auto presets = root.getChildWithName ("UserPresets");
        for (int i = 0; i < presets.getNumChildren(); ++i)
        {
            auto preset = CommunityPreset::fromValueTree (presets.getChild (i));
            if (preset.isValid())
                userPresets_.push_back (std::move (preset));
        }

        const auto scenes = root.getChildWithName ("UserScenes");
        for (int i = 0; i < scenes.getNumChildren(); ++i)
        {
            auto scene = CommunityScene::fromValueTree (scenes.getChild (i));
            if (scene.isValid())
                userScenes_.push_back (std::move (scene));
        }
    }

    void saveLocalSettings() const
    {
        juce::ValueTree root ("CommunityLibrary");
        if (packDirectory_.isDirectory())
            root.setProperty ("packDirectory", packDirectory_.getFullPathName(), nullptr);

        juce::ValueTree presets ("UserPresets");
        for (const auto& preset : userPresets_)
            presets.addChild (preset.toValueTree(), -1, nullptr);
        root.addChild (presets, -1, nullptr);

        juce::ValueTree scenes ("UserScenes");
        for (const auto& scene : userScenes_)
            scenes.addChild (scene.toValueTree(), -1, nullptr);
        root.addChild (scenes, -1, nullptr);

        const auto xml = root.createXml();
        if (xml != nullptr)
            static_cast<void> (settingsFile_.replaceWithText (xml->toString()));
    }

    juce::File settingsFile_;
    juce::File packDirectory_;
    std::vector<CommunityPreset> userPresets_;
    std::vector<CommunityScene> userScenes_;
    std::vector<LoadedPack> loadedPacks_;
};
} // namespace neumaton::community
