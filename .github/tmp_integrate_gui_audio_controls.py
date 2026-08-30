from pathlib import Path


def replace_once(text, old, new, label):
    if old not in text:
        raise RuntimeError(f'missing marker: {label}')
    return text.replace(old, new, 1)

# -----------------------------------------------------------------------------
# DSP semantics: keep correction authority, widen the audible response range,
# and make Humanize explicitly control within-note expressiveness.
# -----------------------------------------------------------------------------
cpp = Path('Source/ModernPitchEngine.cpp')
text = cpp.read_text()
old = '''    if (parameters.scaleLock)
    {
        const double norm = std::pow(requested / 500.0, 1.35);
        switch (latencyMode_)
        {
            case LatencyMode::quality:   response = 3.0 + 4.0 * norm; break;
            case LatencyMode::live:      response = 1.5 + 3.5 * norm; break;
            case LatencyMode::ultraLive: response = 0.35 + 2.65 * norm; break;
        }
        const double humanTiming = 0.8 * static_cast<double>(clamp01(parameters.humanize));
        response = std::min(7.0, response + humanTiming);
    }
'''
new = '''    if (parameters.scaleLock)
    {
        // Scale Lock must not compress the 0..500 ms Response control into an
        // almost inaudible 3..7 ms window. Keep an aggressive low end while
        // giving the upper half a clearly audible, mode-aware trajectory range.
        // This changes timing only: target pitch and Amount remain authoritative.
        const double norm = std::pow(requested / 500.0, 1.35);
        switch (latencyMode_)
        {
            case LatencyMode::quality:   response = 3.0 + 92.0 * norm; break;
            case LatencyMode::live:      response = 1.5 + 63.5 * norm; break;
            case LatencyMode::ultraLive: response = 0.35 + 39.65 * norm; break;
        }
        response += 0.8 * static_cast<double>(clamp01(parameters.humanize));
    }
'''
text = replace_once(text, old, new, 'scale-lock response mapping')
old = '''    float preserve = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve + 0.35f * humanize)
        : clamp01(parameters.preserveVibrato);
    preserve *= stable * periodic * boundarySafety;
'''
new = '''    // Humanize is a musical control, not a confidence attenuator. Outside
    // Scale Lock it explicitly chooses how much same-note vibrato survives:
    // Robot keeps only a small safety residue, Human reaches the configured
    // preserve-vibrato ceiling. Under Scale Lock the dedicated Vibrato control
    // remains authoritative and Humanize only adds a bounded natural bias.
    float preserve = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve + 0.35f * humanize)
        : clamp01(parameters.preserveVibrato * (0.12f + 0.88f * humanize));
    preserve *= stable * periodic * boundarySafety;
'''
text = replace_once(text, old, new, 'humanize vibrato semantics')
cpp.write_text(text)

# -----------------------------------------------------------------------------
# GUI semantics: controls that cannot affect the selected algorithm are disabled
# instead of pretending to be active. Tempo sub-controls follow their mode.
# -----------------------------------------------------------------------------
h = Path('Source/PluginEditor.h')
text = h.read_text()
text = replace_once(text,
'''    void setTempoModeParameter(int modeIndex);
    void updateTempoModeButtons();
    void onRootNoteSelected();
''',
'''    void setTempoModeParameter(int modeIndex);
    void updateTempoModeButtons();
    void updateAudioControlAvailability();
    void onRootNoteSelected();
''', 'editor availability declaration')
h.write_text(text)

editor = Path('Source/PluginEditor.cpp')
text = editor.read_text()
old = '''                const double mappedVal = mode == 1 ? 3.0 + 4.0 * norm
                    : mode == 2 ? 1.5 + 3.5 * norm
                                : 0.35 + 2.65 * norm;
'''
new = '''                const double mappedVal = mode == 1 ? 3.0 + 92.0 * norm
                    : mode == 2 ? 1.5 + 63.5 * norm
                                : 0.35 + 39.65 * norm;
'''
text = replace_once(text, old, new, 'speed display mapping')

text = replace_once(text,
'''    setTempoControlsVisible (false);
    updateTempoModeButtons();
    controlRoomButton.setButtonText (Neumaton::UI::Labels::Main::controlRoom);
''',
'''    setTempoControlsVisible (false);
    updateTempoModeButtons();
    updateAudioControlAvailability();
    controlRoomButton.setButtonText (Neumaton::UI::Labels::Main::controlRoom);
''', 'constructor availability refresh')

