#pragma once

#include <JuceHeader.h>
#include "CustomScalePresets.h"
#include <set>
#include <vector>

//==============================================================================
// Forward declaration
class MicrotonalAutotuneAudioProcessor;

// Callback interface for when the editor should close
class CustomScaleEditorListener
{
public:
    virtual ~CustomScaleEditorListener() = default;
    virtual void customScaleEditorClosed() = 0;
};

//==============================================================================
class CustomScaleEditor : public juce::Component
{
public:
    CustomScaleEditor (MicrotonalAutotuneAudioProcessor& processor,
                       CustomScaleEditorListener& listener,
                       juce::Image backgroundImage);
    ~CustomScaleEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& event) override;

private:
    enum class SpacingMode
    {
        equal = 1,
        linear,
        exponential,
        logarithmic,
        cosine,
        power,
        inversePower
    };

    MicrotonalAutotuneAudioProcessor& processorRef;
    CustomScaleEditorListener& listenerRef;
    juce::Image bgImage;

    // The octave rectangle area (set in resized)
    juce::Rectangle<int> octaveRect;

    // Division positions as ratios [0, 1] in log space.
    // Each value is a position in [0, 1] representing log2(ratio).
    // Boundaries 0.0 (unison) and 1.0 (octave) are implicit.
    std::vector<double> divisions;

    // Interval indices muted by Ctrl/Command-click.
    // For interval count N, valid indices are [0, N - 1].
    std::set<int> excludedIntervalIndices;

    static constexpr int minScaleRatios = 3;
    static constexpr int maxScaleRatios = 33;
    static constexpr double manualMinDistance = 0.01; // ~12 cents

    // UI components
    juce::TextEditor nameEditor;
    juce::TextButton saveButton     { "Salva" };
    juce::TextButton backButton     { "Indietro" };
    juce::TextButton generateButton { "Genera" };
    juce::TextButton clearButton    { "Pulisci" };

    juce::Label titleLabel;
    juce::Label nameLabel;
    juce::Label octaveLabel;
    juce::Label infoLabel;

    juce::Label divisionCountLabel;
    juce::ComboBox divisionCountSelector;

    juce::Label spacingLabel;
    juce::ComboBox spacingSelector;

    juce::Label exponentLabel;
    juce::TextEditor exponentEditor;

    void updateInfoLabel();
    void onSave();

    void buildGeneratorMenus();
    void generateScaleFromControls();
    void clearScale();
    void updateExponentControlState();

    double getExponentFromEditor() const;
    int getSelectedIntervalCount() const;
    SpacingMode getSelectedSpacingMode() const;

    std::vector<double> buildIntervalBoundaries() const;
    int findClosestDivisionIndex (double logPos, double maxDistance) const;
    int findIntervalIndexAt (double logPos) const;
    int getEffectiveRatioCount() const;
    bool shouldSkipDivisionOnSave (int divisionIndex) const;
    void removeDivisionAtIndex (int divisionIndex);
    void toggleExcludedIntervalAt (double logPos);
    void sanitiseExcludedIntervals();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CustomScaleEditor)
};
