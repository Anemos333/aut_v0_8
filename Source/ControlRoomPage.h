#pragma once

#include <JuceHeader.h>
#include <functional>

#include "LivePitchProcessor.h"
#include "NeumatonLabTheme.h"

// Standalone UI page for detailed metering.  The editor can feed it with
// processorRef.getPitchMetering() from the normal message-thread timer.
class ControlRoomPage final : public juce::Component
{
public:
    ControlRoomPage();

    void setMetering (const LivePitchProcessor::Metering& newMetering);

    std::function<void()> onBack;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    LivePitchProcessor::Metering metering_;
    juce::TextButton backButton { "Back" };

    void drawHeader (juce::Graphics& g, juce::Rectangle<int> area);
    void drawDiagnosticGrid (juce::Graphics& g, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlRoomPage)
};
