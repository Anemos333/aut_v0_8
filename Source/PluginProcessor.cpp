#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>
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

    [[nodiscard]] float constrainRetuneSpeedMs (float speedMs,
                                           int /*mode*/,
                                           bool /*scaleLock*/) noexcept
    {
        // The clean engine owns the mode-aware trajectory mapping. Preserve
        // the complete 0..500 ms GUI range without floors or compression.
        if (! std::isfinite (speedMs))
            speedMs = 50.0f;
        return juce::jlimit (0.0f, 500.0f, speedMs);
    }

}

//==============================================================================
MicrotonalAutotuneAudioProcessor::MicrotonalAutotuneAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                          .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    refreshScaleSnapshot();
}

MicrotonalAutotuneAudioProcessor::~MicrotonalAutotuneAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout MicrotonalAutotuneAudioProcessor::createParameterLayout()
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

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "tempoMode", 1 }, "Creative Tempo Mode",
        juce::StringArray { "Off", "Tempo Glide", "Glide Lock" }, 0));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "tempoDivision", 1 }, "Tempo Division",
        juce::StringArray { "1/128", "1/64", "1/32", "1/16", "1/8" }, 2));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tempoGlidePercent", 1 }, "Tempo Glide Length",
        juce::NormalisableRange<float> (5.0f, 100.0f, 1.0f), 35.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tempoLockStrength", 1 }, "Glide Lock Strength",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "tempoSmartOnset", 1 }, "Smart Onset", true));

    // Modifica A: Scale Lock
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "scaleLock", 1 }, "Scale Lock", false));
        
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "lockHysteresis", 1 }, "Lock Hysteresis",
        juce::NormalisableRange<float> (0.0f, 80.0f, 1.0f), 24.0f));
        
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "vibratoPreserve", 1 }, "Vibrato Preserve",
        juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f));

    // Modifica B: Analog Tube
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
    // Factory presets are read-only.
}


//==============================================================================
ModernPitchEngine::LatencyMode MicrotonalAutotuneAudioProcessor::modeToLatency (int mode) noexcept
{
    switch (mode)
    {
        case 1:  return ModernPitchEngine::LatencyMode::quality;
        case 2:  return ModernPitchEngine::LatencyMode::live;
        case 3:  return ModernPitchEngine::LatencyMode::ultraLive;
        default: return ModernPitchEngine::LatencyMode::quality; // fail-safe release mode
    }
}

//==============================================================================
void MicrotonalAutotuneAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate)
                                                    : 44100.0;
    updateAnalogOutputFilters();
    analogOutputWasActive_ = false;
    lastSamplesPerBlock = std::max(1, samplesPerBlock);
    refreshScaleSnapshot();
    // The plugin has one audio engine family only. Quality, Live and
    // Experimental select already-prepared ModernPitchEngine profiles; no
    // legacy renderer is kept beside them.
    const int mode = juce::jlimit (1, 3,
        processingMode.load (std::memory_order_acquire));
    processingMode.store (mode, std::memory_order_release);
    livePitchProcessor.prepare (currentSampleRate,
                                lastSamplesPerBlock,
                                std::max (1, getTotalNumOutputChannels()),
                                modeToLatency (mode));
    const float humanizeVal = apvts.getRawParameterValue ("humanize")->load() / 100.0f;
    livePitchProcessor.setAdvancedParameters (
        35.0f,   // transitionMs
        0.70f,   // preserveVibrato
        humanizeVal,
        0.90f,   // formantPreservation
        0.85f,   // transientProtection
        0.70f,   // detectorSensitivity
        12.0f,   // maximumCorrectionSemitones
        45.0f,   // minimumPitchHz
        1600.0f, // maximumPitchHz
        LivePitchProcessor::StereoMode::linkedMidSide
    );
    setLatencySamples (livePitchProcessor.getLatencySamples());
}

void MicrotonalAutotuneAudioProcessor::releaseResources()
{
    livePitchProcessor.reset();
    resetAnalogOutputFilters();
    analogOutputWasActive_ = false;
}

