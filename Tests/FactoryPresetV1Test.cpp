#include "PluginEditor.h"
#include "PitchCorrectionTrajectory.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
using Processor = MicrotonalAutotuneAudioProcessor;
struct ParameterValue { const char* id; float value; };
constexpr std::array<ParameterValue, 8> legacyValues {{
    { "scaleLock", 1 }, { "lockHysteresis", 73 }, { "vibratoPreserve", 87 },
    { "tempoMode", 2 }, { "tempoDivision", 4 }, { "tempoGlidePercent", 91 },
    { "tempoLockStrength", 62 }, { "tempoSmartOnset", 0 }
}};

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
bool close(double a, double b) { return std::abs(a - b) < 0.011; }
float value(Processor& processor, const char* id)
{
    const auto* raw = processor.getAPVTS().getRawParameterValue(id);
    require(raw != nullptr, "missing parameter");
    return raw->load();
}
void set(Processor& processor, const char* id, float plain)
{
    auto* parameter = processor.getAPVTS().getParameter(id);
    require(parameter != nullptr, "missing writable parameter");
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
}
void setLegacy(Processor& processor)
{
    for (const auto& entry : legacyValues) set(processor, entry.id, entry.value);
}
void checkLegacy(Processor& processor)
{
    for (const auto& entry : legacyValues)
        require(close(value(processor, entry.id), entry.value), "recall overwrote a retired control");
}

void checkFactoryAndState()
{
    Processor processor;
    require(processor.getNumPrograms() == 12, "host program slots changed");
    setLegacy(processor);
    set(processor, "tuningReferenceHz", 432);
    require(processor.activateCommunityScale("scale_0002"), "select factory minor scale");
    processor.tonalCenterIndex.store(4);
    processor.rootNoteIndex.store(4);
    const auto scaleId = processor.getCurrentScaleStableId();
    const auto ratios = processor.getCurrentScaleRatios();

    for (int index = 0; index < processor.getNumPrograms(); ++index)
    {
        processor.setCurrentProgram(index);
        const auto& preset = FactoryPresets::getPreset(index);
        require(processor.getCurrentProgram() == index, "host program index mismatch");
        require(processor.getProgramName(index) == preset.name, "host program name mismatch");
        require(processor.processingMode.load() == preset.processingMode, "preset mode mismatch");
        require(close(value(processor, "speed"), preset.speedMs), "preset Response mismatch");
        require(close(value(processor, "amount"), preset.amount), "preset Amount mismatch");
        require(close(value(processor, "humanize"), preset.humanize), "preset Human Drift mismatch");
        require(close(value(processor, "analogMode"), preset.analogMode ? 1 : 0), "preset texture mismatch");
        require(close(value(processor, "outVolume"), preset.outVolumeDb), "preset output mismatch");
        checkLegacy(processor);
        require(close(value(processor, "tuningReferenceHz"), 432), "factory recall retuned A4");
        require(processor.getCurrentScaleStableId() == scaleId
            && processor.getCurrentScaleRatios() == ratios, "factory recall changed scale");
        require(processor.tonalCenterIndex.load() == 4, "factory recall changed centre");
    }

    // A previous Hard Tune session uses slot 3 but owns its saved audio values.
    processor.setCurrentProgram(3);
    set(processor, "speed", 12);
    set(processor, "humanize", 8);
    juce::MemoryBlock state;
    processor.getStateInformation(state);
    Processor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    require(restored.getCurrentProgram() == 3, "session program slot changed");
    require(close(value(restored, "speed"), 12) && close(value(restored, "humanize"), 8),
        "session restore applied the updated factory settings");
    require(restored.getCurrentScaleStableId() == scaleId
        && restored.tonalCenterIndex.load() == 4
        && close(value(restored, "tuningReferenceHz"), 432), "session musical state changed");
    checkLegacy(restored);
}

