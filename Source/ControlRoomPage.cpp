#include "ControlRoomPage.h"
#include "NeumatonUILabels.h"

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
    Painter::drawCorrectionGauge (g,
                                  topMeters.removeFromLeft (meterW).reduced (4),
                                  metering_.correctionCents);
    Painter::drawRadioTarget (g,
                              topMeters.removeFromLeft (meterW).reduced (4),
                              metering_.detectedPitchHz,
                              metering_.targetPitchHz);
  Painter::drawConsensusGauge ( g,
                                  juce::Rectangle<int> bounds,
                                  float consensus,
                                  float glowConsensus)

}
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

    Painter::drawPanel (g, area.toFloat(), 12.0f, 0.88f);
    auto content = area.reduced (16, 12);

    auto title = content.removeFromTop (24);
    g.setColour (p.ink);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Detailed Metering", title, juce::Justification::centredLeft);

    const auto drawPair = [&] (juce::String nameA, float valueA, juce::Colour colourA,
                              juce::String nameB, float valueB, juce::Colour colourB)
    {
        auto row = content.removeFromTop (28);
        auto left = row.removeFromLeft (row.getWidth() / 2).reduced (4, 0);
        auto right = row.reduced (4, 0);
        Painter::drawGlassBar (g, left, valueA, nameA, colourA);
        Painter::drawGlassBar (g, right, valueB, nameB, colourB);
        content.removeFromTop (2);
    };

    drawPair (Neumaton::UI::Labels::Meter::confidence, metering_.confidence, p.blueGlow,
              Neumaton::UI::Labels::Meter::voicing, metering_.voicing, p.greenFluid);
    drawPair (Neumaton::UI::Labels::Meter::breath, metering_.breathiness, juce::Colour (0xFF5BC0EB),
              Neumaton::UI::Labels::Meter::harmonic, metering_.harmonicity, juce::Colour (0xFF9BE564));
    drawPair (Neumaton::UI::Labels::Meter::noisePath, metering_.noisePath, juce::Colour (0xFFFFD166),
              "Poly", metering_.polyphony, juce::Colour (0xFFB892FF));
    drawPair ("Reliability", metering_.spectralReliability, p.brass,
              "Mask", metering_.maskStability, p.warningRed);

    content.removeFromTop (4);
    g.setColour (p.ink.withAlpha (0.78f));
    g.setFont (juce::FontOptions (12.0f));

    juce::String line = "Detected: "
        + (metering_.detectedPitchHz > 0.0f ? juce::String (metering_.detectedPitchHz, 1) + " Hz" : "--")
        + "   |   Target: "
        + (metering_.targetPitchHz > 0.0f ? juce::String (metering_.targetPitchHz, 1) + " Hz" : "--")
        + "   |   Breath GR: " + juce::String (metering_.noiseReductionDb, 1) + " dB"
        + "   |   Hold: " + juce::String (metering_.sustainedNoteSeconds, 1) + " s";

    if (metering_.pendingOctaveObservations > 0)
        line += "   |   Confirm: " + juce::String (metering_.pendingOctaveObservations);

    g.drawFittedText (line, content.removeFromTop (34), juce::Justification::centredLeft, 2);

    juce::String tempo = "Tempo: ";
    tempo += metering_.tempoActive ? "active" : "off";
    tempo += metering_.tempoHostSyncValid ? " / host sync" : " / free";
    tempo += "   |   BPM " + juce::String (metering_.tempoBpm, 1);
    tempo += "   |   Glide " + juce::String (metering_.tempoGlideTimeMs, 1) + " ms";

    g.drawFittedText (tempo, content.removeFromTop (30), juce::Justification::centredLeft, 2);
}
