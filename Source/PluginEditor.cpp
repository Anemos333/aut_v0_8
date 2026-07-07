#include "PluginEditor.h"
#include "ScaleDefinitions.h"
#include "NeumatonUILabels.h"
#include "Preset.h"
#include "NeumatonUILabels.h"
#include "BinaryData.h"
#include <cmath>

//==============================================================================
ModernLookAndFeel::ModernLookAndFeel()
{
    setColour(juce::Slider::thumbColourId, juce::Colour(0xFFFFFFFF));
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xFF6C63FF));
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xFF2A2D40));
    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xFF1E2135));
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xFF38405F));
    setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xFF1E2135));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xFF6C63FF));
    setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A3048));
    setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF6C63FF));
    setColour(juce::TextButton::textColourOffId, juce::Colours::white);
     
}

void ModernLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                          const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider)
{
    auto outline = slider.findColour (juce::Slider::rotarySliderOutlineColourId);
    auto fill    = slider.findColour (juce::Slider::rotarySliderFillColourId);

    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (10);
    auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    auto lineW = juce::jmin (8.0f, radius * 0.5f);
    auto arcRadius = radius - lineW * 0.5f;

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);

    g.setColour (outline);
    g.strokePath (backgroundArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    // Load background images from BinaryData
 
    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc (bounds.getCentreX(), bounds.getCentreY(), arcRadius, arcRadius, 0.0f, rotaryStartAngle, toAngle, true);

        g.setColour (fill);
        g.strokePath (valueArc, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Glow
        g.setColour (fill.withAlpha(0.3f));
        g.strokePath (valueArc, juce::PathStrokeType (lineW * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    
    // Thumb dot
    auto thumbWidth = lineW * 2.0f;
    juce::Point<float> thumbPoint (bounds.getCentreX() + arcRadius * std::cos (toAngle - juce::MathConstants<float>::halfPi),
                                   bounds.getCentreY() + arcRadius * std::sin (toAngle - juce::MathConstants<float>::halfPi));
    
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillEllipse (juce::Rectangle<float> (thumbWidth, thumbWidth).withCentre (thumbPoint));
}

void ModernLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown)
{
    auto cornerSize = 6.0f;
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f, 0.5f);
    
    auto baseColour = backgroundColour.withMultipliedSaturation (button.hasKeyboardFocus (true) ? 1.3f : 0.9f)
                                      .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f);
                                      
    if (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted)
        baseColour = baseColour.contrasting (shouldDrawButtonAsDown ? 0.2f : 0.05f);

    g.setColour (baseColour);
    g.fillRoundedRectangle (bounds, cornerSize);
    
    g.setColour (button.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (bounds, cornerSize, 1.0f);
}

void ModernLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                      int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box)
{
    auto cornerSize = 6.0f;
    juce::Rectangle<int> boxBounds (0, 0, width, height);

    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (boxBounds.toFloat(), cornerSize);

    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (boxBounds.toFloat().reduced (0.5f, 0.5f), cornerSize, 1.0f);

    juce::Rectangle<int> arrowZone (width - 30, 0, 20, height);
    juce::Path path;
    path.startNewSubPath (arrowZone.getX() + 3.0f, arrowZone.getCentreY() - 2.0f);
    path.lineTo (static_cast<float> (arrowZone.getCentreX()), arrowZone.getCentreY() + 3.0f);
    path.lineTo (arrowZone.getRight() - 3.0f, arrowZone.getCentreY() - 2.0f);

    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withAlpha ((box.isEnabled() ? 0.9f : 0.2f)));
    g.strokePath (path, juce::PathStrokeType (2.0f));
}

void ModernLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& component,
                                     float x, float y, float w, float h,
                                     const bool ticked,
                                     const bool isEnabled,
                                     const bool shouldDrawButtonAsHighlighted,
                                     const bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (isEnabled, shouldDrawButtonAsHighlighted, shouldDrawButtonAsDown);
    
    juce::Rectangle<float> tickBounds (x, y, w, h);
    
    g.setColour (juce::Colour(0xFF1E2135));
    g.fillRoundedRectangle (tickBounds, 4.0f);
    
    g.setColour (juce::Colour(0xFF38405F));
    g.drawRoundedRectangle (tickBounds.reduced(0.5f), 4.0f, 1.0f);
    
    if (ticked)
    {
        g.setColour (component.findColour(juce::ToggleButton::textColourId));
        auto tickShape = tickBounds.reduced(4.0f);
        juce::Path p;
        p.startNewSubPath(tickShape.getX(), tickShape.getCentreY());
        p.lineTo(tickShape.getCentreX() - 2.0f, tickShape.getBottom() - 2.0f);
        p.lineTo(tickShape.getRight(), tickShape.getY() + 2.0f);
        g.strokePath(p, juce::PathStrokeType(2.5f));
    }
}

