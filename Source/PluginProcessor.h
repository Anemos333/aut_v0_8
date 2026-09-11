#pragma once

#include <JuceHeader.h>
#include "ScaleDefinitions.h"
#include "CustomScalePresets.h"
#include "LivePitchProcessor.h"
#include "Preset.h"
#include <vector>
#include <array>
#include <atomic>
#include <cstdint>

class MicrotonalAutotuneAudioProcessor : public juce::AudioProcessor
{
public:
    MicrotonalAutotuneAudioProcessor();
    ~MicrotonalAutotuneAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    void processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Access to APVTS
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Custom presets access (thread-safe via message thread only for GUI)
    CustomScalePresets& getCustomPresets() { return customPresets; }

    // Message-thread utilities. The audio callback consumes an immutable,
    // fixed-capacity snapshot published by refreshScaleSnapshot().
    std::vector<double> getCurrentScaleRatios() const;
    void refreshScaleSnapshot() noexcept;

    // Scale index parameter
    std::atomic<int> currentScaleIndex { 0 };

    // For custom scale: store which custom preset is active (-1 = none, using built-in)
    std::atomic<int> activeCustomPresetIndex { -1 };

    // Root note index: 0-11 = C,C#,D,...,B (12-ET), 12-18 = Ni,Pa,Vu,Ga,Di,Ke,Zo (Byzantine)
    std::atomic<int> rootNoteIndex { 9 }; // default A

    // Release modes: 1 = Quality, 2 = Live, 3 = Experimental.
    // Mode 0 was the removed legacy YIN renderer and is never audible.
    std::atomic<int> processingMode { 1 };

    // Update processing mode — must be called from message thread
    void updateProcessingMode (int newMode);

    // Get the reference frequency for the selected root note
    double getRootFrequency() const;
    void applyFactoryPreset (int index);

    // Lock-free coherent snapshot for the editor/debug overlay.
    [[nodiscard]] LivePitchProcessor::Metering getPitchMetering() const noexcept;

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    CustomScalePresets customPresets;
    int selectedPresetIndex = 3;

    struct ScaleSnapshot
    {
        std::array<double, ModernPitchEngine::maxScaleRatios> ratios {};
        int count = 1;
        double rootFrequency = 440.0;
        std::uint64_t generation = 0;
    };

    struct ScaleSnapshotSlot
    {
        ScaleSnapshot value;
        std::atomic<std::uint32_t> readers { 0 };
    };

    [[nodiscard]] int acquireScaleSnapshot() noexcept;
    void releaseScaleSnapshot (int slotIndex) noexcept;
    [[nodiscard]] static double rootFrequencyForIndex (int index) noexcept;

    std::array<ScaleSnapshotSlot, 3> scaleSnapshotSlots_ {};
    std::atomic<int> publishedScaleSnapshot_ { 0 };
    std::atomic<std::uint64_t> scaleSnapshotGeneration_ { 0 };

    double currentSampleRate = 44100.0;

    // ModernPitchEngine-based live pitch processor (Quality/Live/Experimental modes)
    LivePitchProcessor livePitchProcessor;
static constexpr int maxAnalogOutputChannels = 2;

    std::array<juce::dsp::IIR::Filter<float>, maxAnalogOutputChannels> analogLowShelfFilters_;
    std::array<juce::dsp::IIR::Filter<float>, maxAnalogOutputChannels> analogHighShelfFilters_;
    bool analogOutputWasActive_ = false;

    void updateAnalogOutputFilters();
    void resetAnalogOutputFilters() noexcept;

    void processOutputStage(juce::AudioBuffer<float>& buffer,
                            int numChannels,
                            int numSamples,
                            bool analogMode,
                            float outGain) noexcept;

    // Cached block size for mode changes
    int lastSamplesPerBlock = 512;

    // Convert processingMode int to LatencyMode enum
    static ModernPitchEngine::LatencyMode modeToLatency (int mode) noexcept;

    [[nodiscard]] CreativeTempo::Settings getTempoSettings() const noexcept;
    [[nodiscard]] CreativeTempo::HostPosition readHostTempoPosition(
        int numberOfSamples) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrotonalAutotuneAudioProcessor)
};
