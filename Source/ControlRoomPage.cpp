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

    const auto safe01 = [] (float value)
    {
        if (! std::isfinite (value))
            return 0.0f;

        return juce::jlimit (0.0f, 1.0f, value);
    };

    const auto percentText = [safe01] (float value)
    {
        return juce::String (safe01 (value) * 100.0f, 0) + "%";
    };

    const auto hzText = [] (float hz)
    {
        return hz > 0.0f ? juce::String (hz, 1) + " Hz"
                         : juce::String ("--");
    };

    // ---------------------------------------------------------------------
    // Base panel: più macchina aperta che tabella diagnostica.
    // ---------------------------------------------------------------------
    Painter::drawPanel (g, area.toFloat(), 12.0f, 0.74f);

    auto content = area.reduced (15, 11);

    auto title = content.removeFromTop (24);

    g.setColour (p.ink.withAlpha (0.92f));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Internal Machine", title, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (10.5f));
    g.setColour (p.ink.withAlpha (0.52f));
    g.drawText ("signal route / stability bus / texture return",
                title,
                juce::Justification::centredRight);

    auto footer = content.removeFromBottom (34);
    content.removeFromBottom (3);

    auto machine = content.reduced (2, 2);

    // ---------------------------------------------------------------------
    // Layout bands.
    // ---------------------------------------------------------------------
    const int topBusH = juce::jlimit (38, 50, machine.getHeight() / 4);
    const int bottomBusH = juce::jlimit (38, 50, machine.getHeight() / 4);

    auto topBand = machine.removeFromTop (topBusH);
    machine.removeFromTop (5);

    auto bottomBand = machine.removeFromBottom (bottomBusH);
    machine.removeFromBottom (5);

    auto routeBand = machine;

    // ---------------------------------------------------------------------
    // Main route: five nodes.
    // ---------------------------------------------------------------------
    auto route = routeBand.reduced (3, 3);

    const int gap = 7;
    const int usableW = route.getWidth() - gap * 4;

    const int inputW = usableW * 12 / 100;
    const int senseW = usableW * 22 / 100;
    const int referenceW = usableW * 20 / 100;
    const int coreW = usableW * 28 / 100;
    const int outputW = usableW - inputW - senseW - referenceW - coreW;

   auto takeNode = [&route, gap] (int width) -> juce::Rectangle<int>
{
    auto r = route.removeFromLeft (width);

    if (route.getWidth() > 0)
        route.removeFromLeft (gap);

    return r;
};

    auto inputNode = takeNode (inputW);
    auto senseNode = takeNode (senseW);
    auto referenceNode = takeNode (referenceW);
    auto coreNode = takeNode (coreW);
    auto outputNode = route;

    // Rendi il core un po' più “nobile” verticalmente.
    coreNode = coreNode.expanded (0, 3);

    // ---------------------------------------------------------------------
    // Colours.
    // ---------------------------------------------------------------------
    const auto electricBlue = juce::Colour (0xFF20D8FF);
    const auto syntheticViolet = juce::Colour (0xFF9B5CFF);
    const auto green = juce::Colour (0xFF39FF7A);
    const auto amber = juce::Colour (0xFFFFA02B);
    const auto red = juce::Colour (0xFFFF2A4A);

    const float correctionAmount = juce::jlimit (
        0.0f,
        1.0f,
        std::abs (static_cast<float> (metering_.correctionCents)) / 100.0f);

    const bool strongCorrection = std::abs (
        static_cast<float> (metering_.correctionCents)) > 30.0f;

    const auto coreColour = strongCorrection
        ? syntheticViolet.interpolatedWith (red, 0.45f)
        : syntheticViolet;

    // ---------------------------------------------------------------------
    // Helper: mini bar inside nodes/buses.
    // ---------------------------------------------------------------------
    const auto drawMiniBar = [&g] (juce::Rectangle<float> r,
                                   float value,
                                   juce::Colour colour)
    {
        value = juce::jlimit (0.0f, 1.0f, std::isfinite (value) ? value : 0.0f);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r, 2.5f);

        auto fill = r.reduced (1.2f);
        fill.setWidth (fill.getWidth() * value);

        if (fill.getWidth() > 1.0f)
        {
            g.setColour (colour.withAlpha (0.74f));
            g.fillRoundedRectangle (fill, 2.0f);

            g.setColour (colour.withAlpha (0.18f));
            g.fillRoundedRectangle (fill.expanded (0.0f, 1.2f), 2.5f);
        }

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (r.reduced (0.4f), 2.5f, 0.7f);
    };

    // ---------------------------------------------------------------------
    // Helper: exposed wire / luminous pipe.
    // ---------------------------------------------------------------------
    const auto drawLink = [&g] (juce::Point<float> a,
                                juce::Point<float> b,
                                juce::Colour colour,
                                float intensity)
    {
        intensity = juce::jlimit (0.0f, 1.0f, intensity);

        g.setColour (juce::Colours::black.withAlpha (0.42f));
        g.drawLine (juce::Line<float> (a.translated (0.0f, 1.2f),
                                       b.translated (0.0f, 1.2f)),
                    5.0f);

        g.setColour (juce::Colour (0xFF5B4020).withAlpha (0.88f));
        g.drawLine (juce::Line<float> (a, b), 3.1f);

        g.setColour (colour.withAlpha (0.20f + 0.32f * intensity));
        g.drawLine (juce::Line<float> (a, b), 5.2f);

        g.setColour (colour.withAlpha (0.55f + 0.30f * intensity));
        g.drawLine (juce::Line<float> (a, b), 1.35f);
    };

    // ---------------------------------------------------------------------
    // Helper: machine node.
    // ---------------------------------------------------------------------
    const auto drawNode = [&] (juce::Rectangle<int> r,
                               const juce::String& name,
                               const juce::String& line1,
                               const juce::String& line2,
                               juce::Colour accent,
                               float activity,
                               bool isCore)
    {
        activity = safe01 (activity);

        auto f = r.toFloat();

        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (f.translated (0.0f, 1.4f), isCore ? 9.0f : 7.0f);

        juce::ColourGradient body (
            juce::Colour (0xFF17110D),
            f.getX(),
            f.getY(),
            juce::Colour (0xFF07090D),
            f.getRight(),
            f.getBottom(),
            true);

        g.setGradientFill (body);
        g.fillRoundedRectangle (f, isCore ? 9.0f : 7.0f);

        if (isCore)
        {
            g.setColour (accent.withAlpha (0.16f + 0.24f * activity));
            g.fillRoundedRectangle (f.reduced (2.0f), 7.5f);
        }

        g.setColour (p.brassDark.withAlpha (0.84f));
        g.drawRoundedRectangle (f.reduced (0.5f), isCore ? 9.0f : 7.0f, isCore ? 1.2f : 0.9f);

        g.setColour (accent.withAlpha (0.22f + 0.42f * activity));
        g.drawRoundedRectangle (f.reduced (2.0f), isCore ? 7.0f : 5.5f, isCore ? 1.1f : 0.75f);

        auto inner = r.reduced (7, 5);

        auto nameArea = inner.removeFromTop (17);
        g.setFont (juce::FontOptions (isCore ? 11.5f : 10.5f, juce::Font::bold));
        g.setColour (p.ink.withAlpha (0.92f));
        g.drawFittedText (name, nameArea, juce::Justification::centred, 1);

        auto l1 = inner.removeFromTop (15);
        auto l2 = inner.removeFromTop (15);

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (juce::Colours::white.withAlpha (0.76f));
        g.drawFittedText (line1, l1, juce::Justification::centred, 1);

        g.setColour (juce::Colours::white.withAlpha (0.56f));
        g.drawFittedText (line2, l2, juce::Justification::centred, 1);

        auto bar = juce::Rectangle<float> (
            static_cast<float> (inner.getX()),
            static_cast<float> (r.getBottom() - 11),
            static_cast<float> (inner.getWidth()),
            5.0f);

        drawMiniBar (bar, activity, accent);

        // Piccolo “vetro” sopra il nodo.
        juce::ColourGradient glass (
            juce::Colours::white.withAlpha (0.10f),
            f.getX(),
            f.getY(),
            juce::Colours::transparentWhite,
            f.getX(),
            f.getBottom(),
            false);

        g.setGradientFill (glass);
        g.fillRoundedRectangle (f.reduced (2.0f), isCore ? 7.0f : 5.5f);
    };

    // ---------------------------------------------------------------------
    // Helper: bus blocks.
    // ---------------------------------------------------------------------
    const auto drawBus = [&] (juce::Rectangle<int> r,
                              const juce::String& name,
                              const juce::String& valueText,
                              juce::Colour accent,
                              float activity)
    {
        activity = safe01 (activity);

        auto f = r.toFloat();

        g.setColour (juce::Colours::black.withAlpha (0.26f));
        g.fillRoundedRectangle (f.translated (0.0f, 1.2f), 7.0f);

        g.setColour (juce::Colour (0xCC120D09));
        g.fillRoundedRectangle (f, 7.0f);

        g.setColour (p.brassDark.withAlpha (0.72f));
        g.drawRoundedRectangle (f.reduced (0.5f), 7.0f, 0.9f);

        g.setColour (accent.withAlpha (0.18f + 0.32f * activity));
        g.drawRoundedRectangle (f.reduced (2.0f), 5.0f, 0.8f);

        auto inner = r.reduced (9, 4);

        auto top = inner.removeFromTop (15);

        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.setColour (p.ink.withAlpha (0.88f));
        g.drawFittedText (name, top, juce::Justification::centredLeft, 1);

        g.setFont (juce::FontOptions (9.5f));
        g.setColour (juce::Colours::white.withAlpha (0.64f));
        g.drawFittedText (valueText, inner, juce::Justification::centredLeft, 1);

        auto bar = f.withTrimmedLeft (8.0f)
                    .withTrimmedRight (8.0f)
                    .withY (f.getBottom() - 8.0f)
                    .withHeight (4.0f);

        drawMiniBar (bar, activity, accent);
    };

    // ---------------------------------------------------------------------
    // Bus layout, aligned to the actual route nodes.
    // ---------------------------------------------------------------------
    auto stabilityBus = topBand.reduced (4, 2);
    stabilityBus.setLeft (senseNode.getX());
    stabilityBus.setRight (coreNode.getRight());

    auto textureBus = bottomBand.reduced (4, 2);
    textureBus.setLeft (senseNode.getX() + senseNode.getWidth() / 3);
    textureBus.setRight (outputNode.getRight());
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

    // ---------------------------------------------------------------------
    // Draw links first, so nodes sit on top.
    // ---------------------------------------------------------------------
    const auto linkIntensity = safe01 ((metering_.confidence + metering_.voicing) * 0.5f);

   
     drawLink (centreRight (inputNode),
          centreLeft (senseNode),
          electricBlue,
          linkIntensity);

