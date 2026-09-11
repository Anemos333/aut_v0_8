#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::string readFile(const char* path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::cerr << "cannot_read=" << path << '\n';
        std::exit(EXIT_FAILURE);
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

bool has(const std::string& text, const std::string& token)
{
    return text.find(token) != std::string::npos;
}

bool check(bool condition, const std::string& name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}
}

int main()
{
    bool success = true;
    const auto processor = readFile("Source/PluginProcessor.cpp");
    const auto processorHeader = readFile("Source/PluginProcessor.h");
    const auto editor = readFile("Source/PluginEditor.cpp");
    const auto editorHeader = readFile("Source/PluginEditor.h");
    const auto engine = readFile("Source/ModernPitchEngine.cpp");
    const auto engineHeader = readFile("Source/ModernPitchEngine.h");
    const auto renderer = readFile("Source/SingleWetSpectralRenderer.cpp");
    const auto rendererHeader = readFile("Source/SingleWetSpectralRenderer.h");
    const auto tempo = readFile("Source/Tempo.cpp");

    const std::vector<std::string> parameterIds {
        "speed", "amount", "humanize", "tempoMode", "tempoDivision",
        "tempoGlidePercent", "tempoLockStrength", "tempoSmartOnset",
        "scaleLock", "lockHysteresis", "vibratoPreserve", "analogMode",
        "outVolume"
    };

    for (const auto& id : parameterIds)
    {
        success &= check(has(processor, "ParameterID { \"" + id + "\"")
                         && has(editor, "\"" + id + "\""),
                         "gui_parameter_declared_and_bound_" + id);
    }

    success &= check(has(processor, "livePitchProcessor.process (buffer")
                         && has(processor, "speedMs")
                         && has(processor, "amount"),
                         "response_and_amount_reach_modern_audio");

    success &= check(has(processor,
                         "setScaleLockParameters(scaleLock, lockHysteresis, vibratoPreserve)"),
                     "scale_lock_controls_reach_engine");

    success &= check(has(processor, "setTempoSettings (getTempoSettings())")
                         && has(tempo, "settings.glideFraction")
                         && has(tempo, "settings.lockStrength")
                         && has(tempo, "settings.smartOnset"),
                     "tempo_controls_reach_scheduler");

    success &= check(has(processor, "fastSoftClip(value)")
                         && has(processor, "analogLowShelfFilters_")
                         && has(processor, "analogHighShelfFilters_")
                         && has(processor, "value *= outGain"),
                     "analog_and_output_controls_change_samples");

    success &= check(has(editorHeader, "AudioControlAvailabilityGuard")
                         && !has(editor, "\"High Latency\"")
                         && !has(processor, "detectPitchYIN")
                         && !has(processorHeader, "detectPitchYIN")
                         && !has(processor, "smoothedShiftRatio")
                         && !has(processorHeader, "smoothedShiftRatio")
                         && !has(processor, "circularBuffer")
                         && !has(processorHeader, "circularBuffer")
                         && !has(processor, "yinBuffer")
                         && !has(processorHeader, "yinBuffer")
                         && !has(processor, "findNearestTarget")
                         && !has(processorHeader, "findNearestTarget")
                         && has(processor, "SINGLE_PLUGIN_AUDIO_PATH_V1"),
                     "plugin_has_only_modern_single_audio_path");

    success &= check(has(editorHeader,
                         "const bool tempoShapesTrajectory = tempoMode != 0")
                         && has(editorHeader,
                                "const bool glideLockMode = tempoMode == 2")
                         && has(editorHeader,
                                "tempoDivisionSelector.setEnabled (tempoShapesTrajectory)")
                         && has(editorHeader,
                                "tempoLockStrength.setEnabled (glideLockMode)"),
                     "tempo_gui_matches_active_semantics");

    success &= check(has(editor, "processorRef.refreshScaleSnapshot()")
                         && has(editor, "processorRef.updateProcessingMode (newMode)")
                         && has(editor, "const int newMode = selectedId")
                         && !has(editor, "int newMode = selectedId - 1"),
                     "scale_root_and_mode_selectors_reach_audio_state");

    success &= check(has(editor, "3.0 + 2.0 * norm")
                         && has(editor, "1.5 + 1.5 * norm")
                         && has(editor, "0.35 + 1.15 * norm")
                         && has(engine, "3.0 + 2.0 * norm")
                         && has(engine, "1.5 + 1.5 * norm")
                         && has(engine, "0.35 + 1.15 * norm"),
                     "response_display_matches_dsp_curve");

    success &= check(!has(engine, "TransportClock")
                         && !has(engine, "ChannelPath::")
                         && !has(engine, "calculateReflectionCoefficients")
                         && !has(engine, "updateLpcTarget")
                         && has(engine, "wetRenderers_"),
                     "single_wet_has_no_dormant_transport_renderer");

    success &= check(has(engine, "DETECTOR_IS_OBSERVER_V1")
                         && !has(engine, "makePresenceFallback")
                         && !has(engine, "setImmediateAuthority")
                         && !has(engineHeader, "setImmediateAuthority")
                         && !has(engine, "presenceInitialEvidence")
                         && !has(engine, "presenceMode_ ? (decision.candidate.valid"),
                     "detector_observes_but_never_owns_scale_authority");

    success &= check(!has(engine, "desiredCents = 0.0")
                         && !has(engine, "TrackingState::release")
                         && has(engine, "PHONETIC_STATE_HAS_NO_CORRECTION_AUTHORITY_V1"),
                     "phonetic_and_release_states_cannot_reduce_correction");

    success &= check(has(processor, "HOST_BYPASS_ONLY_DRY_V1")
                         && has(processor, "livePitchProcessor.processBypassed (buffer)")
                         && has(renderer, "ACTIVE_PATH_NEVER_DRY_FALLBACK_V1"),
                     "dry_exists_only_as_explicit_host_bypass");

    success &= check(has(renderer, "FULL_SPECTRUM_SINGLE_TRANSPORT_V1")
                         && has(renderer, "STABLE_SINGLE_LATTICE_TRANSPORT_V3")
                         && has(renderer, "PURE_SINGLE_TRANSPORT_V4")
                         && has(renderer, "MINIMAL_RENDERER_V5")
                         && has(renderer,
                                "const double targetPosition = static_cast<double>(sourceBin) * safeRatio")
                         && has(renderer,
                                "synthesisPhase += expectedPhaseScale")
                         && has(renderer,
                                "* trueSourceBins_[static_cast<std::size_t>(sourceBin)]")
                         && has(renderer,
                                "const double outputPhase = propagatedPhases_[sourceIndex]")
                         && has(renderer,
                                "const float outputMagnitude = magnitude"),
                     "renderer_uses_one_minimal_audio_transport");

    const std::vector<std::string> forbiddenRendererTerms {
        "layer.spectrum[sourceIndex] += fftBuffer_[sourceIndex]",
        "const float harmonicMagnitude",
        "const double sourcePosition = std::clamp(",
        "phaseGuidance",
        "peakLockedPhase",
        "reconstructionGain",
        "phaseAnchor",
        "aperiodicEvidence",
        "updateHarmonicNoiseAnalysis",
        "harmonicMask",
        "noisePath",
        "noiseDominance",
        "breathProtection",
        "smoothedSpectralReliability_",
        "struct Context"
    };
    bool rendererClean = true;
    for (const auto& term : forbiddenRendererTerms)
        rendererClean = rendererClean && !has(renderer, term) && !has(rendererHeader, term);
    success &= check(rendererClean,
                     "protective_logic_is_physically_absent_from_renderer");

    success &= check(!has(rendererHeader, "confidence")
                         && !has(rendererHeader, "voicing")
                         && !has(rendererHeader, "breathReduction")
                         && !has(rendererHeader, "noteBodyLatched")
                         && !has(engine, "rendererContext"),
                     "voice_state_has_no_renderer_audio_api");

    success &= check(has(engine, "pitchStaleSamples")
                         && has(engine, "noteBodyLatched")
                         && has(engine, "vibratoPreserve"),
                     "voice_state_and_vibrato_remain_upstream_supervision");

    success &= check(!has(renderer, "Wind Fix V6")
                         && !has(renderer, "frameSize_ <= 128 ? 42.0f")
                         && !has(renderer, "frameSize_ <= 256 ? 34.0f"),
                     "modern_modes_share_one_reconstruction_law");

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