//==============================================================================
MicrotonalAutotuneAudioProcessorEditor::MicrotonalAutotuneAudioProcessorEditor (
    MicrotonalAutotuneAudioProcessor& p)
    : AudioProcessorEditor (p),
      processorRef (p)
{
    setLookAndFeel(&modernLookAndFeel);
    bgImage = juce::ImageCache::getFromMemory (BinaryData::sfondo1_jpeg,
                                               BinaryData::sfondo1_jpegSize);

    bgImageScaleEditor = juce::ImageCache::getFromMemory (BinaryData::sfondo2_jpg,
                                                          BinaryData::sfondo2_jpgSize);


    presetSelectorLabel.setText (Neumaton::UI::Labels::Main::preset, juce::dontSendNotification);
presetSelectorLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
presetSelectorLabel.setColour (juce::Label::textColourId, juce::Colours::white);
presetSelectorLabel.setJustificationType (juce::Justification::centredRight);
addAndMakeVisible (presetSelectorLabel);

presetSelector.setJustificationType (juce::Justification::centredLeft);
presetSelector.onChange = [this]() { onPresetSelected(); };
addAndMakeVisible (presetSelector);

buildPresetMenu();
    // ==================== Scale Selector ====================
    scaleSelectorLabel.setText (Neumaton::UI::Labels::Main::scale, juce::dontSendNotification);
    scaleSelectorLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    scaleSelectorLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    scaleSelectorLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (scaleSelectorLabel);

    scaleSelector.setJustificationType (juce::Justification::centredLeft);
    scaleSelector.onChange = [this]() { onScaleSelected(); };
    addAndMakeVisible (scaleSelector);

    buildScaleMenu();

    // ==================== Root Note Selector ====================
    rootNoteSelectorLabel.setText (Neumaton::UI::Labels::Main::rootNote, juce::dontSendNotification);
    rootNoteSelectorLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    rootNoteSelectorLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    rootNoteSelectorLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (rootNoteSelectorLabel);

    rootNoteSelector.setJustificationType (juce::Justification::centredLeft);
    rootNoteSelector.addSectionHeading ("Temperamento Equabile");
    rootNoteSelector.addItem ("C",  1);
    rootNoteSelector.addItem ("C#", 2);
    rootNoteSelector.addItem ("D",  3);
    rootNoteSelector.addItem ("D#", 4);
    rootNoteSelector.addItem ("E",  5);
    rootNoteSelector.addItem ("F",  6);
    rootNoteSelector.addItem ("F#", 7);
    rootNoteSelector.addItem ("G",  8);
    rootNoteSelector.addItem ("G#", 9);
    rootNoteSelector.addItem ("A",  10);
    rootNoteSelector.addItem ("A#", 11);
    rootNoteSelector.addItem ("B",  12);
    rootNoteSelector.addSectionHeading ("Bizantino");
    rootNoteSelector.addItem ("Ni", 13);
    rootNoteSelector.addItem ("Pa", 14);
    rootNoteSelector.addItem ("Vu", 15);
    rootNoteSelector.addItem ("Ga", 16);
    rootNoteSelector.addItem ("Di", 17);
    rootNoteSelector.addItem ("Ke", 18);
    rootNoteSelector.addItem ("Zo", 19);
    rootNoteSelector.setSelectedId (processorRef.rootNoteIndex.load() + 1, juce::dontSendNotification);
    rootNoteSelector.onChange = [this]() { onRootNoteSelected(); };
    addAndMakeVisible (rootNoteSelector);

    // ==================== Processing Mode Selector ====================
    modeSelectorLabel.setText (Neumaton::UI::Labels::Main::mode, juce::dontSendNotification);
    modeSelectorLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    modeSelectorLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    modeSelectorLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (modeSelectorLabel);

    modeSelector.setJustificationType (juce::Justification::centredLeft);
    modeSelector.addItem ("High Latency", 1);
    modeSelector.addItem ("Quality",      2);
    modeSelector.addItem ("Live",         3);
    modeSelector.addItem ("Experimental", 4);
    modeSelector.setSelectedId (processorRef.processingMode.load() + 1, juce::dontSendNotification);
    modeSelector.onChange = [this]() { onModeSelected(); };
    addAndMakeVisible (modeSelector);

    // ==================== Speed Knob ====================
    speedKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    speedKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    speedKnob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFF6C63FF));
    speedKnob.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    speedKnob.setLookAndFeel (&mainValveLookAndFeel);
    speedKnob.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    speedKnob.textFromValueFunction = [this](double val) {
        if (scaleLockButton.getToggleState()) {
            double mappedVal = val * (7.0 / 500.0);
            return juce::String(mappedVal, 2) + " ms";
        }
        return juce::String(val, 1) + " ms";
    };
    addAndMakeVisible (speedKnob);

    speedLabel.setText (Neumaton::UI::Labels::Main::response, juce::dontSendNotification);
    speedLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    speedLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    speedLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (speedLabel);

    speedAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "speed", speedKnob);

    // ==================== Amount Knob ====================
    amountKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    amountKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    amountKnob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFFFF6B6B));
    amountKnob.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    amountKnob.setLookAndFeel (&mainValveLookAndFeel);
    amountKnob.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    amountKnob.setTextValueSuffix (" %");
    addAndMakeVisible (amountKnob);

    amountLabel.setText (Neumaton::UI::Labels::Main::correctionAmount, juce::dontSendNotification);
    amountLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    amountLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    amountLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (amountLabel);

    amountAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "amount", amountKnob);

    // ==================== Humanize Slider ====================
    // Human Drift is prepared as a small top-arc valve.
    // Visual convention for the later skin:
    // 0%   -> 135 degrees
    // 50%  ->  90 degrees
    // 100% ->  45 degrees
    humanizeSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    humanizeSlider.setRotaryParameters (
        -juce::MathConstants<float>::pi/4.0f,
         juce::MathConstants<float>::pi/4.0f,
         true);
    humanizeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    humanizeSlider.setTextValueSuffix (" %");
    humanizeSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFF00C878));
    humanizeSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    humanizeSlider.setLookAndFeel (&humanDriftLookAndFeel);
    humanizeSlider.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    humanizeSlider.textFromValueFunction = [](double val)
    {
        juce::String text (val, 1);
        if (val < 5.0)
            return text + " % (Robot)";
        else if (val > 95.0)
            return text + " % (Human)";
        return text + " %";
    };
    addAndMakeVisible (humanizeSlider);

    humanizeLabel.setText (Neumaton::UI::Labels::Main::humanize, juce::dontSendNotification);
    humanizeLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    humanizeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    humanizeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (humanizeLabel);

    humanizeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.getAPVTS(), "humanize", humanizeSlider);

    // ==================== Modifica A: Scale Lock ====================
    scaleLockButton.setButtonText (Neumaton::UI::Labels::Main::scaleLock);
    scaleLockButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(scaleLockButton);
    scaleLockAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.getAPVTS(), "scaleLock", scaleLockButton);
    scaleLockButton.onClick = [this]()
    {
        bool lockOn = scaleLockButton.getToggleState();
        lockHysteresisSlider.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        lockHysteresisLabel.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        vibratoPreserveSlider.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        vibratoPreserveLabel.setVisible (lockOn && !showingTempoPage && !showingScaleEditor);
        repaint();
    };

    lockHysteresisSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    lockHysteresisSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    lockHysteresisSlider.setTextValueSuffix (" c");
    lockHysteresisSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xFFFF0066));
    lockHysteresisSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible(lockHysteresisSlider);
    lockHysteresisLabel.setText (Neumaton::UI::Labels::Main::hold, juce::dontSendNotification);
    lockHysteresisLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    lockHysteresisLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    lockHysteresisLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible(lockHysteresisLabel);
    lockHysteresisAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), "lockHysteresis", lockHysteresisSlider);

    vibratoPreserveSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    vibratoPreserveSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 20);
    vibratoPreserveSlider.setTextValueSuffix (" %");
    vibratoPreserveSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xFFFF0066));
    vibratoPreserveSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible(vibratoPreserveSlider);
    vibratoPreserveLabel.setText (Neumaton::UI::Labels::Main::vibratoPreserve, juce::dontSendNotification);
    vibratoPreserveLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    vibratoPreserveLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    vibratoPreserveLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible(vibratoPreserveLabel);
    vibratoPreserveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), "vibratoPreserve", vibratoPreserveSlider);

    // ==================== Modifica B: Analog Mode ====================
    analogModeButton.setButtonText (Neumaton::UI::Labels::Main::analogTexture);
    analogModeButton.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(analogModeButton);
    analogModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processorRef.getAPVTS(), "analogMode", analogModeButton);

    // Output belongs to the final output stage.
    // It is prepared as a compact visible volume knob.
    outVolumeSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    outVolumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    outVolumeSlider.setTextValueSuffix (" dB");
    outVolumeSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xFFFF9900));
    outVolumeSlider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible(outVolumeSlider);
    outVolumeLabel.setText (Neumaton::UI::Labels::Main::output, juce::dontSendNotification);
    outVolumeLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    outVolumeLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    outVolumeLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible(outVolumeLabel);
    outVolumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getAPVTS(), "outVolume", outVolumeSlider);

    // ==================== Creative Tempo page ====================
    tempoPageButton.setButtonText (Neumaton::UI::Labels::Main::tempoLab);
    tempoPageButton.onClick = [this]() { showTempoPage(); };
    tempoPageButton.setColour (juce::TextButton::buttonColourId,
                               juce::Colour (0xFF38405F));
    tempoPageButton.setColour (juce::TextButton::buttonOnColourId,
                               juce::Colour (0xFF6C63FF));
    addAndMakeVisible (tempoPageButton);

    tempoBackButton.setButtonText (Neumaton::UI::Labels::Tempo::back);
    tempoBackButton.onClick = [this]() { closeTempoPage(); };
    addAndMakeVisible (tempoBackButton);

    const auto configureModeButton = [this](juce::TextButton& button, int mode)
    {
        button.setClickingTogglesState (true);
        button.setRadioGroupId (7710);
        button.setColour (juce::TextButton::buttonColourId,
                          juce::Colour (0xFF2A3048));
        button.setColour (juce::TextButton::buttonOnColourId,
                          juce::Colour (0xFF6C63FF));
        button.onClick = [this, mode]() { setTempoModeParameter (mode); };
        addAndMakeVisible (button);
    };
    configureModeButton (tempoOffButton, 0);
    configureModeButton (tempoGlideButton, 1);
    configureModeButton (glideLockButton, 2);

    tempoDivisionLabel.setText (Neumaton::UI::Labels::Tempo::division, juce::dontSendNotification);
    tempoDivisionLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    tempoDivisionLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    tempoDivisionLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (tempoDivisionLabel);

    tempoDivisionSelector.addItem ("1/128", 1);
    tempoDivisionSelector.addItem ("1/64", 2);
    tempoDivisionSelector.addItem ("1/32", 3);
    tempoDivisionSelector.addItem ("1/16", 4);
    tempoDivisionSelector.addItem ("1/8", 5);
    addAndMakeVisible (tempoDivisionSelector);
    tempoDivisionAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            processorRef.getAPVTS(), "tempoDivision", tempoDivisionSelector);

    tempoGlideLength.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    tempoGlideLength.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    tempoGlideLength.setTextValueSuffix (" %");
    tempoGlideLength.setColour (juce::Slider::rotarySliderFillColourId,
                                juce::Colour (0xFF5BC0EB));
    tempoGlideLength.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible (tempoGlideLength);
    tempoGlideLengthLabel.setText (Neumaton::UI::Labels::Tempo::glideLength, juce::dontSendNotification);
    tempoGlideLengthLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    tempoGlideLengthLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    tempoGlideLengthLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (tempoGlideLengthLabel);
    tempoGlideLengthAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.getAPVTS(), "tempoGlidePercent", tempoGlideLength);

    tempoLockStrength.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    tempoLockStrength.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    tempoLockStrength.setTextValueSuffix (" %");
    tempoLockStrength.setColour (juce::Slider::rotarySliderFillColourId,
                                 juce::Colour (0xFFFFA24A));
    tempoLockStrength.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible (tempoLockStrength);
    tempoLockStrengthLabel.setText (Neumaton::UI::Labels::Tempo::lock, juce::dontSendNotification);
    tempoLockStrengthLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    tempoLockStrengthLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    tempoLockStrengthLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (tempoLockStrengthLabel);
    tempoLockStrengthAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment> (
            processorRef.getAPVTS(), "tempoLockStrength", tempoLockStrength);

    tempoSmartOnset.setButtonText (Neumaton::UI::Labels::Tempo::smartOnset);
    tempoSmartOnset.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible (tempoSmartOnset);
    tempoSmartOnsetAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processorRef.getAPVTS(), "tempoSmartOnset", tempoSmartOnset);

    setTempoControlsVisible (false);
    updateTempoModeButtons();
    controlRoomButton.setButtonText (Neumaton::UI::Labels::Main::controlRoom);
    controlRoomButton.onClick = [this]() { showControlRoom(); };
    addAndMakeVisible (controlRoomButton);
    controlRoomPage.onBack = [this]() { closeControlRoom(); };
    addChildComponent (controlRoomPage);


    // ==================== Window setup ====================
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
    setLookAndFeel(nullptr);
    stopTimer();
}

