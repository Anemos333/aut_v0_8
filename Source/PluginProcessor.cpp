#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
constexpr float analogLowShelfHz = 75.0f;
constexpr float analogHighShelfHz = 4800.0f;
constexpr float analogLowShelfGainDb = -2.0f;
constexpr float analogHighShelfGainDb = -1.5f;
constexpr float analogShelfQ = 0.70710678f;

void setParameterNotifyingHost (juce::AudioProcessorValueTreeState& apvts,
                                const char* parameterId,
                                float plainValue)
{
    if (auto* parameter = apvts.getParameter (parameterId))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (plainValue));
        parameter->endChangeGesture();
    }
}

[[nodiscard]] const char* legacyScaleStableIdForIndex (int index) noexcept
{
    // Exact semantic migration from the pre-database ScaleDefinitions order.
    static constexpr std::array<const char*, 29> ids {
        "scale_0033",                 // Chromatic -> 12-EDO
        "scale_0001",                 // Major / Ionian
        "scale_0005",                 // Dorian
        "scale_0006",                 // Phrygian
        "scale_0007",                 // Lydian
        "scale_0008",                 // Mixolydian
        "scale_0002",                 // Aeolian
        "scale_0009",                 // Locrian
        "scale_0004",                 // Melodic minor
        "scale_0003",                 // Harmonic minor
        "scale_0026",                 // Major pentatonic
        "scale_0025",                 // Minor pentatonic
        "scale_0037",                 // 24 EDO
        "scale_0035",                 // 19 EDO
        "scale_0039",                 // 31 EDO
        "legacy.scale.pythagorean-12",
        "legacy.scale.ptolemaic-v0",
        "scale_0076",                 // Byzantine diatonic
        "scale_0077",                 // Byzantine soft chromatic
        "scale_0078",                 // old Mode III geometry
        "scale_0083",                 // Rast
        "scale_0084",                 // Bayati
        "scale_0087",                 // Saba
        "scale_0085",                 // Hijaz
        "scale_0082",                 // Nahawand
        "scale_0080",                 // Ajam
        "scale_0081",                 // Kurd
        "scale_0031",                 // old Slendro approximation = 5-EDO
        "legacy.scale.pelog-v0"
    };

    if (index < 0 || index >= static_cast<int> (ids.size()))
        return nullptr;
    return ids[static_cast<std::size_t> (index)];
}

float sanitiseOutputSample (float value) noexcept
{
    if (! std::isfinite (value) || std::fpclassify (value) == FP_SUBNORMAL)
        return 0.0f;

    return juce::jlimit (-32.0f, 32.0f, value);
}

float fastSoftClip (float x) noexcept
{
    x = sanitiseOutputSample (x);

    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;

    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

float outputSafetySoftCeiling (float x) noexcept
{
    x = sanitiseOutputSample (x);

    constexpr float threshold = 0.985f;
    constexpr float softness = 0.30f;

    const float ax = std::abs (x);
    if (ax <= threshold)
        return x;

    const float over = ax - threshold;
    const float compressed = threshold + over / (1.0f + softness * over);
    return std::copysign (compressed, x);
}

[[nodiscard]] float constrainRetuneSpeedMs (float speedMs) noexcept
{
    if (! std::isfinite (speedMs))
        speedMs = 50.0f;
    return juce::jlimit (0.0f, 500.0f, speedMs);
}
} // namespace

//==============================================================================
MicrotonalAutotuneAudioProcessor::MicrotonalAutotuneAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    refreshScaleSnapshot();
}