bool MicrotonalAutotuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainInput  = layouts.getChannelSet (true,  0);
    const auto& mainOutput = layouts.getChannelSet (false, 0);

    // Support mono or stereo, input and output must match
    if (mainInput != mainOutput)
        return false;

    if (mainInput != juce::AudioChannelSet::mono() &&
        mainInput != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

//==============================================================================
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
    if (scaleIdx >= 0 && scaleIdx < ScaleDefinitions::getScaleCount())
        return ScaleDefinitions::getScale (scaleIdx).ratios;

    return ScaleDefinitions::getScale (0).ratios;
}

double MicrotonalAutotuneAudioProcessor::rootFrequencyForIndex (int index) noexcept
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

double MicrotonalAutotuneAudioProcessor::getRootFrequency() const
{
    return rootFrequencyForIndex (rootNoteIndex.load (std::memory_order_acquire));
}

void MicrotonalAutotuneAudioProcessor::refreshScaleSnapshot() noexcept
{
    const auto ratios = getCurrentScaleRatios(); // message/non-audio thread only
    ScaleSnapshot next;
    next.count = 0;

    // Preserve the musical invariant expected by the modern ScaleQuantizer:
    // every scale degree lives inside one octave [1.0, 2.0),
    // unison is present, and duplicate octave-equivalent degrees are removed.
    next.ratios[static_cast<std::size_t> (next.count++)] = 1.0;
    
    for (double ratio : ratios)
    {
        if (next.count >= ModernPitchEngine::maxScaleRatios)
            break;
        if (! std::isfinite (ratio) || ratio <= 0.0)
            continue;

        // O(1) mathematically safe octave folding to [1.0, 2.0)
        double l = std::log2(ratio);
        double folded = std::exp2(l - std::floor(l));
        
        if (folded >= 2.0) folded = 1.0;

        next.ratios[static_cast<std::size_t> (next.count++)] = folded;
    }

    std::sort (next.ratios.begin(),
               next.ratios.begin() + next.count);

    int uniqueCount = 0;
    for (int index = 0; index < next.count; ++index)
    {
        const double value = next.ratios[static_cast<std::size_t> (index)];
        if (uniqueCount == 0
            || std::abs (value - next.ratios[static_cast<std::size_t> (uniqueCount - 1)]) > 1.0e-8)
            next.ratios[static_cast<std::size_t> (uniqueCount++)] = value;
    }
    next.count = std::max (1, uniqueCount);
    next.rootFrequency = rootFrequencyForIndex (
        rootNoteIndex.load (std::memory_order_acquire));
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

    // One audio reader can occupy only one slot; this branch is defensive.
    // Keep the previous valid snapshot rather than blocking the message thread.
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
CreativeTempo::Settings
MicrotonalAutotuneAudioProcessor::getTempoSettings() const noexcept
{
    CreativeTempo::Settings settings;

    const auto finiteParameter = [this] (const char* parameterId, float fallback) noexcept
    {
        const float value = apvts.getRawParameterValue (parameterId)->load();
        return std::isfinite (value) ? value : fallback;
    };

    const int mode = static_cast<int> (std::lround (
        finiteParameter ("tempoMode", 0.0f)));
    settings.mode = static_cast<CreativeTempo::Mode> (
        juce::jlimit (0, 2, mode));

    const int division = static_cast<int> (std::lround (
        finiteParameter ("tempoDivision", 2.0f)));
    settings.division = CreativeTempo::divisionFromIndex (division);
    settings.glideFraction = juce::jlimit (0.05f, 1.0f,
        finiteParameter ("tempoGlidePercent", 35.0f) / 100.0f);
    settings.lockStrength = juce::jlimit (0.0f, 1.0f,
        finiteParameter ("tempoLockStrength", 100.0f) / 100.0f);
    settings.smartOnset = finiteParameter ("tempoSmartOnset", 1.0f) >= 0.5f;
    settings.smartOnsetWindow = 0.18f;
    settings.fallbackBpm = 120.0;
    return settings;
}

CreativeTempo::HostPosition
MicrotonalAutotuneAudioProcessor::readHostTempoPosition(
    int numberOfSamples) const noexcept
{
    CreativeTempo::HostPosition result;
    result.numberOfSamples = std::max(0, numberOfSamples);

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto bpm = position->getBpm())
            {
                result.bpm = *bpm;
                result.hasBpm = std::isfinite(result.bpm)
                             && result.bpm > 1.0;
            }

            if (const auto ppq = position->getPpqPosition())
            {
                result.ppqAtBlockStart = *ppq;
                result.hasPpq = std::isfinite(result.ppqAtBlockStart);
            }

            if (const auto sampleTime = position->getTimeInSamples())
            {
                result.timeInSamples = *sampleTime;
                result.hasTimeInSamples = true;
            }

            result.isPlaying = position->getIsPlaying();
            result.isLooping = position->getIsLooping();
        }
    }

    return result;
}
void MicrotonalAutotuneAudioProcessor::applyFactoryPreset (int index)
{
    const int count = FactoryPresets::getNumPresets();
    if (count <= 0)
        return;

    index = juce::jlimit (0, count - 1, index);
    selectedPresetIndex = index;

    const auto& preset = FactoryPresets::getPreset (index);

    updateProcessingMode (juce::jlimit (1, 3, preset.processingMode));

    setParameterNotifyingHost (apvts, "speed",              preset.speedMs);
    setParameterNotifyingHost (apvts, "amount",             preset.amount);
    setParameterNotifyingHost (apvts, "humanize",           preset.humanize);

    setParameterNotifyingHost (apvts, "scaleLock",          preset.scaleLock ? 1.0f : 0.0f);
    setParameterNotifyingHost (apvts, "lockHysteresis",     preset.lockHysteresis);
    setParameterNotifyingHost (apvts, "vibratoPreserve",    preset.vibratoPreserve);

    setParameterNotifyingHost (apvts, "tempoMode",          static_cast<float> (preset.tempoMode));
    setParameterNotifyingHost (apvts, "tempoDivision",      static_cast<float> (preset.tempoDivision));
    setParameterNotifyingHost (apvts, "tempoGlidePercent",  preset.tempoGlidePct);
    setParameterNotifyingHost (apvts, "tempoLockStrength",  preset.tempoLockStrength);
    setParameterNotifyingHost (apvts, "tempoSmartOnset",    preset.tempoSmartOnset ? 1.0f : 0.0f);

    setParameterNotifyingHost (apvts, "analogMode",         preset.analogMode ? 1.0f : 0.0f);
    setParameterNotifyingHost (apvts, "outVolume",          preset.outVolumeDb);
}
void MicrotonalAutotuneAudioProcessor::updateAnalogOutputFilters()
{
    const double sr = std::isfinite(currentSampleRate)
        ? std::max(8000.0, currentSampleRate)
        : 44100.0;

    const auto lowShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        sr,
        analogLowShelfHz,
        analogShelfQ,
        juce::Decibels::decibelsToGain(analogLowShelfGainDb));

    const auto highShelfCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sr,
        analogHighShelfHz,
        analogShelfQ,
        juce::Decibels::decibelsToGain(analogHighShelfGainDb));

    for (int ch = 0; ch < maxAnalogOutputChannels; ++ch)
    {
        analogLowShelfFilters_[static_cast<std::size_t>(ch)].coefficients = lowShelfCoeffs;
        analogHighShelfFilters_[static_cast<std::size_t>(ch)].coefficients = highShelfCoeffs;
    }

    resetAnalogOutputFilters();
}

