
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


void MainValveLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                             int x,
                                             int y,
                                             int width,
                                             int height,
                                             float sliderPos,
                                             const float rotaryStartAngle,
                                             const float rotaryEndAngle,
                                             juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int> (x, y, width, height)
        .toFloat()
        .reduced (5.0f);

    if (bounds.isEmpty())
        return;

    const auto& p = palette();

    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();

    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Main controls are intentionally heavier than Human Drift, but the outer
    // travel bar stays thin so the valve does not become a generic synth knob.
    const float frameRadius = size * 0.455f;
const float arcRadius   = size * 0.410f;

// Corpo più piccolo: lascia aria tra perno artigianale e cornice moderna.
const float bodyRadius  = size * 0.165f;
const float capRadius   = size * 0.125f;

    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto thumb  = slider.findColour (juce::Slider::thumbColourId);

    const auto polar = [centre] (float a, float r)
    {
        return juce::Point<float> (
            centre.x + r * std::cos (a - juce::MathConstants<float>::halfPi),
            centre.y + r * std::sin (a - juce::MathConstants<float>::halfPi));
    };

    // Soft physical shadow. This replaces the previous translucent rectangular
    // backing: the control now rests directly on the future background drawing.
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillEllipse (juce::Rectangle<float> (frameRadius * 2.0f, frameRadius * 2.0f)
        .withCentre (centre.translated (0.0f, size * 0.040f)));
// Coloured outer frame: modern/futuristic layer.
// The brass body is inside; the coloured frame belongs to the machine UI.
// Coloured outer frame: modern/futuristic layer.
// The brass body is inside; the coloured frame belongs to the machine UI.
auto frame = juce::Rectangle<float> (frameRadius * 2.0f, frameRadius * 2.0f)
    .withCentre (centre);

g.setColour (accent.withAlpha (0.22f));
g.fillEllipse (frame);

g.setColour (accent.withAlpha (0.92f));
g.drawEllipse (frame.reduced (0.7f),
               juce::jmax (2.0f, size * 0.022f));

g.setColour (juce::Colours::white.withAlpha (0.20f));
g.drawEllipse (frame.reduced (3.0f),
               juce::jmax (0.8f, size * 0.006f));


    g.setColour (juce::Colour (0xFF252A34).withAlpha (0.92f));
    g.strokePath (backgroundArc,
                  juce::PathStrokeType (juce::jmax (2.0f, size * 0.022f),
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x,
                             centre.y,
                             arcRadius,
                             arcRadius,
                             0.0f,
                             rotaryStartAngle,
                             angle,
                             true);

   // Coloured value arc: thicker and brighter than Human Drift.
g.setColour (p.brassDark.withAlpha (0.92f));
g.strokePath (valueArc,
              juce::PathStrokeType (juce::jmax (4.0f, size * 0.046f),
                                    juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded));

g.setColour (p.brassDark.withAlpha (0.92f));
g.strokePath (valueArc,
              juce::PathStrokeType (juce::jmax (2.2f, size * 0.026f),
                                    juce::PathStrokeType::curved,
                                    juce::PathStrokeType::rounded));
    // Edge ticks: green / white / red as a small navigation-light code.
    for (int i = 0; i <= 8; ++i)
    {
        const float t = static_cast<float> (i) / 8.0f;
        const float tickAngle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
        const bool major = (i == 0 || i == 4 || i == 8);

        juce::Colour tickColour = p.brass.withAlpha (major ? 0.58f : 0.28f);

        if (i == 0)
            tickColour = juce::Colour (0xFF39FF7A);
        else if (i == 4)
            tickColour = juce::Colours::white.withAlpha (0.95f);
        else if (i == 8)
            tickColour = p.warningRed.withAlpha (0.95f);

        const float innerR = arcRadius - (major ? size * 0.055f : size * 0.036f);
        const float outerR = arcRadius + (major ? size * 0.018f : size * 0.006f);

        g.setColour (tickColour);
        g.drawLine ({ polar (tickAngle, innerR), polar (tickAngle, outerR) },
                    major ? juce::jmax (1.4f, size * 0.012f)
                          : juce::jmax (0.9f, size * 0.008f));
    }

    // Brass body: artisanal / physical layer.
    auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f)
        .withCentre (centre);

    juce::ColourGradient bodyGradient (
        p.brass.brighter (0.22f), body.getX(), body.getY(),
        p.brassDark.darker (0.18f), body.getRight(), body.getBottom(),
        true);

    g.setGradientFill (bodyGradient);
    g.fillEllipse (body);

    g.setColour (p.brassDark.withAlpha (0.92f));
    g.drawEllipse (body.reduced (0.5f), juce::jmax (1.1f, size * 0.014f));

    auto innerPlate = body.reduced (size * 0.060f);
    g.setColour (juce::Colour (0xFF17120D).withAlpha (0.46f));
    g.drawEllipse (innerPlate, juce::jmax (1.0f, size * 0.010f));

    // Single tongue valve handle, plus a short rounded tail behind it.
    // The path is drawn pointing upward; rotating by 'angle' keeps it aligned
    // with JUCE's rotary polar convention.
   juce::Path handle;

