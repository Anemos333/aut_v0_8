#include "PluginEditor.h"
#include "ScaleDefinitions.h"
#include "NeumatonUILabels.h"
#include "Preset.h"
#include "BinaryData.h"

#include <cmath>

//==============================================================================
ModernLookAndFeel::ModernLookAndFeel()
{
    setColour (juce::Slider::thumbColourId, juce::Colour (0xFFFFFFFF));
    setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFF6C63FF));
    setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colour (0xFF2A2D40));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xFF1E2135));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xFF38405F));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xFF1E2135));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xFF6C63FF));
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xFF2A3048));
    setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xFF6C63FF));
    setColour (juce::TextButton::textColourOffId, juce::Colours::white);
}

void ModernLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                          int x, int y, int width, int height,
                                          float sliderPos,
                                          const float rotaryStartAngle,
                                          const float rotaryEndAngle,
                                          juce::Slider& slider)
{
    const auto outline = slider.findColour (juce::Slider::rotarySliderOutlineColourId);
    const auto fill = slider.findColour (juce::Slider::rotarySliderFillColourId);

    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto lineW = juce::jmin (8.0f, radius * 0.5f);
    const auto arcRadius = radius - lineW * 0.5f;

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(),
                                 arcRadius, arcRadius, 0.0f,
                                 rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (outline);
    g.strokePath (backgroundArc,
                  juce::PathStrokeType (lineW,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(),
                                arcRadius, arcRadius, 0.0f,
                                rotaryStartAngle, toAngle, true);
        g.setColour (fill);
        g.strokePath (valueArc,
                      juce::PathStrokeType (lineW,
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
        g.setColour (fill.withAlpha (0.3f));
        g.strokePath (valueArc,
                      juce::PathStrokeType (lineW * 2.0f,
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));
    }

    const auto thumbWidth = lineW * 2.0f;
    juce::Point<float> thumbPoint (
        bounds.getCentreX() + arcRadius * std::cos (toAngle - juce::MathConstants<float>::halfPi),
        bounds.getCentreY() + arcRadius * std::sin (toAngle - juce::MathConstants<float>::halfPi));

    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillEllipse (juce::Rectangle<float> (thumbWidth, thumbWidth).withCentre (thumbPoint));
}

void ModernLookAndFeel::drawButtonBackground (juce::Graphics& g,
                                              juce::Button& button,
                                              const juce::Colour& backgroundColour,
                                              bool shouldDrawButtonAsHighlighted,
                                              bool shouldDrawButtonAsDown)
{
    const auto cornerSize = 6.0f;
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    auto baseColour = backgroundColour
        .withMultipliedSaturation (button.hasKeyboardFocus (true) ? 1.3f : 0.9f)
        .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);

    if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
        baseColour = baseColour.contrasting (shouldDrawButtonAsDown ? 0.2f : 0.05f);

    g.setColour (baseColour);
    g.fillRoundedRectangle (bounds, cornerSize);
    g.setColour (button.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, cornerSize, 1.0f);
}

void ModernLookAndFeel::drawComboBox (juce::Graphics& g,
                                      int width, int height,
                                      bool /*isButtonDown*/,
                                      int /*buttonX*/, int /*buttonY*/,
                                      int /*buttonW*/, int /*buttonH*/,
                                      juce::ComboBox& box)
{
    constexpr float cornerSize = 6.0f;
    juce::Rectangle<int> boxBounds (0, 0, width, height);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (boxBounds.toFloat(), cornerSize);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (boxBounds.toFloat().reduced (0.5f), cornerSize, 1.0f);

    juce::Rectangle<int> arrowZone (width - 30, 0, 20, height);
    juce::Path path;
    path.startNewSubPath (arrowZone.getX() + 3.0f, arrowZone.getCentreY() - 2.0f);
    path.lineTo (static_cast<float> (arrowZone.getCentreX()), arrowZone.getCentreY() + 3.0f);
    path.lineTo (arrowZone.getRight() - 3.0f, arrowZone.getCentreY() - 2.0f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId)
                     .withAlpha (box.isEnabled() ? 0.9f : 0.2f));
    g.strokePath (path, juce::PathStrokeType (2.0f));
}

