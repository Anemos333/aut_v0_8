#pragma once

#include <JuceHeader.h>
#include <cmath>

// HumanDriftLookAndFeel
// -----------------------------------------------------------------------------
// Dedicated drawing for the small Human Drift valve.
//
// Design contract:
// - 0%   is visually at 135 degrees.
// - 50%  is visually at  90 degrees.
// - 100% is visually at  45 degrees.
// - The control is intentionally small and secondary compared to Response and
//   Correction Amount.
// - Everything is drawn procedurally for now: no PNG assets, no Blender render.
//   Texture/patina can be added later after the shape language is approved.

class HumanDriftLookAndFeel final : public juce::LookAndFeel_V4
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
                           juce::Slider& slider) override
    {
        auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);

        if (bounds.isEmpty())
            return;

        const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();

        const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float arcRadius = size * 0.43f;
        const float outerRadius = size * 0.42f;
        const float knobRadius = size * 0.29f;

        const auto brassDark  = juce::Colour (0xFF7A5A27);
        const auto brassMid   = juce::Colour (0xFFB88A3A);
        const auto brassLight = juce::Colour (0xFFE6C57A);
        const auto ink        = juce::Colour (0xFF10131D);
        const auto glassGreen = juce::Colour (0xFF39FF7A);
        const auto softGreen  = juce::Colour (0xFF8CE8B2);

        const auto polar = [centre] (float a, float r)
        {
            return juce::Point<float> (
                centre.x + r * std::cos (a - juce::MathConstants<float>::halfPi),
                centre.y + r * std::sin (a - juce::MathConstants<float>::halfPi));
        };

        // Soft shadow: gives the small control some physical weight without
        // needing a rendered PNG.
        auto shadow = juce::Rectangle<float> (outerRadius * 2.0f, outerRadius * 2.0f)
            .withCentre (centre.translated (0.0f, size * 0.045f));

        g.setColour (juce::Colours::black.withAlpha (0.34f));
        g.fillEllipse (shadow);

        // Outer brass ring.
        auto outer = juce::Rectangle<float> (outerRadius * 2.0f, outerRadius * 2.0f)
            .withCentre (centre);

        juce::ColourGradient brassGradient (
            brassLight, outer.getX(), outer.getY(),
            brassDark,  outer.getRight(), outer.getBottom(),
            true);

        g.setGradientFill (brassGradient);
        g.fillEllipse (outer);

        g.setColour (brassDark.withAlpha (0.95f));
        g.drawEllipse (outer.reduced (0.5f), juce::jmax (1.0f, size * 0.025f));

        // Inner dark plate.
        auto inner = outer.reduced (size * 0.115f);

        g.setColour (ink.withAlpha (0.92f));
        g.fillEllipse (inner);

        g.setColour (brassMid.withAlpha (0.62f));
        g.drawEllipse (inner.reduced (0.5f), juce::jmax (1.0f, size * 0.015f));

        // Background travel arc.
        juce::Path travelArc;
        travelArc.addCentredArc (centre.x,
                                  centre.y,
                                  arcRadius,
                                  arcRadius,
                                  0.0f,
                                  rotaryStartAngle,
                                  rotaryEndAngle,
                                  true);

        g.setColour (juce::Colour (0xFF2C3342));
        g.strokePath (travelArc,
                      juce::PathStrokeType (juce::jmax (2.0f, size * 0.045f),
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

        // Value arc.
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x,
                                 centre.y,
                                 arcRadius,
                                 arcRadius,
                                 0.0f,
                                 rotaryStartAngle,
                                 angle,
                                 true);

        g.setColour (glassGreen.withAlpha (0.30f));
        g.strokePath (valueArc,
                      juce::PathStrokeType (juce::jmax (4.0f, size * 0.070f),
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

        g.setColour (glassGreen.withAlpha (0.95f));
        g.strokePath (valueArc,
                      juce::PathStrokeType (juce::jmax (2.0f, size * 0.040f),
                                            juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

        // Ticks: 0, 25, 50, 75, 100. The three structural values are stronger.
        for (int i = 0; i <= 4; ++i)
        {
            const float t = static_cast<float> (i) / 4.0f;
            const float tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);

            const bool major = (i == 0 || i == 2 || i == 4);
            const float innerTickR = arcRadius - (major ? size * 0.105f : size * 0.075f);
            const float outerTickR = arcRadius + (major ? size * 0.020f : size * 0.005f);

            juce::Colour tickColour = brassMid.withAlpha (0.55f);

if (i == 0)
    tickColour = juce::Colour (0xFF39FF7A); // 0: green
else if (i == 2)
    tickColour = juce::Colours::white.withAlpha (0.95f); // 50: white
else if (i == 4)
    tickColour = juce::Colour (0xFFFF3B3B); // 100: red

g.setColour (tickColour);
            g.drawLine (juce::Line<float> (polar (tickAngle, innerTickR),
                                           polar (tickAngle, outerTickR)),
                        major ? juce::jmax (1.3f, size * 0.018f)
                              : juce::jmax (1.0f, size * 0.012f));
        }

        // The valve handle. It is intentionally simpler than the future final
        // faucet: it already communicates "small valve" and turns correctly.
       juce::Path handle;

const float wingW = knobRadius * 1.18f;
const float wingH = knobRadius * 0.38f;
const float bridgeW = knobRadius * 0.62f;
const float bridgeH = knobRadius * 0.28f;

// ponte centrale
handle.addRoundedRectangle (-bridgeW * 0.5f,
                            -bridgeH * 0.5f,
                             bridgeW,
                             bridgeH,
                             bridgeH * 0.45f);

// ala sinistra
handle.addRoundedRectangle (-wingW,
                            -wingH * 0.5f,
                             wingW * 0.70f,
                             wingH,
                             wingH * 0.28f);

// ala destra
handle.addRoundedRectangle (wingW * 0.30f,
                            -wingH * 0.5f,
                             wingW * 0.70f,
                             wingH,
                             wingH * 0.28f);

        auto handleTransform = juce::AffineTransform::rotation (
        angle - juce::MathConstants<float>::halfPi)
        .translated (centre.x, centre.y);

        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillPath (handle, handleTransform.translated (0.0f, 1.4f));

        g.setColour (brassLight);
        g.fillPath (handle, handleTransform);

        g.setColour (brassDark.withAlpha (0.90f));
        g.strokePath (handle,
                      juce::PathStrokeType (juce::jmax (0.8f, size * 0.012f)),
                      handleTransform);

        // Central cap.
        auto cap = juce::Rectangle<float> (knobRadius * 1.16f, knobRadius * 1.16f)
            .withCentre (centre);

        juce::ColourGradient capGradient (
            brassLight, cap.getX(), cap.getY(),
            brassDark,  cap.getRight(), cap.getBottom(),
            true);

        g.setGradientFill (capGradient);
        g.fillEllipse (cap);

        g.setColour (brassDark.withAlpha (0.95f));
        g.drawEllipse (cap.reduced (0.5f), juce::jmax (1.0f, size * 0.018f));

        auto pin = cap.reduced (knobRadius * 0.34f);
        g.setColour (ink.withAlpha (0.84f));
        g.fillEllipse (pin);

        g.setColour (softGreen.withAlpha (0.90f));
        g.drawEllipse (pin.reduced (0.5f), juce::jmax (1.0f, size * 0.012f));

        // Small value mark. It helps because Human Drift has no text box.
        const bool showValue = slider.isMouseOverOrDragging();

if (showValue && size >= 58.0f)
{
    const int valuePercent = juce::jlimit (
        0, 100, static_cast<int> (std::round (slider.getValue())));

    auto valueBubble = juce::Rectangle<float> (38.0f, 16.0f)
        .withCentre (centre.translated (0.0f, size * 0.34f));

    g.setColour (juce::Colour (0xDD10131D));
    g.fillRoundedRectangle (valueBubble, 5.0f);

    g.setColour (glassGreen.withAlpha (0.85f));
    g.drawRoundedRectangle (valueBubble.reduced (0.5f), 5.0f, 1.0f);

    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    g.setColour (juce::Colours::white.withAlpha (0.95f));
    g.drawFittedText (juce::String (valuePercent) + "%",
                      valueBubble.toNearestInt(),
                      juce::Justification::centred,
                      1);
}

        // Tiny 0 / 50 / 100 labels only when the control has enough room.
        // They are deliberately quiet: the tick geometry must stay more important
        // than the typography.
        if (size >= 74.0f)
        {
            g.setFont (juce::FontOptions (7.5f, juce::Font::plain));
            g.setColour (juce::Colours::white.withAlpha (0.55f));

            const auto drawTinyLabel = [&] (const juce::String& text, float tickAngle)
            {
                auto p = polar (tickAngle, arcRadius + size * 0.145f);
                auto r = juce::Rectangle<float> (22.0f, 10.0f).withCentre (p).toNearestInt();
                g.drawFittedText (text, r, juce::Justification::centred, 1);
            };

            drawTinyLabel ("0",   rotaryStartAngle);
            drawTinyLabel ("50",  (rotaryStartAngle + rotaryEndAngle) * 0.5f);
            drawTinyLabel ("100", rotaryEndAngle);
        }
    }
};
