#include "ControlRoomPage.h"
#include "NeumatonUILabels.h"
#include <cmath>

namespace
{
[[nodiscard]] juce::String trackingStateToString (ModernPitchEngine::TrackingState state)
{
    switch (state)
    {
        case ModernPitchEngine::TrackingState::unvoiced:   return "Unvoiced";
        case ModernPitchEngine::TrackingState::attack:     return "Attack";
        case ModernPitchEngine::TrackingState::acquire:    return "Acquire";
        case ModernPitchEngine::TrackingState::stable:     return "Stable";
        case ModernPitchEngine::TrackingState::transition: return "Transition";
        case ModernPitchEngine::TrackingState::release:    return "Release";
    }

    return "Unknown";
}
} // namespace

ControlRoomPage::ControlRoomPage()
{
    backButton.onClick = [this]
    {
        if (onBack)
            onBack();
    };
    addAndMakeVisible (backButton);
}

void ControlRoomPage::setMetering (const LivePitchProcessor::Metering& newMetering)
{
    metering_ = newMetering;
    repaint();
}

void ControlRoomPage::paint (juce::Graphics& g)
{
    using neumaton::lab::Painter;
    const auto bounds = getLocalBounds();
    Painter::drawBackground (g, bounds, {});

    auto area = bounds.reduced (24, 18);
    drawHeader (g, area.removeFromTop (58));
    area.removeFromTop (10);

    auto topMeters = area.removeFromTop (138);
const int meterW = topMeters.getWidth() / 3;

Painter::drawCorrectionGauge (
    g,
    topMeters.removeFromLeft (meterW).reduced (4),
    static_cast<float> (metering_.correctionCents),
    static_cast<float> (metering_.correctionCents));

Painter::drawRadioTarget (
    g,
    topMeters.removeFromLeft (meterW).reduced (4),
    static_cast<float> (metering_.detectedPitchHz),
    static_cast<float> (metering_.targetPitchHz));

Painter::drawConsensusGauge (
    g,
    topMeters.reduced (4),
    static_cast<float> (metering_.consensus),
    static_cast<float> (metering_.consensus));

area.removeFromTop (12);
drawDiagnosticGrid (g, area);
}

void ControlRoomPage::resized()
{
    auto area = getLocalBounds().reduced (24, 18);
    backButton.setBounds (area.removeFromTop (32).removeFromLeft (88));
}