void ModernLookAndFeel::drawTickBox (juce::Graphics& g,
                                     juce::Component& component,
                                     float x, float y, float w, float h,
                                     const bool ticked,
                                     const bool /*isEnabled*/,
                                     const bool /*shouldDrawButtonAsHighlighted*/,
                                     const bool /*shouldDrawButtonAsDown*/)
{
    juce::Rectangle<float> tickBounds (x, y, w, h);
    g.setColour (juce::Colour (0xFF1E2135));
    g.fillRoundedRectangle (tickBounds, 4.0f);
    g.setColour (juce::Colour (0xFF38405F));
    g.drawRoundedRectangle (tickBounds.reduced (0.5f), 4.0f, 1.0f);

    if (ticked)
    {
        g.setColour (component.findColour (juce::ToggleButton::textColourId));
        auto tickShape = tickBounds.reduced (4.0f);
        juce::Path p;
        p.startNewSubPath (tickShape.getX(), tickShape.getCentreY());
        p.lineTo (tickShape.getCentreX() - 2.0f, tickShape.getBottom() - 2.0f);
        p.lineTo (tickShape.getRight(), tickShape.getY() + 2.0f);
        g.strokePath (p, juce::PathStrokeType (2.5f));
    }
}

//==============================================================================
MicrotonalAutotuneAudioProcessorEditor::MicrotonalAutotuneAudioProcessorEditor (
    MicrotonalAutotuneAudioProcessor& p)
    : AudioProcessorEditor (p), processorRef (p)
{
    setLookAndFeel (&modernLookAndFeel);

    bgImage = juce::ImageCache::getFromMemory (
        BinaryData::sfondo1_jpeg, BinaryData::sfondo1_jpegSize);
    bgImageScaleEditor = juce::ImageCache::getFromMemory (
        BinaryData::sfondo2_jpg, BinaryData::sfondo2_jpgSize);

    const auto configureHeaderLabel = [this] (juce::Label& label,
                                               const juce::String& text,
                                               juce::Justification justification = juce::Justification::centredRight)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, juce::Colours::white);
        label.setJustificationType (justification);
        addAndMakeVisible (label);
    };

    configureHeaderLabel (presetSelectorLabel, Neumaton::UI::Labels::Main::preset);
    presetSelector.setJustificationType (juce::Justification::centredLeft);
    presetSelector.onChange = [this] { onPresetSelected(); };
    addAndMakeVisible (presetSelector);
    buildPresetMenu();

    configureHeaderLabel (scaleSelectorLabel, Neumaton::UI::Labels::Main::scale);
    scaleSelector.setJustificationType (juce::Justification::centredLeft);
    scaleSelector.onChange = [this] { onScaleSelected(); };
    addAndMakeVisible (scaleSelector);
    buildScaleMenu();

    configureHeaderLabel (centerSelectorLabel, "Center");
    centerSelector.setJustificationType (juce::Justification::centredLeft);
    const juce::StringArray centreNames {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    for (int i = 0; i < centreNames.size(); ++i)
        centerSelector.addItem (centreNames[i], i + 1);
    centerSelector.setSelectedId (
        juce::jlimit (0, 11, processorRef.tonalCenterIndex.load()) + 1,
        juce::dontSendNotification);
    centerSelector.onChange = [this] { onCenterSelected(); };
    centerSelector.setTooltip (
        "Pitch-class centre/anchor. Its musical role changes with the selected scale.");
    addAndMakeVisible (centerSelector);

    configureHeaderLabel (tuningLabel, "Tuning");
    tuningSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    tuningSlider.setRange (300.0, 600.0, 0.01);
    tuningSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    tuningSlider.setTextValueSuffix (" Hz");
    tuningSlider.setDoubleClickReturnValue (true, 440.0);
    tuningSlider.setTooltip (
        "Absolute A4 reference. It is independent from the selected Center.");
    addAndMakeVisible (tuningSlider);
    tuningAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.getAPVTS(), "tuningReferenceHz", tuningSlider);

    configureHeaderLabel (modeSelectorLabel, Neumaton::UI::Labels::Main::mode);
    modeSelector.setJustificationType (juce::Justification::centredLeft);
    modeSelector.addItem ("Quality", 1);
    modeSelector.addItem ("Live", 2);
    modeSelector.addItem ("Experimental", 3);
    modeSelector.setSelectedId (processorRef.processingMode.load(), juce::dontSendNotification);
    modeSelector.onChange = [this] { onModeSelected(); };
    addAndMakeVisible (modeSelector);

    speedKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    speedKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    speedKnob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFF79572A));
    speedKnob.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    speedKnob.setLookAndFeel (&mainValveLookAndFeel);
    speedKnob.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    speedKnob.textFromValueFunction = [] (double val)
    {
        return juce::String (val, 1) + " ms";
    };
    addAndMakeVisible (speedKnob);
    configureHeaderLabel (speedLabel, Neumaton::UI::Labels::Main::response,
                          juce::Justification::centred);
    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "speed", speedKnob);

    amountKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    amountKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    amountKnob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFF79572A));
    amountKnob.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    amountKnob.setLookAndFeel (&mainValveLookAndFeel);
    amountKnob.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    amountKnob.setTextValueSuffix (" %");
    addAndMakeVisible (amountKnob);
    configureHeaderLabel (amountLabel, Neumaton::UI::Labels::Main::correctionAmount,
                          juce::Justification::centred);
    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "amount", amountKnob);

    humanizeSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    humanizeSlider.setRotaryParameters (
        -juce::MathConstants<float>::pi / 4.0f,
         juce::MathConstants<float>::pi / 4.0f,
         true);
    humanizeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    humanizeSlider.setTextValueSuffix (" %");
    humanizeSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFF00C878));
    humanizeSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    humanizeSlider.setLookAndFeel (&humanDriftLookAndFeel);
    humanizeSlider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    humanizeSlider.textFromValueFunction = [] (double val)
    {
        juce::String text (val, 1);
        if (val < 5.0) return text + " % (Robot)";
        if (val > 95.0) return text + " % (Human)";
        return text + " %";
    };
    addAndMakeVisible (humanizeSlider);
    configureHeaderLabel (humanizeLabel, Neumaton::UI::Labels::Main::humanize,
                          juce::Justification::centred);
    humanizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "humanize", humanizeSlider);

    lockHysteresisSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    lockHysteresisSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    lockHysteresisSlider.setTextValueSuffix (" c");
    lockHysteresisSlider.setLookAndFeel (&utilityRailLookAndFeel);
    lockHysteresisSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xFFFF4FB3));
    lockHysteresisSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible (lockHysteresisSlider);
    configureHeaderLabel (lockHysteresisLabel, Neumaton::UI::Labels::Main::hold,
                          juce::Justification::centredLeft);
    lockHysteresisAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "lockHysteresis", lockHysteresisSlider);

    vibratoPreserveSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    vibratoPreserveSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    vibratoPreserveSlider.setTextValueSuffix (" %");
    vibratoPreserveSlider.setLookAndFeel (&utilityRailLookAndFeel);
    vibratoPreserveSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xFF39FF7A));
    vibratoPreserveSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible (vibratoPreserveSlider);
    configureHeaderLabel (vibratoPreserveLabel, Neumaton::UI::Labels::Main::vibratoPreserve,
                          juce::Justification::centredLeft);
    vibratoPreserveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "vibratoPreserve", vibratoPreserveSlider);

    analogModeButton.setButtonText (Neumaton::UI::Labels::Main::analogTexture);
    analogModeButton.setLookAndFeel (&analogLeverLookAndFeel);
    analogModeButton.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xFFFFA02B));
    analogModeButton.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (analogModeButton);
    analogModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        processorRef.getAPVTS(), "analogMode", analogModeButton);

    outVolumeSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    outVolumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    outVolumeSlider.setTextValueSuffix (" dB");
    outVolumeSlider.setLookAndFeel (&outputKnobLookAndFeel);
    outVolumeSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFFFFA02B));
    outVolumeSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible (outVolumeSlider);
    configureHeaderLabel (outVolumeLabel, Neumaton::UI::Labels::Main::output,
                          juce::Justification::centredLeft);
    outVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "outVolume", outVolumeSlider);

    controlRoomButton.setButtonText (Neumaton::UI::Labels::Main::controlRoom);
    controlRoomButton.onClick = [this] { showControlRoom(); };
    addAndMakeVisible (controlRoomButton);
    controlRoomPage.onBack = [this] { closeControlRoom(); };
    addChildComponent (controlRoomPage);

    updateCenterPresentation();

    setSize (640, 510);
    setResizable (true, true);
    setResizeLimits (520, 460, 1200, 820);

    displayedMetering = processorRef.getPitchMetering();
    startTimerHz (30);
}