MicrotonalAutotuneAudioProcessor::~MicrotonalAutotuneAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout
MicrotonalAutotuneAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "speed", 1 }, "Velocita (ms)",
        juce::NormalisableRange<float> (0.0f, 500.0f, 1.0f), 50.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "humanize", 1 }, "Humanize",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 20.0f));

    // Absolute tuning is deliberately separate from the selected musical centre.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tuningReferenceHz", 1 }, "Tuning Reference A4",
        juce::NormalisableRange<float> (300.0f, 600.0f, 0.01f), 440.0f));

    // Historical ID retained for DAW automation/session compatibility. In V1
    // this visible control is simply named Hold; there is no Scale Lock mode.
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lockHysteresis", 1 }, "Hold",
        juce::NormalisableRange<float> (0.0f, 80.0f, 1.0f), 24.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "vibratoPreserve", 1 }, "Vibrato Preserve",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "analogMode", 1 }, "Analog Mode", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "outVolume", 1 }, "Output Volume",
        juce::NormalisableRange<float> (-36.0f, 3.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

//==============================================================================
const juce::String MicrotonalAutotuneAudioProcessor::getName() const { return "Microtonal Autotune"; }
bool MicrotonalAutotuneAudioProcessor::acceptsMidi() const { return false; }
bool MicrotonalAutotuneAudioProcessor::producesMidi() const { return false; }
bool MicrotonalAutotuneAudioProcessor::isMidiEffect() const { return false; }
double MicrotonalAutotuneAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int MicrotonalAutotuneAudioProcessor::getNumPrograms()
{
    return FactoryPresets::getNumPresets();
}

int MicrotonalAutotuneAudioProcessor::getCurrentProgram()
{
    return juce::jlimit (0, getNumPrograms() - 1, selectedPresetIndex);
}

void MicrotonalAutotuneAudioProcessor::setCurrentProgram (int index)
{
    applyFactoryPreset (index);
}

const juce::String MicrotonalAutotuneAudioProcessor::getProgramName (int index)
{
    if (index < 0 || index >= FactoryPresets::getNumPresets())
        return {};
    return FactoryPresets::getPreset (index).name;
}

void MicrotonalAutotuneAudioProcessor::changeProgramName (int, const juce::String&)
{
}

//==============================================================================
LivePitchProcessor::LatencyMode
MicrotonalAutotuneAudioProcessor::modeToLatency (int mode) noexcept
{
    switch (mode)
    {
        case 1:  return LivePitchProcessor::LatencyMode::quality;
        case 2:  return LivePitchProcessor::LatencyMode::live;
        case 3:  return LivePitchProcessor::LatencyMode::ultraLive;
        default: return LivePitchProcessor::LatencyMode::quality;
    }
}

void MicrotonalAutotuneAudioProcessor::prepareToPlay (double sampleRate,
                                                       int samplesPerBlock)
{
    currentSampleRate = std::isfinite (sampleRate) ? std::max (8000.0, sampleRate)
                                                   : 44100.0;
    updateAnalogOutputFilters();
    analogOutputWasActive_ = false;
    lastSamplesPerBlock = std::max (1, samplesPerBlock);
    refreshScaleSnapshot();

    const int mode = juce::jlimit (1, 3,
        processingMode.load (std::memory_order_acquire));
    processingMode.store (mode, std::memory_order_release);

    livePitchProcessor.prepare (currentSampleRate,
                                lastSamplesPerBlock,
                                std::max (1, getTotalNumOutputChannels()),
                                modeToLatency (mode));

    const float humanizeVal =
        apvts.getRawParameterValue ("humanize")->load() / 100.0f;
    livePitchProcessor.setAdvancedParameters (
        0.0f, humanizeVal, 0.0f, 0.0f, 0.0f,
        45.0f, 1600.0f,
        LivePitchProcessor::StereoMode::linkedMidSide);

    setLatencySamples (livePitchProcessor.getLatencySamples());
}

void MicrotonalAutotuneAudioProcessor::releaseResources()
{
    livePitchProcessor.reset();
    resetAnalogOutputFilters();
    analogOutputWasActive_ = false;
}

bool MicrotonalAutotuneAudioProcessor::isBusesLayoutSupported (
    const BusesLayout& layouts) const
{
    const auto& mainInput  = layouts.getChannelSet (true,  0);
    const auto& mainOutput = layouts.getChannelSet (false, 0);

    if (mainInput != mainOutput)
        return false;

    return mainInput == juce::AudioChannelSet::mono()
        || mainInput == juce::AudioChannelSet::stereo();
}

void MicrotonalAutotuneAudioProcessor::updateProcessingMode (int newMode)
{
    newMode = juce::jlimit (1, 3, newMode);
    const int oldMode = processingMode.load (std::memory_order_acquire);
    if (newMode == oldMode)
        return;

    livePitchProcessor.setLatencyModeNonRealtime (modeToLatency (newMode));
    processingMode.store (newMode, std::memory_order_release);
    setLatencySamples (livePitchProcessor.getLatencySamples());
}

//==============================================================================
std::vector<double> MicrotonalAutotuneAudioProcessor::getCurrentScaleRatios() const
{
    const int customIdx = activeCustomPresetIndex.load (std::memory_order_acquire);
    if (customIdx >= 0 && customIdx < customPresets.getNumPresets())
        return customPresets.getPreset (customIdx).ratios;

    const int scaleIdx = currentScaleIndex.load (std::memory_order_acquire);
    return ScaleDefinitions::getScale (scaleIdx).ratios;
}

double MicrotonalAutotuneAudioProcessor::getCurrentScaleEquave() const noexcept
{
    const int customIdx = activeCustomPresetIndex.load (std::memory_order_acquire);
    if (customIdx >= 0 && customIdx < customPresets.getNumPresets())
        return customPresets.getPreset (customIdx).equaveRatio;

    const int scaleIdx = currentScaleIndex.load (std::memory_order_acquire);
    return ScaleDefinitions::getScale (scaleIdx).equaveRatio;
}

juce::String MicrotonalAutotuneAudioProcessor::getCurrentScaleStableId() const
{
    const int customIdx = activeCustomPresetIndex.load (std::memory_order_acquire);
    if (customIdx >= 0 && customIdx < customPresets.getNumPresets())
        return customPresets.getPreset (customIdx).stableId;

    const int scaleIdx = currentScaleIndex.load (std::memory_order_acquire);
    return juce::String (ScaleDefinitions::getScale (scaleIdx).stableId);
}

double MicrotonalAutotuneAudioProcessor::legacyRootFrequencyForIndex (int index) noexcept
{
    static constexpr std::array<double, 19> rootFreqs {
        261.6256, 277.1826, 293.6648, 311.1270, 329.6276, 349.2282,
        369.9944, 391.9954, 415.3047, 440.0000, 466.1638, 493.8833,
        261.6256,
        261.6256 * 1.122462048309373,
        261.6256 * 1.235894465929289,
        261.6256 * 1.334839854170034,
        261.6256 * 1.498307076876682,
        261.6256 * 1.681792830507429,
        261.6256 * 1.851749424574581
    };

    return rootFreqs[static_cast<std::size_t> (juce::jlimit (0, 18, index))];
}

double MicrotonalAutotuneAudioProcessor::tuningReferenceHz() const noexcept
{
    const auto* raw = apvts.getRawParameterValue ("tuningReferenceHz");
    const float value = raw != nullptr ? raw->load() : 440.0f;
    return std::isfinite (value) ? juce::jlimit (300.0, 600.0, static_cast<double> (value))
                                 : 440.0;
}

double MicrotonalAutotuneAudioProcessor::rootFrequencyFromCenter (
    int centerIndex, double a4ReferenceHz) noexcept
{
    centerIndex = juce::jlimit (0, 11, centerIndex);
    if (! std::isfinite (a4ReferenceHz) || a4ReferenceHz <= 0.0)
        a4ReferenceHz = 440.0;

    const double semitonesFromA = static_cast<double> (centerIndex - 9);
    return a4ReferenceHz * std::exp2 (semitonesFromA / 12.0);
}

double MicrotonalAutotuneAudioProcessor::getRootFrequency() const
{
    return rootFrequencyFromCenter (
        tonalCenterIndex.load (std::memory_order_acquire),
        tuningReferenceHz());
}

void MicrotonalAutotuneAudioProcessor::refreshScaleSnapshot() noexcept
{
    const auto ratios = getCurrentScaleRatios(); // message/non-audio thread only
    double equaveRatio = getCurrentScaleEquave();
    if (! std::isfinite (equaveRatio) || equaveRatio <= 1.0)
        equaveRatio = 2.0;

    const double logEquave = std::log2 (equaveRatio);

    ScaleSnapshot next;
    next.count = 0;
    next.equaveRatio = equaveRatio;
    next.rootFrequency = getRootFrequency(); // diagnostic snapshot only
    next.ratios[static_cast<std::size_t> (next.count++)] = 1.0;

    for (double ratio : ratios)
    {
        if (next.count >= LivePitchProcessor::maxScaleRatios)
            break;
        if (! std::isfinite (ratio) || ratio <= 0.0)
            continue;

        double phase = std::fmod (std::log2 (ratio), logEquave);
        if (phase < 0.0)
            phase += logEquave;

        const double folded = std::exp2 (phase);
        if (std::isfinite (folded) && folded >= 1.0
            && folded < equaveRatio - 1.0e-10)
            next.ratios[static_cast<std::size_t> (next.count++)] = folded;
    }

    std::sort (next.ratios.begin(), next.ratios.begin() + next.count);

    int uniqueCount = 0;
    for (int index = 0; index < next.count; ++index)
    {
        const double value = next.ratios[static_cast<std::size_t> (index)];
        if (uniqueCount == 0
            || std::abs (value - next.ratios[static_cast<std::size_t> (uniqueCount - 1)]) > 1.0e-8)
        {
            next.ratios[static_cast<std::size_t> (uniqueCount++)] = value;
        }
    }

    next.count = std::max (1, uniqueCount);
    next.generation = scaleSnapshotGeneration_.fetch_add (
        1, std::memory_order_relaxed) + 1;

    const int published = publishedScaleSnapshot_.load (std::memory_order_acquire);
    for (int offset = 1; offset <= 2; ++offset)
    {
        const int candidate = (published + offset) % 3;
        auto& slot = scaleSnapshotSlots_[static_cast<std::size_t> (candidate)];
        if (slot.readers.load (std::memory_order_acquire) == 0)
        {
            slot.value = next;
            publishedScaleSnapshot_.store (candidate, std::memory_order_release);
            return;
        }
    }
}

int MicrotonalAutotuneAudioProcessor::acquireScaleSnapshot() noexcept
{
    for (;;)
    {
        const int index = publishedScaleSnapshot_.load (std::memory_order_acquire);
        auto& slot = scaleSnapshotSlots_[static_cast<std::size_t> (index)];
        slot.readers.fetch_add (1, std::memory_order_acq_rel);
        if (index == publishedScaleSnapshot_.load (std::memory_order_acquire))
            return index;
        slot.readers.fetch_sub (1, std::memory_order_release);
    }
}

void MicrotonalAutotuneAudioProcessor::releaseScaleSnapshot (int slotIndex) noexcept
{
    scaleSnapshotSlots_[static_cast<std::size_t> (juce::jlimit (0, 2, slotIndex))]
        .readers.fetch_sub (1, std::memory_order_release);
}

//==============================================================================
void MicrotonalAutotuneAudioProcessor::applyFactoryPreset (int index)
{
    const int count = FactoryPresets::getNumPresets();
    if (count <= 0)
        return;

    index = juce::jlimit (0, count - 1, index);
    selectedPresetIndex = index;
    const auto& preset = FactoryPresets::getPreset (index);

    updateProcessingMode (juce::jlimit (1, 3, preset.processingMode));
    setParameterNotifyingHost (apvts, "speed", preset.speedMs);
    setParameterNotifyingHost (apvts, "amount", preset.amount);
    setParameterNotifyingHost (apvts, "humanize", preset.humanize);
    setParameterNotifyingHost (apvts, "lockHysteresis", preset.lockHysteresis);
    setParameterNotifyingHost (apvts, "vibratoPreserve", preset.vibratoPreserve);
    setParameterNotifyingHost (apvts, "analogMode", preset.analogMode ? 1.0f : 0.0f);
    setParameterNotifyingHost (apvts, "outVolume", preset.outVolumeDb);
}

//==============================================================================
void MicrotonalAutotuneAudioProcessor::updateAnalogOutputFilters()
{
    const double sr = std::isfinite (currentSampleRate)
        ? std::max (8000.0, currentSampleRate) : 44100.0;

    const auto lowShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf (
        sr, analogLowShelfHz, analogShelfQ,
        juce::Decibels::decibelsToGain (analogLowShelfGainDb));

    const auto highShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
        sr, analogHighShelfHz, analogShelfQ,
        juce::Decibels::decibelsToGain (analogHighShelfGainDb));

    for (int ch = 0; ch < maxAnalogOutputChannels; ++ch)
    {
        analogLowShelfFilters_[static_cast<std::size_t> (ch)].coefficients = lowShelfCoeffs;
        analogHighShelfFilters_[static_cast<std::size_t> (ch)].coefficients = highShelfCoeffs;
    }

    resetAnalogOutputFilters();
}

