#pragma once

// Display text only. APVTS IDs and serialized legacy parameters remain intact
// in the processor so that existing sessions and automation keep loading.
namespace Neumaton::UI::Labels
{
namespace Main
{
    inline constexpr const char* preset           = "Preset:";
    inline constexpr const char* scale            = "Scale:";
    inline constexpr const char* mode             = "Mode:";
    inline constexpr const char* correctionAmount = "Correction Amount";
    inline constexpr const char* response         = "Response";
    inline constexpr const char* humanize         = "Human Drift";
    inline constexpr const char* hold             = "Boundary Stability";
    inline constexpr const char* vibratoPreserve   = "Output Drive";
    inline constexpr const char* analogTexture    = "Analog Texture";
    inline constexpr const char* output           = "Output";
    inline constexpr const char* controlRoom      = "Control Room";
}
namespace Meter
{
    inline constexpr const char* correction       = "Correction";
    inline constexpr const char* scaleDegree      = "Scale Degree";
}
} // namespace Neumaton::UI::Labels