//==============================================================================
void MicrotonalAutotuneAudioProcessorEditor::setMainControlsVisible(
    bool shouldBeVisible)
{
    scaleSelector.setVisible (shouldBeVisible);
    scaleSelectorLabel.setVisible (shouldBeVisible);
    rootNoteSelector.setVisible (shouldBeVisible);
    rootNoteSelectorLabel.setVisible (shouldBeVisible);
    speedKnob.setVisible (shouldBeVisible);
    speedLabel.setVisible (shouldBeVisible);
    amountKnob.setVisible (shouldBeVisible);
    amountLabel.setVisible (shouldBeVisible);
    humanizeSlider.setVisible (shouldBeVisible);
    humanizeLabel.setVisible (shouldBeVisible);
    modeSelector.setVisible (shouldBeVisible);
    modeSelectorLabel.setVisible (shouldBeVisible);
    tempoPageButton.setVisible (shouldBeVisible);
    controlRoomButton.setVisible (shouldBeVisible);

    scaleLockButton.setVisible (shouldBeVisible);
    analogModeButton.setVisible (shouldBeVisible);
    outVolumeSlider.setVisible (shouldBeVisible);
    outVolumeLabel.setVisible (shouldBeVisible);
    presetSelector.setVisible (shouldBeVisible);
    presetSelectorLabel.setVisible (shouldBeVisible);

    bool lockIsOn = scaleLockButton.getToggleState();
    lockHysteresisSlider.setVisible (shouldBeVisible && lockIsOn);
    lockHysteresisLabel.setVisible (shouldBeVisible && lockIsOn);
    vibratoPreserveSlider.setVisible (shouldBeVisible && lockIsOn);
    vibratoPreserveLabel.setVisible (shouldBeVisible && lockIsOn);
}
void MicrotonalAutotuneAudioProcessorEditor::showControlRoom()
{
    if (showingScaleEditor)
        return;

    showingControlRoom = true;
    showingTempoPage = false;
    setMainControlsVisible (false);
    setTempoControlsVisible (false);
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

void MicrotonalAutotuneAudioProcessorEditor::setTempoControlsVisible(
    bool shouldBeVisible)
{
    tempoBackButton.setVisible (shouldBeVisible);
    tempoOffButton.setVisible (shouldBeVisible);
    tempoGlideButton.setVisible (shouldBeVisible);
    glideLockButton.setVisible (shouldBeVisible);
    tempoDivisionSelector.setVisible (shouldBeVisible);
    tempoDivisionLabel.setVisible (shouldBeVisible);
    tempoGlideLength.setVisible (shouldBeVisible);
    tempoGlideLengthLabel.setVisible (shouldBeVisible);
    tempoLockStrength.setVisible (shouldBeVisible);
    tempoLockStrengthLabel.setVisible (shouldBeVisible);
    tempoSmartOnset.setVisible (shouldBeVisible);
}

void MicrotonalAutotuneAudioProcessorEditor::showTempoPage()
{
    if (showingScaleEditor)
        return;

    showingTempoPage = true;
    setMainControlsVisible (false);
    setTempoControlsVisible (true);
    updateTempoModeButtons();
    resized();
    bgImageScaleEditor = juce::ImageCache::getFromMemory(BinaryData::sfondo3_jpeg,
        BinaryData::sfondo3_jpegSize);
    repaint();
}

void MicrotonalAutotuneAudioProcessorEditor::closeTempoPage()
{
    showingTempoPage = false;
    setTempoControlsVisible (false);
    setMainControlsVisible (true);
    resized();
    repaint();
}

void MicrotonalAutotuneAudioProcessorEditor::setTempoModeParameter(int modeIndex)
{
    modeIndex = juce::jlimit (0, 2, modeIndex);
    if (auto* parameter = processorRef.getAPVTS().getParameter ("tempoMode"))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (static_cast<float> (modeIndex)));
        parameter->endChangeGesture();
    }
    updateTempoModeButtons();
}