text = replace_once(text,
'''void MicrotonalAutotuneAudioProcessorEditor::showTempoPage()
{
    if (showingScaleEditor)
        return;

    showingTempoPage = true;
''',
'''void MicrotonalAutotuneAudioProcessorEditor::showTempoPage()
{
    if (showingScaleEditor || processorRef.processingMode.load() == 0)
        return;

    showingTempoPage = true;
''', 'tempo page high-latency gate')

old = '''void MicrotonalAutotuneAudioProcessorEditor::updateTempoModeButtons()
{
    const int mode = juce::jlimit (0, 2, static_cast<int> (std::lround (
        processorRef.getAPVTS().getRawParameterValue ("tempoMode")->load())));
    tempoOffButton.setToggleState (mode == 0, juce::dontSendNotification);
    tempoGlideButton.setToggleState (mode == 1, juce::dontSendNotification);
    glideLockButton.setToggleState (mode == 2, juce::dontSendNotification);

    const bool lockMode = mode == 2;
    tempoLockStrength.setEnabled (lockMode);
    tempoLockStrengthLabel.setEnabled (lockMode);
    tempoSmartOnset.setEnabled (lockMode);
}
'''
new = '''void MicrotonalAutotuneAudioProcessorEditor::updateTempoModeButtons()
{
    const int mode = juce::jlimit (0, 2, static_cast<int> (std::lround (
        processorRef.getAPVTS().getRawParameterValue ("tempoMode")->load())));
    tempoOffButton.setToggleState (mode == 0, juce::dontSendNotification);
    tempoGlideButton.setToggleState (mode == 1, juce::dontSendNotification);
    glideLockButton.setToggleState (mode == 2, juce::dontSendNotification);

    const bool modernMode = processorRef.processingMode.load() > 0;
    const bool tempoShapesTrajectory = modernMode && mode != 0;
    const bool lockMode = modernMode && mode == 2;

    tempoOffButton.setEnabled (modernMode);
    tempoGlideButton.setEnabled (modernMode);
    glideLockButton.setEnabled (modernMode);

    tempoDivisionSelector.setEnabled (tempoShapesTrajectory);
    tempoDivisionLabel.setEnabled (tempoShapesTrajectory);
    tempoGlideLength.setEnabled (tempoShapesTrajectory);
    tempoGlideLengthLabel.setEnabled (tempoShapesTrajectory);

    tempoLockStrength.setEnabled (lockMode);
    tempoLockStrengthLabel.setEnabled (lockMode);
    tempoSmartOnset.setEnabled (lockMode);
}

void MicrotonalAutotuneAudioProcessorEditor::updateAudioControlAvailability()
{
    const bool modernMode = processorRef.processingMode.load() > 0;
    const bool scaleLockActive = modernMode && scaleLockButton.getToggleState();

    // High Latency intentionally remains the untouched legacy YIN engine.
    // Modern-only controls are therefore visibly disabled instead of becoming
    // placebo controls. Scale/root, Response, Amount, Analog and Output remain
    // active because the legacy path genuinely uses them.
    humanizeSlider.setEnabled (modernMode);
    humanizeLabel.setEnabled (modernMode);
    scaleLockButton.setEnabled (modernMode);
    tempoPageButton.setEnabled (modernMode);

    lockHysteresisSlider.setEnabled (scaleLockActive);
    lockHysteresisLabel.setEnabled (scaleLockActive);
    vibratoPreserveSlider.setEnabled (scaleLockActive);
    vibratoPreserveLabel.setEnabled (scaleLockActive);

    updateTempoModeButtons();
}
'''
text = replace_once(text, old, new, 'tempo control mode matrix')

text = replace_once(text,
'''    scaleLockButton.onClick = [this]()
    {
        bool lockOn = scaleLockButton.getToggleState();
        lockHysteresisSlider.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        lockHysteresisLabel.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        vibratoPreserveSlider.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        vibratoPreserveLabel.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        repaint();
    };
''',
'''    scaleLockButton.onClick = [this]()
    {
        const bool modernMode = processorRef.processingMode.load() > 0;
        const bool lockOn = modernMode && scaleLockButton.getToggleState();
        lockHysteresisSlider.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        lockHysteresisLabel.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        vibratoPreserveSlider.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        vibratoPreserveLabel.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        updateAudioControlAvailability();
        repaint();
    };
''', 'scale-lock click gate')