void MicrotonalAutotuneAudioProcessor::resetAnalogOutputFilters() noexcept
{
    for (int ch = 0; ch < maxAnalogOutputChannels; ++ch)
    {
        analogLowShelfFilters_[static_cast<std::size_t> (ch)].reset();
        analogHighShelfFilters_[static_cast<std::size_t> (ch)].reset();
    }
}

void MicrotonalAutotuneAudioProcessor::processOutputStage (
    juce::AudioBuffer<float>& buffer,
    int numChannels,
    int numSamples,
    bool analogMode,
    float outGain) noexcept
{
    numChannels = juce::jlimit (0, buffer.getNumChannels(), numChannels);
    numSamples = juce::jlimit (0, buffer.getNumSamples(), numSamples);

    if (numChannels <= 0 || numSamples <= 0)
        return;

    outGain = std::isfinite (outGain) ? juce::jlimit (0.0f, 8.0f, outGain) : 1.0f;

    if (analogMode && ! analogOutputWasActive_)
        resetAnalogOutputFilters();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        float* data = buffer.getWritePointer (channel);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float value = sanitiseOutputSample (data[sample]);

            if (analogMode)
            {
                value = fastSoftClip (value);
                if (channel < maxAnalogOutputChannels)
                {
                    value = analogLowShelfFilters_[static_cast<std::size_t> (channel)].processSample (value);
                    value = analogHighShelfFilters_[static_cast<std::size_t> (channel)].processSample (value);
                }
            }

            value *= outGain;
            data[sample] = outputSafetySoftCeiling (value);
        }
    }

    analogOutputWasActive_ = analogMode;
}

