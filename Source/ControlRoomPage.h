#pragma once

#include <JuceHeader.h>
#include <functional>

#include "LivePitchProcessor.h"
#include "NeumatonLabTheme.h"
#include "CommunityPackPage.h"

// Standalone UI page for detailed metering. The editor can feed it with
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
    class CommunityLauncherButton final : public juce::TextButton
    {
    public:
        explicit CommunityLauncherButton (ControlRoomPage& owner)
            : juce::TextButton ("Community"), owner_ (owner)
        {
            owner_.addAndMakeVisible (*this);
            setTooltip ("Create, export and browse Ergasterion Community Packs");
            onClick = [this]
            {
                auto* editor = findParentComponentOfClass<juce::AudioProcessorEditor>();
                if (editor == nullptr)
                    return;

                auto* processor = dynamic_cast<MicrotonalAutotuneAudioProcessor*> (
                    editor->getAudioProcessor());
                if (processor != nullptr)
                    launchCommunityPackWindow (*processor);
            };
        }

        void parentSizeChanged() override
        {
            setBounds (juce::jmax (104, owner_.getWidth() - 136), 18, 112, 32);
        }

    private:
        ControlRoomPage& owner_;
    };

    LivePitchProcessor::Metering metering_;
    juce::TextButton backButton { "Back" };
    CommunityLauncherButton communityLauncher_ { *this };

    void drawHeader (juce::Graphics& g, juce::Rectangle<int> area);
    void drawDiagnosticGrid (juce::Graphics& g, juce::Rectangle<int> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlRoomPage)
};
