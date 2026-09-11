from pathlib import Path
import re


def require_once(text: str, needle: str, label: str) -> None:
    count = text.count(needle)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 occurrence, found {count}")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    require_once(text, old, label)
    return text.replace(old, new, 1)


def remove_between(text: str, start_marker: str, end_marker: str, label: str,
                   keep_end: bool = True) -> str:
    start = text.find(start_marker)
    if start < 0:
        raise SystemExit(f"{label}: start marker not found")
    end = text.find(end_marker, start)
    if end < 0:
        raise SystemExit(f"{label}: end marker not found")
    return text[:start] + (text[end:] if keep_end else text[end + len(end_marker):])


# -----------------------------------------------------------------------------
# PluginProcessor.h: physically remove the legacy Slow/YIN audio engine state.
processor_h_path = Path("Source/PluginProcessor.h")
h = processor_h_path.read_text()

h = replace_once(
    h,
    "    // 0 = High Latency (Slow), 1 = Quality, 2 = Live, 3 = Experimental\n"
    "    std::atomic<int> processingMode { 1 };",
    "    // Release modes: 1 = Quality, 2 = Live, 3 = Experimental.\n"
    "    // Mode 0 was the removed legacy YIN renderer and is never audible.\n"
    "    std::atomic<int> processingMode { 1 };",
    "processing mode declaration")

h = replace_once(
    h,
    "    // YIN pitch detection (Slow mode)\n"
    "    float detectPitchYIN (const float* buffer, int numSamples, double sampleRate) noexcept;\n\n",
    "",
    "remove slow detector declaration")

h = replace_once(
    h,
    "    // Find nearest note in scale (Slow mode), using the same immutable\n"
    "    // snapshot as the modern engines.\n"
    "    double findNearestTarget (double detectedFreqHz,\n"
    "                              const ScaleSnapshot& snapshot) const noexcept;\n\n",
    "",
    "remove slow target finder declaration")

slow_state_start = "    // Pitch shifting state (Slow mode)\n"
modern_member_marker = "    // ModernPitchEngine-based live pitch processor (Quality/Live/Experimental modes)\n"
start = h.find(slow_state_start)
end = h.find(modern_member_marker, start)
if start < 0 or end < 0:
    raise SystemExit("slow state block markers changed")
h = h[:start] + "    double currentSampleRate = 44100.0;\n\n" + h[end:]

h = replace_once(
    h,
    "    // Get the latency in samples for the current mode\n"
    "    int getLatencyForMode (int mode) const;\n\n",
    "",
    "remove legacy latency helper")
processor_h_path.write_text(h)


# -----------------------------------------------------------------------------
# PluginProcessor.cpp: only the ModernPitchEngine adapter is allowed to render.
processor_path = Path("Source/PluginProcessor.cpp")
p = processor_path.read_text()

p = replace_once(
    p,
    "        default: return ModernPitchEngine::LatencyMode::live; // fallback\n",
    "        default: return ModernPitchEngine::LatencyMode::quality; // fail-safe release mode\n",
    "mode fallback")

# Remove the obsolete mode-0 latency helper completely.
p = remove_between(
    p,
    "int MicrotonalAutotuneAudioProcessor::getLatencyForMode (int mode) const\n",
    "//==============================================================================\nvoid MicrotonalAutotuneAudioProcessor::prepareToPlay",
    "remove legacy latency implementation")

# Remove all Slow/YIN allocation and reset state from prepareToPlay.
prepare_slow_start = "    smoothedShiftRatio = 1.0;\n\n    // Circular buffer: 4x YIN window for comfortable pitch shifting (Slow mode)\n"
prepare_modern_marker = "    // Prepare ModernPitchEngine-based live pitch processor\n"
start = p.find(prepare_slow_start)
end = p.find(prepare_modern_marker, start)
if start < 0 or end < 0:
    raise SystemExit("prepare slow-state markers changed")