MicrotonalAutotuneAudioProcessorEditor::~MicrotonalAutotuneAudioProcessorEditor()
{
    speedKnob.setLookAndFeel (nullptr);
    amountKnob.setLookAndFeel (nullptr);
    humanizeSlider.setLookAndFeel (nullptr);
    lockHysteresisSlider.setLookAndFeel (nullptr);
    vibratoPreserveSlider.setLookAndFeel (nullptr);
    outVolumeSlider.setLookAndFeel (nullptr);
    analogModeButton.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
    stopTimer();
}

//==============================================================================
void MicrotonalAutotuneAudioProcessorEditor::setMainControlsVisible (bool visible)
{
    presetSelector.setVisible (visible);
    presetSelectorLabel.setVisible (visible);
    scaleSelector.setVisible (visible);
    scaleSelectorLabel.setVisible (visible);
    centerSelector.setVisible (visible);
    centerSelectorLabel.setVisible (visible);
    tuningSlider.setVisible (visible);
    tuningLabel.setVisible (visible);
    modeSelector.setVisible (visible);
    modeSelectorLabel.setVisible (visible);

    speedKnob.setVisible (visible);
    speedLabel.setVisible (visible);
    amountKnob.setVisible (visible);
    amountLabel.setVisible (visible);
    humanizeSlider.setVisible (visible);
    humanizeLabel.setVisible (visible);

    analogModeButton.setVisible (visible);
    outVolumeSlider.setVisible (visible);
    outVolumeLabel.setVisible (visible);
    controlRoomButton.setVisible (visible);

    lockHysteresisSlider.setVisible (visible);
    lockHysteresisLabel.setVisible (visible);
    vibratoPreserveSlider.setVisible (visible);
    vibratoPreserveLabel.setVisible (visible);
}

