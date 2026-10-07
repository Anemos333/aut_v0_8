// Licensing architecture contract: licensing may gate UI/content rights, never audio.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
    const std::vector<const char*> audioFiles {
        "Source/PitchCore.h",
        "Source/PitchCore.cpp",
        "Source/PitchEngineV1.h",
        "Source/PitchEngineV1.cpp",
        "Source/SinglePathPitchRenderer.h",
        "Source/SinglePathPitchRenderer.cpp",
        "Source/LivePitchProcessor.h",
        "Source/ModernPitchEngine.h",
        "Source/ModernPitchEngine.cpp"
    };

    bool ok = true;
    bool audioIsolated = true;
    for (const auto* path : audioFiles)
    {
        const auto text = readFile (path);
        const bool isolated =
            ! contains (text, "EntitlementManager")
            && ! contains (text, "PackLicense")
            && ! contains (text, "PackVerifier")
            && ! contains (text, "commercialPackExport");

        if (! isolated)
            std::cerr << "licensing_reference_in_audio_file=" << path << '\n';

        audioIsolated &= isolated;
    }
    ok &= check (audioIsolated, "audio_engine_has_no_licensing_dependency");

    const auto processorCpp = readFile ("Source/PluginProcessor.cpp");
    ok &= check (! contains (processorCpp, "entitlementManager"),
                 "process_block_does_not_consult_entitlement");

    const auto entitlement = readFile ("Source/Entitlement.h");
    ok &= check (contains (entitlement, "NEUMATON_CLOSED_BETA")
                 && contains (entitlement, "commercialPackExport")
                 && contains (entitlement, "deviceLimit"),
                 "entitlement_is_policy_only");

    const auto pack = readFile ("Source/ShareablePack.h");
    ok &= check (contains (pack, "payloadSha256")
                 && contains (pack, "signingPayload")
                 && contains (pack, "PackLicense::commercial")
                 && ! contains (pack, "PRIVATE_KEY"),
                 "pack_schema_has_integrity_without_embedded_private_key");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