p = p[:start] + p[end:]

prepare_start = p.find(prepare_modern_marker)
prepare_end_marker = "\n}\n\nvoid MicrotonalAutotuneAudioProcessor::releaseResources()"
prepare_end = p.find(prepare_end_marker, prepare_start)
if prepare_start < 0 or prepare_end < 0:
    raise SystemExit("prepare modern block markers changed")
new_prepare = """    // The plugin has one audio engine family only. Quality, Live and
    // Experimental select already-prepared ModernPitchEngine profiles; no
    // legacy renderer is kept beside them.
    const int mode = juce::jlimit (1, 3,
        processingMode.load (std::memory_order_acquire));
    processingMode.store (mode, std::memory_order_release);
    livePitchProcessor.prepare (currentSampleRate,
                                lastSamplesPerBlock,
                                std::max (1, getTotalNumOutputChannels()),
                                modeToLatency (mode));
    const float humanizeVal = apvts.getRawParameterValue ("humanize")->load() / 100.0f;
    livePitchProcessor.setAdvancedParameters (
        35.0f,   // transitionMs
        0.70f,   // preserveVibrato
        humanizeVal,
        0.90f,   // formantPreservation
        0.85f,   // transientProtection
        0.70f,   // detectorSensitivity
        12.0f,   // maximumCorrectionSemitones
        45.0f,   // minimumPitchHz
        1600.0f, // maximumPitchHz
        LivePitchProcessor::StereoMode::linkedMidSide
    );
    setLatencySamples (livePitchProcessor.getLatencySamples());"""
p = p[:prepare_start] + new_prepare + p[prepare_end:]

for line in (
    "    circularBuffer.clear();\n",
    "    yinBuffer.clear();\n",
    "    yinAccumulator.clear();\n",
):
    if line in p:
        p = p.replace(line, "", 1)

# Replace mode switching with a strict 1..3 publication. Old mode 0 can never
# re-open a second renderer.
mode_update_start = p.find("void MicrotonalAutotuneAudioProcessor::updateProcessingMode (int newMode)\n")
mode_update_end = p.find("//==============================================================================\nvoid MicrotonalAutotuneAudioProcessor::resetSlowStateNoAlloc()", mode_update_start)
if mode_update_start < 0 or mode_update_end < 0:
    raise SystemExit("mode update / slow reset markers changed")
new_mode_update = """void MicrotonalAutotuneAudioProcessor::updateProcessingMode (int newMode)
{
    newMode = juce::jlimit (1, 3, newMode);
    const int oldMode = processingMode.load (std::memory_order_acquire);
    if (newMode == oldMode)
        return;

    livePitchProcessor.setLatencyModeNonRealtime (modeToLatency (newMode));
    processingMode.store (newMode, std::memory_order_release);
    setLatencySamples (livePitchProcessor.getLatencySamples());
}

"""
p = p[:mode_update_start] + new_mode_update + p[mode_update_end:]

# Remove resetSlowStateNoAlloc + YIN detector in one slice.
p = remove_between(
    p,
    "//==============================================================================\nvoid MicrotonalAutotuneAudioProcessor::resetSlowStateNoAlloc() noexcept\n",
    "//==============================================================================\nstd::vector<double> MicrotonalAutotuneAudioProcessor::getCurrentScaleRatios() const",
    "remove slow reset and YIN detector")

# The immutable scale snapshot remains useful to the modern engine; only the
# legacy nearest-note helper is removed.
p = remove_between(
    p,
    "double MicrotonalAutotuneAudioProcessor::findNearestTarget (\n",
    "//==============================================================================\nCreativeTempo::Settings",
    "remove slow target finder")

p = p.replace(
    "    // Preserve the musical invariant expected by both Slow mode and the modern\n"
    "    // ScaleQuantizer: every scale degree lives inside one octave [1.0, 2.0),\n",
    "    // Preserve the musical invariant expected by the modern ScaleQuantizer:\n"
    "    // every scale degree lives inside one octave [1.0, 2.0),\n",
    1)