void MicrotonalAutotuneAudioProcessorEditor::showControlRoom()
{
    if (showingScaleEditor)
        return;

    showingControlRoom = true;
    setMainControlsVisible (false);
    controlRoomPage.setVisible (true);
    controlRoomPage.setMetering (displayedMetering);
    resized();
    repaint();
}

void MicrotonalAutotuneAudioProcessorEditor::closeControlRoom()
{
    showingControlRoom = false;
    controlRoomPage.setVisible (false);
    setMainControlsVisible (true);
    resized();
    repaint();
}

//==============================================================================
void MicrotonalAutotuneAudioProcessorEditor::buildPresetMenu()
{
    presetSelector.clear (juce::dontSendNotification);
    juce::String lastGroup;

    for (int i = 0; i < FactoryPresets::getNumPresets(); ++i)
    {
        const auto& preset = FactoryPresets::getPreset (i);
        const juce::String group (preset.group);
        if (group != lastGroup)
        {
            presetSelector.addSectionHeading (group);
            lastGroup = group;
        }
        presetSelector.addItem (preset.name, i + 1);
    }

    presetSelector.setSelectedId (
        processorRef.getCurrentProgram() + 1,
        juce::dontSendNotification);
}

void MicrotonalAutotuneAudioProcessorEditor::onPresetSelected()
{
    const int index = presetSelector.getSelectedId() - 1;
    if (index < 0 || index >= FactoryPresets::getNumPresets())
        return;

    processorRef.applyFactoryPreset (index);
    modeSelector.setSelectedId (processorRef.processingMode.load(), juce::dontSendNotification);
    setMainControlsVisible (! showingScaleEditor && ! showingControlRoom);
    resized();
    repaint();
}

void MicrotonalAutotuneAudioProcessorEditor::buildScaleMenu()
{
    scaleSelector.clear (juce::dontSendNotification);

    const auto& scales = ScaleDefinitions::getAllScales();
    juce::String lastCategory;

    for (int i = 0; i < static_cast<int> (scales.size()); ++i)
    {
        const auto& scale = scales[static_cast<std::size_t> (i)];
        if (! scale.visibleInMenu)
            continue;

        const juce::String category (scale.category);
        if (category != lastCategory)
        {
            scaleSelector.addSectionHeading (category);
            lastCategory = category;
        }
        scaleSelector.addItem (juce::String (scale.name), i + 1);
    }

    const int activeFactory = processorRef.currentScaleIndex.load();
    if (activeFactory >= 0 && activeFactory < static_cast<int> (scales.size())
        && ! scales[static_cast<std::size_t> (activeFactory)].visibleInMenu
        && processorRef.activeCustomPresetIndex.load() < 0)
    {
        scaleSelector.addSeparator();
        scaleSelector.addSectionHeading ("Legacy session");
        scaleSelector.addItem (
            juce::String (scales[static_cast<std::size_t> (activeFactory)].name),
            activeFactory + 1);
    }

    scaleSelector.addSeparator();
    scaleSelector.addSectionHeading ("Your scales");

    const int numCustom = processorRef.getCustomPresets().getNumPresets();
    for (int i = 0; i < numCustom; ++i)
    {
        const auto& preset = processorRef.getCustomPresets().getPreset (i);
        scaleSelector.addItem (preset.name, 10001 + i);
    }

    if (numCustom < CustomScalePresets::maxPresets)
        scaleSelector.addItem ("Create scale...", 10000);

    if (numCustom > 0)
    {
        scaleSelector.addSeparator();
        for (int i = 0; i < numCustom; ++i)
        {
            const auto& preset = processorRef.getCustomPresets().getPreset (i);
            scaleSelector.addItem ("Delete: " + preset.name, 20001 + i);
        }
    }

    const int customIdx = processorRef.activeCustomPresetIndex.load();
    if (customIdx >= 0 && customIdx < numCustom)
        scaleSelector.setSelectedId (10001 + customIdx, juce::dontSendNotification);
    else
        scaleSelector.setSelectedId (activeFactory + 1, juce::dontSendNotification);
}