void MicrotonalAutotuneAudioProcessorEditor::updateTempoModeButtons()
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

    modeSelector.setSelectedId (
        processorRef.processingMode.load() + 1,
        juce::dontSendNotification);

    updateTempoModeButtons();

    const bool mainVisible = !showingTempoPage && !showingScaleEditor;
    setMainControlsVisible (mainVisible);

    resized();
    repaint();
}
void MicrotonalAutotuneAudioProcessorEditor::buildScaleMenu()
{
    scaleSelector.clear (juce::dontSendNotification);

    const auto& scales = ScaleDefinitions::getAllScales();
    int itemId = 1;

    // Group scales by category using section headers
    juce::String lastCategory;

    for (int i = 0; i < static_cast<int> (scales.size()); ++i)
    {
        const auto& scale = scales[static_cast<size_t> (i)];
        juce::String category (scale.category);

        if (category != lastCategory)
        {
            scaleSelector.addSectionHeading (category);
            lastCategory = category;
        }

        scaleSelector.addItem (juce::String (scale.name), itemId);
        itemId++;
    }

    // Separator before custom scales
    scaleSelector.addSeparator();
    scaleSelector.addSectionHeading ("Le tue scale");

    int numCustom = processorRef.getCustomPresets().getNumPresets();

    if (numCustom == 0)
    {
        // "Crea scala" item
        scaleSelector.addItem ("Crea scala...", 10000);
    }
    else
    {
        // Show custom presets
        for (int i = 0; i < numCustom; ++i)
        {
            const auto& preset = processorRef.getCustomPresets().getPreset (i);
            scaleSelector.addItem (preset.name, 10001 + i);
        }

        // "Crea scala" if under limit, with delete options
        if (numCustom < CustomScalePresets::maxPresets)
            scaleSelector.addItem ("Crea scala...", 10000);

        // Delete options
        scaleSelector.addSeparator();
        for (int i = 0; i < numCustom; ++i)
        {
            const auto& preset = processorRef.getCustomPresets().getPreset (i);
            scaleSelector.addItem ("Elimina: " + preset.name, 20001 + i);
        }
    }

    // Set current selection
    int customIdx = processorRef.activeCustomPresetIndex.load();
    if (customIdx >= 0 && customIdx < numCustom)
    {
        scaleSelector.setSelectedId (10001 + customIdx, juce::dontSendNotification);
    }
    else
    {
        int scaleIdx = processorRef.currentScaleIndex.load();
        scaleSelector.setSelectedId (scaleIdx + 1, juce::dontSendNotification);
    }
}

void MicrotonalAutotuneAudioProcessorEditor::onScaleSelected()
{
    int selectedId = scaleSelector.getSelectedId();

    if (selectedId == 0)
        return;

    // "Crea scala..."
    if (selectedId == 10000)
    {
        showCustomScaleEditor();
        return;
    }

    // Delete a custom preset
    if (selectedId >= 20001)
    {
        int deleteIdx = selectedId - 20001;
        processorRef.getCustomPresets().removePreset (deleteIdx);

        // If the deleted preset was active, revert to built-in
        if (processorRef.activeCustomPresetIndex.load() == deleteIdx)
        {
            processorRef.activeCustomPresetIndex.store (-1);
            processorRef.currentScaleIndex.store (0);
        }
        else if (processorRef.activeCustomPresetIndex.load() > deleteIdx)
        {
            // Adjust index after deletion
            processorRef.activeCustomPresetIndex.store (
                processorRef.activeCustomPresetIndex.load() - 1);
        }

        processorRef.refreshScaleSnapshot();
        buildScaleMenu();
        return;
    }

    // Custom preset selected
    if (selectedId >= 10001 && selectedId < 20000)
    {
        int customIdx = selectedId - 10001;
        processorRef.activeCustomPresetIndex.store (customIdx);
        processorRef.refreshScaleSnapshot();
        return;
    }

    // Built-in scale selected
    int scaleIdx = selectedId - 1;
    if (scaleIdx >= 0 && scaleIdx < ScaleDefinitions::getScaleCount())
    {
        processorRef.currentScaleIndex.store (scaleIdx);
        processorRef.activeCustomPresetIndex.store (-1);
        processorRef.refreshScaleSnapshot();
    }
}