p = replace_once(
    p,
    "    updateProcessingMode (juce::jlimit (0, 3, preset.processingMode));\n",
    "    updateProcessingMode (juce::jlimit (1, 3, preset.processingMode));\n",
    "factory preset mode clamp")

p = replace_once(
    p,
    "    int mode = juce::jlimit (0, 3, processingMode.load (std::memory_order_relaxed));\n",
    "    const int mode = juce::jlimit (1, 3, processingMode.load (std::memory_order_relaxed));\n",
    "audio callback mode clamp")

# Replace the entire modern/slow fork with the one existing modern path.
modern_fork_start = p.find("    // ==================== MODERN ENGINE MODES (Quality / Live / Experimental) ====================\n")
modern_fork_end = p.find("//==============================================================================\nvoid MicrotonalAutotuneAudioProcessor::processBlockBypassed", modern_fork_start)
if modern_fork_start < 0 or modern_fork_end < 0:
    raise SystemExit("processBlock modern/slow fork markers changed")
new_audio_path = """    // SINGLE_PLUGIN_AUDIO_PATH_V1: every release mode uses the same modern
    // detector -> correction controller -> SingleWetSpectralRenderer chain.
    // There is no mode-local YIN/circular-buffer renderer and no dry blend.
    livePitchProcessor.setTempoSettings (getTempoSettings());
    livePitchProcessor.setTempoHostPosition (
        readHostTempoPosition (numSamples));
    livePitchProcessor.setScaleLockParameters(scaleLock, lockHysteresis, vibratoPreserve);

    livePitchProcessor.setAdvancedParameters (
        35.0f,   // transitionMs
        0.70f,   // preserveVibrato
        humanizeVal,
        0.90f,   // formantPreservation
        0.85f,   // transientProtection
        0.70f,   // detectorSensitivity
        12.0f,   // maximumCorrectionSemitones
        45.0f,   // minimumPitchHz
        1600.0f, // maximumPitchHz
        LivePitchProcessor::StereoMode::linkedMidSide
    );

    livePitchProcessor.process (buffer,
                                scaleSnapshot.ratios.data(),
                                scaleSnapshot.count,
                                scaleSnapshot.rootFrequency,
                                speedMs,
                                amount);
    releaseScaleSnapshot (snapshotIndex);

    processOutputStage (buffer,
                        totalNumInputChannels,
                        numSamples,
                        analogMode,
                        outGain);
}
"""
p = p[:modern_fork_start] + new_audio_path + p[modern_fork_end:]

# Explicit host bypass is the only legitimate dry route and remains latency
# aligned through the selected modern renderer.
bypass_start = p.find("void MicrotonalAutotuneAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,\n")
bypass_end = p.find("//==============================================================================\nLivePitchProcessor::Metering", bypass_start)
if bypass_start < 0 or bypass_end < 0:
    raise SystemExit("bypass function markers changed")
new_bypass = """void MicrotonalAutotuneAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                              juce::MidiBuffer&)
{
    // HOST_BYPASS_ONLY_DRY_V1: this is the host's explicit plugin bypass, not
    // a detector/correction decision and not an alternate active audio path.
    livePitchProcessor.processBypassed (buffer);
}

"""
p = p[:bypass_start] + new_bypass + p[bypass_end:]

meter_start = p.find("LivePitchProcessor::Metering\nMicrotonalAutotuneAudioProcessor::getPitchMetering() const noexcept\n")
meter_end = p.find("//==============================================================================\nbool MicrotonalAutotuneAudioProcessor::hasEditor() const", meter_start)
if meter_start < 0 or meter_end < 0:
    raise SystemExit("meter function markers changed")