void MicrotonalAutotuneAudioProcessorEditor::onScaleSelected()
{
    const int selectedId = scaleSelector.getSelectedId();
    if (selectedId == 0)
        return;

    if (selectedId == 10000)
    {
        showCustomScaleEditor();
        return;
    }

    if (selectedId >= 20001)
    {
        const int deleteIdx = selectedId - 20001;
        processorRef.getCustomPresets().removePreset (deleteIdx);

        const int active = processorRef.activeCustomPresetIndex.load();
        if (active == deleteIdx)
        {
            processorRef.activeCustomPresetIndex.store (-1);
            processorRef.currentScaleIndex.store (0);
        }
        else if (active > deleteIdx)
        {
            processorRef.activeCustomPresetIndex.store (active - 1);
        }

        processorRef.refreshScaleSnapshot();
        buildScaleMenu();
        updateCenterPresentation();
        return;
    }

    if (selectedId >= 10001 && selectedId < 20000)
    {
        const int customIdx = selectedId - 10001;
        if (customIdx >= 0 && customIdx < processorRef.getCustomPresets().getNumPresets())
        {
            processorRef.activeCustomPresetIndex.store (customIdx);
            processorRef.refreshScaleSnapshot();
            updateCenterPresentation();
        }
        return;
    }

    const int scaleIdx = selectedId - 1;
    if (scaleIdx >= 0 && scaleIdx < ScaleDefinitions::getScaleCount())
    {
        processorRef.currentScaleIndex.store (scaleIdx);
        processorRef.activeCustomPresetIndex.store (-1);
        processorRef.refreshScaleSnapshot();
        updateCenterPresentation();
    }
}

void MicrotonalAutotuneAudioProcessorEditor::updateCenterPresentation()
{
    juce::String label = "Center";
    juce::String tooltip = "Musical centre used to transpose the selected pitch set.";

    if (processorRef.activeCustomPresetIndex.load() < 0)
    {
        const auto& scale = ScaleDefinitions::getScale (
            processorRef.currentScaleIndex.load());

        switch (scale.centerRole)
        {
            case TonalCenterRole::gridAnchor:
                label = "Anchor";
                tooltip = "Phase/reference anchor of the tuning grid; it need not imply a tonal tonic.";
                break;
            case TonalCenterRole::referenceDegree:
            case TonalCenterRole::ensembleReference:
            case TonalCenterRole::reconstructionReference:
                label = "Reference";
                tooltip = "Reference degree used to place this relative or reconstructed tuning.";
                break;
            case TonalCenterRole::movableTonicSa:
                label = "Sa / Center";
                tooltip = "Movable Sa anchored to the selected modern pitch class.";
                break;
            case TonalCenterRole::modalCenter:
            case TonalCenterRole::modalTonic:
                label = "Center";
                tooltip = "Movable modal centre/tonic; absolute pitch is set independently by Tuning.";
                break;
            case TonalCenterRole::tonicOrKeyCenter:
            case TonalCenterRole::keyCenterAndTuningAnchor:
                break;
        }
    }

    centerSelectorLabel.setText (label, juce::dontSendNotification);
    centerSelector.setTooltip (tooltip);
    tuningLabel.setText ("Tuning", juce::dontSendNotification);
}

void MicrotonalAutotuneAudioProcessorEditor::onCenterSelected()
{
    const int selectedId = centerSelector.getSelectedId();
    if (selectedId <= 0)
        return;

    const int center = juce::jlimit (0, 11, selectedId - 1);
    processorRef.tonalCenterIndex.store (center, std::memory_order_release);
    processorRef.rootNoteIndex.store (center, std::memory_order_release); // legacy mirror only
}

void MicrotonalAutotuneAudioProcessorEditor::onModeSelected()
{
    const int selectedId = modeSelector.getSelectedId();
    if (selectedId > 0)
    {
        processorRef.updateProcessingMode (selectedId);
        speedKnob.updateText();
        repaint();
    }
}