void MicrotonalAutotuneAudioProcessorEditor::showCustomScaleEditor()
{
    showingScaleEditor = true;

    // Hide main and creative-tempo controls.
    showingTempoPage = false;
    setMainControlsVisible (false);
    setTempoControlsVisible (false);

    // Create and show custom scale editor
    customScaleEditorPage = std::make_unique<CustomScaleEditor> (
        processorRef, *this, bgImageScaleEditor);
    addAndMakeVisible (*customScaleEditorPage);
    customScaleEditorPage->setBounds (getLocalBounds());
}

void MicrotonalAutotuneAudioProcessorEditor::customScaleEditorClosed()
{
    showingScaleEditor = false;

    // Remove custom scale editor
    customScaleEditorPage.reset();

    // Show main components
    setMainControlsVisible (true);
    setTempoControlsVisible (false);

    // Rebuild menu to include any new preset
    buildScaleMenu();

    repaint();
}

//==============================================================================
juce::String MicrotonalAutotuneAudioProcessorEditor::trackingStateToString (
    ModernPitchEngine::TrackingState state)
{
    switch (state)
    {
        case ModernPitchEngine::TrackingState::unvoiced:   return "Unvoiced";
        case ModernPitchEngine::TrackingState::attack:     return "Attack";
        case ModernPitchEngine::TrackingState::acquire:    return "Acquire";
        case ModernPitchEngine::TrackingState::stable:     return "Stable";
        case ModernPitchEngine::TrackingState::transition: return "Transition";
        case ModernPitchEngine::TrackingState::release:    return "Release";
    }

    return "Unknown";
}

void MicrotonalAutotuneAudioProcessorEditor::timerCallback()
{
if (showingControlRoom)
       controlRoomPage.setMetering (displayedMetering);    
    displayedMetering = processorRef.getPitchMetering();
    if (showingTempoPage)
        updateTempoModeButtons();

    // Update Scale Lock visual state only when it changes
    bool lockIsOn = scaleLockButton.getToggleState();
    if (lockIsOn != lastScaleLockState_)
    {
        lastScaleLockState_ = lockIsOn;
        speedKnob.setColour (juce::Slider::rotarySliderFillColourId,
                             lockIsOn ? juce::Colour (0xFFFF0066)
                                      : juce::Colour (0xFF6C63FF));
        speedKnob.updateText();

        // Toggle text color: green when active
        scaleLockButton.setColour (juce::ToggleButton::textColourId,
                                   lockIsOn ? juce::Colour (0xFF00CC66)
                                            : juce::Colours::white);

        // Grey out sub-params when lock is off (but keep them hidden via onClick)
        lockHysteresisSlider.setEnabled (lockIsOn);
        vibratoPreserveSlider.setEnabled (lockIsOn);
    }

    // Update Analog Mode visual state only when it changes
    bool analogIsOn = analogModeButton.getToggleState();
    if (analogIsOn != lastAnalogModeState_)
    {
        lastAnalogModeState_ = analogIsOn;
        analogModeButton.setColour (juce::ToggleButton::textColourId,
                                    analogIsOn ? juce::Colour (0xFFFF8800)
                                               : juce::Colours::white);
    }

    repaint();
}

void MicrotonalAutotuneAudioProcessorEditor::drawMeterPanel (
    juce::Graphics& g,
    juce::Rectangle<int> bounds)
{
    if (bounds.isEmpty())
        return;

    const auto panel = bounds.toFloat();
    g.setColour (juce::Colour (0xB0101422));
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (juce::Colour (0x507F8CFF));
    g.drawRoundedRectangle (panel.reduced (0.5f), 8.0f, 1.0f);

    auto content = bounds.reduced (12, 8);
    auto valueRow = content.removeFromTop (24);
    auto statusRow = content.removeFromTop (20);
    content.removeFromTop (4);

    const auto pitchText = displayedMetering.detectedPitchHz > 0.0f
        ? juce::String (displayedMetering.detectedPitchHz, 1) + " Hz"
        : juce::String ("--");
    const auto targetText = displayedMetering.targetPitchHz > 0.0f
        ? juce::String (displayedMetering.targetPitchHz, 1) + " Hz"
        : juce::String ("--");

    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.drawText (juce::String (Neumaton::UI::Labels::Meter::detectedPitch) + ": " + pitchText,
                valueRow.removeFromLeft (content.getWidth() / 3),
                juce::Justification::centredLeft);
    g.drawText (juce::String (Neumaton::UI::Labels::Meter::targetFrequency) + ": " + targetText,
                valueRow.removeFromLeft (content.getWidth() / 3),
                juce::Justification::centredLeft);
    g.drawText (juce::String (Neumaton::UI::Labels::Meter::correction) + ": "
                    + juce::String (displayedMetering.correctionCents, 1)
                    + " ct",
                valueRow,
                juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (12.0f));
    g.setColour (juce::Colour (0xFFD8DDF5));
    juce::String status = "State: " + trackingStateToString (displayedMetering.state)
        + "   Paths: " + juce::String (displayedMetering.detectorSupport)
        + "/4   Octave: " + juce::String (displayedMetering.octaveState);
    if (displayedMetering.pendingOctaveObservations > 0)
        status += "   Confirm: "
               + juce::String (displayedMetering.pendingOctaveObservations);
    if (displayedMetering.dualSynthesisActive)
        status += "   Dual: "
               + juce::String (displayedMetering.transitionBlend * 100.0f, 0)
               + "%";
    if (displayedMetering.noiseReductionDb > 0.05f)
        status += "   Breath GR: "
               + juce::String (displayedMetering.noiseReductionDb, 1)
               + " dB";
    status += "   Poly: "
           + juce::String (displayedMetering.polyphony * 100.0f, 0)
           + "%   Rel: "
           + juce::String (displayedMetering.spectralReliability * 100.0f, 0)
           + "%   Mask: "
           + juce::String (displayedMetering.maskStability * 100.0f, 0)
           + "%";
    if (displayedMetering.sustainedNoteSeconds > 0.05f)
        status += "   Hold: "
               + juce::String (displayedMetering.sustainedNoteSeconds, 1)
               + " s";
    g.drawText (status, statusRow, juce::Justification::centredLeft);

    const auto drawBar = [&g](juce::Rectangle<int> area,
                              float value,
                              const juce::String& name,
                              juce::Colour colour)
    {
        value = juce::jlimit (0.0f, 1.0f, value);
        auto labelArea = area.removeFromLeft (76);
        g.setColour (juce::Colour (0xFFD8DDF5));
        g.drawText (name, labelArea, juce::Justification::centredLeft);

        auto bar = area.reduced (2, 5).toFloat();
        g.setColour (juce::Colour (0xFF272C40));
        g.fillRoundedRectangle (bar, 3.0f);
        auto fill = bar;
        fill.setWidth (bar.getWidth() * value);
        g.setColour (colour);
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawText (juce::String (value * 100.0f, 0) + "%",
                    bar.toNearestInt(),
                    juce::Justification::centred);
    };

    auto firstBarRow = content.removeFromTop (24);
    content.removeFromTop (2);
    auto secondBarRow = content.removeFromTop (24);

    const int firstBarWidth = firstBarRow.getWidth() / 3;
    drawBar (firstBarRow.removeFromLeft (firstBarWidth),
             displayedMetering.confidence,
             Neumaton::UI::Labels::Meter::confidence,
             juce::Colour (0xFF6C63FF));
    drawBar (firstBarRow.removeFromLeft (firstBarWidth),
             displayedMetering.voicing,
             Neumaton::UI::Labels::Meter::voicing,
             juce::Colour (0xFF00C878));
    drawBar (firstBarRow,
             displayedMetering.consensus,
             Neumaton::UI::Labels::Meter::consensus,
             juce::Colour (0xFFFFA24A));

    const int secondBarWidth = secondBarRow.getWidth() / 3;
    drawBar (secondBarRow.removeFromLeft (secondBarWidth),
             displayedMetering.breathiness,
             Neumaton::UI::Labels::Meter::breath,
             juce::Colour (0xFF5BC0EB));
    drawBar (secondBarRow.removeFromLeft (secondBarWidth),
             displayedMetering.harmonicity,
             Neumaton::UI::Labels::Meter::harmonic,
             juce::Colour (0xFF9BE564));
    drawBar (secondBarRow,
             displayedMetering.noisePath,
             Neumaton::UI::Labels::Meter::noisePath,
             juce::Colour (0xFFFFD166));
}