//==============================================================================
void MicrotonalAutotuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                      juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    if (numSamples == 0 || totalNumInputChannels == 0)
        return;

    float speedMs = apvts.getRawParameterValue ("speed")->load();
    float amountPct = apvts.getRawParameterValue ("amount")->load();
    float humanizePct = apvts.getRawParameterValue ("humanize")->load();
    const bool analogMode =
        apvts.getRawParameterValue ("analogMode")->load() > 0.5f;
    const float outVolumeDb = apvts.getRawParameterValue ("outVolume")->load();

    const int mode = juce::jlimit (1, 3,
        processingMode.load (std::memory_order_relaxed));

    speedMs = constrainRetuneSpeedMs (speedMs);
    amountPct = std::isfinite (amountPct) ? juce::jlimit (0.0f, 100.0f, amountPct) : 0.0f;
    humanizePct = std::isfinite (humanizePct) ? juce::jlimit (0.0f, 100.0f, humanizePct) : 20.0f;

    const float amount = amountPct / 100.0f;
    const float humanizeVal = humanizePct / 100.0f;
    const float outGain = juce::Decibels::decibelsToGain (outVolumeDb);

    const int snapshotIndex = acquireScaleSnapshot();
    const auto& scaleSnapshot =
        scaleSnapshotSlots_[static_cast<std::size_t> (snapshotIndex)].value;

    livePitchProcessor.setAdvancedParameters (
        0.0f, humanizeVal, 0.0f, 0.0f, 0.0f,
        45.0f, 1600.0f,
        LivePitchProcessor::StereoMode::linkedMidSide);

    livePitchProcessor.process (buffer,
                                scaleSnapshot.ratios.data(),
                                scaleSnapshot.count,
                                getRootFrequency(),
                                speedMs,
                                amount,
                                scaleSnapshot.equaveRatio);
    releaseScaleSnapshot (snapshotIndex);

    processOutputStage (buffer,
                        totalNumInputChannels,
                        numSamples,
                        analogMode,
                        outGain);
}