void MicrotonalAutotuneAudioProcessorEditor::showCustomScaleEditor()
{
    showingScaleEditor = true;
    setMainControlsVisible (false);

    customScaleEditorPage = std::make_unique<CustomScaleEditor> (
        processorRef, *this, bgImageScaleEditor);
    addAndMakeVisible (*customScaleEditorPage);
    customScaleEditorPage->setBounds (getLocalBounds());
}

void MicrotonalAutotuneAudioProcessorEditor::customScaleEditorClosed()
{
    showingScaleEditor = false;
    customScaleEditorPage.reset();
    setMainControlsVisible (true);
    buildScaleMenu();
    updateCenterPresentation();
    resized();
    repaint();
}

//==============================================================================
juce::String MicrotonalAutotuneAudioProcessorEditor::trackingStateToString (
    LivePitchProcessor::TrackingState state)
{
    switch (state)
    {
        case LivePitchProcessor::TrackingState::unvoiced:   return "Unvoiced";
        case LivePitchProcessor::TrackingState::attack:     return "Attack";
        case LivePitchProcessor::TrackingState::acquire:    return "Acquire";
        case LivePitchProcessor::TrackingState::stable:     return "Stable";
        case LivePitchProcessor::TrackingState::transition: return "Transition";
        case LivePitchProcessor::TrackingState::release:    return "Release";
    }
    return "Unknown";
}

void MicrotonalAutotuneAudioProcessorEditor::timerCallback()
{
    displayedMetering = processorRef.getPitchMetering();
    if (showingControlRoom)
        controlRoomPage.setMetering (displayedMetering);

    const auto smoothTowards = [] (float current, float target, float amount)
    {
        if (! std::isfinite (target))
            target = 0.0f;
        return current + (target - current) * amount;
    };

    visualCorrectionGlowCents_ = smoothTowards (
        visualCorrectionGlowCents_,
        static_cast<float> (displayedMetering.correctionCents),
        0.065f);
    const bool analogIsOn = analogModeButton.getToggleState();
    if (analogIsOn != lastAnalogModeState_)
    {
        lastAnalogModeState_ = analogIsOn;
        analogModeButton.setColour (juce::ToggleButton::textColourId,
                                    analogIsOn ? juce::Colour (0xFFFF8800)
                                               : juce::Colours::white);
    }

    repaint();
}

