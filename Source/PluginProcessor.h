#pragma once

#include <JuceHeader.h>
#include "ScaleDefinitions.h"
#include "CustomScalePresets.h"
#include "CommunityPackLibrary.h"
#include "Entitlement.h"
#include "LivePitchProcessor.h"
#include "Preset.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

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
    neumaton::community::CommunityPackLibrary& getCommunityPackLibrary() noexcept
    {
        return communityPackLibrary;
    }

    neumaton::licensing::EntitlementManager& getEntitlementManager() noexcept
    {
        return entitlementManager;
    }

    const neumaton::licensing::EntitlementManager& getEntitlementManager() const noexcept
    {
        return entitlementManager;
    }

    std::vector<double> getCurrentScaleRatios() const;
    double getCurrentScaleEquave() const noexcept;
    juce::String getCurrentScaleStableId() const;
    void refreshScaleSnapshot() noexcept;

    // Explicit compatibility default: old index 0 was Chromatic. In the new
    // 120-scale corpus the equivalent entry is scale_0033 (12-EDO), index 32.
    std::atomic<int> currentScaleIndex { ScaleDefinitions::defaultFactoryScaleIndex };
    std::atomic<int> activeCustomPresetIndex { -1 };

    // New V1 model: musical centre and absolute tuning reference are separate.
    // 0..11 = C..B. The old rootNoteIndex is retained only for state migration.
    std::atomic<int> tonalCenterIndex { 9 };
    std::atomic<int> rootNoteIndex { 9 };

    // Release modes: 1 = Studio/512, 2 = Live/256, 3 = Low Latency/128.
    std::atomic<int> processingMode { 1 };

    void updateProcessingMode (int newMode);
    double getRootFrequency() const;
    void applyFactoryPreset (int index);

    // ------------------------------------------------------------------
    // Community Pack V1 message-thread API. These operations never enter the
    // audio callback; they only update the same scale snapshot/APVTS state that
    // the existing GUI already owns.
    // ------------------------------------------------------------------
    [[nodiscard]] juce::String saveCurrentCommunityPreset (const juce::String& name)
    {
        return communityPackLibrary.addCurrentPreset (
            name,
            apvts,
            processingMode.load (std::memory_order_acquire),
            tonalCenterIndex.load (std::memory_order_acquire));
    }

    bool applyCommunityPreset (const neumaton::community::CommunityPreset& preset)
    {
        if (! preset.isValid())
            return false;

        preset.applyTo (apvts);

        if (preset.tonalCenterIndex >= 0)
        {
            const int center = juce::jlimit (0, 11, preset.tonalCenterIndex);
            tonalCenterIndex.store (center, std::memory_order_release);
            rootNoteIndex.store (center, std::memory_order_release);
        }

        updateProcessingMode (preset.processingMode);
        refreshScaleSnapshot();
        return true;
    }

    bool applyCommunityPresetTree (const juce::ValueTree& tree)
    {
        return applyCommunityPreset (
            neumaton::community::CommunityPreset::fromValueTree (tree));
    }

    [[nodiscard]] juce::String saveCurrentCommunityScene (
        const juce::String& name,
        const juce::String& presetStableId)
    {
        return communityPackLibrary.addScene (
            name, getCurrentScaleStableId(), presetStableId);
    }

    [[nodiscard]] std::vector<juce::ValueTree> getCommunityScaleEntries() const
    {
        std::vector<juce::ValueTree> entries;
        const auto& factory = ScaleDefinitions::getAllScales();
        entries.reserve (factory.size()
            + static_cast<std::size_t> (customPresets.getNumPresets()));

        const auto ratiosToString = [] (const std::vector<double>& ratios)
        {
            juce::String text;
            for (std::size_t i = 0; i < ratios.size(); ++i)
            {
                if (i != 0)
                    text += ",";
                text += juce::String (ratios[i], 12);
            }
            return text;
        };

        for (const auto& scale : factory)
        {
            if (! scale.visibleInMenu)
                continue;
            juce::ValueTree node ("Scale");
            node.setProperty ("stableId", juce::String (scale.stableId), nullptr);
            node.setProperty ("name", juce::String (scale.name), nullptr);
            node.setProperty ("category", juce::String (scale.category), nullptr);
            node.setProperty ("equaveRatio", scale.equaveRatio, nullptr);
            node.setProperty ("ratios", ratiosToString (scale.ratios), nullptr);
            entries.push_back (std::move (node));
        }

        for (int i = 0; i < customPresets.getNumPresets(); ++i)
        {
            const auto& scale = customPresets.getPreset (i);
            juce::ValueTree node ("Scale");
            node.setProperty ("stableId", scale.stableId, nullptr);
            node.setProperty ("sourcePackId", scale.sourcePackId, nullptr);
            node.setProperty ("name", scale.name, nullptr);
            node.setProperty ("category", scale.category, nullptr);
            node.setProperty ("equaveRatio", scale.equaveRatio, nullptr);
            node.setProperty ("ratios", ratiosToString (scale.ratios), nullptr);
            entries.push_back (std::move (node));
        }

        return entries;
    }

    bool activateCommunityScale (
        const juce::String& stableId,
        const neumaton::sharing::PackDocument* sourcePack = nullptr)
    {
        if (stableId.isEmpty())
            return false;

        const int factoryIndex = ScaleDefinitions::findScaleIndexByStableId (
            stableId.toStdString());
        if (factoryIndex >= 0)
        {
            currentScaleIndex.store (factoryIndex, std::memory_order_release);
            activeCustomPresetIndex.store (-1, std::memory_order_release);
            refreshScaleSnapshot();
            return true;
        }

        const int localIndex = customPresets.findPresetIndexByStableId (stableId);
        if (localIndex >= 0)
        {
            activeCustomPresetIndex.store (localIndex, std::memory_order_release);
            refreshScaleSnapshot();
            return true;
        }

        if (sourcePack == nullptr)
            return false;

        const auto node = neumaton::community::CommunityPackLibrary::findEntryByStableId (
            sourcePack->scales, stableId);
        if (! node.isValid())
            return false;

        neumaton::sharing::PackDocument single;
        single.manifest = sourcePack->manifest;
        single.scales.addChild (node.createCopy(), -1, nullptr);
        if (customPresets.importScalePack (single) <= 0)
            return false;

        const int importedIndex = customPresets.findPresetIndexByStableId (stableId);
        if (importedIndex < 0)
            return false;

        activeCustomPresetIndex.store (importedIndex, std::memory_order_release);
        refreshScaleSnapshot();
        return true;
    }

    bool applyCommunityScene (
        const neumaton::community::CommunityScene& scene,
        const neumaton::sharing::PackDocument* sourcePack = nullptr)
    {
        if (! scene.isValid())
            return false;

        const bool scaleApplied = activateCommunityScale (
            scene.scaleStableId, sourcePack);

        const neumaton::community::CommunityPreset* localPreset =
            communityPackLibrary.findUserPreset (scene.presetStableId);
        if (localPreset != nullptr)
            return scaleApplied && applyCommunityPreset (*localPreset);

        if (sourcePack == nullptr)
            return false;

        const auto presetTree =
            neumaton::community::CommunityPackLibrary::findEntryByStableId (
                sourcePack->presets, scene.presetStableId);
        return scaleApplied && applyCommunityPresetTree (presetTree);
    }

    [[nodiscard]] LivePitchProcessor::Metering getPitchMetering() const noexcept;

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    CustomScalePresets customPresets;
    neumaton::community::CommunityPackLibrary communityPackLibrary;
    neumaton::licensing::EntitlementManager entitlementManager;
    int selectedPresetIndex = 3;

    struct ScaleSnapshot
    {
        std::array<double, LivePitchProcessor::maxScaleRatios> ratios {};
        int count = 1;
        double rootFrequency = 440.0;
        double equaveRatio = 2.0;
        std::uint64_t generation = 0;
    };

    struct ScaleSnapshotSlot
    {
        ScaleSnapshot value;
        std::atomic<std::uint32_t> readers { 0 };
    };

    [[nodiscard]] int acquireScaleSnapshot() noexcept;
    void releaseScaleSnapshot (int slotIndex) noexcept;

    [[nodiscard]] static double legacyRootFrequencyForIndex (int index) noexcept;
    [[nodiscard]] double tuningReferenceHz() const noexcept;
    [[nodiscard]] static double rootFrequencyFromCenter (
        int centerIndex, double a4ReferenceHz) noexcept;

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

    // Tempo Lab is hidden in V1. These remain only to read old APVTS state and
    // preserve session compatibility; LivePitchProcessor gives them no audio authority.
    [[nodiscard]] CreativeTempo::Settings getTempoSettings() const noexcept;
    [[nodiscard]] CreativeTempo::HostPosition readHostTempoPosition(
        int numberOfSamples) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrotonalAutotuneAudioProcessor)
};