void MicrotonalAutotuneAudioProcessor::resetAnalogOutputFilters() noexcept
{
    for (int ch = 0; ch < maxAnalogOutputChannels; ++ch)
    {
        analogLowShelfFilters_[static_cast<std::size_t>(ch)].reset();
        analogHighShelfFilters_[static_cast<std::size_t>(ch)].reset();
    }
}
void MicrotonalAutotuneAudioProcessor::processOutputStage(
    juce::AudioBuffer<float>& buffer,
    int numChannels,
    int numSamples,
    bool analogMode,
    float outGain) noexcept
{
    numChannels = juce::jlimit(0, buffer.getNumChannels(), numChannels);
    numSamples = juce::jlimit(0, buffer.getNumSamples(), numSamples);

    if (numChannels <= 0 || numSamples <= 0)
        return;

    outGain = std::isfinite(outGain) ? juce::jlimit(0.0f, 8.0f, outGain) : 1.0f;

    if (analogMode && ! analogOutputWasActive_)
        resetAnalogOutputFilters();

    for (int channel = 0; channel < numChannels; ++channel)
    {
        float* data = buffer.getWritePointer(channel);

        for (int sample = 0; sample < numSamples; ++sample)
        {
            float value = sanitiseOutputSample(data[sample]);

            if (analogMode)
            {
                value = fastSoftClip(value);

                if (channel < maxAnalogOutputChannels)
                {
                    value = analogLowShelfFilters_[static_cast<std::size_t>(channel)].processSample(value);
                    value = analogHighShelfFilters_[static_cast<std::size_t>(channel)].processSample(value);
                }
            }

            value *= outGain;
            data[sample] = outputSafetySoftCeiling(value);
        }
    }

    analogOutputWasActive_ = analogMode;
}
//==============================================================================
void MicrotonalAutotuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    // Clear unused output channels
    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    if (numSamples == 0 || totalNumInputChannels == 0)
        return;

    // Get parameters
    float speedMs = apvts.getRawParameterValue ("speed")->load();
    float amountPct = apvts.getRawParameterValue ("amount")->load();
    float humanizePct = apvts.getRawParameterValue ("humanize")->load();

    bool scaleLock = apvts.getRawParameterValue ("scaleLock")->load() > 0.5f;
    float lockHysteresis = apvts.getRawParameterValue ("lockHysteresis")->load();
    float vibratoPreserve = apvts.getRawParameterValue ("vibratoPreserve")->load() / 100.0f; // 0-1
    bool analogMode = apvts.getRawParameterValue ("analogMode")->load() > 0.5f;
    float outVolumeDb = apvts.getRawParameterValue ("outVolume")->load();
    
    const int mode = juce::jlimit (1, 3, processingMode.load (std::memory_order_relaxed));

    speedMs = constrainRetuneSpeedMs (speedMs, mode, scaleLock);
    
    amountPct = std::isfinite (amountPct) ? juce::jlimit (0.0f, 100.0f, amountPct) : 0.0f;
    humanizePct = std::isfinite (humanizePct) ? juce::jlimit (0.0f, 100.0f, humanizePct) : 20.0f;

    const float amount = amountPct / 100.0f;
    const float humanizeVal = humanizePct / 100.0f;
    const float outGain = juce::Decibels::decibelsToGain(outVolumeDb);

    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        float* data = buffer.getWritePointer (channel);
        for (int sample = 0; sample < numSamples; ++sample)
        {
            float value = data[sample];
            value = (! std::isfinite (value) || std::fpclassify (value) == FP_SUBNORMAL)
                ? 0.0f : juce::jlimit (-32.0f, 32.0f, value);
            data[sample] = value;
        }
    }

    const int snapshotIndex = acquireScaleSnapshot();
    const auto& scaleSnapshot = scaleSnapshotSlots_[static_cast<std::size_t> (snapshotIndex)].value;

    // SINGLE_PLUGIN_AUDIO_PATH_V1: every release mode uses the same modern
    // detector -> correction controller -> SingleWetSpectralRenderer chain.
    // There is no mode-local YIN/circular-buffer renderer and no dry blend.
    livePitchProcessor.setTempoSettings (getTempoSettings());
    livePitchProcessor.setTempoHostPosition (
        readHostTempoPosition (numSamples));
    livePitchProcessor.setScaleLockParameters(scaleLock, lockHysteresis, vibratoPreserve);

    livePitchProcessor.setAdvancedParameters (
        35.0f,   // transitionMs
        0.70f,   // preserveVibrato
        humanizeVal,
        0.90f,   // formantPreservation
        0.85f,   // transientProtection
        0.70f,   // detectorSensitivity
        12.0f,   // maximumCorrectionSemitones
        45.0f,   // minimumPitchHz
        1600.0f, // maximumPitchHz
        LivePitchProcessor::StereoMode::linkedMidSide
    );

    livePitchProcessor.process (buffer,
                                scaleSnapshot.ratios.data(),
                                scaleSnapshot.count,
                                scaleSnapshot.rootFrequency,
                                speedMs,
                                amount);
    releaseScaleSnapshot (snapshotIndex);

    processOutputStage (buffer,
                        totalNumInputChannels,
                        numSamples,
                        analogMode,
                        outGain);
}
//==============================================================================
void MicrotonalAutotuneAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                              juce::MidiBuffer&)
{
    // HOST_BYPASS_ONLY_DRY_V1: this is the host's explicit plugin bypass, not
    // a detector/correction decision and not an alternate active audio path.
    livePitchProcessor.processBypassed (buffer);
}

