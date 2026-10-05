#pragma once

#include <cstdint>

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

    juce::ComboBox presetSelector;
    juce::Label presetSelectorLabel;

    juce::ComboBox scaleSelector;
    juce::Label scaleSelectorLabel;

    // V1 musical placement model: the centre/anchor is separate from A4 tuning.
    juce::ComboBox centerSelector;
    juce::Label centerSelectorLabel;

    juce::Slider tuningSlider;
    juce::Label tuningLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tuningAttachment;

    juce::ComboBox modeSelector;
    juce::Label modeSelectorLabel;

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

    juce::TextButton controlRoomButton { "Control room" };
    ControlRoomPage controlRoomPage;
    bool showingControlRoom = false;

    void buildPresetMenu();
    void onPresetSelected();
    void buildScaleMenu();
    void onScaleSelected();
    void updateCenterPresentation();
    void onCenterSelected();
    void onModeSelected();

    void showCustomScaleEditor();
    void showControlRoom();
    void closeControlRoom();
    void setMainControlsVisible (bool shouldBeVisible);

    void timerCallback() override;
    [[nodiscard]] static juce::String trackingStateToString (
        LivePitchProcessor::TrackingState state);

    bool lastScaleLockState_ = false;
    bool lastAnalogModeState_ = false;

    LivePitchProcessor::Metering displayedMetering;
    float visualCorrectionGlowCents_ = 0.0f;
    float visualConsensusGlow_ = 0.0f;

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
        void timerCallback() override { refresh(); }

        void refresh()
        {
            const int processingMode = owner.processorRef.processingMode.load();
            const int centerIndex = juce::jlimit (
                0, 11, owner.processorRef.tonalCenterIndex.load());
            const bool scaleLockActive = owner.scaleLockButton.getToggleState();
            const bool mainPage = ! owner.showingScaleEditor
                && ! owner.showingControlRoom;

            // Community presets/scenes can update non-APVTS musical state while
            // this editor remains open. Keep the visible controls authoritative
            // with the processor without generating another user-change callback.
            if (owner.centerSelector.getSelectedId() != centerIndex + 1)
                owner.centerSelector.setSelectedId (
                    centerIndex + 1, juce::dontSendNotification);

            if (owner.modeSelector.getSelectedId() != processingMode)
                owner.modeSelector.setSelectedId (
                    processingMode, juce::dontSendNotification);

            const auto scaleStableId = owner.processorRef.getCurrentScaleStableId();
            if (scaleStableId != lastScaleStableId_)
            {
                lastScaleStableId_ = scaleStableId;
                owner.buildScaleMenu();
                owner.updateCenterPresentation();
            }

            owner.humanizeSlider.setEnabled (true);
            owner.humanizeLabel.setEnabled (true);
            owner.scaleLockButton.setEnabled (true);

            owner.lockHysteresisSlider.setEnabled (scaleLockActive);
            owner.lockHysteresisLabel.setEnabled (scaleLockActive);
            owner.vibratoPreserveSlider.setEnabled (scaleLockActive);
            owner.vibratoPreserveLabel.setEnabled (scaleLockActive);

            owner.lockHysteresisSlider.setVisible (mainPage && scaleLockActive);
            owner.lockHysteresisLabel.setVisible (mainPage && scaleLockActive);
            owner.vibratoPreserveSlider.setVisible (mainPage && scaleLockActive);
            owner.vibratoPreserveLabel.setVisible (mainPage && scaleLockActive);

            const bool lockState = owner.scaleLockButton.getToggleState();
            if (processingMode != lastProcessingMode_
                || lockState != lastScaleLockState_)
            {
                lastProcessingMode_ = processingMode;
                lastScaleLockState_ = lockState;
                owner.speedKnob.updateText();
            }
        }

        MicrotonalAutotuneAudioProcessorEditor& owner;
        int lastProcessingMode_ = -1;
        bool lastScaleLockState_ = false;
        juce::String lastScaleStableId_;
    };

    AudioControlAvailabilityGuard audioControlAvailabilityGuard_ { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrotonalAutotuneAudioProcessorEditor)
};
