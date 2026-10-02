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

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    CustomScalePresets& getCustomPresets() { return customPresets; }

    std::vector<double> getCurrentScaleRatios() const;
    void refreshScaleSnapshot() noexcept;

    std::atomic<int> currentScaleIndex { 0 };
    std::atomic<int> activeCustomPresetIndex { -1 };
    std::atomic<int> rootNoteIndex { 9 };

    // Release modes: 1 = Studio/512, 2 = Live/256, 3 = Low Latency/128.
    std::atomic<int> processingMode { 1 };

    void updateProcessingMode (int newMode);
    double getRootFrequency() const;
    void applyFactoryPreset (int index);

    [[nodiscard]] LivePitchProcessor::Metering getPitchMetering() const noexcept;

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    CustomScalePresets customPresets;
    int selectedPresetIndex = 3;

    struct ScaleSnapshot
    {
        std::array<double, LivePitchProcessor::maxScaleRatios> ratios {};
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

    int lastSamplesPerBlock = 512;

    static LivePitchProcessor::LatencyMode modeToLatency (int mode) noexcept;

    [[nodiscard]] CreativeTempo::Settings getTempoSettings() const noexcept;
    [[nodiscard]] CreativeTempo::HostPosition readHostTempoPosition(
        int numberOfSamples) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrotonalAutotuneAudioProcessor)
};
