#pragma once

#include <JuceHeader.h>

#include "Entitlement.h"

#include <algorithm>
#include <memory>
#include <vector>

namespace neumaton::sharing
{
inline constexpr int currentPackSchemaVersion = 3;
inline constexpr const char* communityPackExtension = ".ecpk";
inline constexpr const char* communityPackWildcard = "*.ecpk";

using PackLicense = licensing::PackLicense;

[[nodiscard]] inline const char* packLicenseToString (PackLicense license) noexcept
{
    return license == PackLicense::commercial ? "commercial" : "community";
}

[[nodiscard]] inline PackLicense packLicenseFromString (const juce::String& text) noexcept
{
    return text.trim().equalsIgnoreCase ("commercial")
        ? PackLicense::commercial
        : PackLicense::community;
}

enum class PackIntegrityStatus
{
    legacyUnsigned,
    valid,
    missingHash,
    hashMismatch
};

namespace detail
{
inline void appendField (juce::String& destination,
                         const juce::String& name,
                         const juce::String& value)
{
    destination << juce::String (name.getNumBytesAsUTF8()) << ":" << name
                << juce::String (value.getNumBytesAsUTF8()) << ":" << value;
}

[[nodiscard]] inline juce::String canonicalVar (const juce::var& value)
{
    if (value.isVoid())
        return "void:";
    if (value.isBool())
        return value ? "bool:1" : "bool:0";
    if (value.isInt())
        return "int:" + juce::String (static_cast<int> (value));
    if (value.isInt64())
        return "int64:" + juce::String (static_cast<juce::int64> (value));
    if (value.isDouble())
        return "double:" + juce::String (static_cast<double> (value), 17);

    return "string:" + value.toString();
}

[[nodiscard]] inline juce::String canonicalTree (const juce::ValueTree& tree)
{
    if (! tree.isValid())
        return "invalid";

    juce::String result;
    appendField (result, "type", tree.getType().toString());

    std::vector<juce::String> properties;
    properties.reserve (static_cast<std::size_t> (tree.getNumProperties()));
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        const auto name = tree.getPropertyName (i).toString();
        juce::String encoded;
        appendField (encoded, name, canonicalVar (tree.getProperty (tree.getPropertyName (i))));
        properties.push_back (std::move (encoded));
    }

    std::sort (properties.begin(), properties.end(),
        [] (const juce::String& a, const juce::String& b)
        {
            return a.compare (b) < 0;
        });

    for (const auto& property : properties)
        appendField (result, "property", property);

    std::vector<juce::String> children;
    children.reserve (static_cast<std::size_t> (tree.getNumChildren()));
    for (int i = 0; i < tree.getNumChildren(); ++i)
        children.push_back (canonicalTree (tree.getChild (i)));

    // Pack collections are semantic sets keyed by stable ids. Sorting the
    // canonical child representation keeps the hash independent of UI order.
    std::sort (children.begin(), children.end(),
        [] (const juce::String& a, const juce::String& b)
        {
            return a.compare (b) < 0;
        });

    for (const auto& child : children)
        appendField (result, "child", child);

    return result;
}

[[nodiscard]] inline juce::String sha256 (const juce::String& text)
{
    const auto* bytes = text.toRawUTF8();
    const auto byteCount = static_cast<std::size_t> (text.getNumBytesAsUTF8());
    return juce::SHA256 (bytes, byteCount).toHexString();
}
} // namespace detail

struct PackManifest
{
    juce::String stableId;
    juce::String name;
    juce::String author;
    juce::String version { "1.0.0" };
    juce::String description;

    // Schema 3 licensing/integrity metadata. creatorId may stay empty for
    // locally-authored community packs until an account/server exists.
    juce::String creatorId;
    PackLicense license = PackLicense::community;
    juce::String payloadSha256;

    // Reserved for the future server signature. No private key is ever stored
    // in the plugin; unsigned local packs remain normal Community Packs.
    juce::String keyId;
    juce::String signature;

    int schemaVersion = currentPackSchemaVersion;

