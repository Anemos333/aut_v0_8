#pragma once

// NeumatonUILabels.h
// -----------------------------------------------------------------------------
// Single source of truth for the text shown in the plug-in interface.
//
// Important: these are DISPLAY LABELS only.
// Do not rename APVTS parameter IDs such as "amount", "speed", "humanize",
// "scaleLock", etc. unless you explicitly want to handle preset/session
// migration and DAW automation compatibility.
//
// The goal is to make the interface language coherent while keeping the DSP
// and saved-session compatibility stable.

namespace Neumaton::UI::Labels
{
namespace Main
{
    inline constexpr const char* preset           = "Preset:";
    inline constexpr const char* scale            = "Scale:";
    inline constexpr const char* rootNote         = "Root:";
    inline constexpr const char* mode             = "Mode:";

    inline constexpr const char* correctionAmount = "Correction Amount";
    inline constexpr const char* response         = "Response";

    // "Breath Control" is aesthetically strong, but may be confused with
    // breath/noise reduction metering. "Human Drift" says more honestly that
    // this is a musical looseness / non-robotic correction control.
    // Change this to "Breath Control" if you prefer the more poetic label.
    inline constexpr const char* humanize         = "Human Drift";

    inline constexpr const char* scaleLock        = "Scale Lock";
    inline constexpr const char* hold             = "Hold";
    inline constexpr const char* vibratoPreserve  = "Vibrato Preserve";

    // "Texture" is preferable to "Colour" if the mode changes the behaviour,
    // grain or instability of the correction rather than only the spectral tone.
    inline constexpr const char* analogTexture    = "Analog Texture";

    inline constexpr const char* output           = "Output";

    inline constexpr const char* tempoLab         = "Tempo Lab";
    inline constexpr const char* controlRoom      = "Control Room";
}

namespace Meter
{
    inline constexpr const char* detectedPitch    = "Detected Pitch";
    inline constexpr const char* targetFrequency  = "Target Frequency";
    inline constexpr const char* correction       = "Correction";
    inline constexpr const char* consensus        = "Consensus";
    inline constexpr const char* confidence       = "Confidence";
    inline constexpr const char* voicing          = "Voicing";
    inline constexpr const char* breath           = "Breath";
    inline constexpr const char* harmonic         = "Harmonic";
    inline constexpr const char* noisePath        = "Noise Path";
}

namespace Tempo
{
    // Creative Tempo is intentionally retired as a visible title.
    // The individual mode names are left unchanged for now.
    inline constexpr const char* pageTitle        = "Tempo Laboratory";

    inline constexpr const char* back             = "Back";
    inline constexpr const char* off              = "Off";
    inline constexpr const char* tempoGlide       = "Tempo Glide";
    inline constexpr const char* glideLock        = "Glide Lock";
    inline constexpr const char* division         = "Division";
    inline constexpr const char* glideLength      = "Glide Length";
    inline constexpr const char* lock             = "Lock";
    inline constexpr const char* smartOnset       = "Smart Onset";

    inline constexpr const char* requiresMode     = "Requires Quality, Live, or Experimental";
    inline constexpr const char* disabled         = "Disabled";
    inline constexpr const char* hostSync         = "Host sync";
    inline constexpr const char* bpmFallback      = "Fallback BPM / immediate glide";
    inline constexpr const char* waitingForGrid   = "Target waiting for next lock point";
    inline constexpr const char* targetFree       = "Free target";
}

namespace ScaleEditor
{
    // To be redesigned later.
    inline constexpr const char* title            = "Scale Editor";
}
} // namespace Neumaton::UI::Labels