drawLink (centreRight (senseNode),
          centreLeft (referenceNode),
          electricBlue.interpolatedWith (syntheticViolet, 0.35f),
          safe01 (metering_.confidence));

drawLink (centreRight (referenceNode),
          centreLeft (coreNode),
          syntheticViolet,
          safe01 (metering_.maskStability));

drawLink (centreRight (coreNode),
          centreLeft (outputNode),
          coreColour,
          juce::jmax (correctionAmount, safe01 (metering_.wetMix)));

drawLink (centreBottom (stabilityBus),
          centreTop (coreNode),
          green,
          safe01 ((metering_.consensus + metering_.maskStability) * 0.5f));

drawLink (centreTop (textureBus),
          centreBottom (coreNode),
          amber,
          safe01 ((metering_.breathiness + metering_.noisePath + metering_.harmonicity) / 3.0f));

    // ---------------------------------------------------------------------
    // Draw buses.
    // ---------------------------------------------------------------------
    drawBus (stabilityBus,
             "STABILITY BUS",
             "rel " + percentText (metering_.spectralReliability)
                + "   mask " + percentText (metering_.maskStability)
                + "   hold " + juce::String (metering_.sustainedNoteSeconds, 1) + "s",
             green,
             safe01 ((metering_.spectralReliability + metering_.maskStability + metering_.consensus) / 3.0f));

    drawBus (textureBus,
             "TEXTURE RETURN",
             "breath " + percentText (metering_.breathiness)
                + "   harm " + percentText (metering_.harmonicity)
                + "   noise " + percentText (metering_.noisePath)
                + "   poly " + percentText (metering_.polyphony),
             amber,
             safe01 ((metering_.breathiness + metering_.harmonicity + metering_.noisePath) / 3.0f));

    // ---------------------------------------------------------------------
    // Draw main nodes.
    // ---------------------------------------------------------------------
    drawNode (inputNode,
              "INPUT",
              "voice " + percentText (metering_.voicing),
              "conf " + percentText (metering_.confidence),
              electricBlue,
              safe01 ((metering_.voicing + metering_.confidence) * 0.5f),
              false);

    drawNode (senseNode,
              "PITCH SENSE",
              hzText (metering_.detectedPitchHz),
              "paths " + juce::String (metering_.detectorSupport) + "/4"
                + "   oct " + juce::String (metering_.octaveState),
              electricBlue.interpolatedWith (syntheticViolet, 0.25f),
              safe01 (metering_.confidence),
              false);

    drawNode (referenceNode,
              "REFERENCE",
              "target " + hzText (metering_.targetPitchHz),
              "mask " + percentText (metering_.maskStability),
              syntheticViolet,
              safe01 (metering_.maskStability),
              false);

    drawNode (coreNode,
              "CORRECTION CORE",
              juce::String (metering_.correctionCents, 1) + " ct",
              "cons " + percentText (metering_.consensus),
              coreColour,
              juce::jmax (correctionAmount, safe01 (metering_.consensus)),
              true);

    drawNode (outputNode,
              "OUTPUT",
              "wet " + percentText (metering_.wetMix),
              "GR " + juce::String (metering_.noiseReductionDb, 1) + " dB",
              amber,
              safe01 (metering_.wetMix),
              false);

    // ---------------------------------------------------------------------
    // Footer: compact diagnostic sentence.
    // ---------------------------------------------------------------------
    g.setColour (juce::Colours::black.withAlpha (0.24f));
    g.fillRoundedRectangle (footer.toFloat(), 6.0f);

    g.setColour (p.glassEdge.withAlpha (0.28f));
    g.drawRoundedRectangle (footer.toFloat().reduced (0.5f), 6.0f, 0.8f);

    g.setColour (p.ink.withAlpha (0.78f));
    g.setFont (juce::FontOptions (11.5f));

    juce::String line = "State: " + trackingStateToString (metering_.state)
        + "   |   Paths " + juce::String (metering_.detectorSupport) + "/4"
        + "   |   Octave " + juce::String (metering_.octaveState)
        + "   |   Confirm " + juce::String (metering_.pendingOctaveObservations);

    line += "   |   Tempo ";
    line += metering_.tempoActive ? "active" : "off";
    line += metering_.tempoHostSyncValid ? " / host" : " / free";
    line += "   |   BPM " + juce::String (metering_.tempoBpm, 1);

    g.drawFittedText (line,
                      footer.reduced (10, 4),
                      juce::Justification::centredLeft,
                      2);
}


