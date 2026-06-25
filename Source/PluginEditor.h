#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CustomScaleEditor.h"

//==============================================================================
class ModernLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ModernLookAndFeel();
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) override;
    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox& box) override;
    void drawTickBox (juce::Graphics& g, juce::Component& component,
                      float x, float y, float w, float h,
                      const bool ticked,
                      const bool isEnabled,
                      const bool shouldDrawButtonAsHighlighted,
                      const bool shouldDrawButtonAsDown) override;
};

//==============================================================================
class MicrotonalAutotuneAudioProcessorEditor : public juce::AudioProcessorEditor,
                                                public CustomScaleEditorListener,
                                                private juce::Timer
{
public:
    explicit MicrotonalAutotuneAudioProcessorEditor (MicrotonalAutotuneAudioProcessor&);
    ~MicrotonalAutotuneAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // CustomScaleEditorListener
    void customScaleEditorClosed() override;

private:
    ModernLookAndFeel modernLookAndFeel;
    MicrotonalAutotuneAudioProcessor& processorRef;

    // Background image
    juce::Image bgImage;
    juce::Image bgImageScaleEditor;

    // Main page components
    juce::ComboBox scaleSelector;
    juce::Label scaleSelectorLabel;

    juce::ComboBox rootNoteSelector;
    juce::Label rootNoteSelectorLabel;

    juce::Slider speedKnob;
    juce::Label  speedLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttachment;

    juce::Slider amountKnob;
    juce::Label  amountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;

    juce::Slider humanizeSlider;
    juce::Label  humanizeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humanizeAttachment;

    // Scale Lock & Analog Mode
    juce::ToggleButton scaleLockButton { "Scale Lock" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> scaleLockAttachment;

    juce::Slider lockHysteresisSlider;
    juce::Label lockHysteresisLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lockHysteresisAttachment;

    juce::Slider vibratoPreserveSlider;
    juce::Label vibratoPreserveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> vibratoPreserveAttachment;

    juce::ToggleButton analogModeButton { "Analog Mode" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> analogModeAttachment;

    juce::Slider outVolumeSlider;
    juce::Label outVolumeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outVolumeAttachment;

    // Custom scale editor (shown/hidden)
    std::unique_ptr<CustomScaleEditor> customScaleEditorPage;
    bool showingScaleEditor = false;

    // Processing mode selector (replaces old LIVE button)
    juce::ComboBox modeSelector;
    juce::Label modeSelectorLabel;

    // Third page: creative tempo controls. This is intentionally separate
    // from the main processing-mode selector.
    juce::TextButton tempoPageButton { "Tempo" };
    juce::TextButton tempoBackButton { "Indietro" };
    juce::TextButton tempoOffButton { "Off" };
    juce::TextButton tempoGlideButton { "Tempo Glide" };
    juce::TextButton glideLockButton { "Glide Lock" };
    juce::ComboBox tempoDivisionSelector;
    juce::Label tempoDivisionLabel;
    juce::Slider tempoGlideLength;
    juce::Label tempoGlideLengthLabel;
    juce::Slider tempoLockStrength;
    juce::Label tempoLockStrengthLabel;
    juce::ToggleButton tempoSmartOnset { "Smart onset" };

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        tempoDivisionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        tempoGlideLengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        tempoLockStrengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        tempoSmartOnsetAttachment;
    bool showingTempoPage = false;

    // Methods
    void buildScaleMenu();
    void onScaleSelected();
    void showCustomScaleEditor();
    void showTempoPage();
    void closeTempoPage();
    void setMainControlsVisible(bool shouldBeVisible);
    void setTempoControlsVisible(bool shouldBeVisible);
    void setTempoModeParameter(int modeIndex);
    void updateTempoModeButtons();
    void onRootNoteSelected();
    void onModeSelected();

    void timerCallback() override;
    [[nodiscard]] static juce::String trackingStateToString (
        ModernPitchEngine::TrackingState state);
    void drawMeterPanel (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawTempoPage (juce::Graphics& g, juce::Rectangle<int> bounds);

    // State tracking for optimized timer updates
    bool lastScaleLockState_ = false;
    bool lastAnalogModeState_ = false;

    LivePitchProcessor::Metering displayedMetering;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrotonalAutotuneAudioProcessorEditor)
};