void ControlRoomPage::drawHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& p = neumaton::lab::palette();
    neumaton::lab::Painter::drawPanel (g, area.toFloat(), 12.0f, 0.92f);

    auto textArea = area.reduced (12, 0);
    textArea.removeFromLeft (104); // spazio per Back

    g.setColour (p.ink);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText (Neumaton::UI::Labels::Main::controlRoom, textArea.removeFromTop (30), juce::Justification::centred);

    g.setFont (juce::FontOptions (12.5f));
    g.setColour (p.ink.withAlpha (0.78f));

    juce::String status = "State: " + trackingStateToString (metering_.state)
        + "   |   Paths: " + juce::String (metering_.detectorSupport) + "/4"
        + "   |   Octave: " + juce::String (metering_.octaveState);

    if (metering_.dualSynthesisActive)
        status += "   |   Dual " + juce::String (metering_.transitionBlend * 100.0f, 0) + "%";

    g.drawText (status, textArea, juce::Justification::centred);
}
void ControlRoomPage::drawDiagnosticGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    using neumaton::lab::Painter;

    const auto& p = neumaton::lab::palette();

    const auto safe01 = [] (float value) -> float
    {
        if (! std::isfinite (value))
            return 0.0f;

        return juce::jlimit (0.0f, 1.0f, value);
    };

    const auto percentText = [&safe01] (float value) -> juce::String
    {
        return juce::String (safe01 (value) * 100.0f, 0) + "%";
    };

    const auto electricBlue     = juce::Colour (0xFF20D8FF);
    const auto analysisGreen    = juce::Colour (0xFF39FF7A);
    const auto syntheticViolet  = juce::Colour (0xFF9B5CFF);
    const auto amber            = juce::Colour (0xFFFFA02B);
    const auto warningRed       = juce::Colour (0xFFFF2A4A);
    const auto vapourBlue       = juce::Colour (0xFF9BE7FF);
    const auto harmonicGreen    = juce::Colour (0xFFB6FF7A);

    // ---------------------------------------------------------------------
    // Base panel.
    // ---------------------------------------------------------------------
    Painter::drawPanel (g, area.toFloat(), 12.0f, 0.72f);

    auto content = area.reduced (15, 11);

    auto title = content.removeFromTop (24);

    g.setColour (p.ink.withAlpha (0.94f));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Internal Machine", title, juce::Justification::centredLeft);

    g.setColour (p.ink.withAlpha (0.52f));
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("audio path / analysis taps / correction logic",
                title,
                juce::Justification::centredRight);

    auto footer = content.removeFromBottom (28);
    content.removeFromBottom (3);

    auto machine = content.reduced (2, 1);

    // ---------------------------------------------------------------------
    // Geometry helpers.
    // ---------------------------------------------------------------------
    const auto makeRect = [machine] (float centreX,
                                     float centreY,
                                     float widthNorm,
                                     float heightNorm) -> juce::Rectangle<int>
    {
        const int w = juce::jmax (34, juce::roundToInt (
            static_cast<float> (machine.getWidth()) * widthNorm));

        const int h = juce::jmax (28, juce::roundToInt (
            static_cast<float> (machine.getHeight()) * heightNorm));

        const int cx = machine.getX() + juce::roundToInt (
            static_cast<float> (machine.getWidth()) * centreX);

        const int cy = machine.getY() + juce::roundToInt (
            static_cast<float> (machine.getHeight()) * centreY);

        return juce::Rectangle<int> (cx - w / 2, cy - h / 2, w, h)
            .getIntersection (machine);
    };

    const auto centreLeft = [] (juce::Rectangle<int> r) -> juce::Point<float>
    {
        return {
            static_cast<float> (r.getX()),
            static_cast<float> (r.getCentreY())
        };
    };

    const auto centreRight = [] (juce::Rectangle<int> r) -> juce::Point<float>
    {
        return {
            static_cast<float> (r.getRight()),
            static_cast<float> (r.getCentreY())
        };
    };

    const auto centreTop = [] (juce::Rectangle<int> r) -> juce::Point<float>
    {
        return {
            static_cast<float> (r.getCentreX()),
            static_cast<float> (r.getY())
        };
    };

    const auto centreBottom = [] (juce::Rectangle<int> r) -> juce::Point<float>
    {
        return {
            static_cast<float> (r.getCentreX()),
            static_cast<float> (r.getBottom())
        };
    };

    const auto rectCentre = [] (juce::Rectangle<int> r) -> juce::Point<float>
    {
        return {
            static_cast<float> (r.getCentreX()),
            static_cast<float> (r.getCentreY())
        };
    };

    // ---------------------------------------------------------------------
    // Main audio path nodes.
    // ---------------------------------------------------------------------
    auto inputNode = makeRect (0.075f, 0.50f, 0.095f, 0.22f);
    auto senseTap  = makeRect (0.315f, 0.50f, 0.125f, 0.22f);
    auto coreNode  = makeRect (0.675f, 0.50f, 0.205f, 0.31f);
    auto outputNode = makeRect (0.925f, 0.50f, 0.105f, 0.22f);

    // ---------------------------------------------------------------------
    // Eight instruments: one parameter, one instrument, one value.
    // ---------------------------------------------------------------------
    auto voicingInstrument = makeRect (0.145f, 0.18f, 0.145f, 0.23f);
    auto confidenceInstrument = makeRect (0.330f, 0.18f, 0.145f, 0.23f);
    auto spectralInstrument = makeRect (0.600f, 0.18f, 0.170f, 0.23f);
    auto maskInstrument = makeRect (0.790f, 0.18f, 0.150f, 0.23f);

    auto breathInstrument = makeRect (0.145f, 0.82f, 0.145f, 0.23f);
    auto harmonicInstrument = makeRect (0.330f, 0.82f, 0.155f, 0.23f);
    auto noiseInstrument = makeRect (0.535f, 0.82f, 0.155f, 0.23f);
    auto polyInstrument = makeRect (0.745f, 0.82f, 0.145f, 0.23f);

    const float analysisActivity = safe01 (
        (metering_.voicing + metering_.confidence) * 0.5f);

    const float correctionActivity = juce::jlimit (
        0.0f,
        1.0f,
        std::abs (static_cast<float> (metering_.correctionCents)) / 100.0f);

    const bool strongCorrection =
        std::abs (static_cast<float> (metering_.correctionCents)) > 30.0f;

    const auto coreColour = strongCorrection
        ? syntheticViolet.interpolatedWith (warningRed, 0.42f)
        : syntheticViolet;

    // ---------------------------------------------------------------------
    // Link drawing: audio path and analysis probes.
    // ---------------------------------------------------------------------
    const auto drawCable = [&g] (juce::Point<float> a,
                                 juce::Point<float> b,
                                 juce::Colour colour,
                                 float activity,
                                 float width,
                                 bool audioPath)
    {
        activity = juce::jlimit (0.0f, 1.0f, activity);

        // Shadow.
        g.setColour (juce::Colours::black.withAlpha (audioPath ? 0.46f : 0.34f));
        g.drawLine (juce::Line<float> (a.translated (0.0f, 1.2f),
                                       b.translated (0.0f, 1.2f)),
                    width + (audioPath ? 3.0f : 1.6f));

        // Brass / dark body.
        g.setColour (juce::Colour (0xFF5B4020).withAlpha (audioPath ? 0.92f : 0.66f));
        g.drawLine (juce::Line<float> (a, b), width);

        // Inner light.
        g.setColour (colour.withAlpha ((audioPath ? 0.22f : 0.14f) + 0.34f * activity));
        g.drawLine (juce::Line<float> (a, b), width + (audioPath ? 2.4f : 1.0f));

        g.setColour (colour.withAlpha ((audioPath ? 0.62f : 0.48f) + 0.28f * activity));
        g.drawLine (juce::Line<float> (a, b), audioPath ? 1.55f : 0.95f);
    };

    // ---------------------------------------------------------------------
    // Node drawing: process nodes show no fast values, only process position.
    // ---------------------------------------------------------------------
    const auto drawProcessNode = [&] (juce::Rectangle<int> r,
                                      const juce::String& label,
                                      juce::Colour accent,
                                      float activity,
                                      bool core)
    {
        activity = safe01 (activity);

        auto f = r.toFloat();
        const float corner = core ? 10.0f : 7.0f;

        g.setColour (juce::Colours::black.withAlpha (0.34f));
        g.fillRoundedRectangle (f.translated (0.0f, 1.4f), corner);

        juce::ColourGradient body (
            juce::Colour (0xFF17110D),
            f.getX(),
            f.getY(),
            juce::Colour (0xFF07090D),
            f.getRight(),
            f.getBottom(),
            true);

        g.setGradientFill (body);
        g.fillRoundedRectangle (f, corner);

        if (core)
        {
            g.setColour (accent.withAlpha (0.14f + 0.28f * activity));
            g.fillRoundedRectangle (f.reduced (2.0f), corner - 2.0f);

            if (strongCorrection)
            {
                g.setColour (warningRed.withAlpha (0.18f));
                g.fillRoundedRectangle (f.reduced (5.0f), corner - 3.0f);
            }
        }

        g.setColour (p.brassDark.withAlpha (0.88f));
        g.drawRoundedRectangle (f.reduced (0.5f), corner, core ? 1.25f : 0.9f);

        g.setColour (accent.withAlpha (0.28f + 0.42f * activity));
        g.drawRoundedRectangle (f.reduced (2.0f), corner - 2.0f, core ? 1.2f : 0.8f);

        // Small glass wash.
        juce::ColourGradient glass (
            juce::Colours::white.withAlpha (0.10f),
            f.getX(),
            f.getY(),
            juce::Colours::transparentWhite,
            f.getX(),
            f.getBottom(),
            false);

        g.setGradientFill (glass);
        g.fillRoundedRectangle (f.reduced (2.0f), corner - 2.0f);

        g.setFont (juce::FontOptions (core ? 11.5f : 10.5f, juce::Font::bold));
        g.setColour (p.ink.withAlpha (0.94f));
        g.drawFittedText (label, r.reduced (5, 3), juce::Justification::centred, 2);
    };

    // ---------------------------------------------------------------------
    // Instrument drawing.
    // ---------------------------------------------------------------------
    enum class InstrumentKind
    {
        vessel,
        lock,
        resonance,
        branch,
        focus,
        clamp
    };

    const auto drawMiniValueBar = [&g] (juce::Rectangle<float> r,
                                        float value,
                                        juce::Colour accent)
    {
        value = juce::jlimit (0.0f, 1.0f, std::isfinite (value) ? value : 0.0f);

        g.setColour (juce::Colours::black.withAlpha (0.34f));
        g.fillRoundedRectangle (r, 2.5f);

        auto fill = r.reduced (1.0f);
        fill.setWidth (fill.getWidth() * value);

        if (fill.getWidth() > 1.0f)
        {
            g.setColour (accent.withAlpha (0.72f));
            g.fillRoundedRectangle (fill, 2.0f);

            g.setColour (accent.withAlpha (0.20f));
            g.fillRoundedRectangle (fill.expanded (0.0f, 1.0f), 2.5f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.11f));
        g.drawRoundedRectangle (r.reduced (0.4f), 2.5f, 0.7f);
    };

    const auto drawInstrument = [&] (juce::Rectangle<int> r,
                                     const juce::String& label,
                                     float value,
                                     juce::Colour accent,
                                     InstrumentKind kind)
    {
        value = safe01 (value);

        auto f = r.toFloat();

        g.setColour (juce::Colours::black.withAlpha (0.27f));
        g.fillRoundedRectangle (f.translated (0.0f, 1.1f), 7.0f);

        g.setColour (juce::Colour (0xD0100B08));
        g.fillRoundedRectangle (f, 7.0f);

        g.setColour (p.brassDark.withAlpha (0.70f));
        g.drawRoundedRectangle (f.reduced (0.5f), 7.0f, 0.8f);

        g.setColour (accent.withAlpha (0.20f + 0.34f * value));
        g.drawRoundedRectangle (f.reduced (2.0f), 5.5f, 0.75f);

        auto inner = r.reduced (6, 4);

        auto labelArea = inner.removeFromTop (14);
        auto valueArea = inner.removeFromBottom (13);
        auto instrumentArea = inner.reduced (2, 1).toFloat();

        g.setFont (juce::FontOptions (8.8f, juce::Font::bold));
        g.setColour (p.ink.withAlpha (0.90f));
        g.drawFittedText (label, labelArea, juce::Justification::centred, 1);

        // Common dark glass well.
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (instrumentArea, 4.0f);

        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (instrumentArea.reduced (0.5f), 4.0f, 0.7f);

        if (kind == InstrumentKind::vessel)
        {
            auto chamber = instrumentArea.reduced (instrumentArea.getWidth() * 0.30f, 3.0f);

            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (chamber, 5.0f);

            auto fill = chamber.reduced (2.0f);
            const float fullH = fill.getHeight();
            fill.setY (fill.getBottom() - fullH * value);
            fill.setHeight (fullH * value);

            g.setColour (accent.withAlpha (0.28f));
            g.fillRoundedRectangle (fill.expanded (2.0f, 1.0f), 4.0f);

            g.setColour (accent.withAlpha (0.78f));
            g.fillRoundedRectangle (fill, 3.5f);

            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.drawRoundedRectangle (chamber.reduced (0.4f), 5.0f, 0.8f);
        }
        else if (kind == InstrumentKind::lock || kind == InstrumentKind::focus || kind == InstrumentKind::clamp)
        {
            const auto c = instrumentArea.getCentre();
            const float radius = juce::jmin (instrumentArea.getWidth(), instrumentArea.getHeight()) * 0.33f;

            g.setColour (accent.withAlpha (0.14f + 0.20f * value));
            g.fillEllipse (juce::Rectangle<float> (radius * 2.1f, radius * 2.1f).withCentre (c));

            g.setColour (juce::Colours::white.withAlpha (0.16f + 0.20f * value));
            g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c), 0.9f);

            const float start = -2.35f;
            const float end = 0.75f;
            const float angle = juce::jmap (value, start, end);

            const auto needleEnd = c + juce::Point<float> (std::cos (angle), std::sin (angle)) * radius;

            g.setColour (accent.withAlpha (0.70f));
            g.drawLine (juce::Line<float> (c, needleEnd), 1.4f);

            if (kind == InstrumentKind::clamp)
            {
                const float clampW = radius * (0.65f + 0.35f * value);
                g.setColour (p.brass.withAlpha (0.72f));
                g.drawLine (c.x - clampW, c.y + radius * 0.52f,
                            c.x + clampW, c.y + radius * 0.52f,
                            1.5f);
            }
        }
        else if (kind == InstrumentKind::resonance)
        {
            const float x1 = instrumentArea.getX() + 6.0f;
            const float x2 = instrumentArea.getRight() - 6.0f;
            const float cy = instrumentArea.getCentreY();

            for (int i = -1; i <= 1; ++i)
            {
                const float y = cy + static_cast<float> (i) * 5.0f;
                const float alpha = 0.20f + 0.55f * value;

                juce::Path wave;
                wave.startNewSubPath (x1, y);

                const float amp = 1.5f + value * 3.5f;
                wave.cubicTo (x1 + (x2 - x1) * 0.25f, y - amp,
                              x1 + (x2 - x1) * 0.50f, y + amp,
                              x1 + (x2 - x1) * 0.75f, y - amp);
                wave.cubicTo (x1 + (x2 - x1) * 0.86f, y - amp * 0.4f,
                              x2 - 2.0f, y + amp * 0.4f,
                              x2, y);

                g.setColour (accent.withAlpha (alpha));
                g.strokePath (wave, juce::PathStrokeType (0.9f + value * 0.8f));
            }
        }
        else if (kind == InstrumentKind::branch)
        {
            const auto c = instrumentArea.getCentre();

            const float left = instrumentArea.getX() + 6.0f;
            const float right = instrumentArea.getRight() - 6.0f;

            const auto startPoint = juce::Point<float> (left, c.y);
            const auto midPoint = juce::Point<float> (c.x, c.y);

            g.setColour (accent.withAlpha (0.34f + 0.44f * value));
            g.drawLine (juce::Line<float> (startPoint, midPoint), 1.3f);

            const int activeBranches = 1 + juce::roundToInt (value * 2.0f);

            for (int i = 0; i < 3; ++i)
            {
                const float offset = static_cast<float> (i - 1) * 5.0f;
                const auto endPoint = juce::Point<float> (right, c.y + offset);

                g.setColour (accent.withAlpha (i < activeBranches ? 0.78f : 0.18f));
                g.drawLine (juce::Line<float> (midPoint, endPoint), i < activeBranches ? 1.4f : 0.8f);
            }

            g.setColour (juce::Colours::white.withAlpha (0.16f));
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (midPoint));
        }

        // Small value bar and small numeric readout.
        drawMiniValueBar (
            juce::Rectangle<float> (
                static_cast<float> (valueArea.getX() + 2),
                static_cast<float> (valueArea.getY() + 1),
                static_cast<float> (valueArea.getWidth() - 4),
                4.0f),
            value,
            accent);

        g.setFont (juce::FontOptions (8.5f));
        g.setColour (juce::Colours::white.withAlpha (0.62f));
        g.drawFittedText (percentText (value),
                          valueArea.withTrimmedTop (4),
                          juce::Justification::centred,
                          1);

        // Glass wash.
        juce::ColourGradient glass (
            juce::Colours::white.withAlpha (0.08f),
            f.getX(),
            f.getY(),
            juce::Colours::transparentWhite,
            f.getX(),
            f.getBottom(),
            false);

        g.setGradientFill (glass);
        g.fillRoundedRectangle (f.reduced (2.0f), 5.5f);
    };

    // ---------------------------------------------------------------------
    // Draw audio path first.
    // ---------------------------------------------------------------------
    drawCable (centreRight (inputNode),
               centreLeft (senseTap),
               electricBlue,
               analysisActivity,
               5.2f,
               true);

    drawCable (centreRight (senseTap),
               centreLeft (coreNode),
               electricBlue.interpolatedWith (syntheticViolet, 0.42f),
               safe01 (metering_.confidence),
               5.2f,
               true);

    drawCable (centreRight (coreNode),
               centreLeft (outputNode),
               coreColour,
               juce::jmax (correctionActivity, safe01 (metering_.maskStability)),
               5.2f,
               true);

    // ---------------------------------------------------------------------
    // Instrument probes.
    // ---------------------------------------------------------------------
    drawCable (centreBottom (voicingInstrument),
               centreTop (inputNode),
               analysisGreen,
               safe01 (metering_.voicing),
               1.8f,
               false);

    drawCable (centreBottom (confidenceInstrument),
               centreTop (senseTap),
               electricBlue,
               safe01 (metering_.confidence),
               1.8f,
               false);

    drawCable (centreBottom (spectralInstrument),
               centreTop (coreNode),
               vapourBlue,
               safe01 (metering_.spectralReliability),
               1.8f,
               false);

    drawCable (centreBottom (maskInstrument),
               centreTop (coreNode).translated (coreNode.getWidth() * 0.18f, 0.0f),
               analysisGreen,
               safe01 (metering_.maskStability),
               1.8f,
               false);

    drawCable (centreTop (breathInstrument),
               centreBottom (senseTap).translated (-senseTap.getWidth() * 0.18f, 0.0f),
               vapourBlue,
               safe01 (metering_.breathiness),
               1.7f,
               false);

    drawCable (centreTop (harmonicInstrument),
               centreBottom (senseTap).translated (senseTap.getWidth() * 0.18f, 0.0f),
               harmonicGreen,
               safe01 (metering_.harmonicity),
               1.7f,
               false);

    drawCable (centreTop (noiseInstrument),
               centreBottom (senseTap).translated (senseTap.getWidth() * 0.45f, 0.0f),
               amber,
               safe01 (metering_.noisePath),
               1.7f,
               false);

    drawCable (centreTop (noiseInstrument).translated (noiseInstrument.getWidth() * 0.18f, 0.0f),
               centreBottom (coreNode).translated (-coreNode.getWidth() * 0.20f, 0.0f),
               amber,
               safe01 (metering_.noisePath),
               1.3f,
               false);

    drawCable (centreTop (polyInstrument),
               centreBottom (coreNode).translated (coreNode.getWidth() * 0.20f, 0.0f),
               syntheticViolet,
               safe01 (metering_.polyphony),
               1.7f,
               false);

    // ---------------------------------------------------------------------
    // Draw nodes.
    // ---------------------------------------------------------------------
    drawProcessNode (inputNode,
                     "INPUT",
                     electricBlue,
                     safe01 (metering_.voicing),
                     false);

    drawProcessNode (senseTap,
                     "SENSE TAP",
                     electricBlue.interpolatedWith (analysisGreen, 0.30f),
                     analysisActivity,
                     false);

    drawProcessNode (coreNode,
                     "CORRECTION CORE",
                     coreColour,
                     juce::jmax (correctionActivity, safe01 (metering_.maskStability)),
                     true);

    drawProcessNode (outputNode,
                     "OUTPUT",
                     amber,
                     juce::jmax (correctionActivity, safe01 (metering_.maskStability)),
                     false);

    // ---------------------------------------------------------------------
    // Draw instruments on top.
    // ---------------------------------------------------------------------
    drawInstrument (voicingInstrument,
                    "VOICING",
                    metering_.voicing,
                    analysisGreen,
                    InstrumentKind::vessel);

    drawInstrument (confidenceInstrument,
                    "CONFIDENCE",
                    metering_.confidence,
                    electricBlue,
                    InstrumentKind::lock);

    drawInstrument (spectralInstrument,
                    "SPECTRAL",
                    metering_.spectralReliability,
                    vapourBlue,
                    InstrumentKind::focus);

    drawInstrument (maskInstrument,
                    "MASK",
                    metering_.maskStability,
                    analysisGreen,
                    InstrumentKind::clamp);

    drawInstrument (breathInstrument,
                    "BREATH",
                    metering_.breathiness,
                    vapourBlue,
                    InstrumentKind::vessel);

    drawInstrument (harmonicInstrument,
                    "HARMONIC",
                    metering_.harmonicity,
                    harmonicGreen,
                    InstrumentKind::resonance);

    drawInstrument (noiseInstrument,
                    "NOISE PATH",
                    metering_.noisePath,
                    amber,
                    InstrumentKind::branch);

    drawInstrument (polyInstrument,
                    "POLYPHONY",
                    metering_.polyphony,
                    syntheticViolet,
                    InstrumentKind::branch);

    // ---------------------------------------------------------------------
    // Footer: compact debug strip, not the protagonist of the page.
    // ---------------------------------------------------------------------
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRoundedRectangle (footer.toFloat(), 6.0f);

    g.setColour (p.glassEdge.withAlpha (0.28f));
    g.drawRoundedRectangle (footer.toFloat().reduced (0.5f), 6.0f, 0.8f);

    g.setColour (p.ink.withAlpha (0.76f));
    g.setFont (juce::FontOptions (11.0f));

    juce::String line = "State: " + trackingStateToString (metering_.state)
        + "   |   Paths " + juce::String (metering_.detectorSupport) + "/4"
        + "   |   Octave " + juce::String (metering_.octaveState);

    if (metering_.pendingOctaveObservations > 0)
        line += "   |   Confirm " + juce::String (metering_.pendingOctaveObservations);

    line += "   |   Tempo ";
    line += metering_.tempoActive ? "active" : "off";
    line += metering_.tempoHostSyncValid ? " / host" : " / free";

    g.drawFittedText (line,
                      footer.reduced (10, 4),
                      juce::Justification::centredLeft,
                      2);
}