void MicrotonalAutotuneAudioProcessorEditor::drawTempoPage(
    juce::Graphics& g,
    juce::Rectangle<int> bounds)
{
    auto panel = bounds.reduced (24, 18);
    g.setColour (juce::Colour (0xB0101422));
    g.fillRoundedRectangle (panel.toFloat(), 12.0f);
    g.setColour (juce::Colour (0x607F8CFF));
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 12.0f, 1.0f);

    auto status = panel.reduced (18, 12);
    auto title = status.removeFromTop (38);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText (Neumaton::UI::Labels::Tempo::pageTitle, title, juce::Justification::centred);

    status.removeFromTop (250);
    auto textRow = status.removeFromTop (28);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.setColour (juce::Colour (0xFFD8DDF5));

    juce::String syncText;
    if (processorRef.processingMode.load() == 0)
        syncText = Neumaton::UI::Labels::Tempo::requiresMode;
    else if (!displayedMetering.tempoActive)
        syncText = Neumaton::UI::Labels::Tempo::disabled;
    else if (displayedMetering.tempoHostSyncValid)
        syncText = Neumaton::UI::Labels::Tempo::hostSync;
    else
        syncText = Neumaton::UI::Labels::Tempo::bpmFallback;

    juce::String modeText = "Off";
    if (displayedMetering.tempoMode == CreativeTempo::Mode::tempoGlide)
        modeText = "Tempo Glide";
    else if (displayedMetering.tempoMode == CreativeTempo::Mode::glideLock)
        modeText = "Glide Lock";

    g.drawText (modeText + "   |   " + syncText
                    + "   |   "
                    + juce::String (displayedMetering.tempoBpm, 1) + " BPM"
                    + "   |   Glide "
                    + juce::String (displayedMetering.tempoGlideTimeMs, 1)
                    + " ms",
                textRow, juce::Justification::centred);

    auto waitingRow = status.removeFromTop (24);
    g.setColour (displayedMetering.tempoWaitingForGrid
        ? juce::Colour (0xFFFFA24A)
        : juce::Colour (0xFF9BE564));
    g.drawText (displayedMetering.tempoWaitingForGrid
                    ? Neumaton::UI::Labels::Tempo::waitingForGrid
                    : Neumaton::UI::Labels::Tempo::targetFree,
                waitingRow, juce::Justification::centred);

    auto phaseArea = status.removeFromTop (28).reduced (40, 7).toFloat();
    g.setColour (juce::Colour (0xFF272C40));
    g.fillRoundedRectangle (phaseArea, 4.0f);
    auto fill = phaseArea;
    fill.setWidth (phaseArea.getWidth()
        * juce::jlimit (0.0f, 1.0f, displayedMetering.tempoGridPhase));
    g.setColour (juce::Colour (0xFF5BC0EB));
    g.fillRoundedRectangle (fill, 4.0f);
}