new_meter = """LivePitchProcessor::Metering
MicrotonalAutotuneAudioProcessor::getPitchMetering() const noexcept
{
    return livePitchProcessor.getMetering();
}

"""
p = p[:meter_start] + new_meter + p[meter_end:]

old_restore = """            // Restore processing mode (with backward compatibility for liveModeEnabled)
            if (tree.hasProperty ("processingMode"))
            {
                int mode = juce::jlimit (0, 3,
                    static_cast<int> (tree.getProperty ("processingMode")));
                updateProcessingMode (mode);
            }
            else if (tree.hasProperty ("liveModeEnabled"))
            {
                // Backward compatibility: old sessions with liveModeEnabled bool
                bool wasLive = static_cast<int> (tree.getProperty ("liveModeEnabled")) != 0;
                int mode = wasLive ? 2 : 0; // map old Live to new Live mode
                updateProcessingMode (mode);
            }
"""
new_restore = """            // Restore only the three modern modes. Legacy mode 0 is mapped
            // forward to Quality instead of resurrecting its removed renderer.
            if (tree.hasProperty ("processingMode"))
            {
                const int storedMode = static_cast<int> (tree.getProperty ("processingMode"));
                updateProcessingMode (storedMode == 0 ? 1 : juce::jlimit (1, 3, storedMode));
            }
            else if (tree.hasProperty ("liveModeEnabled"))
            {
                const bool wasLive = static_cast<int> (tree.getProperty ("liveModeEnabled")) != 0;
                updateProcessingMode (wasLive ? 2 : 1);
            }
"""
p = replace_once(p, old_restore, new_restore, "state restore mode migration")
processor_path.write_text(p)


# -----------------------------------------------------------------------------
# PluginEditor.cpp: expose only Quality / Live / Experimental, preserving the
# numeric values already used by presets and saved modern sessions.
editor_path = Path("Source/PluginEditor.cpp")
e = editor_path.read_text()
old_modes = """    modeSelector.setJustificationType (juce::Justification::centredLeft);
    modeSelector.addItem ("High Latency", 1);
    modeSelector.addItem ("Quality",      2);
    modeSelector.addItem ("Live",         3);
    modeSelector.addItem ("Experimental", 4);
    modeSelector.setSelectedId (processorRef.processingMode.load() + 1, juce::dontSendNotification);
"""
new_modes = """    modeSelector.setJustificationType (juce::Justification::centredLeft);
    modeSelector.addItem ("Quality",      1);
    modeSelector.addItem ("Live",         2);
    modeSelector.addItem ("Experimental", 3);
    modeSelector.setSelectedId (processorRef.processingMode.load(), juce::dontSendNotification);
"""
e = replace_once(e, old_modes, new_modes, "mode selector entries")

e = replace_once(
    e,
    "    modeSelector.setSelectedId (\n"
    "        processorRef.processingMode.load() + 1,\n"
    "        juce::dontSendNotification);\n",
    "    modeSelector.setSelectedId (\n"
    "        processorRef.processingMode.load(),\n"
    "        juce::dontSendNotification);\n",
    "preset mode selector refresh")

e = replace_once(
    e,
    "        int newMode = selectedId - 1; // ComboBox ID 1-4 → mode 0-3\n"
    "        processorRef.updateProcessingMode (newMode);\n",
    "        const int newMode = selectedId; // IDs are the release mode values 1..3.\n"
    "        processorRef.updateProcessingMode (newMode);\n",
    "mode selection mapping")

old_sync = """    juce::String syncText;
    if (processorRef.processingMode.load() == 0)
        syncText = Neumaton::UI::Labels::Tempo::requiresMode;
    else if (!displayedMetering.tempoActive)
        syncText = Neumaton::UI::Labels::Tempo::disabled;
"""
new_sync = """    juce::String syncText;
    if (!displayedMetering.tempoActive)
        syncText = Neumaton::UI::Labels::Tempo::disabled;
"""
e = replace_once(e, old_sync, new_sync, "remove tempo legacy-mode status")
editor_path.write_text(e)