//==============================================================================
void MicrotonalAutotuneAudioProcessorEditor::paint (juce::Graphics& g)
{
    if (showingScaleEditor || showingControlRoom)
        return;

    const auto bounds = getLocalBounds().toFloat();
    if (bgImage.isValid())
        g.drawImage (bgImage, bounds, juce::RectanglePlacement::fillDestination);
    else
    {
        juce::ColourGradient gradient (
            juce::Colour (0xFF141726), bounds.getTopLeft(),
            juce::Colour (0xFF0B0D1A), bounds.getBottomRight(), false);
        g.setGradientFill (gradient);
        g.fillRect (bounds);
    }

    g.setColour (juce::Colour (0x66070A12));
    g.fillRect (bounds);

    juce::ColourGradient radialGlow (
        juce::Colour (0x226C63FF),
        bounds.getCentreX(), bounds.getCentreY() - 30.0f,
        juce::Colours::transparentBlack,
        bounds.getCentreX() + 300.0f, bounds.getCentreY() + 300.0f,
        true);
    g.setGradientFill (radialGlow);
    g.fillRect (bounds);

    const auto drawPanel = [&g] (juce::Rectangle<int> r, float corner = 12.0f)
    {
        if (r.isEmpty())
            return;
        const auto f = r.toFloat();
        g.setColour (juce::Colour (0x68101422));
        g.fillRoundedRectangle (f, corner);
        g.setColour (juce::Colour (0x447F8CFF));
        g.drawRoundedRectangle (f.reduced (0.5f), corner, 1.0f);
    };

    auto headerPanel = presetSelectorLabel.getBounds()
        .getUnion (presetSelector.getBounds())
        .getUnion (scaleSelectorLabel.getBounds())
        .getUnion (scaleSelector.getBounds())
        .getUnion (centerSelectorLabel.getBounds())
        .getUnion (centerSelector.getBounds())
        .getUnion (tuningLabel.getBounds())
        .getUnion (tuningSlider.getBounds())
        .getUnion (modeSelectorLabel.getBounds())
        .getUnion (modeSelector.getBounds())
        .expanded (14, 10);
    drawPanel (headerPanel);

    const auto expressionPanel = lockHysteresisSlider.getBounds()
        .getUnion (lockHysteresisLabel.getBounds())
        .getUnion (vibratoPreserveSlider.getBounds())
        .getUnion (vibratoPreserveLabel.getBounds());
    drawPanel (expressionPanel.expanded (14, 10));

    const auto outputStagePanel = analogModeButton.getBounds()
        .getUnion (outVolumeSlider.getBounds())
        .getUnion (outVolumeLabel.getBounds());
    drawPanel (outputStagePanel.expanded (14, 10));

    auto lowerArea = getLocalBounds();
    auto titleArea = lowerArea.removeFromBottom (38);
    auto titleTextArea = titleArea.reduced (24, 0);
    auto instrumentArea = lowerArea.removeFromBottom (112).reduced (18, 4);
    auto instrumentContent = instrumentArea;

    const int sideMeterW = juce::jlimit (128, 164, instrumentContent.getWidth() / 4);
    auto correctionArea = instrumentContent.removeFromLeft (sideMeterW).reduced (4);
    auto degreeArea = instrumentContent.removeFromRight (sideMeterW).reduced (4);
    auto targetArea = instrumentContent.reduced (8, 0);
    targetArea.removeFromTop (32);
    targetArea = targetArea.reduced (2);

    neumaton::lab::Painter::drawCorrectionGauge (
        g, correctionArea,
        static_cast<float> (displayedMetering.correctionCents),
        visualCorrectionGlowCents_);
    neumaton::lab::Painter::drawRadioTarget (
        g, targetArea,
        displayedMetering.detectedPitchHz,
        displayedMetering.targetPitchHz);
    neumaton::lab::Painter::drawScaleDegreeGauge (
        g, degreeArea,
        displayedMetering.targetDegreeIndex,
        displayedMetering.targetDegreeCount);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("Neumaton", titleTextArea, juce::Justification::centred);

    const int mode = processorRef.processingMode.load();
    juce::Colour dotColour = juce::Colour (0xFF888888);
    if (mode == 1) dotColour = juce::Colour (0xFF4488FF);
    if (mode == 2) dotColour = juce::Colour (0xFF00CC66);
    if (mode == 3) dotColour = juce::Colour (0xFFFF8800);

    const auto modeBounds = modeSelector.getBounds();
    const int dotX = modeBounds.getRight() + 6;
    const int dotY = modeBounds.getCentreY();
    g.setColour (dotColour);
    g.fillEllipse (static_cast<float> (dotX - 4), static_cast<float> (dotY - 4), 8.0f, 8.0f);
    g.setColour (dotColour.withAlpha (0.3f));
    g.fillEllipse (static_cast<float> (dotX - 7), static_cast<float> (dotY - 7), 14.0f, 14.0f);
}