text = replace_once(text,
'''        // Grey out sub-params when lock is off (but keep them hidden via onClick)
        lockHysteresisSlider.setEnabled (lockIsOn);
        vibratoPreserveSlider.setEnabled (lockIsOn);
''',
'''        // Grey out sub-params when their owning modern Scale Lock is inactive.
        const bool modernMode = processorRef.processingMode.load() > 0;
        lockHysteresisSlider.setEnabled (modernMode && lockIsOn);
        vibratoPreserveSlider.setEnabled (modernMode && lockIsOn);
''', 'timer lock gate')

text = replace_once(text,
'''    if (showingTempoPage)
        updateTempoModeButtons();

    // Update Scale Lock visual state only when it changes
''',
'''    if (showingTempoPage)
        updateTempoModeButtons();

    updateAudioControlAvailability();

    // Update Scale Lock visual state only when it changes
''', 'timer availability refresh')

text = replace_once(text,
'''    updateTempoModeButtons();

    const bool mainVisible = !showingTempoPage && !showingScaleEditor;
''',
'''    updateTempoModeButtons();
    updateAudioControlAvailability();

    const bool mainVisible = !showingTempoPage && !showingScaleEditor;
''', 'preset availability refresh')

text = replace_once(text,
'''        processorRef.updateProcessingMode (newMode);
        speedKnob.updateText();
        repaint(); // refresh the mode indicator dot
''',
'''        processorRef.updateProcessingMode (newMode);
        speedKnob.updateText();
        updateAudioControlAvailability();
        repaint(); // refresh the mode indicator dot
''', 'mode availability refresh')
editor.write_text(text)

# -----------------------------------------------------------------------------
# Runtime engine tests: strengthen audible endpoint semantics for GUI controls.
# -----------------------------------------------------------------------------
test = Path('Tests/CleanModernPitchEngineTest.cpp')
text = test.read_text()
marker = '''    CreativeTempo::Controller tempoController;
'''
insertion = '''    // Scale Lock itself must be audible, not only its sub-parameters.
    auto unlockedBoundary = base;
    unlockedBoundary.scaleLock = false;
    unlockedBoundary.retuneTimeMs = 0.0f;
    const auto unlockedBoundaryResult = render(
        ModernPitchEngine::LatencyMode::quality, unlockedBoundary,
        twoNoteScale, 440.0, 5.0, boundaryStep);
    success &= check(std::abs(unlockedBoundaryResult.finalMeter.targetPitchHz
                              - highHysteresisResult.finalMeter.targetPitchHz) > 15.0f,
                     "scale_lock_switch_changes_target_hold");

    // The upper half of Response under Scale Lock must be clearly audible.
    auto lockedFast = highHysteresis;
    lockedFast.lockHysteresis = 0.0f;
    lockedFast.retuneTimeMs = 0.0f;
    auto lockedSlow = lockedFast;
    lockedSlow.retuneTimeMs = 500.0f;
    const auto lockedFastResult = render(
        ModernPitchEngine::LatencyMode::quality, lockedFast,
        unison, 440.0, 0.40, steady452);
    const auto lockedSlowResult = render(
        ModernPitchEngine::LatencyMode::quality, lockedSlow,
        unison, 440.0, 0.40, steady452);
    success &= check(std::abs(lockedFastResult.finalMeter.correctionCents)
                     > std::abs(lockedSlowResult.finalMeter.correctionCents) + 1.5f,
                     "scale_lock_response_has_audible_range");

    // Humanize outside Scale Lock must audibly preserve same-note vibrato,
    // rather than acting only as a classifier tolerance.
    auto robotVibrato = base;
    robotVibrato.scaleLock = false;
    robotVibrato.humanize = 0.0f;
    robotVibrato.preserveVibrato = 0.70f;
    auto humanVibrato = robotVibrato;
    humanVibrato.humanize = 1.0f;
    const auto robotVibratoResult = render(
        ModernPitchEngine::LatencyMode::live, robotVibrato,
        unison, 440.0, 6.0, vibratoInput);
    const auto humanVibratoResult = render(
        ModernPitchEngine::LatencyMode::live, humanVibrato,
        unison, 440.0, 6.0, vibratoInput);
    const double robotVibratoDeviation = standardDeviation(robotVibratoResult, 3.0);
    const double humanVibratoDeviation = standardDeviation(humanVibratoResult, 3.0);
    success &= check(humanVibratoDeviation + 0.35 < robotVibratoDeviation,
                     "humanize_preserves_same_note_vibrato");

    // Scale and root selectors must produce distinct musical destinations.
    const auto steady458 = [](double) { return 458.0; };
    std::vector<double> chromatic;
    for (int degree = 0; degree < 12; ++degree)
        chromatic.push_back(std::exp2(static_cast<double>(degree) / 12.0));
    const auto unisonTarget = render(
        ModernPitchEngine::LatencyMode::live, base,
        unison, 440.0, 4.0, steady458);
    const auto chromaticTarget = render(
        ModernPitchEngine::LatencyMode::live, base,
        chromatic, 440.0, 4.0, steady458);
    success &= check(std::abs(unisonTarget.finalMeter.targetPitchHz
                              - chromaticTarget.finalMeter.targetPitchHz) > 15.0f,
                     "scale_selector_changes_target_pitch");

    const auto shiftedRootTarget = render(
        ModernPitchEngine::LatencyMode::live, base,
        unison, 466.1637615, 4.0, steady458);
    success &= check(std::abs(unisonTarget.finalMeter.targetPitchHz
                              - shiftedRootTarget.finalMeter.targetPitchHz) > 20.0f,
                     "root_selector_changes_target_pitch");

    CreativeTempo::Controller tempoController;
'''
text = replace_once(text, marker, insertion, 'extended GUI runtime tests')