//==============================================================================
LivePitchProcessor::Metering
MicrotonalAutotuneAudioProcessor::getPitchMetering() const noexcept
{
    return livePitchProcessor.getMetering();
}

//==============================================================================
bool MicrotonalAutotuneAudioProcessor::hasEditor() const { return true; }

juce::AudioProcessorEditor* MicrotonalAutotuneAudioProcessor::createEditor()
{
    return new MicrotonalAutotuneAudioProcessorEditor (*this);
}

//==============================================================================
void MicrotonalAutotuneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Save APVTS parameters
    auto state = apvts.copyState();

    // Save scale selection
    state.setProperty ("scaleIndex", currentScaleIndex.load(), nullptr);
    state.setProperty ("customPresetIndex", activeCustomPresetIndex.load(), nullptr);
    state.setProperty ("rootNoteIndex", rootNoteIndex.load(), nullptr);
    state.setProperty ("processingMode", processingMode.load(), nullptr);
    state.setProperty ("factoryPresetIndex", selectedPresetIndex, nullptr);

    // Save custom presets
    auto customTree = customPresets.toValueTree();
    state.addChild (customTree, -1, nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml != nullptr)
        copyXmlToBinary (*xml, destData);
}

void MicrotonalAutotuneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState != nullptr)
    {
        auto tree = juce::ValueTree::fromXml (*xmlState);

        if (tree.isValid())
        {
            apvts.replaceState (tree);

            // Restore scale selection
            if (tree.hasProperty ("scaleIndex"))
                currentScaleIndex.store (static_cast<int> (tree.getProperty ("scaleIndex")));

            if (tree.hasProperty ("customPresetIndex"))
                activeCustomPresetIndex.store (static_cast<int> (tree.getProperty ("customPresetIndex")));

            if (tree.hasProperty ("rootNoteIndex"))
                rootNoteIndex.store (static_cast<int> (tree.getProperty ("rootNoteIndex")));

            // Restore only the three modern modes. Legacy mode 0 is mapped
            // forward to Quality instead of resurrecting its removed renderer.
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

            // Restore custom presets
            auto customTree = tree.getChildWithName ("CustomScales");
            if (customTree.isValid())
                customPresets.fromValueTree (customTree);

            currentScaleIndex.store (juce::jlimit (0,
                std::max (0, ScaleDefinitions::getScaleCount() - 1),
                currentScaleIndex.load()), std::memory_order_relaxed);
            rootNoteIndex.store (juce::jlimit (0, 18, rootNoteIndex.load()),
                                 std::memory_order_relaxed);
            const int customCount = customPresets.getNumPresets();
            activeCustomPresetIndex.store (juce::jlimit (-1,
                std::max (-1, customCount - 1), activeCustomPresetIndex.load()),
                std::memory_order_relaxed);
            refreshScaleSnapshot();
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MicrotonalAutotuneAudioProcessor();
}