    [[nodiscard]] bool isValid() const noexcept
    {
        return stableId.trim().isNotEmpty()
            && name.trim().isNotEmpty()
            && author.trim().isNotEmpty()
            && schemaVersion > 0
            && schemaVersion <= currentPackSchemaVersion;
    }

    [[nodiscard]] bool hasCompleteSignature() const noexcept
    {
        return keyId.trim().isNotEmpty() && signature.trim().isNotEmpty();
    }

    [[nodiscard]] bool hasPartialSignature() const noexcept
    {
        return keyId.trim().isNotEmpty() != signature.trim().isNotEmpty();
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

        if (schemaVersion >= 3)
        {
            tree.setProperty ("creatorId", creatorId, nullptr);
            tree.setProperty ("license", packLicenseToString (license), nullptr);
            tree.setProperty ("payloadSha256", payloadSha256, nullptr);
            tree.setProperty ("keyId", keyId, nullptr);
            tree.setProperty ("signature", signature, nullptr);
        }

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

        if (result.schemaVersion >= 3)
        {
            result.creatorId = tree.getProperty ("creatorId").toString();
            result.license = packLicenseFromString (
                tree.getProperty ("license", "community").toString());
            result.payloadSha256 = tree.getProperty ("payloadSha256").toString();
            result.keyId = tree.getProperty ("keyId").toString();
            result.signature = tree.getProperty ("signature").toString();
        }
        else
        {
            // Old packs predate commercial creator rights. Treating them as
            // Community is the safe backwards-compatible interpretation.
            result.license = PackLicense::community;
        }

        return result;
    }
};

// Ergasterion Community Pack. The container deliberately remains plain XML
// under .ecpk: inspectable, versioned and easy to migrate. Schema 3 adds a
// deterministic content digest and signature slots without encrypting content.
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

    [[nodiscard]] juce::String calculatePayloadSha256() const
    {
        juce::String canonical;
        detail::appendField (canonical, "scales", detail::canonicalTree (scales));
        detail::appendField (canonical, "presets", detail::canonicalTree (presets));
        detail::appendField (canonical, "scenes", detail::canonicalTree (scenes));
        return detail::sha256 (canonical);
    }

    void sealIntegrity()
    {
        if (manifest.schemaVersion >= 3)
            manifest.payloadSha256 = calculatePayloadSha256();
    }

    [[nodiscard]] PackIntegrityStatus verifyIntegrity() const
    {
        if (manifest.schemaVersion < 3)
            return PackIntegrityStatus::legacyUnsigned;

        if (manifest.payloadSha256.trim().isEmpty())
            return PackIntegrityStatus::missingHash;

        return manifest.payloadSha256.equalsIgnoreCase (calculatePayloadSha256())
            ? PackIntegrityStatus::valid
            : PackIntegrityStatus::hashMismatch;
    }

    // This is the exact logical message that a future server-side Ed25519
    // implementation should sign. Changing Community -> Commercial changes the
    // message even when the preset payload itself is unchanged.
    [[nodiscard]] juce::String signingPayload() const
    {
        juce::String result;
        detail::appendField (result, "schemaVersion", juce::String (manifest.schemaVersion));
        detail::appendField (result, "stableId", manifest.stableId);
        detail::appendField (result, "creatorId", manifest.creatorId);
        detail::appendField (result, "license", packLicenseToString (manifest.license));
        detail::appendField (result, "payloadSha256", manifest.payloadSha256);
        return result;
    }

    [[nodiscard]] juce::ValueTree toValueTree() const
    {
        juce::ValueTree root ("ErgasterionCommunityPack");
        root.setProperty ("schemaVersion", manifest.schemaVersion, nullptr);
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
        result.manifest.schemaVersion = rootSchema;

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

        if (rootSchema < 3)
            result.manifest.license = PackLicense::community;

        return result;
    }

    [[nodiscard]] bool writeToFile (juce::File target) const
    {
        if (! isValid())
            return false;

        if (! target.hasFileExtension (communityPackExtension))
            target = target.withFileExtension (communityPackExtension);

        auto sealed = *this;
        sealed.sealIntegrity();

        const auto xml = sealed.toValueTree().createXml();
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