text = replace_once(text,
'''              << "vibrato_on_stddev=" << onDeviation << '\\n'
              << "short_glide_ms=" << shortGlide << '\\n'
''',
'''              << "vibrato_on_stddev=" << onDeviation << '\\n'
              << "robot_vibrato_stddev=" << robotVibratoDeviation << '\\n'
              << "human_vibrato_stddev=" << humanVibratoDeviation << '\\n'
              << "locked_fast_cents=" << lockedFastResult.finalMeter.correctionCents << '\\n'
              << "locked_slow_cents=" << lockedSlowResult.finalMeter.correctionCents << '\\n'
              << "short_glide_ms=" << shortGlide << '\\n'
''', 'extended diagnostic output')
test.write_text(text)

# -----------------------------------------------------------------------------
# Static GUI-to-audio wiring test. Runtime tests above cover trajectory/targets;
# this test makes dead APVTS controls or placebo applicability difficult to add.
# -----------------------------------------------------------------------------
contract_test = r'''#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::string read(const char* path)
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

bool contains(const std::string& text, const std::string& token)
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
    bool ok = true;
    const auto processor = read("Source/PluginProcessor.cpp");
    const auto editor = read("Source/PluginEditor.cpp");
    const auto engine = read("Source/ModernPitchEngine.cpp");
    const auto tempo = read("Source/Tempo.cpp");

    const std::vector<std::string> apvtsIds {
        "speed", "amount", "humanize", "tempoMode", "tempoDivision",
        "tempoGlidePercent", "tempoLockStrength", "tempoSmartOnset",
        "scaleLock", "lockHysteresis", "vibratoPreserve", "analogMode",
        "outVolume"
    };
    for (const auto& id : apvtsIds)
    {
        ok &= check(contains(processor, "ParameterID { \\\"" + id + "\\\"")
                    && contains(editor, "\\\"" + id + "\\\""),
                    "gui_parameter_declared_and_bound_" + id);
    }

    ok &= check(contains(processor, "livePitchProcessor.process (buffer")
                && contains(processor, "speedMs")
                && contains(processor, "amount"),
                "response_and_amount_reach_modern_audio");
    ok &= check(contains(processor, "setScaleLockParameters(scaleLock, lockHysteresis, vibratoPreserve)"),
                "scale_lock_controls_reach_engine");
    ok &= check(contains(processor, "setTempoSettings (getTempoSettings())")
                && contains(tempo, "settings.glideFraction")
                && contains(tempo, "settings.lockStrength")
                && contains(tempo, "settings.smartOnset"),
                "tempo_controls_reach_scheduler");
    ok &= check(contains(processor, "fastSoftClip(value)")
                && contains(processor, "analogLowShelfFilters_")
                && contains(processor, "analogHighShelfFilters_")
                && contains(processor, "value *= outGain"),
                "analog_and_output_controls_change_samples");
    ok &= check(contains(editor, "updateAudioControlAvailability()")
                && contains(editor, "humanizeSlider.setEnabled (modernMode)")
                && contains(editor, "scaleLockButton.setEnabled (modernMode)")
                && contains(editor, "tempoPageButton.setEnabled (modernMode)"),
                "high_latency_disables_modern_only_controls");
    ok &= check(contains(editor, "const bool tempoShapesTrajectory = modernMode && mode != 0")
                && contains(editor, "const bool lockMode = modernMode && mode == 2")
                && contains(editor, "tempoDivisionSelector.setEnabled (tempoShapesTrajectory)")
                && contains(editor, "tempoLockStrength.setEnabled (lockMode)"),
                "tempo_gui_matches_active_semantics");
    ok &= check(contains(editor, "processorRef.refreshScaleSnapshot()")
                && contains(editor, "processorRef.updateProcessingMode (newMode)"),
                "scale_root_and_mode_selectors_reach_audio_state");
    ok &= check(contains(engine, "parameters.preserveVibrato * (0.12f + 0.88f * humanize)")
                && contains(engine, "3.0 + 92.0 * norm")
                && contains(engine, "1.5 + 63.5 * norm")
                && contains(engine, "0.35 + 39.65 * norm"),
                "humanize_and_response_have_explicit_audible_mapping");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
'''
Path('Tests/GuiAudioControlContractTest.cpp').write_text(contract_test)

