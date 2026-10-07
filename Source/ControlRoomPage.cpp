#include "ControlRoomPage.h"
#include "NeumatonUILabels.h"
#include <cmath>

namespace
{
juce::String stateText (LivePitchProcessor::TrackingState state)
{
    switch (state)
    {
        case LivePitchProcessor::TrackingState::stable:     return "Stable";
        case LivePitchProcessor::TrackingState::transition: return "Transition";
        case LivePitchProcessor::TrackingState::acquire:    return "Acquire";
        default:                                          return "Unvoiced";
    }
}

float safe01 (float value)
{
    return std::isfinite (value) ? juce::jlimit (0.0f, 1.0f, value) : 0.0f;
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

void ControlRoomPage::setPresentation (const LivePitchProcessor::Metering& newMetering,
                                       const neumaton::ui::ScaleDegreeDisplay& degree,
                                       int latencySamples, double sampleRate,
                                       bool analogTexture, float outputDb)
{
    metering_ = newMetering;
    degree_ = degree;
    latencySamples_ = latencySamples;
    sampleRate_ = sampleRate;
    analogTexture_ = analogTexture;
    outputDb_ = outputDb;
    if (isVisible())
        repaint();
}

void ControlRoomPage::paint (juce::Graphics& g)
{
    using neumaton::lab::Painter;
    Painter::drawBackground (g, getLocalBounds(), {});
    auto area = getLocalBounds().reduced (24, 18);
    drawHeader (g, area.removeFromTop (58));
    area.removeFromTop (10);
    auto meters = area.removeFromTop (124);
    const int width = meters.getWidth() / 3;
    Painter::drawCorrectionGauge (g, meters.removeFromLeft (width).reduced (4),
        metering_.correctionCents, metering_.correctionCents);
    Painter::drawRadioTarget (g, meters.removeFromLeft (width).reduced (4),
        metering_.detectedPitchHz, metering_.targetPitchHz);
    Painter::drawScaleDegree (g, meters.reduced (4), degree_);
    area.removeFromTop (10);
    drawDiagnosticGrid (g, area);
}

void ControlRoomPage::resized()
{
    backButton.setBounds (24, 18, 88, 32);
}

void ControlRoomPage::drawHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& p = neumaton::lab::palette();
    neumaton::lab::Painter::drawPanel (g, area.toFloat(), 12.0f, 0.92f);
    auto textArea = area.reduced (12, 0).withTrimmedLeft (104).withTrimmedRight (124);
    g.setColour (p.ink);
    g.setFont (juce::FontOptions (21.0f, juce::Font::bold));
    g.drawText (Neumaton::UI::Labels::Main::controlRoom,
                textArea.removeFromTop (30), juce::Justification::centred);
    g.setFont (juce::FontOptions (11.5f));
    g.setColour (p.ink.withAlpha (0.78f));
    juce::String latencyText = juce::String (latencySamples_) + " samples";
    if (std::isfinite (sampleRate_) && sampleRate_ > 0.0)
        latencyText += " / " + juce::String (1000.0 * latencySamples_ / sampleRate_, 2) + " ms";
    g.drawFittedText (stateText (metering_.state) + "  |  " + latencyText,
                     textArea, juce::Justification::centred, 1);
}

void ControlRoomPage::drawDiagnosticGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    using neumaton::lab::Painter;
    const auto& p = neumaton::lab::palette();
    const auto audio = juce::Colour (0xFF20D8FF);
    const auto analysis = juce::Colour (0xFF39FF7A);
    const auto target = juce::Colour (0xFF9B5CFF);
    const auto amber = juce::Colour (0xFFFFA02B);
    const bool stable = metering_.state == LivePitchProcessor::TrackingState::stable;
    const bool analysisActive = stable || metering_.state == LivePitchProcessor::TrackingState::acquire;
    const bool hasTarget = std::isfinite (metering_.targetPitchHz) && metering_.targetPitchHz > 0.0f;

    Painter::drawPanel (g, area.toFloat(), 12.0f, 0.72f);
    auto content = area.reduced (14, 10);
    auto title = content.removeFromTop (24);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.setColour (p.ink);
    g.drawText ("Internal Machine", title, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (10.5f));
    g.setColour (p.ink.withAlpha (0.62f));
    g.drawText ("V1 / one audio path", title, juce::Justification::centredRight);

    auto legend = content.removeFromBottom (18);
    auto metrics = content.removeFromBottom (46);
    content.removeFromBottom (8);
    auto machine = content.reduced (2, 3);
    constexpr int gap = 14;
    const int nodeW = (machine.getWidth() - 3 * gap) / 4;
    const int nodeH = juce::jlimit (46, 64, (machine.getHeight() - 26) / 2);
    const int upperY = machine.getY();
    const int lowerY = machine.getBottom() - nodeH;
    const auto node = [&] (int column, int y)
    {
        return juce::Rectangle<int> (machine.getX() + column * (nodeW + gap), y, nodeW, nodeH);
    };
    const auto inputNode = node (0, upperY);
    const auto gateNode = node (1, upperY);
    const auto detectorNode = node (2, upperY);
    const auto targetNode = node (3, upperY);
    const auto rendererNode = node (2, lowerY);
    const auto outputNode = node (3, lowerY);