//==============================================================================
void MicrotonalAutotuneAudioProcessorEditor::paint (juce::Graphics& g)
{
    if (showingScaleEditor)
        return;
 if (showingControlRoom)
        return;
 
    auto bounds = getLocalBounds().toFloat();

    if (bgImage.isValid())
    {
        g.drawImage (bgImage, bounds, juce::RectanglePlacement::fillDestination);
    }
    else
    {
        juce::ColourGradient gradient (
            juce::Colour (0xFF141726), bounds.getTopLeft(),
            juce::Colour (0xFF0B0D1A), bounds.getBottomRight(),
            false);

        g.setGradientFill (gradient);
        g.fillRect (bounds);
    }

    // Overlay più leggero: rende i controlli leggibili senza cancellare l’immagine.
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

        auto f = r.toFloat();

        g.setColour (juce::Colour (0x68101422));
        g.fillRoundedRectangle (f, corner);

        g.setColour (juce::Colour (0x447F8CFF));
        g.drawRoundedRectangle (f.reduced (0.5f), corner, 1.0f);
    };

    if (showingTempoPage)
    {
        drawTempoPage (g, getLocalBounds());
        return;
    }

    // Pannelli dietro i gruppi principali.
    auto headerPanel = presetSelectorLabel.getBounds()
    .getUnion (presetSelector.getBounds())
    .getUnion (scaleSelectorLabel.getBounds())
    .getUnion (scaleSelector.getBounds())
    .getUnion (rootNoteSelectorLabel.getBounds())
    .getUnion (rootNoteSelector.getBounds())
    .getUnion (modeSelectorLabel.getBounds())
    .getUnion (modeSelector.getBounds())
    .getUnion (tempoPageButton.getBounds())
    .expanded (14, 10);

    drawPanel (headerPanel);

    // The two main valves now sit directly on the future background drawing.
    // No translucent backing panels here: their physical shadow is drawn by
    // MainValveLookAndFeel itself.

    auto scaleLockPanel = scaleLockButton.getBounds();

    if (scaleLockButton.getToggleState())
        scaleLockPanel = scaleLockPanel
            .getUnion (lockHysteresisSlider.getBounds())
            .getUnion (lockHysteresisLabel.getBounds())
            .getUnion (vibratoPreserveSlider.getBounds())
            .getUnion (vibratoPreserveLabel.getBounds());

    drawPanel (scaleLockPanel.expanded (14, 10));

    // Output stage: separate from the musical lock controls.
    // This will later become a distinct final valve / output module.
    auto outputStagePanel = analogModeButton.getBounds()
        .getUnion (outVolumeSlider.getBounds())
        .getUnion (outVolumeLabel.getBounds());

    drawPanel (outputStagePanel.expanded (14, 10));

    
    auto lowerArea = getLocalBounds();
    auto titleArea = lowerArea.removeFromBottom (38);
    auto titleTextArea = titleArea.reduced (24, 0);

    auto instrumentArea = lowerArea.removeFromBottom (112).reduced (18, 4);
    auto instrumentContent = instrumentArea;

    const int sideMeterW = juce::jlimit (
        128, 164, instrumentContent.getWidth() / 4);

    auto correctionArea = instrumentContent.removeFromLeft (sideMeterW).reduced (4);
    auto consensusArea  = instrumentContent.removeFromRight (sideMeterW).reduced (4);

    auto targetArea = instrumentContent.reduced (8, 0);
    targetArea.removeFromTop (32); // spazio comunicativo per il bottone Control Room
    targetArea = targetArea.reduced (2, 2);

    neumaton::lab::Painter::drawCorrectionGauge (
        g,
        correctionArea,
        displayedMetering.correctionCents);

    neumaton::lab::Painter::drawRadioTarget (
        g,
        targetArea,
        displayedMetering.detectedPitchHz,
        displayedMetering.targetPitchHz);

    neumaton::lab::Painter::drawConsensusGauge (
        g,
        consensusArea,
        displayedMetering.consensus);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("Neumaton", titleTextArea, juce::Justification::centred);

    int mode = processorRef.processingMode.load();
    if (mode > 0)
    {
        juce::Colour dotColour;
        switch (mode)
        {
            case 1:  dotColour = juce::Colour (0xFF4488FF); break;
            case 2:  dotColour = juce::Colour (0xFF00CC66); break;
            case 3:  dotColour = juce::Colour (0xFFFF8800); break;
            default: dotColour = juce::Colour (0xFF888888); break;
        }

        auto modeBounds = modeSelector.getBounds();
        int dotX = modeBounds.getRight() + 6;
        int dotY = modeBounds.getCentreY();

        g.setColour (dotColour);
        g.fillEllipse (static_cast<float> (dotX - 4), static_cast<float> (dotY - 4), 8.0f, 8.0f);

        g.setColour (dotColour.withAlpha (0.3f));
        g.fillEllipse (static_cast<float> (dotX - 7), static_cast<float> (dotY - 7), 14.0f, 14.0f);
    }
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

    if (showingTempoPage)
    {
        auto area = getLocalBounds().reduced (34, 26);

        tempoBackButton.setBounds (area.removeFromTop (30).removeFromLeft (90));
        area.removeFromTop (44);

        auto modeRow = area.removeFromTop (38);
        const int modeWidth = modeRow.getWidth() / 3;

        tempoOffButton.setBounds    (modeRow.removeFromLeft (modeWidth).reduced (5, 2));
        tempoGlideButton.setBounds  (modeRow.removeFromLeft (modeWidth).reduced (5, 2));
        glideLockButton.setBounds   (modeRow.reduced (5, 2));

        area.removeFromTop (18);

        auto divisionRow = area.removeFromTop (54);
        tempoDivisionLabel.setBounds    (divisionRow.removeFromLeft (120));
        tempoDivisionSelector.setBounds (divisionRow.removeFromLeft (150).reduced (6, 10));
        tempoSmartOnset.setBounds       (divisionRow.reduced (18, 10));

        area.removeFromTop (12);

        auto knobs = area.removeFromTop (150);
        const int half = knobs.getWidth() / 2;

        auto glideArea = knobs.removeFromLeft (half);
        auto lockArea = knobs;

        const int tempoKnobSize = juce::jlimit (
            82, 112, juce::jmin (glideArea.getWidth(), glideArea.getHeight() - 28));

        tempoGlideLength.setBounds (
            glideArea.getCentreX() - tempoKnobSize / 2,
            glideArea.getY(),
            tempoKnobSize,
            tempoKnobSize);

        tempoGlideLengthLabel.setBounds (
            glideArea.getX(),
            glideArea.getY() + tempoKnobSize + 2,
            glideArea.getWidth(),
            22);

        tempoLockStrength.setBounds (
            lockArea.getCentreX() - tempoKnobSize / 2,
            lockArea.getY(),
            tempoKnobSize,
            tempoKnobSize);

        tempoLockStrengthLabel.setBounds (
            lockArea.getX(),
            lockArea.getY() + tempoKnobSize + 2,
            lockArea.getWidth(),
            22);

        return;
    }

    auto area = getLocalBounds().reduced (24, 18);

    const bool lockOn = scaleLockButton.getToggleState();

    // Main-page conceptual grid:
    // HEADER / MAIN VALVES / UTILITY STRIP / INSTRUMENT STRIP / TITLE.
    auto headerArea = area.removeFromTop (78);
    area.removeFromTop (10);

    auto titleArea = area.removeFromBottom (38);
    auto instrumentArea = area.removeFromBottom (112);

    // La utility strip appartiene ancora al gesto audio, ma deve stare più in basso
    // e non mordere i pannelli dei due controlli principali.
    // Scendiamo ancora: meno gap verso il cruscotto, più respiro dai rubinetti.
    area.removeFromBottom (4);
    auto utilityArea = area.removeFromBottom (72);
    area.removeFromBottom (27); // separazione dai rubinetti principali

    auto mainControlsArea = area;

    // ==================== Header: musical selection only ====================
    auto firstRow = headerArea.removeFromTop (30);
    auto secondRow = headerArea.removeFromTop (30);

    auto tempoArea = firstRow.getUnion (secondRow).removeFromRight (112);
    tempoPageButton.setBounds (tempoArea.reduced (4, 8));

    firstRow.removeFromRight (120);
    secondRow.removeFromRight (120);

    const int firstGap = 10;
    auto presetArea = firstRow.removeFromLeft ((firstRow.getWidth() - firstGap) / 2);
    firstRow.removeFromLeft (firstGap);
    auto scaleArea = firstRow;

    presetSelectorLabel.setBounds (presetArea.removeFromLeft (62));
    presetSelector.setBounds (presetArea.reduced (0, 1));

    scaleSelectorLabel.setBounds (scaleArea.removeFromLeft (54));
    scaleSelector.setBounds (scaleArea.reduced (0, 1));

    const int secondGap = 10;
    auto noteArea = secondRow.removeFromLeft ((secondRow.getWidth() - secondGap) / 2);
    secondRow.removeFromLeft (secondGap);
    auto modeArea = secondRow;

    rootNoteSelectorLabel.setBounds (noteArea.removeFromLeft (54));
    rootNoteSelector.setBounds (noteArea.reduced (0, 1));

    modeSelectorLabel.setBounds (modeArea.removeFromLeft (54));
    modeSelector.setBounds (modeArea.reduced (0, 1));

    // ==================== Main valves ====================
    auto controls = mainControlsArea.reduced (4, 0);

    const int third = controls.getWidth() / 3;

    auto responseArea = controls.removeFromLeft (third);
    auto humanArea = controls.removeFromLeft (third);
    auto amountArea = controls;

    const auto placeLargeValve = [] (juce::Slider& knob,
                                     juce::Label& label,
                                     juce::Rectangle<int> bounds)
    {
        bounds = bounds.reduced (6, 0);

        const int labelH = 24;
        const int availableH = juce::jmax (82, bounds.getHeight() - labelH);
        const int knobSize = juce::jlimit (
            90, 150, juce::jmin (bounds.getWidth(), availableH));

        const int x = bounds.getCentreX() - knobSize / 2;
        const int y = bounds.getY()
            + juce::jmax (0, (bounds.getHeight() - labelH - knobSize) / 2);

        knob.setBounds (x, y, knobSize, knobSize);
        label.setBounds (bounds.getX(),
                         y + knobSize + 2,
                         bounds.getWidth(),
                         labelH);
    };

    const auto placeSmallValve = [] (juce::Slider& knob,
                                     juce::Label& label,
                                     juce::Rectangle<int> bounds)
    {
        bounds = bounds.reduced (8, 0);

        const int labelH = 24;
        const int availableH = juce::jmax (58, bounds.getHeight() - labelH);
        const int knobSize = juce::jlimit (
            62, 84, juce::jmin (bounds.getWidth(), availableH));

        const int x = bounds.getCentreX() - knobSize / 2;
        const int y = bounds.getY()
            + juce::jmax (0, (bounds.getHeight() - labelH - knobSize) / 2);

        knob.setBounds (x, y, knobSize, knobSize);
        label.setBounds (bounds.getX(),
                         y + knobSize + 2,
                         bounds.getWidth(),
                         labelH);
    };

    placeLargeValve (speedKnob, speedLabel, responseArea);
    placeSmallValve (humanizeSlider, humanizeLabel, humanArea);
    placeLargeValve (amountKnob, amountLabel, amountArea);

    // ==================== Utility strip ====================
    auto utility = utilityArea.reduced (4, 2);

    // Two modules:
    // - left module: roughly 3/4, Scale Lock and its dependent controls
    // - right module: roughly 1/4, final Output Stage
    constexpr int moduleGap = 14; // slightly clearer separation between utility modules

    const int outputModuleW = juce::jlimit (
        142, 170, utility.getWidth() / 4);

    auto outputModule = utility.removeFromRight (outputModuleW);
    utility.removeFromRight (moduleGap);
    auto lockModule = utility;

    // ---- Scale Lock module ----
    auto lockRow = lockModule.removeFromTop (30);

    scaleLockButton.setBounds (
        lockRow.removeFromLeft (124).reduced (2, 2));

    if (lockOn)
    {
        lockRow.removeFromLeft (8);

        auto holdArea = lockRow.reduced (4, 2);
        lockHysteresisLabel.setBounds (holdArea.removeFromLeft (52));
        lockHysteresisSlider.setBounds (holdArea);

        lockModule.removeFromTop (6);
        auto vibratoRow = lockModule.removeFromTop (28).reduced (4, 2);
        vibratoPreserveLabel.setBounds (vibratoRow.removeFromLeft (184)); // align slider start with Hold
        vibratoPreserveSlider.setBounds (vibratoRow);
    }
    else
    {
        lockHysteresisLabel.setBounds (juce::Rectangle<int>());
        lockHysteresisSlider.setBounds (juce::Rectangle<int>());
        vibratoPreserveLabel.setBounds (juce::Rectangle<int>());
        vibratoPreserveSlider.setBounds (juce::Rectangle<int>());
    }

    // ---- Output Stage module ----
    auto outputStage = outputModule.reduced (4, 2);

    auto analogRow = outputStage.removeFromTop (24);
    analogModeButton.setBounds (analogRow.reduced (2, 1));

    outputStage.removeFromTop (6);

    auto outRow = outputStage;
    outVolumeLabel.setBounds (outRow.removeFromLeft (52).reduced (0, 2));

    const int outputKnobSize = juce::jlimit (
        34, 40, juce::jmin (outRow.getWidth(), outRow.getHeight()));

    outVolumeSlider.setBounds (
        outRow.withSizeKeepingCentre (outputKnobSize, outputKnobSize));

    // ==================== Instrument strip navigation ==================
    // Control Room appartiene al cruscotto: sotto questa soglia non si gestisce
    // più direttamente l'audio, si osserva la macchina.
    auto instrumentContent = instrumentArea.reduced (18, 4);
    const int sideMeterW = juce::jlimit (
        128, 164, instrumentContent.getWidth() / 4);

    instrumentContent.removeFromLeft (sideMeterW);
    instrumentContent.removeFromRight (sideMeterW);

    auto targetColumn = instrumentContent.reduced (8, 0);
    auto controlRoomArea = targetColumn.removeFromTop (32);
    controlRoomButton.setBounds (
        controlRoomArea.withSizeKeepingCentre (128, 28));
}


void MicrotonalAutotuneAudioProcessorEditor::onRootNoteSelected()
{
    int selectedId = rootNoteSelector.getSelectedId();
    if (selectedId > 0)
    {
        processorRef.rootNoteIndex.store (selectedId - 1);
        processorRef.refreshScaleSnapshot();
    }
}

void MicrotonalAutotuneAudioProcessorEditor::onModeSelected()
{
    int selectedId = modeSelector.getSelectedId();
    if (selectedId > 0)
    {
        int newMode = selectedId - 1; // ComboBox ID 1-4 → mode 0-3
        processorRef.updateProcessingMode (newMode);
        repaint(); // refresh the mode indicator dot
    }
}
