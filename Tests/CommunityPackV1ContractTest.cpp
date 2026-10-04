#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
std::string readFile (const char* path)
{
    std::ifstream file (path, std::ios::binary);
    if (! file)
    {
        std::cerr << "cannot_read=" << path << '\n';
        std::exit (EXIT_FAILURE);
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool contains (const std::string& text, const std::string& token)
{
    return text.find (token) != std::string::npos;
}

bool check (bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}
}

int main()
{
    const auto pack = readFile ("Source/ShareablePack.h");
    const auto library = readFile ("Source/CommunityPackLibrary.h");
    const auto customScales = readFile ("Source/CustomScalePresets.h");
    const auto processor = readFile ("Source/PluginProcessor.h");
    const auto page = readFile ("Source/CommunityPackPage.h");

    bool ok = true;

    ok &= check (
        contains (pack, "communityPackExtension = \".ecpk\"")
            && contains (pack, "ErgasterionCommunityPack")
            && contains (pack, "currentPackSchemaVersion = 2"),
        "ecpk_has_versioned_ergasterion_container");

    ok &= check (
        contains (pack, "name.trim().isNotEmpty()")
            && contains (pack, "author.trim().isNotEmpty()")
            && contains (pack, "juce::ValueTree scales { \"Scales\" }")
            && contains (pack, "juce::ValueTree presets { \"Presets\" }")
            && contains (pack, "juce::ValueTree scenes { \"Scenes\" }"),
        "manifest_and_three_content_sections_exist");

    ok &= check (
        contains (library, "struct CommunityScene")
            && contains (library, "scaleStableId")
            && contains (library, "presetStableId")
            && contains (library, "scene.scaleStableId")
            && contains (library, "scene.presetStableId"),
        "scene_is_scale_plus_preset_by_stable_id");

    ok &= check (
        contains (customScales, "std::numeric_limits<int>::max()")
            && ! contains (customScales, "static constexpr int maxPresets = 7"),
        "custom_scale_library_no_longer_has_seven_slot_limit");

    ok &= check (
        contains (library, "setPackDirectory")
            && contains (library, "findChildFiles")
            && contains (library, "communityPackWildcard")
            && ! contains (library, "getChildFile (\"CommunityPacks\")")
            && ! contains (library, "getChildFile (\"Packs\")"),
        "pack_discovery_uses_user_selected_directory");

    ok &= check (
        contains (library, "struct CommunityPreset")
            && contains (library, "addCurrentPreset")
            && contains (library, "addScene")
            && contains (library, "buildPack"),
        "local_presets_scenes_and_pack_builder_exist");

    ok &= check (
        contains (processor, "applyCommunityPreset")
            && contains (processor, "activateCommunityScale")
            && contains (processor, "applyCommunityScene"),
        "processor_exposes_message_thread_pack_application_api");

    ok &= check (
        contains (page, "Export .ecpk")
            && contains (page, "addSectionHeading (manifest.author)")
            && contains (page, "contentType_.addItem (\"Scales\"")
            && contains (page, "contentType_.addItem (\"Presets\"")
            && contains (page, "contentType_.addItem (\"Scenes\""),
        "browser_groups_pack_content_by_author_and_type");

    ok &= check (
        ! contains (library, "cryptographic")
            && contains (page, "Author / signature"),
        "v1_authorship_is_declared_metadata_not_fake_crypto");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