    const auto right = [] (juce::Rectangle<int> r) { return r.toFloat().getCentre().withX (static_cast<float> (r.getRight())); };
    const auto left = [] (juce::Rectangle<int> r) { return r.toFloat().getCentre().withX (static_cast<float> (r.getX())); };
    const auto bottom = [] (juce::Rectangle<int> r) { return r.toFloat().getCentre().withY (static_cast<float> (r.getBottom())); };
    const auto top = [] (juce::Rectangle<int> r) { return r.toFloat().getCentre().withY (static_cast<float> (r.getY())); };
    const auto cable = [&] (juce::Point<float> a, juce::Point<float> b,
                            juce::Colour colour, bool active)
    {
        g.setColour (p.brassDark.withAlpha (0.80f));
        g.drawLine (juce::Line<float> (a, b), 4.0f);
        g.setColour (colour.withAlpha (active ? 0.90f : 0.30f));
        g.drawLine (juce::Line<float> (a, b), 1.6f);
        const auto direction = b - a;
        const float length = direction.getDistanceFromOrigin();
        if (length > 1.0f)
        {
            const auto unit = direction / length;
            const auto perpendicular = juce::Point<float> (-unit.y, unit.x);
            juce::Path arrow;
            arrow.startNewSubPath (b);
            arrow.lineTo (b - unit * 5.0f + perpendicular * 2.8f);
            arrow.lineTo (b - unit * 5.0f - perpendicular * 2.8f);
            arrow.closeSubPath();
            g.fillPath (arrow);
        }
    };
    cable (right (inputNode), left (gateNode), analysis, analysisActive);
    cable (right (gateNode), left (detectorNode), analysis, analysisActive);
    cable (right (detectorNode), left (targetNode), target, stable);
    cable (bottom (targetNode), top (rendererNode), hasTarget && ! stable ? amber : target, hasTarget);
    const auto audioCorner = bottom (inputNode).withY (left (rendererNode).y);
    cable (bottom (inputNode), audioCorner, audio, true);
    cable (audioCorner, left (rendererNode), audio, true);
    cable (right (rendererNode), left (outputNode), audio, true);

    const auto process = [&] (juce::Rectangle<int> bounds, const juce::String& name,
                              const juce::String& value, juce::Colour accent, bool active)
    {
        Painter::drawPanel (g, bounds.toFloat(), 7.0f, 0.94f);
        g.setColour (accent.withAlpha (active ? 0.80f : 0.28f));
        g.drawRoundedRectangle (bounds.toFloat().reduced (1.5f), 6.0f, 1.2f);
        auto text = bounds.reduced (5, 5);
        g.setColour (p.ink.withAlpha (0.95f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawFittedText (name, text.removeFromTop (18), juce::Justification::centred, 1);
        g.setFont (juce::FontOptions (10.0f));
        g.setColour (accent.withAlpha (0.90f));
        g.drawFittedText (value, text, juce::Justification::centred, 2);
    };
    process (inputNode, "Input", "Mono / stereo", audio, true);
    process (gateNode, "Gate", "Periodicity", analysis, true);
    process (detectorNode, "PitchCore", stateText (metering_.state), analysis, analysisActive);
    const auto degreeText = degree_.degree > 0
        ? "Degree " + juce::String (degree_.degree) + "/" + juce::String (degree_.count)
        : juce::String ("Awaiting target");
    process (targetNode, "Scale / trajectory", degreeText + (hasTarget && ! stable ? " / held" : ""),
             stable ? target : amber, hasTarget);
    process (rendererNode, "Single renderer", juce::String (metering_.correctionCents, 1) + " ct", audio, true);
    process (outputNode, "Output", (analogTexture_ ? "Analog / " : "")
             + juce::String (outputDb_, 1) + " dB", amber, true);

    // Only values populated by LivePitchProcessor::getMetering() are displayed.
    const int metricW = metrics.getWidth() / 3;
    const auto metric = [&] (juce::Rectangle<int> bounds, const juce::String& label,
                             const juce::String& value, float normalised)
    {
        bounds = bounds.reduced (5, 2);
        auto labelArea = bounds.removeFromTop (14);
        g.setFont (juce::FontOptions (10.5f));
        g.setColour (p.ink.withAlpha (0.70f));
        g.drawText (label, labelArea, juce::Justification::centred);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.setColour (p.ink);
        g.drawText (value, bounds.withTrimmedBottom (5), juce::Justification::centred);
        auto bar = bounds.removeFromBottom (3).toFloat();
        g.setColour (p.brassDark);
        g.fillRoundedRectangle (bar, 1.0f);
        bar.setWidth (bar.getWidth() * safe01 (normalised));
        g.setColour (analysis.withAlpha (0.75f));
        g.fillRoundedRectangle (bar, 1.0f);
    };
    // Candidate quality is exported only on a valid analysis-hop sample.
    // Between hops the adapter contains zero placeholders, not a 0% estimate.
    const bool hasMeasurement = metering_.detectorSupport > 0;
    metric (metrics.removeFromLeft (metricW), "Confidence",
            hasMeasurement ? juce::String (safe01 (metering_.confidence) * 100.0f, 0) + "%" : "--",
            hasMeasurement ? metering_.confidence : 0.0f);
    metric (metrics.removeFromLeft (metricW), "Periodicity",
            hasMeasurement ? juce::String (safe01 (metering_.harmonicity) * 100.0f, 0) + "%" : "--",
            hasMeasurement ? metering_.harmonicity : 0.0f);
    metric (metrics, "Stable duration", juce::String (metering_.sustainedNoteSeconds, 2) + " s",
            metering_.sustainedNoteSeconds / 2.0f);

    g.setFont (juce::FontOptions (10.0f));
    g.setColour (p.ink.withAlpha (0.60f));
    g.drawFittedText ("Blue: audio   Green: analysis   Violet: target   Amber: held / output",
                      legend, juce::Justification::centred, 1);
}
