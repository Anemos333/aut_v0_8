#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "CustomScaleEditor.h"
#include "NeumatonLabTheme.h"
#include "HumanDriftLookAndFeel.h"
#include "ControlRoomPage.h"

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

    void customScaleEditorClosed() override;

private:
    ModernLookAndFeel modernLookAndFeel;
    HumanDriftLookAndFeel humanDriftLookAndFeel;
    neumaton::lab::MainValveLookAndFeel mainValveLookAndFeel;
    neumaton::lab::OutputKnobLookAndFeel outputKnobLookAndFeel;
    neumaton::lab::UtilityRailSliderLookAndFeel utilityRailLookAndFeel {
        neumaton::lab::UtilityRailSliderLookAndFeel::Options {
            false,
            false,
            true,
            1.0f
        }
    };
    neumaton::lab::LabLeverToggleLookAndFeel scaleLockLeverLookAndFeel;

    neumaton::lab::LabLeverToggleLookAndFeel analogLeverLookAndFeel {
        neumaton::lab::LabLeverToggleLookAndFeel::Options {
            true,
            false,
            0.92f
        }
    };

    MicrotonalAutotuneAudioProcessor& processorRef;

    juce::Image bgImage;
    juce::Image bgImageScaleEditor;

    juce::ComboBox scaleSelector;
    juce::Label scaleSelectorLabel;

    juce::ComboBox rootNoteSelector;
    juce::Label rootNoteSelectorLabel;

    juce::Slider speedKnob;
    juce::Label speedLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttachment;

    juce::Slider amountKnob;
    juce::Label amountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;

    juce::Slider humanizeSlider;
    juce::Label humanizeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> humanizeAttachment;

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

    std::unique_ptr<CustomScaleEditor> customScaleEditorPage;
    bool showingScaleEditor = false;

    juce::ComboBox modeSelector;
    juce::Label modeSelectorLabel;
    juce::ComboBox presetSelector;
    juce::Label presetSelectorLabel;
    juce::TextButton controlRoomButton { "Control room" };
    ControlRoomPage controlRoomPage;
    bool showingControlRoom = false;
    void showControlRoom();
    void closeControlRoom();

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
    void buildPresetMenu();
    void onPresetSelected();

    void timerCallback() override;
    [[nodiscard]] static juce::String trackingStateToString (
        ModernPitchEngine::TrackingState state);
    void drawMeterPanel (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawTempoPage (juce::Graphics& g, juce::Rectangle<int> bounds);

    bool lastScaleLockState_ = false;
    bool lastAnalogModeState_ = false;

    LivePitchProcessor::Metering displayedMetering;
    float visualCorrectionGlowCents_ = 0.0f;
    float visualConsensusGlow_ = 0.0f;

    // No placebo controls: this guard changes UI applicability only. It never
    // rewrites parameter values, sensor authority or correction depth.
    class AudioControlAvailabilityGuard final : private juce::Timer
    {
    public:
        explicit AudioControlAvailabilityGuard (
            MicrotonalAutotuneAudioProcessorEditor& editor)
            : owner (editor)
        {
            refresh();
            startTimerHz (20);
        }

        ~AudioControlAvailabilityGuard() override
        {
            stopTimer();
        }

    private:
        void timerCallback() override
        {
            refresh();
        }

        void refresh()
        {
            const int processingMode = owner.processorRef.processingMode.load();
            const bool modernMode = processingMode > 0;
            const bool scaleLockActive = modernMode
                && owner.scaleLockButton.getToggleState();
            const bool mainPage = !owner.showingTempoPage
                && !owner.showingScaleEditor
                && !owner.showingControlRoom;

            // High Latency intentionally stays the untouched legacy YIN path.
            // Controls not consumed there are disabled instead of becoming
            // placebo controls. Scale/root/Response/Amount/Analog/Output remain
            // enabled because the legacy path genuinely consumes them.
            owner.humanizeSlider.setEnabled (modernMode);
            owner.humanizeLabel.setEnabled (modernMode);
            owner.scaleLockButton.setEnabled (modernMode);
            owner.tempoPageButton.setEnabled (modernMode);

            owner.lockHysteresisSlider.setEnabled (scaleLockActive);
            owner.lockHysteresisLabel.setEnabled (scaleLockActive);
            owner.vibratoPreserveSlider.setEnabled (scaleLockActive);
            owner.vibratoPreserveLabel.setEnabled (scaleLockActive);

            owner.lockHysteresisSlider.setVisible (mainPage && scaleLockActive);
            owner.lockHysteresisLabel.setVisible (mainPage && scaleLockActive);
            owner.vibratoPreserveSlider.setVisible (mainPage && scaleLockActive);
            owner.vibratoPreserveLabel.setVisible (mainPage && scaleLockActive);

            const int tempoMode = juce::jlimit (0, 2,
                static_cast<int> (std::lround (
                    owner.processorRef.getAPVTS()
                        .getRawParameterValue ("tempoMode")->load())));
            const bool tempoShapesTrajectory = modernMode && tempoMode != 0;
            const bool glideLockMode = modernMode && tempoMode == 2;

            owner.tempoOffButton.setEnabled (modernMode);
            owner.tempoGlideButton.setEnabled (modernMode);
            owner.glideLockButton.setEnabled (modernMode);
            owner.tempoDivisionSelector.setEnabled (tempoShapesTrajectory);
            owner.tempoDivisionLabel.setEnabled (tempoShapesTrajectory);
            owner.tempoGlideLength.setEnabled (tempoShapesTrajectory);
            owner.tempoGlideLengthLabel.setEnabled (tempoShapesTrajectory);
            owner.tempoLockStrength.setEnabled (glideLockMode);
            owner.tempoLockStrengthLabel.setEnabled (glideLockMode);
            owner.tempoSmartOnset.setEnabled (glideLockMode);

            if (!modernMode && owner.showingTempoPage)
                owner.closeTempoPage();

            const bool lockState = owner.scaleLockButton.getToggleState();
            if (processingMode != lastProcessingMode_
                || lockState != lastScaleLockState_)
            {
                lastProcessingMode_ = processingMode;
                lastScaleLockState_ = lockState;
                // PluginEditor.cpp already maps the displayed Response value to
                // the exact Scale Lock response curve used by the engine.
                owner.speedKnob.updateText();
            }
        }

        MicrotonalAutotuneAudioProcessorEditor& owner;
        int lastProcessingMode_ = -1;
        bool lastScaleLockState_ = false;
    };

    AudioControlAvailabilityGuard audioControlAvailabilityGuard_ { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrotonalAutotuneAudioProcessorEditor)
};