void MicrotonalAutotuneAudioProcessorEditor::resized()
{
    if (showingScaleEditor && customScaleEditorPage != nullptr)
    {
        customScaleEditorPage->setBounds (getLocalBounds());
        return;
    }

    if (showingControlRoom)
    {
        controlRoomPage.setBounds (getLocalBounds());
        return;
    }

    auto area = getLocalBounds().reduced (24, 18);
    auto headerArea = area.removeFromTop (82);
    area.removeFromTop (10);

    area.removeFromBottom (38); // title
    auto instrumentArea = area.removeFromBottom (112);
    area.removeFromBottom (4);
    auto utilityArea = area.removeFromBottom (72);
    area.removeFromBottom (27);
    auto mainControlsArea = area;

    // Row 1: Preset + Scale. Row 2: Center + Tuning + Mode.
    auto firstRow = headerArea.removeFromTop (30);
    headerArea.removeFromTop (8);
    auto secondRow = headerArea.removeFromTop (30);

    constexpr int gap = 10;
    auto presetArea = firstRow.removeFromLeft ((firstRow.getWidth() - gap) / 2);
    firstRow.removeFromLeft (gap);
    auto scaleArea = firstRow;

    presetSelectorLabel.setBounds (presetArea.removeFromLeft (62));
    presetSelector.setBounds (presetArea.reduced (0, 1));
    scaleSelectorLabel.setBounds (scaleArea.removeFromLeft (54));
    scaleSelector.setBounds (scaleArea.reduced (0, 1));

    const int available = secondRow.getWidth() - 2 * gap;
    const int centerW = juce::roundToInt (available * 0.29f);
    const int tuningW = juce::roundToInt (available * 0.39f);

    auto centerArea = secondRow.removeFromLeft (centerW);
    secondRow.removeFromLeft (gap);
    auto tuningArea = secondRow.removeFromLeft (tuningW);
    secondRow.removeFromLeft (gap);
    auto modeArea = secondRow;

    centerSelectorLabel.setBounds (centerArea.removeFromLeft (74));
    centerSelector.setBounds (centerArea.reduced (0, 1));
    tuningLabel.setBounds (tuningArea.removeFromLeft (56));
    tuningSlider.setBounds (tuningArea.reduced (0, 2));
    modeSelectorLabel.setBounds (modeArea.removeFromLeft (46));
    modeSelector.setBounds (modeArea.reduced (0, 1));

    auto controls = mainControlsArea.reduced (4, 0);
    const int third = controls.getWidth() / 3;
    auto responseArea = controls.removeFromLeft (third);
    auto humanArea = controls.removeFromLeft (third);
    auto amountArea = controls;

    const auto placeLargeValve = [] (juce::Slider& knob,
                                     juce::Label& label,
                                     juce::Rectangle<int> bounds)
    {
        bounds = bounds.reduced (4, 0);
        constexpr int labelH = 24;
        const int knobSize = juce::jlimit (
            122, 148,
            juce::jmin (bounds.getWidth() - 8,
                        bounds.getHeight() - labelH - 2));
        const int labelY = bounds.getBottom() - labelH;
        const int knobY = labelY - knobSize - 2;
        const int knobX = bounds.getCentreX() - knobSize / 2;
        knob.setBounds (knobX, knobY, knobSize, knobSize);
        label.setBounds (bounds.getX(), labelY, bounds.getWidth(), labelH);
    };

    const auto placeSmallValve = [] (juce::Slider& knob,
                                     juce::Label& label,
                                     juce::Rectangle<int> bounds)
    {
        bounds = bounds.reduced (8, 0);
        constexpr int labelH = 24;
        const int knobSize = juce::jlimit (
            68, 84,
            juce::jmin (bounds.getWidth() - 8,
                        bounds.getHeight() - labelH - 2));
        const int labelY = bounds.getBottom() - labelH;
        const int knobY = labelY - knobSize - 2;
        const int knobX = bounds.getCentreX() - knobSize / 2;
        knob.setBounds (knobX, knobY, knobSize, knobSize);
        label.setBounds (bounds.getX(), labelY, bounds.getWidth(), labelH);
    };

    placeLargeValve (speedKnob, speedLabel, responseArea);
    placeSmallValve (humanizeSlider, humanizeLabel, humanArea);
    placeLargeValve (amountKnob, amountLabel, amountArea);

    auto utility = utilityArea.reduced (4, 2);
    constexpr int moduleGap = 14;
    const int outputModuleW = juce::jlimit (142, 170, utility.getWidth() / 4);
    auto outputModule = utility.removeFromRight (outputModuleW);
    utility.removeFromRight (moduleGap);
    auto expressionModule = utility;

    auto holdRow = expressionModule.removeFromTop (30).reduced (4, 2);
    lockHysteresisLabel.setBounds (holdRow.removeFromLeft (52));
    lockHysteresisSlider.setBounds (holdRow);

    expressionModule.removeFromTop (6);
    auto vibratoRow = expressionModule.removeFromTop (28).reduced (4, 2);
    vibratoPreserveLabel.setBounds (vibratoRow.removeFromLeft (136));
    vibratoPreserveSlider.setBounds (vibratoRow);

    auto outputStage = outputModule.reduced (4, 2);
    analogModeButton.setBounds (outputStage.removeFromTop (24).reduced (2, 1));
    outputStage.removeFromTop (6);
    auto outRow = outputStage;
    outVolumeLabel.setBounds (outRow.removeFromLeft (52).reduced (0, 2));
    const int outputKnobSize = juce::jlimit (
        34, 40, juce::jmin (outRow.getWidth(), outRow.getHeight()));
    outVolumeSlider.setBounds (
        outRow.withSizeKeepingCentre (outputKnobSize, outputKnobSize));

    auto instrumentContent = instrumentArea.reduced (18, 4);
    const int sideMeterW = juce::jlimit (128, 164, instrumentContent.getWidth() / 4);
    instrumentContent.removeFromLeft (sideMeterW);
    instrumentContent.removeFromRight (sideMeterW);
    auto targetColumn = instrumentContent.reduced (8, 0);
    auto controlRoomArea = targetColumn.removeFromTop (32);
    controlRoomButton.setBounds (
        controlRoomArea.withSizeKeepingCentre (128, 28));
}