contract = '''# GUI to Audio Control Contract — Single-Wet branch

The release rule is **no placebo controls**. A user-facing control must either
change a defined audible dimension or be disabled while its owning feature is
not applicable. Navigation and metering controls are explicitly exempt.

| GUI control | Active modes | Audible contract |
|---|---|---|
| Scale | all | changes quantizer degree set / target pitch |
| Root | all | transposes quantizer reference / target pitch |
| Mode | all | selects High Latency YIN or one single-wet frame/latency profile |
| Response | all | changes correction trajectory time; under Scale Lock the full knob has a clearly audible mode-aware range |
| Amount | all | scales correction cents, never dry/wet mix |
| Humanize | modern only | changes same-note tolerance and vibrato preservation; disabled in High Latency |
| Scale Lock | modern only | changes target identity hold/commit behaviour; disabled in High Latency |
| Hold | modern + Scale Lock | changes scale-degree hysteresis; hidden/disabled otherwise |
| Vibrato Preserve | modern + Scale Lock | changes retained same-note vibrato; hidden/disabled otherwise |
| Tempo Mode | modern only | Off bypasses tempo scheduling; Glide/Lock alter the same correction trajectory |
| Tempo Division | modern + Tempo active | changes beat grid / glide time; disabled in Tempo Off |
| Glide Length | modern + Tempo active | changes beat-derived transition duration; disabled in Tempo Off |
| Glide Lock Strength | modern + Glide Lock | changes release position toward the next grid; disabled otherwise |
| Smart Onset | modern + Glide Lock | can release a pending target near the grid on a musical onset; disabled otherwise |
| Analog Texture | all | engages output soft saturation plus fixed low/high shelves |
| Output | all | changes final output gain before the safety ceiling |
| Preset | all | macro: changes the above parameters and processing mode through host-notifying writes |
| Custom Scale editor/selection | all | changes the published scale snapshot used by the target quantizer |

Navigation-only controls (Tempo page/back, Control Room/back) and meters are not
sound controls and are not required to alter audio.

The Modern Quality/Live/Experimental renderer remains one wet spectral path:
there is no delayed dry, dry/wet blend, confidence-authority attenuation, or
secondary synthesis layer. Sensor evidence may change identity/search caution
but not Amount/correction depth.
'''
Path('Tests/GUI_AUDIO_CONTROL_CONTRACT.md').write_text(contract)

# CMake target for the static GUI wiring contract.
cm = Path('CMakeLists.txt')
text = cm.read_text()
marker = '''    juce_add_console_app(SingleWetSpectralRendererTest
'''
block = '''    add_executable(GuiAudioControlContractTest
        Tests/GuiAudioControlContractTest.cpp
    )
    set_target_properties(GuiAudioControlContractTest PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/clean-engine-tests"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/clean-engine-tests"
    )

    juce_add_console_app(SingleWetSpectralRendererTest
'''
text = replace_once(text, marker, block, 'GUI contract CMake target')
cm.write_text(text)

print('GUI audio-control integration applied')
