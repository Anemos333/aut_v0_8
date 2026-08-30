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

    // CustomScaleEditorListener
    void customScaleEditorClosed() override;

private:
    ModernLookAndFeel modernLookAndFeel;
    HumanDriftLookAndFeel humanDriftLookAndFeel;
    neumaton::lab::MainValveLookAndFeel mainValveLookAndFeel;
    neumaton::lab::OutputKnobLookAndFeel outputKnobLookAndFeel;
    neumaton::lab::UtilityRailSliderLookAndFeel utilityRailLookAndFeel {
        neumaton::lab::UtilityRailSliderLookAndFeel::Options {
            false,  // perspectiveScale
            false,  // showEndLabels
            true,   // strongGlow
            1.0f    // thumbScale
        }
    };
    neumaton::lab::LabLeverToggleLookAndFeel scaleLockLeverLookAndFeel;

    neumaton::lab::LabLeverToggleLookAndFeel analogLeverLookAndFeel {
        neumaton::lab::LabLeverToggleLookAndFeel::Options {
            true,   // compact
            false,  // dangerOff
            0.92f   // leverScale
        }
    };

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
    juce::Label speedLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttachment;

    juce::Slider amountKnob;
    juce::Label amountLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;

    juce::Slider humanizeSlider;
    juce::Label humanizeLabel;
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
    juce::ComboBox presetSelector;
    juce::Label presetSelectorLabel;
    juce::TextButton controlRoomButton { "Control room" };
    ControlRoomPage controlRoomPage;
    bool showingControlRoom = false;
    void showControlRoom();
    void closeControlRoom();

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
    void buildPresetMenu();
    void onPresetSelected();

    void timerCallback() override;
    [[nodiscard]] static juce::String trackingStateToString (
        ModernPitchEngine::TrackingState state);
    void drawMeterPanel (juce::Graphics& g, juce::Rectangle<int> bounds);
    void drawTempoPage (juce::Graphics& g, juce::Rectangle<int> bounds);

    // State tracking for optimized timer updates
    bool lastScaleLockState_ = false;
    bool lastAnalogModeState_ = false;

    LivePitchProcessor::Metering displayedMetering;
    float visualCorrectionGlowCents_ = 0.0f;
    float visualConsensusGlow_ = 0.0f;

    // A GUI control is never allowed to look active while its owning DSP
    // function cannot affect audio. The legacy High Latency/YIN path remains
    // intentionally unchanged, so modern-only controls are disabled there.
    // Likewise Tempo sub-controls are enabled only in the modes that consume
    // them. This guard changes UI applicability only; it never changes an
    // audio parameter value or correction authority.
    class AudioControlAvailabilityGuard final : private juce::Timer
    {
    public:
        explicit AudioControlAvailabilityGuard (
            MicrotonalAutotuneAudioProcessorEditor& editor)
            : owner (editor)
        {
            installSpeedTextMapping();
            refresh();
            startTimerHz (20);
        }

        ~AudioControlAvailabilityGuard() override
        {
            stopTimer();
        }

    private:
        void installSpeedTextMapping()
        {
            owner.speedKnob.textFromValueFunction = [this] (double value)
            {
                if (owner.scaleLockButton.getToggleState())
                {
                    const int mode = owner.processorRef.processingMode.load();
                    if (mode > 0)
                    {
                        const double norm = std::pow (
                            juce::jlimit (0.0, 1.0, value / 500.0), 1.35);
                        const double mapped = mode == 1 ? 3.0 + 92.0 * norm
                            : mode == 2 ? 1.5 + 63.5 * norm
                                        : 0.35 + 39.65 * norm;
                        return juce::String (mapped, 2) + " ms";
                    }
                }
                return juce::String (value, 1) + " ms";
            };
        }

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

            owner.humanizeSlider.setEnabled (modernMode);
            owner.humanizeLabel.setEnabled (modernMode);
            owner.scaleLockButton.setEnabled (modernMode);
            owner.tempoPageButton.setEnabled (modernMode);

            owner.lockHysteresisSlider.setEnabled (scaleLockActive);
            owner.lockHysteresisLabel.setEnabled (scaleLockActive);
            owner.vibratoPreserveSlider.setEnabled (scaleLockActive);
            owner.vibratoPreserveLabel.setEnabled (scaleLockActive);

            // Dependent Scale Lock controls are not merely greyed out: when
            // High Latency owns the audio path they disappear from the active
            // control surface, preventing a placebo interaction.
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

            // If a host automation switches to the untouched legacy mode while
            // the Tempo page is open, return to the main page rather than leave
            // an apparently active page with no DSP consumer.
            if (!modernMode && owner.showingTempoPage)
                owner.closeTempoPage();

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
    };

    // Declared last so every component it touches is already constructed.
    AudioControlAvailabilityGuard audioControlAvailabilityGuard_ { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrotonalAutotuneAudioProcessorEditor)
};