# -----------------------------------------------------------------------------
# PluginEditor.h: every selectable mode is modern, so no controls depend on a
# removed legacy path being active.
editor_h_path = Path("Source/PluginEditor.h")
eh = editor_h_path.read_text()
eh = replace_once(
    eh,
    "            const int processingMode = owner.processorRef.processingMode.load();\n"
    "            const bool modernMode = processingMode > 0;\n"
    "            const bool scaleLockActive = modernMode\n"
    "                && owner.scaleLockButton.getToggleState();\n",
    "            const int processingMode = owner.processorRef.processingMode.load();\n"
    "            const bool scaleLockActive = owner.scaleLockButton.getToggleState();\n",
    "availability guard modern mode")

legacy_guard = """            // High Latency intentionally stays the untouched legacy YIN path.
            // Controls not consumed there are disabled instead of becoming
            // placebo controls. Scale/root/Response/Amount/Analog/Output remain
            // enabled because the legacy path genuinely consumes them.
            owner.humanizeSlider.setEnabled (modernMode);
            owner.humanizeLabel.setEnabled (modernMode);
            owner.scaleLockButton.setEnabled (modernMode);
            owner.tempoPageButton.setEnabled (modernMode);
"""
modern_guard = """            // All selectable modes share the same modern audio path.
            owner.humanizeSlider.setEnabled (true);
            owner.humanizeLabel.setEnabled (true);
            owner.scaleLockButton.setEnabled (true);
            owner.tempoPageButton.setEnabled (true);
"""
eh = replace_once(eh, legacy_guard, modern_guard, "remove high-latency control gate")

eh = replace_once(
    eh,
    "            const bool tempoShapesTrajectory = modernMode && tempoMode != 0;\n"
    "            const bool glideLockMode = modernMode && tempoMode == 2;\n",
    "            const bool tempoShapesTrajectory = tempoMode != 0;\n"
    "            const bool glideLockMode = tempoMode == 2;\n",
    "tempo applicability")

for control in ("tempoOffButton", "tempoGlideButton", "glideLockButton"):
    eh = replace_once(
        eh,
        f"            owner.{control}.setEnabled (modernMode);\n",
        f"            owner.{control}.setEnabled (true);\n",
        f"enable {control}")

eh = replace_once(
    eh,
    "\n            if (!modernMode && owner.showingTempoPage)\n"
    "                owner.closeTempoPage();\n",
    "",
    "remove legacy tempo-page close")
editor_h_path.write_text(eh)


# -----------------------------------------------------------------------------
# ModernPitchEngine.cpp: detector labels may never write zero correction, and
# the old release-to-zero state machine is physically removed from active code.
engine_path = Path("Source/ModernPitchEngine.cpp")
m = engine_path.read_text()

old_prebody = """    if (!exactAuthority
        && !state.noteBodyLatched && richEvidence
        && (confirmedBreathFrame || confirmedAbsenceFrame))
    {
        setState(TrackingState::unvoiced);
        state.desiredCents = 0.0;
        return;
    }

"""
m = replace_once(
    m, old_prebody,
    "    // PHONETIC_STATE_HAS_NO_CORRECTION_AUTHORITY_V1: breath/absence\n"
    "    // evidence may classify the detector state, but it never writes a\n"
    "    // source/unity correction. A valid coordinate continues into the same\n"
    "    // quantizer; an invalid coordinate keeps the already-owned target.\n\n",
    "remove pre-body dry release")

m = replace_once(
    m,
    "        || state.trackingState == TrackingState::release\n"
    "        || state.trackingState == TrackingState::unvoiced;\n",
    "        || state.trackingState == TrackingState::unvoiced;\n",
    "release forbidden-state trace")

