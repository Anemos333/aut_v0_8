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

    juce::Label compatibilityLabel;

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
    void updatePresetPresentation();
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
    void updateMeterPresentation();

    bool lastAnalogModeState_ = false;

    LivePitchProcessor::Metering displayedMetering;
    float visualCorrectionGlowCents_ = 0.0f;
    neumaton::ui::ScaleDegreeDisplay displayedDegree;

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

            owner.updatePresetPresentation();

            const auto scaleStableId = owner.processorRef.getCurrentScaleStableId();
            if (scaleStableId != lastScaleStableId_)
            {
                lastScaleStableId_ = scaleStableId;
                owner.buildScaleMenu();
                owner.updateCenterPresentation();
            }

            owner.humanizeSlider.setEnabled (true);
            owner.humanizeLabel.setEnabled (true);
            // The two repurposed controls use fresh parameter IDs so old
            // session Hold/Vibrato values cannot silently change the sound.
            owner.lockHysteresisSlider.setEnabled (true);
            owner.lockHysteresisLabel.setEnabled (true);
            const bool analogEnabled = owner.processorRef.getAPVTS()
                .getRawParameterValue ("analogMode")->load() > 0.5f;
            owner.vibratoPreserveSlider.setEnabled (analogEnabled);
            owner.vibratoPreserveLabel.setEnabled (analogEnabled);
            owner.compatibilityLabel.setVisible (mainPage);
            owner.lockHysteresisSlider.setVisible (mainPage);
            owner.lockHysteresisLabel.setVisible (mainPage);
            owner.vibratoPreserveSlider.setVisible (mainPage);
            owner.vibratoPreserveLabel.setVisible (mainPage);
        }

        MicrotonalAutotuneAudioProcessorEditor& owner;
        juce::String lastScaleStableId_;
    };

    AudioControlAvailabilityGuard audioControlAvailabilityGuard_ { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrotonalAutotuneAudioProcessorEditor)
};
