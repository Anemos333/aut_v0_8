
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

   // Coloured value arc: thicker and brighter than Human Drift.
    juce::Path valueArc;
    valueArc.addCentredArc (centre.x,
                             centre.y,
                             arcRadius,
                             arcRadius,
                             0.0f,
                             rotaryStartAngle,
                             angle,
                             true);
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
LabLeverToggleLookAndFeel::LabLeverToggleLookAndFeel()
    : LabLeverToggleLookAndFeel (Options{})
{
}

LabLeverToggleLookAndFeel::LabLeverToggleLookAndFeel (Options optionsToUse)
    : options (optionsToUse)
{
}

void LabLeverToggleLookAndFeel::drawToggleButton (
    juce::Graphics& g,
    juce::ToggleButton& button,
    bool shouldDrawButtonAsHighlighted,
    bool shouldDrawButtonAsDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    if (bounds.isEmpty())
        return;

    const auto& p = palette();

    const bool isOn = button.getToggleState();
    const bool active = shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown;

    const auto accent = button.findColour (juce::ToggleButton::tickColourId);
    const auto textColour = button.findColour (juce::ToggleButton::textColourId);

    const auto brassLight = juce::Colour (0xFFD8B06A);
    const auto brassMid   = juce::Colour (0xFFB58A4A);
    const auto brassDark  = juce::Colour (0xFF6E4B24);
    const auto baseDark   = juce::Colour (0xFF15120E);

    // Area: leva a sinistra, testo a destra.
    const float leverBoxW = options.compact
        ? juce::jmin (40.0f, bounds.getWidth() * 0.38f)
        : juce::jmin (46.0f, bounds.getWidth() * 0.36f);

    auto leverBox = bounds.removeFromLeft (leverBoxW).reduced (2.0f, 2.0f);
    auto textBox  = bounds.reduced (2.0f, 0.0f);

    // ---------------------------------------------------------------------
    // 1. Placca di base
    // ---------------------------------------------------------------------
    auto plate = leverBox.reduced (1.0f);

    g.setColour (juce::Colours::black.withAlpha (0.28f));
    g.fillRoundedRectangle (plate.translated (0.0f, 1.2f), 5.0f);

    juce::ColourGradient plateGradient (
        juce::Colour (0xFF2A2117), plate.getX(), plate.getY(),
        juce::Colour (0xFF0D0E11), plate.getRight(), plate.getBottom(),
        true);

    g.setGradientFill (plateGradient);
    g.fillRoundedRectangle (plate, 5.0f);

    g.setColour ((isOn ? accent : brassDark).withAlpha (isOn ? 0.70f : 0.48f));
    g.drawRoundedRectangle (plate.reduced (0.5f), 5.0f, 1.0f);

    // ---------------------------------------------------------------------
    // 2. Fessura / corsa della leva
    // ---------------------------------------------------------------------
    const auto centre = plate.getCentre();

    const float slotH = plate.getHeight() * 0.68f;
    const float slotW = juce::jmax (5.0f, plate.getWidth() * 0.20f);

    auto slot = juce::Rectangle<float> (slotW, slotH)
        .withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.62f));
    g.fillRoundedRectangle (slot, slotW * 0.45f);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (slot.reduced (0.4f), slotW * 0.45f, 0.8f);

    // ---------------------------------------------------------------------
    // 3. Leva inclinata
    // ---------------------------------------------------------------------
    // OFF: inclinata verso il basso.
    // ON:  inclinata verso l'alto.
    const float pivotY = plate.getCentreY() + plate.getHeight() * 0.22f;
    const float pivotX = plate.getCentreX();

    const float leverLength = plate.getHeight() * 0.54f * options.leverScale;
    const float leverAngle = isOn ? -0.48f : 0.48f;

    const auto pivot = juce::Point<float> (pivotX, pivotY);
    const auto end = pivot + juce::Point<float> (
        std::sin (leverAngle) * leverLength,
       -std::cos (leverAngle) * leverLength);

    // Glow/stato sotto la leva.
    if (isOn)
    {
        g.setColour (accent.withAlpha (0.22f));
        g.drawLine ({ pivot, end }, juce::jmax (5.0f, plate.getWidth() * 0.13f));
    }

    // Asta metallica.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine ({ pivot.translated (0.0f, 1.0f),
                  end.translated (0.0f, 1.0f) },
                juce::jmax (3.2f, plate.getWidth() * 0.08f));

    juce::ColourGradient leverGradient (
        brassLight, end.x, end.y,
        brassDark, pivot.x, pivot.y,
        false);

    g.setGradientFill (leverGradient);
    g.drawLine ({ pivot, end },
                juce::jmax (2.4f, plate.getWidth() * 0.065f));

    // Pomello terminale.
    const float tipR = juce::jmax (4.0f, plate.getWidth() * 0.105f);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (
        juce::Rectangle<float> (tipR * 2.0f, tipR * 2.0f)
            .withCentre (end.translated (0.0f, 1.0f)));

    juce::ColourGradient tipGradient (
        brassLight.brighter (0.15f), end.x - tipR, end.y - tipR,
        brassDark,                   end.x + tipR, end.y + tipR,
        true);

    g.setGradientFill (tipGradient);
    g.fillEllipse (
        juce::Rectangle<float> (tipR * 2.0f, tipR * 2.0f)
            .withCentre (end));

    g.setColour (brassDark.withAlpha (0.95f));
    g.drawEllipse (
        juce::Rectangle<float> (tipR * 2.0f, tipR * 2.0f)
            .withCentre (end)
            .reduced (0.4f),
        0.8f);

    // Fulcro.
    const float pivotR = juce::jmax (4.5f, plate.getWidth() * 0.115f);

    g.setColour (baseDark.withAlpha (0.88f));
    g.fillEllipse (
        juce::Rectangle<float> (pivotR * 2.0f, pivotR * 2.0f)
            .withCentre (pivot));

    g.setColour (brassMid.withAlpha (0.90f));
    g.drawEllipse (
        juce::Rectangle<float> (pivotR * 2.0f, pivotR * 2.0f)
            .withCentre (pivot)
            .reduced (0.5f),
        1.0f);

    // ---------------------------------------------------------------------
    // 4. Piccola luce di stato
    // ---------------------------------------------------------------------
    const float lampR = juce::jmax (2.3f, plate.getWidth() * 0.055f);
    auto lampCentre = juce::Point<float> (
        plate.getRight() - lampR * 2.0f,
        plate.getY() + lampR * 2.0f);

    if (isOn)
    {
        g.setColour (accent.withAlpha (0.26f));
        g.fillEllipse (
            juce::Rectangle<float> (lampR * 4.2f, lampR * 4.2f)
                .withCentre (lampCentre));

        g.setColour (accent.withAlpha (0.95f));
    }
    else
    {
        g.setColour (juce::Colour (0xFF2B2B2B));
    }

    g.fillEllipse (
        juce::Rectangle<float> (lampR * 2.0f, lampR * 2.0f)
            .withCentre (lampCentre));

    g.setColour (juce::Colours::white.withAlpha (isOn ? 0.55f : 0.12f));
    g.drawEllipse (
        juce::Rectangle<float> (lampR * 2.0f, lampR * 2.0f)
            .withCentre (lampCentre)
            .reduced (0.3f),
        0.7f);

    // ---------------------------------------------------------------------
    // 5. Testo
    // ---------------------------------------------------------------------
    const float textAlpha = button.isEnabled()
        ? (isOn ? 0.96f : 0.72f)
        : 0.35f;

    g.setFont (juce::FontOptions (
        options.compact ? 11.0f : 12.0f,
        juce::Font::bold));

    g.setColour ((isOn ? accent.brighter (0.20f) : textColour)
        .withAlpha (textAlpha));

    g.drawFittedText (button.getButtonText(),
                      textBox.toNearestInt(),
                      juce::Justification::centredLeft,
                      1);
}
void OutputKnobLookAndFeel::drawRotarySlider (juce::Graphics& g,
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
        .reduced (2.0f);

    if (bounds.isEmpty())
        return;

    const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto centre = bounds.getCentre();

    const float angle = rotaryStartAngle
        + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const auto accent = slider.findColour (
        juce::Slider::rotarySliderFillColourId);

    const auto metalLight = juce::Colour (0xFFB9B0A2);
    const auto metalMid   = juce::Colour (0xFF6E6A64);
    const auto metalDark  = juce::Colour (0xFF252525);
    const auto rubber     = juce::Colour (0xFF101114);
    const auto amber      = accent.brighter (0.25f);

    const float outerRadius = size * 0.46f;
    const float gripRadius  = size * 0.38f;
    const float capRadius   = size * 0.22f;
    const float arcRadius   = size * 0.43f;

    const auto polar = [centre] (float a, float r)
    {
        return juce::Point<float> (
            centre.x + r * std::cos (a - juce::MathConstants<float>::halfPi),
            centre.y + r * std::sin (a - juce::MathConstants<float>::halfPi));
    };

    // Ombra: il pot deve sembrare montato sul pannello, non disegnato sopra.
    g.setColour (juce::Colours::black.withAlpha (0.38f));
    g.fillEllipse (
        juce::Rectangle<float> (outerRadius * 2.0f, outerRadius * 2.0f)
            .withCentre (centre.translated (0.0f, size * 0.045f)));

    // Ghiera esterna metallica.
    auto outer = juce::Rectangle<float> (outerRadius * 2.0f,
                                         outerRadius * 2.0f)
        .withCentre (centre);

    juce::ColourGradient outerGradient (
        metalLight, outer.getX(), outer.getY(),
        metalDark,  outer.getRight(), outer.getBottom(),
        true);

    g.setGradientFill (outerGradient);
    g.fillEllipse (outer);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (outer.reduced (0.4f), juce::jmax (1.0f, size * 0.022f));

    // Arco di valore: ambra, abbastanza moderno ma non “magico”.
    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x,
                                  centre.y,
                                  arcRadius,
                                  arcRadius,
                                  0.0f,
                                  rotaryStartAngle,
                                  rotaryEndAngle,
                                  true);

    g.setColour (juce::Colour (0xFF2A2C30).withAlpha (0.90f));
    g.strokePath (backgroundArc,
                  juce::PathStrokeType (juce::jmax (2.0f, size * 0.040f),
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

    g.setColour (amber.withAlpha (0.30f));
    g.strokePath (valueArc,
                  juce::PathStrokeType (juce::jmax (3.0f, size * 0.060f),
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    g.setColour (amber.withAlpha (0.95f));
    g.strokePath (valueArc,
                  juce::PathStrokeType (juce::jmax (1.7f, size * 0.034f),
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    // Corpo centrale scuro, tipo potenziometro/pedale.
    auto grip = juce::Rectangle<float> (gripRadius * 2.0f,
                                        gripRadius * 2.0f)
        .withCentre (centre);

    juce::ColourGradient gripGradient (
        juce::Colour (0xFF34373D), grip.getX(), grip.getY(),
        rubber,                   grip.getRight(), grip.getBottom(),
        true);

    g.setGradientFill (gripGradient);
    g.fillEllipse (grip);

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawEllipse (grip.reduced (0.5f), juce::jmax (1.0f, size * 0.018f));

    // Piccole scanalature da potenziometro.
    for (int i = 0; i < 18; ++i)
    {
        const float t = static_cast<float> (i) / 18.0f;
        const float a = t * juce::MathConstants<float>::twoPi;

        const auto p1 = polar (a, gripRadius * 0.83f);
        const auto p2 = polar (a, gripRadius * 0.98f);

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawLine ({ p1, p2 }, juce::jmax (0.55f, size * 0.007f));
    }

    // Indicatore principale: tacca bianca + piccolo glow ambra.
    const auto indicatorOuter = polar (angle, gripRadius * 0.82f);
    const auto indicatorInner = polar (angle, gripRadius * 0.34f);

    g.setColour (amber.withAlpha (0.25f));
    g.drawLine ({ indicatorInner, indicatorOuter },
                juce::jmax (3.0f, size * 0.070f));

    g.setColour (juce::Colours::white.withAlpha (0.92f));
    g.drawLine ({ indicatorInner, indicatorOuter },
                juce::jmax (1.2f, size * 0.026f));

    // Cap centrale metallico.
    auto cap = juce::Rectangle<float> (capRadius * 2.0f,
                                       capRadius * 2.0f)
        .withCentre (centre);

    juce::ColourGradient capGradient (
        metalLight.brighter (0.15f), cap.getX(), cap.getY(),
        metalMid.darker (0.15f),     cap.getRight(), cap.getBottom(),
        true);

    g.setGradientFill (capGradient);
    g.fillEllipse (cap);

    g.setColour (juce::Colours::black.withAlpha (0.60f));
    g.drawEllipse (cap.reduced (0.4f), juce::jmax (0.8f, size * 0.014f));

    // Puntino centrale scuro: dettaglio da hardware.
    auto pin = cap.reduced (capRadius * 0.52f);
    g.setColour (juce::Colour (0xFF161616).withAlpha (0.85f));
    g.fillEllipse (pin);
}
UtilityRailSliderLookAndFeel::UtilityRailSliderLookAndFeel()
    : UtilityRailSliderLookAndFeel (Options{})
{
}

UtilityRailSliderLookAndFeel::UtilityRailSliderLookAndFeel (
    Options optionsToUse)
    : options (optionsToUse)
{
}

void UtilityRailSliderLookAndFeel::drawLinearSlider (
    juce::Graphics& g,
    int x,
    int y,
    int width,
    int height,
    float sliderPos,
    float minSliderPos,
    float maxSliderPos,
    const juce::Slider::SliderStyle style,
    juce::Slider& slider)
{
    juce::ignoreUnused (minSliderPos, maxSliderPos);

    if (style != juce::Slider::LinearHorizontal)
    {
        juce::LookAndFeel_V4::drawLinearSlider (
            g, x, y, width, height,
            sliderPos, minSliderPos, maxSliderPos,
            style, slider);
        return;
    }

    auto area = juce::Rectangle<float> (x, y, width, height)
        .reduced (1.0f, 1.0f);

    if (area.getWidth() <= 8.0f || area.getHeight() <= 8.0f)
        return;

    const auto& p = palette();

    const auto brassLight = juce::Colour (0xFFD8B06A);
    const auto brassMid   = juce::Colour (0xFFB58A4A);
    const auto brassDark  = juce::Colour (0xFF6E4B24);
    const auto wearDark   = juce::Colour (0xFF493019);

    const auto activeColour = slider.findColour (
        juce::Slider::trackColourId).brighter (options.strongGlow ? 0.45f : 0.18f);

    const bool active = slider.isMouseOverOrDragging();

    // Cursore: abbastanza grande da sembrare un oggetto fisico,
    // ma non così grande da rubare spazio alla scala.
    const float thumbW = juce::jlimit (
        16.0f, 26.0f, area.getHeight() * 0.92f * options.thumbScale);

    const float thumbH = juce::jlimit (
        10.0f, 18.0f, area.getHeight() * 0.58f * options.thumbScale);

    const float pointerH = juce::jlimit (
        4.0f, 8.0f, area.getHeight() * 0.22f * options.thumbScale);

    // Il rail è leggermente inset: così il cursore non esce dai bordi.
    auto rail = area.reduced (thumbW * 0.48f, 0.0f);
    rail = rail.withY (area.getBottom() - 8.0f)
               .withHeight (juce::jlimit (4.0f, 6.0f, area.getHeight() * 0.18f));

    const float pos = juce::jlimit (rail.getX(), rail.getRight(), sliderPos);

    // ---------------------------------------------------------------------
    // 1. Ombra e binario fisico
    // ---------------------------------------------------------------------
    g.setColour (juce::Colours::black.withAlpha (0.26f));
    g.fillRoundedRectangle (rail.translated (0.0f, 1.4f), 3.0f);

    juce::ColourGradient railGradient (
        juce::Colour (0xFF2A2117), rail.getX(), rail.getY(),
        juce::Colour (0xFF6A4A26), rail.getX(), rail.getBottom(),
        false);

    g.setGradientFill (railGradient);
    g.fillRoundedRectangle (rail, 3.0f);

    g.setColour (brassMid.withAlpha (0.72f));
    g.drawRoundedRectangle (rail.reduced (0.4f), 3.0f, 0.9f);

    // Linea attiva: la parte "futuristica" dentro il rail meccanico.
    auto activeRail = rail.reduced (1.5f, rail.getHeight() * 0.26f);
    activeRail.setRight (pos);

    if (activeRail.getWidth() > 1.0f)
    {
        g.setColour (activeColour.withAlpha (options.strongGlow ? 0.82f : 0.62f));
        g.fillRoundedRectangle (activeRail, 2.0f);

        if (options.strongGlow || active)
        {
            g.setColour (activeColour.withAlpha (0.24f));
            g.fillRoundedRectangle (activeRail.expanded (0.0f, 1.4f), 2.5f);
        }
    }

    // ---------------------------------------------------------------------
    // 2. Scala graduata
    // ---------------------------------------------------------------------
    const float scaleBottomY = rail.getY() - 2.0f;

    for (int i = 0; i <= 20; ++i)
    {
        const float t = static_cast<float> (i) / 20.0f;

        // Variante prospettica: utile più avanti per Tempo Mode.
        // Le tacche al centro sembrano più vicine, quelle ai lati più lontane.
        const float distanceFromCentre = std::abs (t - 0.5f) * 2.0f;
        const float perspective = options.perspectiveScale
            ? (1.0f - 0.48f * distanceFromCentre)
            : 1.0f;

        const float tickX = rail.getX() + t * rail.getWidth();

        const bool major = (i % 5 == 0);
        const float baseTickH = major ? 9.0f : 5.0f;
        const float tickH = baseTickH * perspective;

        const float alpha = (major ? 0.58f : 0.30f) * perspective;

        // Piccolo abbassamento laterale nella modalità prospettica:
        // suggerisce che la scala entri sotto il banco.
        const float yOffset = options.perspectiveScale
            ? distanceFromCentre * 2.0f
            : 0.0f;

        g.setColour (juce::Colours::white.withAlpha (alpha));
        g.drawLine (tickX,
                    scaleBottomY - tickH + yOffset,
                    tickX,
                    scaleBottomY + yOffset,
                    major ? 1.1f : 0.75f);
    }

    if (options.showEndLabels && area.getWidth() > 90.0f)
    {
        g.setFont (juce::FontOptions (8.0f, juce::Font::plain));
        g.setColour (juce::Colours::white.withAlpha (0.58f));

        auto leftText = juce::Rectangle<float> (
            rail.getX() - 8.0f,
            area.getY(),
            20.0f,
            10.0f).toNearestInt();

        auto rightText = juce::Rectangle<float> (
            rail.getRight() - 12.0f,
            area.getY(),
            28.0f,
            10.0f).toNearestInt();

        g.drawFittedText ("0", leftText, juce::Justification::centred, 1);
        g.drawFittedText ("100", rightText, juce::Justification::centred, 1);
    }

    // ---------------------------------------------------------------------
    // 3. Cursore: rettangolo + freccetta
    // ---------------------------------------------------------------------
    const float thumbY = rail.getCentreY() - thumbH * 0.50f;
    auto thumbBody = juce::Rectangle<float> (
        pos - thumbW * 0.5f,
        thumbY,
        thumbW,
        thumbH);

    juce::Path pointer;
    pointer.startNewSubPath (pos, thumbBody.getY() - pointerH);
    pointer.lineTo (pos - thumbW * 0.30f, thumbBody.getY() + 1.0f);
    pointer.lineTo (pos + thumbW * 0.30f, thumbBody.getY() + 1.0f);
    pointer.closeSubPath();

    // Ombra del cursore.
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillRoundedRectangle (thumbBody.translated (0.0f, 1.3f), 2.5f);
    g.fillPath (pointer, juce::AffineTransform::translation (0.0f, 1.2f));

    // Corpo ottone.
    juce::ColourGradient thumbGradient (
        brassLight, thumbBody.getX(), thumbBody.getY(),
        brassDark,  thumbBody.getRight(), thumbBody.getBottom(),
        true);

    g.setGradientFill (thumbGradient);
    g.fillRoundedRectangle (thumbBody, 2.5f);
    g.fillPath (pointer);

    // Zona centrale usurata: come se il pezzo fosse stato spostato col dito.
    auto wear = thumbBody.reduced (thumbW * 0.26f, thumbH * 0.22f);

    g.setColour (wearDark.withAlpha (0.56f));
    g.fillRoundedRectangle (wear, 1.8f);

    // Piccola lucidatura sopra e bordo.
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawLine (thumbBody.getX() + 2.0f,
                thumbBody.getY() + 2.0f,
                thumbBody.getRight() - 2.0f,
                thumbBody.getY() + 2.0f,
                0.8f);

    g.setColour (brassDark.withAlpha (0.90f));
    g.drawRoundedRectangle (thumbBody.reduced (0.4f), 2.5f, 0.9f);

    g.setColour (brassLight.withAlpha (0.88f));
    g.strokePath (pointer, juce::PathStrokeType (0.8f));

    // Stato attivo: non mostra un valore, ma rende il cursore più leggibile.
    if (active)
    {
        g.setColour (activeColour.withAlpha (0.36f));
        g.drawRoundedRectangle (thumbBody.expanded (2.0f, 1.5f), 3.2f, 1.1f);
    }
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
                               float glowNormalisedValue,
                               const juce::String& title,
                               const juce::String& valueText,
                               juce::Colour accent,
                               bool redWarning)
{
    normalisedValue = safeNormalise (normalisedValue);
    glowNormalisedValue = safeNormalise (glowNormalisedValue);

    const auto& p = palette();

    const auto electricBlue = juce::Colour (0xFF20D8FF);
    const auto syntheticViolet = juce::Colour (0xFF9B5CFF);
    const auto warningRed = juce::Colour (0xFFFF2A4A);

    auto lightColour = electricBlue.interpolatedWith (
        syntheticViolet,
        glowNormalisedValue);

    if (redWarning)
        lightColour = lightColour.interpolatedWith (warningRed, 0.38f);

    // Più trasparente: il quadrante deve appoggiarsi sul disegno.
    drawPanel (g, bounds, 10.0f, 0.42f);

    auto meter = bounds.reduced (10.0f, 8.0f);
    auto titleArea = meter.removeFromTop (18.0f);

    g.setColour (p.ink.withAlpha (0.90f));
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (title, titleArea.toNearestInt(), juce::Justification::centred);

    const auto dial = meter.reduced (4.0f, 0.0f);
    const float radius = juce::jmin (dial.getWidth(), dial.getHeight() * 1.65f) * 0.44f;
    const auto centre = juce::Point<float> (dial.getCentreX(), dial.getBottom() - 8.0f);

    constexpr float start = juce::MathConstants<float>::pi * 1.15f;
    constexpr float end   = juce::MathConstants<float>::pi * 1.85f;

    const float angle = juce::jmap (normalisedValue, start, end);
    const float glowAngle = juce::jmap (glowNormalisedValue, start, end);

    // Arco base, quasi inciso nel vetro.
    juce::Path arc;
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);

    g.setColour (p.brassDark.withAlpha (0.72f));
    g.strokePath (arc,
                  juce::PathStrokeType (5.0f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.strokePath (arc,
                  juce::PathStrokeType (1.2f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    // Arco luminoso ruotato di 90° in senso orario.
    // Se visivamente ruota dalla parte sbagliata, cambia +halfPi in -halfPi.
    const float glowRotation = juce::MathConstants<float>::halfPi;

    juce::Path glowArc;
    glowArc.addCentredArc (centre.x,
                           centre.y,
                           radius,
                           radius,
                           0.0f,
                           start + glowRotation,
                           glowAngle + glowRotation,
                           true);

    g.setColour (lightColour.withAlpha (0.25f));
    g.strokePath (glowArc,
                  juce::PathStrokeType (8.0f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    g.setColour (lightColour.withAlpha (0.88f));
    g.strokePath (glowArc,
                  juce::PathStrokeType (3.0f,
                                        juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded));

    // Tacche.
    for (int i = 0; i <= 8; ++i)
    {
        const float tickNorm = static_cast<float> (i) / 8.0f;
        const float tickAngle = juce::jmap (tickNorm, start, end);

        const bool major = (i % 2 == 0);
        const float tickLen = major ? 10.0f : 6.0f;

        const auto outer = centre
            + juce::Point<float> (std::cos (tickAngle), std::sin (tickAngle)) * radius;

        const auto inner = centre
            + juce::Point<float> (std::cos (tickAngle), std::sin (tickAngle)) * (radius - tickLen);

        g.setColour (p.ink.withAlpha (major ? 0.62f : 0.34f));
        g.drawLine (juce::Line<float> (inner, outer), major ? 1.3f : 0.75f);
    }

    // Lancetta più caratteristica: non più una semplice linea.
    const auto dir = juce::Point<float> (std::cos (angle), std::sin (angle));
    const auto normal = juce::Point<float> (-dir.y, dir.x);

    const auto needleTip  = centre + dir * (radius - 12.0f);
    const auto needleBase = centre + dir * 8.0f;
    const auto needleBack = centre - dir * 5.0f;

    juce::Path needle;
    needle.startNewSubPath (needleTip.x, needleTip.y);
    needle.lineTo ((needleBase + normal * 3.2f).x,
                   (needleBase + normal * 3.2f).y);
    needle.lineTo (needleBack.x, needleBack.y);
    needle.lineTo ((needleBase - normal * 3.2f).x,
                   (needleBase - normal * 3.2f).y);
    needle.closeSubPath();

    g.setColour (lightColour.withAlpha (0.20f));
    g.drawLine (juce::Line<float> (centre, needleTip), 8.0f);

    juce::ColourGradient needleGradient (
        juce::Colours::white.withAlpha (0.95f),
        needleTip.x,
        needleTip.y,
        lightColour.withAlpha (0.85f),
        centre.x,
        centre.y,
        false);

    g.setGradientFill (needleGradient);
    g.fillPath (needle);

    g.setColour ((redWarning ? warningRed : accent).withAlpha (redWarning ? 0.58f : 0.32f));
    g.strokePath (needle, juce::PathStrokeType (1.2f));

    // Perno centrale.
    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f)
        .withCentre (centre.translated (0.0f, 1.0f)));

    g.setColour (p.brass);
    g.fillEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (centre));

    g.setColour (p.brassDark.withAlpha (0.88f));
    g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f)
        .withCentre (centre)
        .reduced (0.5f),
        0.9f);

    // Vetro/lente sopra il quadrante.
    auto lens = bounds.reduced (3.0f);

    juce::ColourGradient lensGradient (
        juce::Colours::white.withAlpha (0.15f),
        lens.getX(),
        lens.getY(),
        juce::Colours::transparentWhite,
        lens.getX(),
        lens.getBottom(),
        false);

    g.setGradientFill (lensGradient);
    g.fillRoundedRectangle (lens, 10.0f);

    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.drawRoundedRectangle (lens.reduced (0.7f), 10.0f, 0.9f);

    // Testo.
    g.setColour (p.ink.withAlpha (0.88f));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (valueText,
                bounds.withTrimmedTop (bounds.getHeight() - 22.0f).toNearestInt(),
                juce::Justification::centred);
}
void Painter::drawCorrectionGauge (juce::Graphics& g,
                                   juce::Rectangle<int> bounds,
                                   float correctionCents,
                                   float glowCorrectionCents)
{
    const float limited = juce::jlimit (
        -100.0f,
        100.0f,
        std::isfinite (correctionCents) ? correctionCents : 0.0f);

    const float glowLimited = juce::jlimit (
        -100.0f,
        100.0f,
        std::isfinite (glowCorrectionCents) ? glowCorrectionCents : 0.0f);

    const float normalised = juce::jmap (limited, -100.0f, 100.0f, 0.0f, 1.0f);
    const float glowNormalised = juce::jmap (glowLimited, -100.0f, 100.0f, 0.0f, 1.0f);

    const bool redWarning = std::abs (limited) > 30.0f;

    drawNeedleMeter (g,
                     bounds.toFloat(),
                     normalised,
                     glowNormalised,
                     "Correction",
                     juce::String (correctionCents, 1) + " ct",
                     palette().warningRed,
                     redWarning);
}

void Painter::drawConsensusGauge (juce::Graphics& g,
                                  juce::Rectangle<int> bounds,
                                  float consensus,
                                  float glowConsensus)
{
    const float normalised = safeNormalise (consensus);
    const float glowNormalised = safeNormalise (glowConsensus);

    drawNeedleMeter (g,
                     bounds.toFloat(),
                     normalised,
                     glowNormalised,
                     "Consensus",
                     juce::String (normalised * 100.0f, 0) + "%",
                     palette().blueGlow,
                     false);
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

    // Più trasparente: deve sembrare uno strumento appoggiato al banco.
    drawPanel (g, f, 10.0f, 0.46f);

    auto area = bounds.reduced (14, 10);
    auto title = area.removeFromTop (20);

    g.setColour (p.ink.withAlpha (0.92f));
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText ("Target tuning", title, juce::Justification::centred);

    auto rail = area.reduced (4, 18).withHeight (20);
    rail.setCentre (area.getCentre());

    auto railF = rail.toFloat();
    auto frame = rail.expanded (9, 12).toFloat();

    // Ombra sotto la cornice.
    g.setColour (juce::Colours::black.withAlpha (0.32f));
    g.fillRoundedRectangle (frame.translated (0.0f, 1.6f), 7.0f);

    // Cornice ottone: laboratorio/liutaio.
    juce::ColourGradient frameGradient (
        p.brass.brighter (0.20f),
        frame.getX(),
        frame.getY(),
        p.brassDark.darker (0.18f),
        frame.getRight(),
        frame.getBottom(),
        true);

    g.setGradientFill (frameGradient);
    g.fillRoundedRectangle (frame, 7.0f);

    g.setColour (p.brassDark.withAlpha (0.92f));
    g.drawRoundedRectangle (frame.reduced (0.5f), 7.0f, 1.1f);

    g.setColour (juce::Colours::white.withAlpha (0.13f));
    g.drawRoundedRectangle (frame.reduced (2.0f), 5.5f, 0.8f);

    // Fessura interna, più analogica/radio.
    juce::ColourGradient railGradient (
        juce::Colour (0xFF07090D),
        railF.getX(),
        railF.getY(),
        juce::Colour (0xFF263149),
        railF.getX(),
        railF.getBottom(),
        false);

    g.setGradientFill (railGradient);
    g.fillRoundedRectangle (railF, 4.0f);

    g.setColour (p.glassEdge.withAlpha (0.72f));
    g.drawRoundedRectangle (railF.reduced (0.5f), 4.0f, 0.9f);

    // Tacche musicali: ottave e quinte.
    const auto drawFreqTick = [&] (float hz,
                                   float height,
                                   float thickness,
                                   juce::Colour colour)
    {
        if (hz < minimumHz || hz > maximumHz)
            return;

        const float norm = frequencyToLogPosition (hz, minimumHz, maximumHz);
        const float tickX = railF.getX() + norm * railF.getWidth();

        g.setColour (colour);
        g.drawLine (tickX,
                    railF.getCentreY() - height * 0.5f,
                    tickX,
                    railF.getCentreY() + height * 0.5f,
                    thickness);
    };

    // Ottave: tacche principali.
    for (float hz = minimumHz; hz <= maximumHz * 1.001f; hz *= 2.0f)
    {
        drawFreqTick (hz,
                      railF.getHeight() + 13.0f,
                      1.35f,
                      p.ink.withAlpha (0.72f));
    }

    // Quinte: tacche medie, meno invadenti.
    for (float hz = minimumHz * 1.5f; hz <= maximumHz * 1.001f; hz *= 2.0f)
    {
        drawFreqTick (hz,
                      railF.getHeight() + 6.0f,
                      0.95f,
                      p.brass.withAlpha (0.64f));
    }

    // Luce detected pitch: radio/sintetica.
    if (detectedHz > 0.0f)
    {
        const float detectedNorm = frequencyToLogPosition (detectedHz, minimumHz, maximumHz);
        const float x = railF.getX() + detectedNorm * railF.getWidth();

        const auto detectedColour = juce::Colour (0xFF20D8FF)
            .interpolatedWith (juce::Colour (0xFF9B5CFF), 0.35f);

        g.setColour (detectedColour.withAlpha (0.18f));
        g.fillRoundedRectangle (
            juce::Rectangle<float> (11.0f, railF.getHeight() + 20.0f)
                .withCentre ({ x, railF.getCentreY() }),
            3.0f);

        g.setColour (detectedColour.withAlpha (0.90f));
        g.drawLine (x,
                    railF.getY() - 10.0f,
                    x,
                    railF.getBottom() + 10.0f,
                    1.4f);
    }

    // Cursore target rosso: mantiene il gesto bello che già funziona,
    // ma diventa più fisico.
    const float targetNorm = frequencyToLogPosition (targetHz, minimumHz, maximumHz);
    const float targetX = railF.getX() + targetNorm * railF.getWidth();

    auto cursor = juce::Rectangle<float> (
        targetX - 5.0f,
        railF.getY() - 8.0f,
        10.0f,
        railF.getHeight() + 16.0f);

    juce::ColourGradient cursorGradient (
        juce::Colour (0xFFFF6B54),
        cursor.getX(),
        cursor.getY(),
        juce::Colour (0xFF8E1717),
        cursor.getRight(),
        cursor.getBottom(),
        true);

    g.setGradientFill (cursorGradient);
    g.fillRoundedRectangle (cursor, 2.0f);

    g.setColour (juce::Colours::black.withAlpha (0.34f));
    g.drawRoundedRectangle (cursor.reduced (0.2f), 2.0f, 1.0f);

    g.setColour (juce::Colours::white.withAlpha (0.48f));
    g.drawRoundedRectangle (cursor.reduced (0.8f), 1.5f, 0.75f);

    // Vetro sopra tutto il controllo.
    auto glass = frame.reduced (2.0f);

    juce::ColourGradient glassGradient (
        juce::Colours::white.withAlpha (0.13f),
        glass.getX(),
        glass.getY(),
        juce::Colours::transparentWhite,
        glass.getX(),
        glass.getBottom(),
        false);

    g.setGradientFill (glassGradient);
    g.fillRoundedRectangle (glass, 7.0f);

    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (glass.reduced (0.7f), 7.0f, 0.8f);

    // Testo Hz.
    auto text = area.removeFromBottom (18);

    g.setColour (p.ink.withAlpha (0.88f));
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