void MicrotonalAutotuneAudioProcessor::processBlockBypassed (
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    livePitchProcessor.processBypassed (buffer);
}

LivePitchProcessor::Metering
MicrotonalAutotuneAudioProcessor::getPitchMetering() const noexcept
{
    return livePitchProcessor.getMetering();
}

bool MicrotonalAutotuneAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* MicrotonalAutotuneAudioProcessor::createEditor()
{
    return new MicrotonalAutotuneAudioProcessorEditor (*this);
}

//==============================================================================
void MicrotonalAutotuneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    const int customIndex = activeCustomPresetIndex.load (std::memory_order_acquire);
    const bool customActive = customIndex >= 0 && customIndex < customPresets.getNumPresets();

    state.setProperty ("scaleDatabaseSchemaVersion",
                       ScaleDefinitions::databaseSchemaVersion, nullptr);
    state.setProperty ("activeScaleKind", customActive ? "custom" : "factory", nullptr);
    state.setProperty ("activeScaleStableId", getCurrentScaleStableId(), nullptr);

    // Index properties remain for older builds, but stable ids own new persistence.
    state.setProperty ("scaleIndex", currentScaleIndex.load(), nullptr);
    state.setProperty ("customPresetIndex", customIndex, nullptr);
    state.setProperty ("tonalCenterIndex", tonalCenterIndex.load(), nullptr);
    state.setProperty ("rootNoteIndex", tonalCenterIndex.load(), nullptr);
    state.setProperty ("processingMode", processingMode.load(), nullptr);
    state.setProperty ("factoryPresetIndex", selectedPresetIndex, nullptr);

    state.addChild (customPresets.toValueTree(), -1, nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void MicrotonalAutotuneAudioProcessor::setStateInformation (const void* data,
                                                             int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xmlState);
    if (! tree.isValid())
        return;

    apvts.replaceState (tree);

    const auto customTree = tree.getChildWithName ("CustomScales");
    if (customTree.isValid())
        customPresets.fromValueTree (customTree);

    bool scaleResolved = false;
    const auto stableId = tree.getProperty ("activeScaleStableId").toString();
    const auto kind = tree.getProperty ("activeScaleKind").toString();

    if (stableId.isNotEmpty())
    {
        if (kind == "custom")
        {
            const int custom = customPresets.findPresetIndexByStableId (stableId);
            if (custom >= 0)
            {
                activeCustomPresetIndex.store (custom, std::memory_order_relaxed);
                scaleResolved = true;
            }
        }
        else
        {
            const int factory = ScaleDefinitions::findScaleIndexByStableId (
                stableId.toStdString());
            if (factory >= 0)
            {
                currentScaleIndex.store (factory, std::memory_order_relaxed);
                activeCustomPresetIndex.store (-1, std::memory_order_relaxed);
                scaleResolved = true;
            }
        }
    }

    if (! scaleResolved && tree.hasProperty ("scaleDatabaseSchemaVersion"))
    {
        // Database-era fallback for states created before stable-id persistence.
        const int storedFactory = static_cast<int> (tree.getProperty ("scaleIndex", 0));
        const int storedCustom = static_cast<int> (tree.getProperty ("customPresetIndex", -1));

        if (storedCustom >= 0 && storedCustom < customPresets.getNumPresets())
        {
            activeCustomPresetIndex.store (storedCustom, std::memory_order_relaxed);
        }
        else
        {
            currentScaleIndex.store (juce::jlimit (
                0, std::max (0, ScaleDefinitions::getScaleCount() - 1), storedFactory),
                std::memory_order_relaxed);
            activeCustomPresetIndex.store (-1, std::memory_order_relaxed);
        }
        scaleResolved = true;
    }

    if (! scaleResolved)
    {
        // Pre-database sessions persisted only an integer scale index.
        const int legacyIndex = static_cast<int> (tree.getProperty ("scaleIndex", 0));
        const char* legacyId = legacyScaleStableIdForIndex (legacyIndex);
        int migrated = legacyId != nullptr
            ? ScaleDefinitions::findScaleIndexByStableId (legacyId)
            : -1;
        if (migrated < 0)
            migrated = ScaleDefinitions::findScaleIndexByStableId ("scale_0001");

        currentScaleIndex.store (std::max (0, migrated), std::memory_order_relaxed);
        activeCustomPresetIndex.store (-1, std::memory_order_relaxed);
    }

    if (tree.hasProperty ("tonalCenterIndex"))
    {
        const int center = juce::jlimit (0, 11,
            static_cast<int> (tree.getProperty ("tonalCenterIndex")));
        tonalCenterIndex.store (center, std::memory_order_relaxed);
        rootNoteIndex.store (center, std::memory_order_relaxed);
    }
    else
    {
        // Exact migration of the old C..B + Ni..Zo frequency table.
        const int legacyRoot = juce::jlimit (0, 18,
            static_cast<int> (tree.getProperty ("rootNoteIndex", 9)));
        const double legacyHz = legacyRootFrequencyForIndex (legacyRoot);

        int center = static_cast<int> (std::lround (
            9.0 + 12.0 * std::log2 (legacyHz / 440.0)));
        center = juce::jlimit (0, 11, center);

        const double migratedA4 = legacyHz
            / std::exp2 (static_cast<double> (center - 9) / 12.0);

        tonalCenterIndex.store (center, std::memory_order_relaxed);
        rootNoteIndex.store (center, std::memory_order_relaxed);
        setParameterNotifyingHost (apvts, "tuningReferenceHz",
                                   static_cast<float> (migratedA4));
    }

    if (tree.hasProperty ("processingMode"))
    {
        const int storedMode = static_cast<int> (tree.getProperty ("processingMode"));
        updateProcessingMode (storedMode == 0 ? 1 : juce::jlimit (1, 3, storedMode));
    }
    else if (tree.hasProperty ("liveModeEnabled"))
    {
        const bool wasLive = static_cast<int> (tree.getProperty ("liveModeEnabled")) != 0;
        updateProcessingMode (wasLive ? 2 : 1);
    }

    if (tree.hasProperty ("factoryPresetIndex"))
    {
        selectedPresetIndex = juce::jlimit (
            0,
            std::max (0, FactoryPresets::getNumPresets() - 1),
            static_cast<int> (tree.getProperty ("factoryPresetIndex")));
    }

    refreshScaleSnapshot();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MicrotonalAutotuneAudioProcessor();
}