m = replace_once(
    m,
    "        && (!state.noteBodyLatched\n"
    "            || state.trackingState == TrackingState::unvoiced\n"
    "            || state.trackingState == TrackingState::release);\n",
    "        && (!state.noteBodyLatched\n"
    "            || state.trackingState == TrackingState::unvoiced);\n",
    "release onset trace")

m = replace_once(
    m,
    "    else if (state.trackingState == TrackingState::unvoiced\n"
    "             || state.trackingState == TrackingState::release)\n",
    "    else if (state.trackingState == TrackingState::unvoiced)\n",
    "release acquire trace")

release_cleanup_start = m.find(
    "        if (state.trackingState == TrackingState::release\n"
    "            && std::abs(state.currentCents) < 0.001)\n")
if release_cleanup_start < 0:
    raise SystemExit("release-to-zero cleanup not found")
release_cleanup_end = m.find("        }\n", release_cleanup_start)
if release_cleanup_end < 0:
    raise SystemExit("release-to-zero cleanup end not found")
release_cleanup_end += len("        }\n")
m = m[:release_cleanup_start] + m[release_cleanup_end:]

m = replace_once(
    m,
    "        if (linkedCorrection_.noteBodyLatched\n"
    "            && linkedCorrection_.trackingState != TrackingState::unvoiced\n"
    "            && linkedCorrection_.trackingState != TrackingState::release)\n",
    "        if (linkedCorrection_.noteBodyLatched\n"
    "            && linkedCorrection_.trackingState != TrackingState::unvoiced)\n",
    "release sustained trace")

if "TrackingState::release" in m:
    raise SystemExit("active engine still contains release-state logic")
if "desiredCents = 0.0" in m:
    raise SystemExit("active engine still contains explicit zero-correction release")
engine_path.write_text(m)


# -----------------------------------------------------------------------------
# Active renderer must never return source audio because preparation failed.
# Explicit host bypass remains a separate API and is the only dry route.
renderer_path = Path("Source/SingleWetSpectralRenderer.cpp")
r = renderer_path.read_text()
r = replace_once(
    r,
    "    if (frameSize_ <= 0 || inputRing_.empty())\n"
    "        return inputSample;\n",
    "    if (frameSize_ <= 0 || inputRing_.empty())\n"
    "    {\n"
    "        // ACTIVE_PATH_NEVER_DRY_FALLBACK_V1: an invalid lifecycle state\n"
    "        // fails closed instead of silently granting source-pitch authority.\n"
    "        return 0.0f;\n"
    "    }\n",
    "active renderer dry fallback")
renderer_path.write_text(r)


# -----------------------------------------------------------------------------
# Static contract test: make the subtractive architecture a permanent gate.
test_path = Path("Tests/GuiAudioControlContractTest.cpp")
t = test_path.read_text()
t = replace_once(
    t,
    "    const auto processor = readFile(\"Source/PluginProcessor.cpp\");\n",
    "    const auto processor = readFile(\"Source/PluginProcessor.cpp\");\n"
    "    const auto processorHeader = readFile(\"Source/PluginProcessor.h\");\n",
    "read processor header")

old_high_latency_test = """    success &= check(has(editorHeader, "AudioControlAvailabilityGuard")
                         && has(editorHeader, "humanizeSlider.setEnabled (modernMode)")
                         && has(editorHeader, "scaleLockButton.setEnabled (modernMode)")
                         && has(editorHeader, "tempoPageButton.setEnabled (modernMode)"),
                     "high_latency_disables_modern_only_controls");

"""
new_high_latency_test = """    success &= check(has(editorHeader, "AudioControlAvailabilityGuard")
                         && !has(editor, "\\\"High Latency\\\"")
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

"""
t = replace_once(t, old_high_latency_test, new_high_latency_test,
                 "replace high-latency contract")

