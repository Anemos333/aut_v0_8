#pragma once

#include <JuceHeader.h>

// First UI chantier for Neumaton.
// This file intentionally contains only drawing helpers: no APVTS attachments,
// no DSP state, no audio-thread interaction.  It is safe to iterate artistically.
namespace neumaton::lab
{
struct Palette
{
    juce::Colour backgroundTop;
    juce::Colour backgroundBottom;
    juce::Colour panelDark;
    juce::Colour panelEdge;
    juce::Colour brass;
    juce::Colour brassDark;
    juce::Colour glass;
    juce::Colour glassEdge;
    juce::Colour ink;
    juce::Colour parchment;
    juce::Colour warningRed;
    juce::Colour greenFluid;
    juce::Colour blueGlow;
};

[[nodiscard]] const Palette& palette() noexcept;

class MainValveLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPos,
                           const float rotaryStartAngle,
                           const float rotaryEndAngle,
                           juce::Slider& slider) override;
};

class Painter final
{
public:
    static void drawBackground (juce::Graphics& g,
                                juce::Rectangle<int> bounds,
                                const juce::Image& placeholderBackground);

    static void drawPanel (juce::Graphics& g,
                           juce::Rectangle<float> bounds,
                           float corner = 12.0f,
                           float alpha = 1.0f);

    static void drawNeedleMeter (juce::Graphics& g,
                                 juce::Rectangle<float> bounds,
                                 float normalisedValue,
                                 const juce::String& title,
                                 const juce::String& valueText,
                                 juce::Colour accent);

    static void drawCorrectionGauge (juce::Graphics& g,
                                     juce::Rectangle<int> bounds,
                                     float correctionCents);

    static void drawConsensusGauge (juce::Graphics& g,
                                    juce::Rectangle<int> bounds,
                                    float consensus);

    static void drawRadioTarget (juce::Graphics& g,
                                 juce::Rectangle<int> bounds,
                                 float detectedHz,
                                 float targetHz,
                                 float minimumHz = 55.0f,
                                 float maximumHz = 1760.0f);

    static void drawLeverSwitch (juce::Graphics& g,
                                 juce::Rectangle<int> bounds,
                                 bool isOn,
                                 const juce::String& label);

    static void drawGlassBar (juce::Graphics& g,
                              juce::Rectangle<int> bounds,
                              float normalisedValue,
                              const juce::String& label,
                              juce::Colour fillColour);

    [[nodiscard]] static float frequencyToLogPosition (float hz,
                                                       float minimumHz,
                                                       float maximumHz) noexcept;
};
class UtilityRailSliderLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    struct Options
    {
        bool perspectiveScale = false; // per Tempo Mode, non per utility bar
        bool showEndLabels    = false; // mostra 0 e 100
        bool strongGlow       = false; // più futuristico/elettrico
        float thumbScale      = 1.0f;  // grandezza cursore
    };

    UtilityRailSliderLookAndFeel();
    explicit UtilityRailSliderLookAndFeel (Options optionsToUse);

    void drawLinearSlider (juce::Graphics& g,
                           int x,
                           int y,
                           int width,
                           int height,
                           float sliderPos,
                           float minSliderPos,
                           float maxSliderPos,
                           const juce::Slider::SliderStyle style,
                           juce::Slider& slider) override;

private:
    Options options;
};
} // namespace neumaton::lab
