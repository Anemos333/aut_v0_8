#pragma once

#include <JuceHeader.h>
#include "CustomScalePresets.h"
#include "ScaleEditorGeometry.h"

#include <vector>

class MicrotonalAutotuneAudioProcessor;

class CustomScaleEditorListener
{
public:
    virtual ~CustomScaleEditorListener() = default;
    virtual void customScaleEditorClosed() = 0;
};

class CustomScaleEditor final : public juce::Component
{
public:
    CustomScaleEditor (MicrotonalAutotuneAudioProcessor& processor,
                       CustomScaleEditorListener& listener,
                       juce::Image backgroundImage);
    ~CustomScaleEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;

private:
    enum class ValueMode
    {
        cents = 1,
        ratio = 2
    };

    MicrotonalAutotuneAudioProcessor& processorRef;
    CustomScaleEditorListener& listenerRef;
    juce::Image bgImage;

    neumaton::scaleeditor::Geometry geometry_;
    neumaton::scaleeditor::Geometry dragStartGeometry_;
    double equaveRatio_ = 2.0;
    int selectedDegree_ = -1;
    int draggingDegree_ = -1;
    bool draggingWarp_ = false;

    juce::Rectangle<int> scaleRail_;
    juce::String statusMessage_;

    juce::Label titleLabel_;
    juce::Label helpLabel_;
    juce::Label nameLabel_;
    juce::TextEditor nameEditor_;

    juce::Label equaveLabel_;
    juce::TextEditor equaveEditor_;
    juce::Label equaveHintLabel_;

    juce::Label stepsLabel_;
    juce::TextEditor stepsEditor_;
    juce::TextButton divideButton_ { "Divide" };
    juce::TextButton redistributeButton_ { "Redistribute free" };
    juce::TextButton clearButton_ { "Clear" };

    juce::Label valueLabel_;
    juce::ComboBox valueModeSelector_;
    juce::TextEditor valueEditor_;
    juce::ToggleButton lockToggle_ { "Locked" };
    juce::ToggleButton includeToggle_ { "Included" };
    juce::ToggleButton snapToggle_ { "Snap 1c" };

    juce::Label infoLabel_;
    juce::TextButton saveButton_ { "Save scale" };
    juce::TextButton backButton_ { "Back" };

    void configureUi();
    void applyEqualDivision();
    void redistributeFreeDegrees();
    void clearScale();
    void applyEquaveEditor();
    void applySelectedValueEditor();
    void onSave();

    void selectDegree (int index);
    void refreshSelectedControls();
    void refreshInfo();
    void updateStepsEditorFromGeometry();

    [[nodiscard]] int findDegreeAt (juce::Point<int> point, float tolerancePixels = 10.0f) const;
    [[nodiscard]] double phaseFromX (float x) const noexcept;
    [[nodiscard]] float xFromPhase (double phase) const noexcept;
    [[nodiscard]] double equaveCents() const noexcept;
    [[nodiscard]] double selectedRatio() const noexcept;
    [[nodiscard]] double selectedCents() const noexcept;
    [[nodiscard]] ValueMode valueMode() const noexcept;
    [[nodiscard]] double snappedPhase (double phase) const noexcept;

    static bool parsePositiveRatio (juce::String text, double& result);
    static juce::String formatRatio (double ratio);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustomScaleEditor)
};
