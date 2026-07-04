
#include "NeumatonLabTheme.h"

#include <cmath>

namespace neumaton::lab
{
namespace
{
[[nodiscard]] float safeNormalise (float value) noexcept
{
    return juce::jlimit (0.0f, 1.0f, std::isfinite (value) ? value : 0.0f);
}
} // namespace

const Palette& palette() noexcept
{
    static const Palette p {
        juce::Colour (0xFF15100D), // backgroundTop
        juce::Colour (0xFF08090B), // backgroundBottom
        juce::Colour (0xCC160F0A), // panelDark
        juce::Colour (0x88D7A951), // panelEdge
        juce::Colour (0xFFD7A951), // brass
        juce::Colour (0xFF79572A), // brassDark
        juce::Colour (0x332DD3C5), // glass
        juce::Colour (0xAA77E9DE), // glassEdge
        juce::Colour (0xFFEFE3C6), // ink
        juce::Colour (0xFFE5D2A3), // parchment
        juce::Colour (0xFFE33A2F), // warningRed
        juce::Colour (0xFF38C987), // greenFluid
        juce::Colour (0xFF4FB4FF)  // blueGlow
    };
    return p;
}

void Painter::drawBackground (juce::Graphics& g,
                              juce::Rectangle<int> bounds,
                              const juce::Image& placeholderBackground)
{
    const auto b = bounds.toFloat();
    const auto& p = palette();

    if (placeholderBackground.isValid())
        g.drawImage (placeholderBackground, b, juce::RectanglePlacement::fillDestination);
    else
    {
        juce::ColourGradient gradient (p.backgroundTop,
                                       b.getTopLeft(),
                                       p.backgroundBottom,
                                       b.getBottomRight(),
                                       false);
        g.setGradientFill (gradient);
        g.fillRect (b);
    }

    // Vignette da laboratorio: tiene insieme placeholder moderni e futuri disegni.
    juce::ColourGradient vignette (juce::Colours::transparentBlack,
                                   b.getCentreX(), b.getCentreY(),
                                   juce::Colour (0xCC000000),
                                   b.getRight(), b.getBottom(),
                                   true);
    g.setGradientFill (vignette);
    g.fillRect (b);

    // Polvere/aria: linee quasi invisibili, utili finché mancano gli sfondi finali.
    g.setColour (juce::Colours::white.withAlpha (0.025f));
    for (int y = bounds.getY() + 18; y < bounds.getBottom(); y += 23)
        g.drawHorizontalLine (y, static_cast<float> (bounds.getX()), static_cast<float> (bounds.getRight()));
}

void Painter::drawPanel (juce::Graphics& g,
                         juce::Rectangle<float> bounds,
                         float corner,
                         float alpha)
{
    const auto& p = palette();
    alpha = juce::jlimit (0.0f, 1.0f, alpha);

    g.setColour (p.panelDark.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (p.panelEdge.withMultipliedAlpha (alpha));
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.2f);

    g.setColour (juce::Colours::black.withAlpha (0.30f * alpha));
    g.drawRoundedRectangle (bounds.reduced (2.0f), corner - 2.0f, 1.0f);
}

void Painter::drawNeedleMeter (juce::Graphics& g,
                               juce::Rectangle<float> bounds,
                               float normalisedValue,
                               const juce::String& title,
                               const juce::String& valueText,
                               juce::Colour accent)
{
    normalisedValue = safeNormalise (normalisedValue);
    const auto& p = palette();
    drawPanel (g, bounds, 10.0f, 0.95f);

    auto meter = bounds.reduced (10.0f, 8.0f);
    auto titleArea = meter.removeFromTop (18.0f);

    g.setColour (p.ink);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (title, titleArea.toNearestInt(), juce::Justification::centred);

    const auto dial = meter.reduced (4.0f, 0.0f);
    const float radius = juce::jmin (dial.getWidth(), dial.getHeight() * 1.65f) * 0.44f;
    const auto centre = juce::Point<float> (dial.getCentreX(), dial.getBottom() - 8.0f);

    constexpr float start = juce::MathConstants<float>::pi * 1.15f;
    constexpr float end   = juce::MathConstants<float>::pi * 1.85f;
    const float angle = juce::jmap (normalisedValue, start, end);

    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour (p.brassDark.withAlpha (0.85f));
    g.strokePath (arc, juce::PathStrokeType (5.0f,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));

    juce::Path activeArc;
    activeArc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
    g.setColour (accent.withAlpha (0.90f));
    g.strokePath (activeArc, juce::PathStrokeType (4.0f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    for (int i = 0; i <= 8; ++i)
    {
        const float tickNorm = static_cast<float> (i) / 8.0f;
        const float tickAngle = juce::jmap (tickNorm, start, end);
        const auto outer = centre + juce::Point<float> (std::cos (tickAngle), std::sin (tickAngle)) * radius;
        const auto inner = centre + juce::Point<float> (std::cos (tickAngle), std::sin (tickAngle)) * (radius - (i % 2 == 0 ? 10.0f : 6.0f));
        g.setColour (p.ink.withAlpha (0.55f));
        g.drawLine ({ inner, outer }, i % 2 == 0 ? 1.4f : 0.8f);
    }

    const auto needleEnd = centre + juce::Point<float> (std::cos (angle), std::sin (angle)) * (radius - 13.0f);
    g.setColour (accent.withAlpha (0.22f));
    g.drawLine ({ centre, needleEnd }, 7.0f);
    g.setColour (p.warningRed);
    g.drawLine ({ centre, needleEnd }, 2.2f);
    g.setColour (p.brass);
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (centre));

    g.setColour (p.ink.withAlpha (0.88f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (valueText,
                bounds.withTrimmedTop (bounds.getHeight() - 22.0f).toNearestInt(),
                juce::Justification::centred);
}

void Painter::drawCorrectionGauge (juce::Graphics& g,
                                   juce::Rectangle<int> bounds,
                                   float correctionCents)
{
    const float limited = juce::jlimit (-100.0f, 100.0f,
                                       std::isfinite (correctionCents) ? correctionCents : 0.0f);
    const float normalised = juce::jmap (limited, -100.0f, 100.0f, 0.0f, 1.0f);
    drawNeedleMeter (g,
                     bounds.toFloat(),
                     normalised,
                     "Correction",
                     juce::String (correctionCents, 1) + " ct",
                     palette().warningRed);
}

void Painter::drawConsensusGauge (juce::Graphics& g,
                                  juce::Rectangle<int> bounds,
                                  float consensus)
{
    const float normalised = safeNormalise (consensus);
    drawNeedleMeter (g,
                     bounds.toFloat(),
                     normalised,
                     "Consensus",
                     juce::String (normalised * 100.0f, 0) + "%",
                     palette().greenFluid);
}

float Painter::frequencyToLogPosition (float hz,
                                       float minimumHz,
                                       float maximumHz) noexcept
{
    if (! std::isfinite (hz) || hz <= 0.0f)
        return 0.0f;

    minimumHz = juce::jmax (1.0f, minimumHz);
    maximumHz = juce::jmax (minimumHz + 1.0f, maximumHz);

    const float logMin = std::log (minimumHz);
    const float logMax = std::log (maximumHz);
    const float logHz = std::log (juce::jlimit (minimumHz, maximumHz, hz));
    return juce::jlimit (0.0f, 1.0f, (logHz - logMin) / (logMax - logMin));
}

void Painter::drawRadioTarget (juce::Graphics& g,
                               juce::Rectangle<int> bounds,
                               float detectedHz,
                               float targetHz,
                               float minimumHz,
                               float maximumHz)
{
    const auto& p = palette();
    auto f = bounds.toFloat();
    drawPanel (g, f, 10.0f, 0.96f);

    auto area = bounds.reduced (14, 10);
    auto title = area.removeFromTop (20);
    g.setColour (p.ink);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText ("Target tuning", title, juce::Justification::centred);

    auto rail = area.reduced (4, 18).withHeight (18);
    rail.setCentre (area.getCentre());

    g.setColour (p.glass);
    g.fillRoundedRectangle (rail.toFloat(), 4.0f);
    g.setColour (p.glassEdge);
    g.drawRoundedRectangle (rail.toFloat().reduced (0.5f), 4.0f, 1.0f);

    // Scala logaritmica: musicalmente è più coerente della posizione lineare in Hz.
    for (int i = 0; i <= 4; ++i)
    {
        const float norm = static_cast<float> (i) / 4.0f;
        const int x = rail.getX() + juce::roundToInt (norm * static_cast<float> (rail.getWidth()));
        g.setColour (p.ink.withAlpha (i == 0 || i == 4 ? 0.70f : 0.38f));
        g.drawVerticalLine (x, static_cast<float> (rail.getY() - 5), static_cast<float> (rail.getBottom() + 5));
    }

    if (detectedHz > 0.0f)
    {
        const float detectedNorm = frequencyToLogPosition (detectedHz, minimumHz, maximumHz);
        const int x = rail.getX() + juce::roundToInt (detectedNorm * static_cast<float> (rail.getWidth()));
        g.setColour (p.blueGlow.withAlpha (0.45f));
        g.drawVerticalLine (x, static_cast<float> (rail.getY() - 9), static_cast<float> (rail.getBottom() + 9));
    }

    const float targetNorm = frequencyToLogPosition (targetHz, minimumHz, maximumHz);
    const int targetX = rail.getX() + juce::roundToInt (targetNorm * static_cast<float> (rail.getWidth()));
    auto cursor = juce::Rectangle<int> (targetX - 5, rail.getY() - 7, 10, rail.getHeight() + 14);
    g.setColour (p.warningRed.withAlpha (0.94f));
    g.fillRoundedRectangle (cursor.toFloat(), 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.65f));
    g.drawRoundedRectangle (cursor.toFloat().reduced (0.5f), 2.0f, 0.8f);

    auto text = area.removeFromBottom (18);
    g.setColour (p.ink.withAlpha (0.86f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (targetHz > 0.0f ? juce::String (targetHz, 1) + " Hz" : "--",
                text,
                juce::Justification::centred);
}

void Painter::drawLeverSwitch (juce::Graphics& g,
                               juce::Rectangle<int> bounds,
                               bool isOn,
                               const juce::String& label)
{
    const auto& p = palette();
    auto f = bounds.toFloat();
    drawPanel (g, f, 8.0f, 0.88f);

    auto leverArea = bounds.reduced (8, 6);
    auto textArea = leverArea.removeFromRight (juce::jmax (72, leverArea.getWidth() - 44));
    auto slot = leverArea.reduced (6, 2).toFloat();

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (slot, 6.0f);
    g.setColour ((isOn ? p.greenFluid : p.brassDark).withAlpha (0.80f));
    g.drawRoundedRectangle (slot, 6.0f, 1.1f);

    const float knobY = isOn ? slot.getY() + 4.0f : slot.getBottom() - 16.0f;
    g.setColour (p.brass);
    g.fillEllipse (slot.getCentreX() - 7.0f, knobY, 14.0f, 14.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (slot.getCentreX() - 7.0f, knobY, 14.0f, 14.0f, 1.0f);

    g.setColour (p.ink.withAlpha (isOn ? 0.95f : 0.62f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (label, textArea, juce::Justification::centredLeft);
}

void Painter::drawGlassBar (juce::Graphics& g,
                            juce::Rectangle<int> bounds,
                            float normalisedValue,
                            const juce::String& label,
                            juce::Colour fillColour)
{
    const auto& p = palette();
    normalisedValue = safeNormalise (normalisedValue);

    auto area = bounds.reduced (2, 2);
    auto labelArea = area.removeFromLeft (86);
    g.setColour (p.ink.withAlpha (0.86f));
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.drawText (label, labelArea, juce::Justification::centredLeft);

    auto tube = area.reduced (0, 5).toFloat();
    g.setColour (p.glass);
    g.fillRoundedRectangle (tube, 4.0f);

    auto fill = tube.reduced (2.0f);
    fill.setWidth (fill.getWidth() * normalisedValue);
    g.setColour (fillColour.withAlpha (0.78f));
    g.fillRoundedRectangle (fill, 3.0f);

    g.setColour (p.glassEdge.withAlpha (0.80f));
    g.drawRoundedRectangle (tube.reduced (0.5f), 4.0f, 1.0f);
}
} // namespace neumaton::lab