// Linguetta principale: più meccanica, con punta.
// Il path punta verso l'alto; la rotazione del controllo la orienta.
const float baseW   = bodyRadius * 0.58f;
const float neckW   = bodyRadius * 0.40f;
const float tipW    = bodyRadius * 0.18f;
const float baseY   = bodyRadius * 0.02f;
const float neckY   = -bodyRadius * 0.95f;
const float tipY    = -bodyRadius * 1.52f;

// Corpo della linguetta, leggermente rastremato.
handle.startNewSubPath (-baseW * 0.5f, baseY);
handle.lineTo (-neckW * 0.5f, neckY);
handle.lineTo (-tipW  * 0.5f, tipY);
handle.quadraticTo (0.0f, tipY - bodyRadius * 0.16f,
                    tipW * 0.5f, tipY);
handle.lineTo (neckW * 0.5f, neckY);
handle.lineTo (baseW * 0.5f, baseY);
handle.closeSubPath();

// Piccola coda tonda dietro il perno.
// Serve a farla sembrare una valvola fisica e non solo una lancetta.
const float tailW = bodyRadius * 0.34f;
const float tailH = bodyRadius * 0.44f;

handle.addRoundedRectangle (-tailW * 0.5f,
                            bodyRadius * 0.22f,
                            tailW,
                            tailH,
                            tailW * 0.50f);
    auto handleTransform = juce::AffineTransform::rotation (angle)
        .translated (centre.x, centre.y);

    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillPath (handle, handleTransform.translated (0.0f, 1.5f));

    juce::ColourGradient handleGradient (
        p.brass.brighter (0.34f), centre.x - bodyRadius, centre.y - bodyRadius,
        p.brassDark, centre.x + bodyRadius, centre.y + bodyRadius,
        false);

    g.setGradientFill (handleGradient);
    g.fillPath (handle, handleTransform);

    g.setColour (p.brassDark.withAlpha (0.94f));
    g.strokePath (handle,
                  juce::PathStrokeType (juce::jmax (1.0f, size * 0.010f)),
                  handleTransform);

    // Central cap.
    auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f)
        .withCentre (centre);

    juce::ColourGradient capGradient (
        p.brass.brighter (0.35f), cap.getX(), cap.getY(),
        p.brassDark.darker (0.10f), cap.getRight(), cap.getBottom(),
        true);

    g.setGradientFill (capGradient);
    g.fillEllipse (cap);

    g.setColour (p.brassDark.withAlpha (0.95f));
    g.drawEllipse (cap.reduced (0.5f), juce::jmax (1.0f, size * 0.012f));

    auto screwSlot = juce::Rectangle<float> (capRadius * 1.22f, juce::jmax (2.0f, capRadius * 0.22f))
        .withCentre (centre);

    g.setColour (juce::Colour (0xFF16120D).withAlpha (0.65f));
    g.fillRoundedRectangle (screwSlot, screwSlot.getHeight() * 0.5f);

    // Small bright position bead at the end of the information arc.
    auto bead = polar (angle, arcRadius);
    g.setColour (accent.withAlpha (0.28f));
    g.fillEllipse (juce::Rectangle<float> (size * 0.085f, size * 0.085f).withCentre (bead));
    g.setColour (thumb.withAlpha (0.92f));
    g.fillEllipse (juce::Rectangle<float> (size * 0.045f, size * 0.045f).withCentre (bead));
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