old_tempo_test = """    success &= check(has(editorHeader,
                         "const bool tempoShapesTrajectory = modernMode && tempoMode != 0")
                         && has(editorHeader,
                                "const bool glideLockMode = modernMode && tempoMode == 2")
                         && has(editorHeader,
                                "tempoDivisionSelector.setEnabled (tempoShapesTrajectory)")
                         && has(editorHeader,
                                "tempoLockStrength.setEnabled (glideLockMode)"),
                     "tempo_gui_matches_active_semantics");
"""
new_tempo_test = """    success &= check(has(editorHeader,
                         "const bool tempoShapesTrajectory = tempoMode != 0")
                         && has(editorHeader,
                                "const bool glideLockMode = tempoMode == 2")
                         && has(editorHeader,
                                "tempoDivisionSelector.setEnabled (tempoShapesTrajectory)")
                         && has(editorHeader,
                                "tempoLockStrength.setEnabled (glideLockMode)"),
                     "tempo_gui_matches_active_semantics");
"""
t = replace_once(t, old_tempo_test, new_tempo_test, "tempo contract")

t = replace_once(
    t,
    "    success &= check(has(editor, \"processorRef.refreshScaleSnapshot()\")\n"
    "                         && has(editor, \"processorRef.updateProcessingMode (newMode)\"),\n"
    "                     \"scale_root_and_mode_selectors_reach_audio_state\");\n",
    "    success &= check(has(editor, \"processorRef.refreshScaleSnapshot()\")\n"
    "                         && has(editor, \"processorRef.updateProcessingMode (newMode)\")\n"
    "                         && has(editor, \"const int newMode = selectedId\")\n"
    "                         && !has(editor, \"selectedId - 1\"),\n"
    "                     \"scale_root_and_mode_selectors_reach_audio_state\");\n",
    "mode selector contract")

# Add authority/bypass invariants immediately before the renderer transport test.
renderer_anchor = "    success &= check(has(renderer, \"FULL_SPECTRUM_SINGLE_TRANSPORT_V1\")\n"
require_once(t, renderer_anchor, "renderer test anchor")
extra_contract = """    success &= check(!has(engine, "desiredCents = 0.0")
                         && !has(engine, "TrackingState::release")
                         && has(engine, "PHONETIC_STATE_HAS_NO_CORRECTION_AUTHORITY_V1"),
                     "phonetic_and_release_states_cannot_reduce_correction");

    success &= check(has(processor, "HOST_BYPASS_ONLY_DRY_V1")
                         && has(processor, "livePitchProcessor.processBypassed (buffer)")
                         && has(renderer, "ACTIVE_PATH_NEVER_DRY_FALLBACK_V1"),
                     "dry_exists_only_as_explicit_host_bypass");

"""
t = t.replace(renderer_anchor, extra_contract + renderer_anchor, 1)
test_path.write_text(t)


# Final materializer-level guardrails.
for path, forbidden in {
    Path("Source/PluginProcessor.cpp"): [
        "detectPitchYIN", "smoothedShiftRatio", "circularBuffer", "yinBuffer",
        "yinAccumulator", "lastDetectedPitch", "slowMeterPitchHz",
        "slowMeterTargetHz", "slowResetRequested", "findNearestTarget",
        "SLOW MODE", "High Latency"
    ],
    Path("Source/PluginProcessor.h"): [
        "detectPitchYIN", "smoothedShiftRatio", "circularBuffer", "yinBuffer",
        "yinAccumulator", "lastDetectedPitch", "slowMeterPitchHz",
        "slowMeterTargetHz", "slowResetRequested", "findNearestTarget",
        "High Latency"
    ],
    Path("Source/PluginEditor.cpp"): ["\"High Latency\""],
    Path("Source/PluginEditor.h"): ["High Latency", "modernMode"],
}.items():
    text = path.read_text()
    for token in forbidden:
        if token in text:
            raise SystemExit(f"{path}: forbidden legacy token remains: {token}")

print("single-authority subtractive cleanup materialized")