void checkSharedPresets()
{
    using namespace neumaton;
    Processor source;
    setLegacy(source);
    source.setCurrentProgram(9);
    set(source, "tuningReferenceHz", 432);
    const auto captured = community::CommunityPreset::capture("V1 preset", source.getAPVTS(), 1, 4);
    require(captured.values.getNumProperties() == 6, "new capture contains retired controls");
    for (const auto& entry : legacyValues)
        require(!captured.values.hasProperty(entry.id), "new capture stored a retired control");

    auto legacy = captured;
    legacy.values = captured.values.createCopy();
    for (const auto& entry : legacyValues)
        legacy.values.setProperty(entry.id, entry.value, nullptr);
    sharing::PackDocument pack;
    pack.manifest.stableId = "test.legacy";
    pack.manifest.name = "Legacy preset test";
    pack.manifest.author = "Regression test";
    pack.presets.addChild(legacy.toValueTree(), -1, nullptr);
    pack.sealIntegrity();
    auto restored = sharing::PackDocument::fromValueTree(pack.toValueTree());
    const auto preset = community::CommunityPreset::fromValueTree(restored.presets.getChild(0));
    require(preset.values.isEquivalentTo(legacy.values), "legacy pack values were discarded");
    restored.presets.removeAllChildren(nullptr);
    restored.presets.addChild(preset.toValueTree(), -1, nullptr);
    require(restored.verifyIntegrity() == sharing::PackIntegrityStatus::valid
        && restored.calculatePayloadSha256() == pack.manifest.payloadSha256,
        "legacy preset round-trip invalidated the pack hash");

    Processor destination;
    for (const auto& entry : legacyValues) set(destination, entry.id, 0);
    std::array<float, legacyValues.size()> before {};
    for (std::size_t i = 0; i < before.size(); ++i) before[i] = value(destination, legacyValues[i].id);
    require(destination.applyCommunityPreset(preset), "apply legacy pack preset");
    for (std::size_t i = 0; i < before.size(); ++i)
        require(close(value(destination, legacyValues[i].id), before[i]), "old pack applied a retired control");
    for (const auto* id : community::sharedPresetParameterIds())
        require(close(value(destination, id), value(source, id)), "old pack missed an active control");
    require(destination.processingMode.load() == 1 && destination.tonalCenterIndex.load() == 4,
        "shared preset missed mode/centre");
}

void checkEffectiveProfiles()
{
    std::array<double, 12> ratios {};
    for (std::size_t i = 0; i < ratios.size(); ++i) ratios[i] = std::exp2(static_cast<double>(i) / 12);
    neumaton::pitch::ScaleQuantizer quantizer;
    require(quantizer.setScale(ratios.data(), 12, 440, 2), "prepare quantizer");
    for (int a = 0; a < FactoryPresets::getNumPresets(); ++a)
    {
        const auto& first = FactoryPresets::getPreset(a);
        const auto firstTarget = quantizer.quantize(450, first.amount / 100, first.humanize / 100);
        for (int b = 0; b < a; ++b)
        {
            const auto& second = FactoryPresets::getPreset(b);
            const auto secondTarget = quantizer.quantize(450, second.amount / 100, second.humanize / 100);
            require(first.processingMode != second.processingMode || first.speedMs != second.speedMs
                || std::abs(firstTarget.liveWindowCents - secondTarget.liveWindowCents) > 1e-5
                || first.analogMode != second.analogMode, "effective duplicate: gain is not a distinction");
        }
    }

    const auto& full = FactoryPresets::getPreset(3);
    require(juce::String(full.name) == "Full Correction" && full.speedMs == 0
        && full.amount == 100 && full.humanize == 0, "maximum correction preset missing");
    neumaton::render::PitchCorrectionTrajectory trajectory;
    trajectory.prepare(48000);
    for (int cents = -49; cents <= 49; ++cents)
    {
        neumaton::pitch::PitchResult observation;
        observation.state = neumaton::pitch::TrackingState::stable;
        observation.hasStable = true;
        observation.stableHz = static_cast<float>(440 * std::exp2(static_cast<double>(cents) / 1200));
        const auto target = quantizer.quantize(observation.stableHz, full.amount / 100, full.humanize / 100);
        const auto correction = trajectory.process(observation, quantizer,
            full.amount / 100, full.humanize / 100, full.speedMs);
        require(target.liveWindowCents == 0 && std::abs(correction - target.correctionCents) < 1e-8,
            "Full Correction leaves a residual or response delay");
    }
}

