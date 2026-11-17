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
            node.setProperty ("degreeColours", neumaton::scalecolours::encode (
                neumaton::scalecolours::defaults (scale.stableId, scale.ratios, scale.equaveRatio)), nullptr);
            entries.push_back (std::move (node));
        }

        for (const auto& scale : customPresets.toValueTree())
            entries.push_back (scale);

        return entries;
    }

    bool activateCommunityScale (
        const juce::String& stableId,
        const neumaton::sharing::PackDocument* sourcePack = nullptr)
    {
        if (stableId.isEmpty())
            return false;

        const auto node = sourcePack == nullptr ? juce::ValueTree()
            : neumaton::community::CommunityPackLibrary::findEntryByStableId (sourcePack->scales, stableId);
        if (sourcePack != nullptr && ! node.isValid())
            return false;

        const int factory = ScaleDefinitions::findScaleIndexByStableId (stableId.toStdString());
        int local = customPresets.findPresetIndexByStableId (stableId);
        if (node.isValid())
        {
            // A pack can carry a new palette for a scale already present in the session.
            // Keep that version independent so switching back restores the original.
            neumaton::sharing::PackDocument single;
            single.manifest = sourcePack->manifest;
            single.scales.addChild (node.createCopy(), -1, nullptr);
            CustomScalePresets decoded;
            if (! decoded.importScalePack (single)) return false;
            const auto& incoming = decoded.getPreset (0);
            const auto matches = [&incoming] (const auto& ratios, double equave, const auto& colours)
            {
                return std::abs (incoming.equaveRatio - equave) < 1.e-10
                    && incoming.degreeColours == colours && incoming.ratios.size() == ratios.size()
                    && std::equal (ratios.begin(), ratios.end(), incoming.ratios.begin(),
                        [] (double a, double b) { return std::abs (a - b) < 1.e-8; });
            };
            bool existing = false;
            if (factory >= 0)
            {
                const auto& scale = ScaleDefinitions::getScale (factory);
                existing = matches (scale.ratios, scale.equaveRatio, neumaton::scalecolours::defaults (
                    stableId, scale.ratios, scale.equaveRatio));
            }
            else if (local >= 0)
            {
                const auto& scale = customPresets.getPreset (local);
                existing = matches (scale.ratios, scale.equaveRatio, scale.degreeColours);
            }
            if (! existing)
            {
                auto key = stableId;
                if (factory >= 0 || local >= 0)
                    key = sourcePack->manifest.stableId + "." + stableId + "." + single.calculatePayloadSha256();
                local = customPresets.findPresetIndexByStableId (key);
                if (local < 0)
                {
                    auto entry = single.scales.getChild (0);
                    entry.setProperty ("stableId", key, nullptr);
                    entry.setProperty ("degreeColours", neumaton::scalecolours::encode (incoming.degreeColours), nullptr);
                    if (! customPresets.importScalePack (single)) return false;
                    local = customPresets.findPresetIndexByStableId (key);
                }
            }
        }
        if (local < 0 && factory < 0)
            return false;
        if (local < 0)
            currentScaleIndex.store (factory, std::memory_order_release);
        activeCustomPresetIndex.store (local, std::memory_order_release);
        refreshScaleSnapshot();
        return true;
    }

    bool applyCommunityScene (
        const neumaton::community::CommunityScene& scene,
        const neumaton::sharing::PackDocument* sourcePack = nullptr)
    {
        if (! scene.isValid())
            return false;

        neumaton::community::CommunityPreset preset;
        if (sourcePack != nullptr)
            preset = neumaton::community::CommunityPreset::fromValueTree (
                neumaton::community::CommunityPackLibrary::findEntryByStableId (
                    sourcePack->presets, scene.presetStableId));
        else if (const auto* local = communityPackLibrary.findUserPreset (scene.presetStableId))
            preset = *local;

        return preset.isValid() && activateCommunityScale (scene.scaleStableId, sourcePack)
            && applyCommunityPreset (preset);
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
    float analogDriveSmoothed_ = 0.0f;
    float analogDriveSmoothingCoefficient_ = 0.0f;

    void updateAnalogOutputFilters();
    void resetAnalogOutputFilters() noexcept;

    void processOutputStage(juce::AudioBuffer<float>& buffer,
                            int numChannels,
                            int numSamples,
                            bool analogMode,
                            float outGain,
                            float outputDrive) noexcept;

    int lastSamplesPerBlock = 512;

    static LivePitchProcessor::LatencyMode modeToLatency (int mode) noexcept;

    // Tempo Lab is hidden in V1. These remain only to read old APVTS state and
    // preserve session compatibility; LivePitchProcessor gives them no audio authority.
    [[nodiscard]] CreativeTempo::Settings getTempoSettings() const noexcept;
    [[nodiscard]] CreativeTempo::HostPosition readHostTempoPosition(
        int numberOfSamples) const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MicrotonalAutotuneAudioProcessor)
};