void dispatch() { juce::MessageManager::getInstance()->runDispatchLoopUntil(100); }
void checkEditor()
{
    Processor processor;
    auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
    juce::ComboBox* selector = nullptr;
    for (auto* child : editor->getChildren())
        if (auto* box = dynamic_cast<juce::ComboBox*>(child))
            if (box->getNumItems() == 12 && box->getItemText(3) == "Full Correction") selector = box;
    require(selector != nullptr, "preset menu missing");
    dispatch();
    require(selector->getSelectedId() == 0 && selector->getTextWhenNothingSelected() == "Custom",
        "fresh settings mislabelled as Full Correction");
    require(close(value(processor, "speed"), 50) && close(value(processor, "humanize"), 20), "opening editor changed defaults");
    selector->setSelectedId(4, juce::sendNotificationSync);
    dispatch();
    require(selector->getText() == "Full Correction" && close(value(processor, "speed"), 0), "menu recall failed");
    set(processor, "speed", 12);
    set(processor, "humanize", 8); dispatch();
    require(selector->getSelectedId() == 0, "modified preset not labelled Custom");
    processor.setCurrentProgram(9); dispatch();
    require(selector->getText() == "Slow Orbit", "host program change not reflected in editor");
    setLegacy(processor); dispatch();
    require(selector->getText() == "Slow Orbit", "retired controls affect preset matching");
}

void checkVoice(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
    require(reader != nullptr, "read real voice WAV");
    const int length = static_cast<int>(std::min(reader->lengthInSamples,
        static_cast<juce::int64>(reader->sampleRate * 12)));
    require(length > reader->sampleRate, "voice clip too short");
    juce::AudioBuffer<float> input(1, length);
    require(reader->read(&input, 0, length, 0, true, false), "load real voice WAV");
    std::vector<std::vector<float>> outputs;
    for (int index = 0; index < FactoryPresets::getNumPresets(); ++index)
    {
        Processor processor;
        processor.setCurrentProgram(index);
        require(processor.activateCommunityScale("scale_0002"), "select voice test scale");
        processor.setRateAndBufferSizeDetails(reader->sampleRate, 256);
        processor.prepareToPlay(reader->sampleRate, 256);
        juce::AudioBuffer<float> block(1, 256);
        juce::MidiBuffer midi;
        std::vector<float> output;
        output.reserve(static_cast<std::size_t>(length));
        int stableBlocks = 0;
        const auto gain = juce::Decibels::decibelsToGain(value(processor, "outVolume"));
        for (int position = 0; position < length; position += 256)
        {
            const int count = std::min(256, length - position);
            block.clear();
            block.copyFrom(0, 0, input, 0, position, count);
            processor.processBlock(block, midi);
            stableBlocks += processor.getPitchMetering().state == LivePitchProcessor::TrackingState::stable ? 1 : 0;
            for (int sample = 0; sample < count; ++sample)
            {
                const float x = block.getSample(0, sample);
                // The existing soft ceiling is bounded below 4, not a hard
                // digital-full-scale limiter. Presets must retain that bound.
                require(std::isfinite(x) && std::abs(x) <= 4.0f, "unsafe preset output");
                output.push_back(x / gain); // Compare sound independently of preset volume.
            }
        }
        require(stableBlocks > 0, "voice test never acquired a stable pitch");
        outputs.push_back(std::move(output));
        processor.releaseResources();
    }
    double smallestDifference = 1;
    for (std::size_t a = 0; a < outputs.size(); ++a)
        for (std::size_t b = 0; b < a; ++b)
        {
            double difference = 0;
            for (int i = 0; i < length; ++i)
            {
                const double delta = outputs[a][static_cast<std::size_t>(i)] - outputs[b][static_cast<std::size_t>(i)];
                difference += delta * delta;
            }
            difference = std::sqrt(difference / length);
            require(difference > 1e-5, "presets produce identical gain-normalised voice output");
            smallestDifference = std::min(smallestDifference, difference);
        }
    std::cout << "PASS: 12 distinct finite voice outputs, minimum gain-normalised RMS difference "
              << smallestDifference << " (" << file.getFileName() << ")\n";
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        juce::ScopedJuceInitialiser_GUI initialise;
        checkFactoryAndState();
        checkSharedPresets();
        checkEffectiveProfiles();
        checkEditor();
        for (int i = 1; i < argc; ++i) checkVoice(juce::File(argv[i]));
        std::cout << "PASS: preset recall, legacy state/pack integrity, maximum correction, unique profiles and live menu\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
