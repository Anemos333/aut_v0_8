#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>
#include <limits>

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
int MicrotonalAutotuneAudioProcessor::getNumPrograms() { return 1; }
int MicrotonalAutotuneAudioProcessor::getCurrentProgram() { return 0; }
void MicrotonalAutotuneAudioProcessor::setCurrentProgram (int) {}
const juce::String MicrotonalAutotuneAudioProcessor::getProgramName (int) { return {}; }
void MicrotonalAutotuneAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
ModernPitchEngine::LatencyMode MicrotonalAutotuneAudioProcessor::modeToLatency (int mode) noexcept
{
    switch (mode)
    {
        case 1:  return ModernPitchEngine::LatencyMode::quality;
        case 2:  return ModernPitchEngine::LatencyMode::live;
        case 3:  return ModernPitchEngine::LatencyMode::ultraLive;
        default: return ModernPitchEngine::LatencyMode::live; // fallback
    }
}

int MicrotonalAutotuneAudioProcessor::getLatencyForMode (int mode) const
{
    if (mode == 0)
        return yinWindowSize; // Slow mode: 2048 samples

    // For ModernPitchEngine modes, query the engine
    // The frame size depends on sample rate and mode
    // We can compute it the same way the engine does
    return livePitchProcessor.getLatencySamples();
}

//==============================================================================
void MicrotonalAutotuneAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate)
                                                    : 44100.0;
    lastSamplesPerBlock = std::max(1, samplesPerBlock);
    refreshScaleSnapshot();
    smoothedShiftRatio = 1.0;

    // Circular buffer: 4x YIN window for comfortable pitch shifting (Slow mode)
    circBufSize = yinWindowSize * 4;
    circularBuffer.assign (static_cast<size_t> (circBufSize), 0.0f);
    circBufWritePos = 0;
    circBufReadPos = static_cast<double> (circBufSize - yinWindowSize);

    // YIN accumulation buffer (Slow mode)
    yinBuffer.assign (yinWindowSize, 0.0f);
    yinAccumulator.assign (static_cast<size_t> (yinWindowSize / 2), 0.0f);
    yinBufferPos = 0;
    lastDetectedPitch = 0.0f;
    slowMeterPitchHz.store (0.0f, std::memory_order_relaxed);
    slowMeterTargetHz.store (0.0f, std::memory_order_relaxed);
    slowResetRequested.store (false, std::memory_order_relaxed);

    // Prepare ModernPitchEngine-based live pitch processor
    int mode = processingMode.load();
    if (mode > 0)
    {
        livePitchProcessor.prepare (currentSampleRate,
                                    lastSamplesPerBlock,
                                    std::max (1, getTotalNumOutputChannels()),
                                    modeToLatency (mode));
        // Set sensible defaults for the advanced parameters
        float humanizeVal = apvts.getRawParameterValue ("humanize")->load() / 100.0f;
        livePitchProcessor.setAdvancedParameters (
            35.0f,   // transitionMs
            0.70f,   // preserveVibrato
            humanizeVal, // humanize
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
    else
    {
        // Slow mode: prepare engine with default for potential future switch
        livePitchProcessor.prepare (currentSampleRate,
                                    lastSamplesPerBlock,
                                    std::max (1, getTotalNumOutputChannels()),
                                    ModernPitchEngine::LatencyMode::live);
        float humanizeVal = apvts.getRawParameterValue ("humanize")->load() / 100.0f;
        livePitchProcessor.setAdvancedParameters (
            35.0f, 0.70f, humanizeVal, 0.90f, 0.85f, 0.70f, 12.0f, 45.0f, 1600.0f,
            LivePitchProcessor::StereoMode::linkedMidSide
        );
        setLatencySamples (yinWindowSize);
    }
}

void MicrotonalAutotuneAudioProcessor::releaseResources()
{
    circularBuffer.clear();
    yinBuffer.clear();
    yinAccumulator.clear();
    livePitchProcessor.reset();
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
    newMode = juce::jlimit (0, 3, newMode);
    const int oldMode = processingMode.load (std::memory_order_acquire);
    if (newMode == oldMode)
        return;

    if (newMode > 0)
    {
        // Publish a fully prepared modern engine before making the mode visible
        // to the callback. No allocation or prepare() occurs here.
        livePitchProcessor.setLatencyModeNonRealtime (modeToLatency (newMode));
        processingMode.store (newMode, std::memory_order_release);
        setLatencySamples (livePitchProcessor.getLatencySamples());
    }
    else
    {
        // Do not mutate Slow/High-Latency buffers from the message thread.
        // The callback consumes this request before touching that state.
        slowResetRequested.store (true, std::memory_order_release);
        processingMode.store (0, std::memory_order_release);
        setLatencySamples (yinWindowSize);
    }
}

void MicrotonalAutotuneAudioProcessor::resetSlowStateNoAlloc() noexcept
{
    smoothedShiftRatio = 1.0;
    std::fill (circularBuffer.begin(), circularBuffer.end(), 0.0f);
    circBufWritePos = 0;
    circBufReadPos = static_cast<double> (std::max (0, circBufSize - yinWindowSize));
    std::fill (yinBuffer.begin(), yinBuffer.end(), 0.0f);
    std::fill (yinAccumulator.begin(), yinAccumulator.end(), 0.0f);
    yinBufferPos = 0;
    lastDetectedPitch = 0.0f;
    slowMeterPitchHz.store (0.0f, std::memory_order_relaxed);
    slowMeterTargetHz.store (0.0f, std::memory_order_relaxed);
}

//==============================================================================
// YIN Pitch Detection Algorithm (Slow mode — unchanged from original)
float MicrotonalAutotuneAudioProcessor::detectPitchYIN (const float* buffer, int numSamples, double sampleRate) noexcept
{
    if (buffer == nullptr || numSamples < 2 || ! std::isfinite (sampleRate)
        || sampleRate <= 0.0)
        return 0.0f;

    const int halfWindow = numSamples / 2;
    if (halfWindow <= 1 || static_cast<int> (yinAccumulator.size()) < halfWindow)
        return 0.0f;
    float* diff = yinAccumulator.data();
    std::fill_n (diff, halfWindow, 0.0f);

    // Step 1: Difference function
    for (int tau = 0; tau < halfWindow; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < halfWindow; ++j)
        {
            float delta = buffer[j] - buffer[j + tau];
            sum += delta * delta;
        }
        diff[tau] = sum;
    }

    // Step 2: Cumulative Mean Normalized Difference Function (CMNDF)
    diff[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau < halfWindow; ++tau)
    {
        runningSum += diff[tau];
        if (runningSum > 0.0f)
            diff[tau] = diff[tau] * static_cast<float> (tau) / runningSum;
        else
            diff[tau] = 1.0f;
    }

    // Step 3: Absolute threshold
    constexpr float threshold = 0.15f;
    int tauEstimate = -1;

    // Skip tau=0 and tau=1 (too high frequency / not meaningful)
    for (int tau = 2; tau < halfWindow; ++tau)
    {
        if (diff[tau] < threshold)
        {
            // Find the local minimum
            while (tau + 1 < halfWindow &&
                   diff[static_cast<size_t> (tau + 1)] < diff[tau])
            {
                ++tau;
            }
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate < 2)
        return 0.0f;

    // Step 4: Parabolic interpolation for sub-sample accuracy
    double betterTau = static_cast<double> (tauEstimate);

    if (tauEstimate > 0 && tauEstimate < halfWindow - 1)
    {
        float s0 = diff[static_cast<size_t> (tauEstimate - 1)];
        float s1 = diff[static_cast<size_t> (tauEstimate)];
        float s2 = diff[static_cast<size_t> (tauEstimate + 1)];

        float denom = 2.0f * (2.0f * s1 - s0 - s2);
        if (std::abs (denom) > 1e-10f)
            betterTau = static_cast<double> (tauEstimate) + static_cast<double> ((s0 - s2) / denom);
    }

    if (betterTau <= 0.0)
        return 0.0f;

    return static_cast<float> (sampleRate / betterTau);
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

    // Preserve the musical invariant expected by both Slow mode and the modern
    // ScaleQuantizer: every scale degree lives inside one octave [1.0, 2.0),
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

double MicrotonalAutotuneAudioProcessor::findNearestTarget (
    double detectedFreqHz, const ScaleSnapshot& snapshot) const noexcept
{
    if (! std::isfinite (detectedFreqHz) || detectedFreqHz <= 0.0
        || snapshot.count <= 0 || ! std::isfinite (snapshot.rootFrequency)
        || snapshot.rootFrequency <= 0.0)
        return detectedFreqHz;

    const double logRatio = std::log2 (detectedFreqHz / snapshot.rootFrequency);
    const double octave = std::floor (logRatio);
    const double fractionalOctave = logRatio - octave;
    double bestDist = std::numeric_limits<double>::max();
    double bestLogTarget = logRatio;

    for (int index = 0; index < snapshot.count; ++index)
    {
        const double ratio = snapshot.ratios[static_cast<std::size_t> (index)];
        if (! std::isfinite (ratio) || ratio <= 0.0)
            continue;
        const double logPos = std::log2 (ratio);
        for (int wrap = -1; wrap <= 1; ++wrap)
        {
            const double candidate = logPos + static_cast<double> (wrap);
            const double distance = std::abs (fractionalOctave - candidate);
            if (distance < bestDist)
            {
                bestDist = distance;
                bestLogTarget = octave + candidate;
            }
        }
    }

    const double result = snapshot.rootFrequency * std::exp2 (bestLogTarget);
    return std::isfinite (result) && result > 0.0 ? result : detectedFreqHz;
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
    const auto& preset = FactoryPresets::getPreset (index);

    auto setParam = [this] (const juce::String& id, float plainValue)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
            p->endChangeGesture();
        }
    };

    setParam ("speed", preset.speedMs);
    setParam ("amount", preset.amount);
    setParam ("humanize", preset.humanize);

    setParam ("scaleLock", preset.scaleLock ? 1.0f : 0.0f);
    setParam ("lockHysteresis", preset.lockHysteresis);
    setParam ("vibratoPreserve", preset.vibratoPreserve);

    setParam ("tempoMode", static_cast<float> (preset.tempoMode));
    setParam ("tempoDivision", static_cast<float> (preset.tempoDivision));
    setParam ("tempoGlidePercent", preset.tempoGlidePct);
    setParam ("tempoLockStrength", preset.tempoLockStrength);
    setParam ("tempoSmartOnset", preset.tempoSmartOnset ? 1.0f : 0.0f);

    setParam ("analogMode", preset.analogMode ? 1.0f : 0.0f);
    setParam ("outVolume", preset.outVolumeDb);

    updateProcessingMode (preset.processingMode);
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

    int mode = juce::jlimit (0, 3, processingMode.load (std::memory_order_relaxed));

    speedMs = std::isfinite (speedMs) ? juce::jlimit (0.0f, 500.0f, speedMs) : 50.0f;
    if (scaleLock || mode == 3) {
        speedMs = speedMs * (7.0f / 500.0f); // Map 0-500 to 0-7 ms
    }
    
    amountPct = std::isfinite (amountPct) ? juce::jlimit (0.0f, 100.0f, amountPct) : 0.0f;
    humanizePct = std::isfinite (humanizePct) ? juce::jlimit (0.0f, 100.0f, humanizePct) : 20.0f;

    const float amount = amountPct / 100.0f;
    const float humanizeVal = humanizePct / 100.0f;

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

    // ==================== MODERN ENGINE MODES (Quality / Live / Experimental) ====================
    if (mode > 0)
    {
        // Creative tempo is an optional target scheduler. Off is a strict
        // pass-through, so the V5 correction path is unchanged.
        livePitchProcessor.setTempoSettings (getTempoSettings());
        livePitchProcessor.setTempoHostPosition (
            readHostTempoPosition (numSamples));
        livePitchProcessor.setScaleLockParameters(scaleLock, lockHysteresis, vibratoPreserve);

        // Update dynamic advanced parameters
        livePitchProcessor.setAdvancedParameters (
            35.0f,   // transitionMs
            0.70f,   // preserveVibrato
            humanizeVal, // humanize
            0.90f,   // formantPreservation
            0.85f,   // transientProtection
            0.70f,   // detectorSensitivity
            12.0f,   // maximumCorrectionSemitones
            45.0f,   // minimumPitchHz
            1600.0f, // maximumPitchHz
            LivePitchProcessor::StereoMode::linkedMidSide
        );

        // Use the AudioBuffer overload so the engine handles mono/stereo properly
        livePitchProcessor.process (buffer,
                                    scaleSnapshot.ratios.data(),
                                    scaleSnapshot.count,
                                    scaleSnapshot.rootFrequency,
                                    speedMs,
                                    amount);
        releaseScaleSnapshot (snapshotIndex);
        
        // Modifica B: Analog output + gain for modern mode
        if (scaleLock)
        {
            float outGain = juce::Decibels::decibelsToGain(outVolumeDb);
            // Fast polynomial soft-clipper (Padé approximant for tanh)
            auto fastSoftClip = [](float x) -> float {
                if (x < -3.0f) return -1.0f;
                if (x > 3.0f) return 1.0f;
                float x2 = x * x;
                return x * (27.0f + x2) / (27.0f + 9.0f * x2);
            };
            
            for (int channel = 0; channel < totalNumInputChannels; ++channel)
            {
                float* data = buffer.getWritePointer (channel);
                for (int sample = 0; sample < numSamples; ++sample)
                {
                    float value = data[sample];
                    if (analogMode) {
                        value = fastSoftClip(value); // efficient symmetric soft clip
                    }
                    data[sample] = value * outGain;
                }
            }
        }
        return;
    }

    // ==================== SLOW MODE (original YIN processing — unchanged) ====================
    if (slowResetRequested.exchange (false, std::memory_order_acq_rel))
        resetSlowStateNoAlloc();

    double speedSamples = (std::max)(1.0, (static_cast<double>(speedMs) / 1000.0) * currentSampleRate);
    double speedCoeff = 1.0;
    if (speedSamples > 1.0)
    {
        // Speed smoothing coefficient (one-pole filter)
        // speedMs = time to reach ~63% of target
        speedCoeff = 1.0 - std::exp (-1.0 / speedSamples);
    }

    // Process first channel, then copy to others (mono processing for pitch)
    const float* inputData = buffer.getReadPointer (0);

    // Accumulate samples into YIN buffer for pitch detection
    for (int i = 0; i < numSamples; ++i)
    {
        yinBuffer[static_cast<size_t> (yinBufferPos)] = inputData[i];
        yinBufferPos++;

        if (yinBufferPos >= yinWindowSize)
        {
            // Run YIN pitch detection
            float detectedPitch = detectPitchYIN (yinBuffer.data(), yinWindowSize, currentSampleRate);

            // Only update if we got a valid pitch (20 Hz to 5000 Hz range)
            if (detectedPitch > 20.0f && detectedPitch < 5000.0f)
            {
                lastDetectedPitch = detectedPitch;
                slowMeterPitchHz.store (detectedPitch,
                                        std::memory_order_relaxed);
            }

            yinBufferPos = 0;
        }
    }

    // Compute target shift ratio
    double targetShiftRatio = 1.0;
    if (lastDetectedPitch > 20.0f)
    {
        double targetFreq = findNearestTarget (
            static_cast<double> (lastDetectedPitch), scaleSnapshot);
        if (targetFreq > 0.0)
        {
            targetShiftRatio = targetFreq / static_cast<double> (lastDetectedPitch);
            slowMeterTargetHz.store (static_cast<float> (targetFreq),
                                     std::memory_order_relaxed);
        }
    }
    else
    {
        slowMeterTargetHz.store (0.0f, std::memory_order_relaxed);
    }
    releaseScaleSnapshot (snapshotIndex);

    // Process each sample with pitch shifting via variable-rate read from circular buffer
    float* outputData = buffer.getWritePointer (0);
    if (! std::isfinite (smoothedShiftRatio))
        smoothedShiftRatio = 1.0;
    if (! std::isfinite (circBufReadPos))
        circBufReadPos = static_cast<double> ((std::max) (0, circBufSize - yinWindowSize));

    for (int i = 0; i < numSamples; ++i)
    {
        // Write input to circular buffer
        circularBuffer[static_cast<size_t> (circBufWritePos)] = inputData[i];
        circBufWritePos = (circBufWritePos + 1) % circBufSize;

        // Smooth the shift ratio
        smoothedShiftRatio += speedCoeff * (targetShiftRatio - smoothedShiftRatio);

        // Apply amount: blend between 1.0 (no correction) and smoothedShiftRatio
        double effectiveRatio = 1.0 + (smoothedShiftRatio - 1.0) * static_cast<double> (amount);

        // Read from circular buffer at variable rate
        circBufReadPos += effectiveRatio;
        while (circBufReadPos >= static_cast<double> (circBufSize))
            circBufReadPos -= static_cast<double> (circBufSize);
        while (circBufReadPos < 0.0)
            circBufReadPos += static_cast<double> (circBufSize);

        // Hermite (cubic) interpolation for higher audio quality
        int readIdx1 = static_cast<int> (circBufReadPos);
        int readIdx0 = (readIdx1 - 1 + circBufSize) % circBufSize;
        int readIdx2 = (readIdx1 + 1) % circBufSize;
        int readIdx3 = (readIdx1 + 2) % circBufSize;
        
        double frac = circBufReadPos - static_cast<double> (readIdx1);
        
        float y0 = circularBuffer[static_cast<size_t> (readIdx0)];
        float y1 = circularBuffer[static_cast<size_t> (readIdx1)];
        float y2 = circularBuffer[static_cast<size_t> (readIdx2)];
        float y3 = circularBuffer[static_cast<size_t> (readIdx3)];
        
        float c0 = y1;
        float c1 = 0.5f * (y2 - y0);
        float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        float c3 = 1.5f * (y1 - y2) + 0.5f * (y3 - y0);
        
        float sample = static_cast<float> (
            ((c3 * frac + c2) * frac + c1) * frac + c0);

        outputData[i] = std::isfinite (sample)
            ? juce::jlimit (-32.0f, 32.0f, sample) : 0.0f;
    }

    // Output stage: Analog saturation + Output Gain (shared with modern path)
    {
        float outGain = juce::Decibels::decibelsToGain (outVolumeDb);
        auto fastSoftClip = [](float x) -> float {
            if (x < -3.0f) return -1.0f;
            if (x > 3.0f) return 1.0f;
            float x2 = x * x;
            return x * (27.0f + x2) / (27.0f + 9.0f * x2);
        };
        auto scaleLockSoftCompressor = [] (float x) -> float
{
    // Curva stateless molto morbida: non pompa, non richiede membri nuovi,
    // protegge solo i picchi sopra circa -3 dBFS.
    constexpr float threshold = 0.7079458f; // circa -3 dBFS
    constexpr float softness = 0.35f;       // basso = molto soft

    const float ax = std::abs(x);
    if (ax <= threshold)
        return x;

    const float over = ax - threshold;
    const float compressed = threshold + over / (1.0f + softness * over);
    return std::copysign(compressed, x);
};

        for (int i = 0; i < numSamples; ++i)
        {
            float value = outputData[i];
            if (analogMode)
                value = fastSoftClip (value);
            outputData[i] = value * outGain;
            outputData[i] = scaleLockSoftCompressor(outputData[i]);
        }
    }

    // Copy mono result to all output channels
    for (int ch = 1; ch < totalNumOutputChannels; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);
}

//==============================================================================
void MicrotonalAutotuneAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                              juce::MidiBuffer&)
{
    int mode = processingMode.load (std::memory_order_relaxed);

    if (mode > 0)
    {
        // Use the engine's bypassed processing to maintain aligned latency
        livePitchProcessor.processBypassed (buffer);
    }
    else
    {
        // Slow mode: apply delay manually to maintain latency alignment.
        if (slowResetRequested.exchange (false, std::memory_order_acq_rel))
            resetSlowStateNoAlloc();
        if (! std::isfinite (circBufReadPos))
            circBufReadPos = static_cast<double> (std::max (0, circBufSize - yinWindowSize));

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                const float input = (! std::isfinite (data[i])
                                     || std::fpclassify (data[i]) == FP_SUBNORMAL)
                    ? 0.0f : juce::jlimit (-32.0f, 32.0f, data[i]);
                circularBuffer[static_cast<size_t> (circBufWritePos)] = input;
                circBufWritePos = (circBufWritePos + 1) % circBufSize;

                circBufReadPos += 1.0;
                while (circBufReadPos >= static_cast<double> (circBufSize))
                    circBufReadPos -= static_cast<double> (circBufSize);

                int readIdx = static_cast<int> (circBufReadPos);
                const float delayed = circularBuffer[static_cast<size_t> (readIdx)];
                data[i] = std::isfinite (delayed)
                    ? juce::jlimit (-32.0f, 32.0f, delayed) : 0.0f;
                if (! std::isfinite (delayed))
                    circularBuffer[static_cast<size_t> (readIdx)] = 0.0f;
            }
        }
    }
}

//==============================================================================
LivePitchProcessor::Metering
MicrotonalAutotuneAudioProcessor::getPitchMetering() const noexcept
{
    if (processingMode.load (std::memory_order_relaxed) > 0)
        return livePitchProcessor.getMetering();

    LivePitchProcessor::Metering meter;
    meter.detectedPitchHz = slowMeterPitchHz.load (std::memory_order_relaxed);
    meter.targetPitchHz = slowMeterTargetHz.load (std::memory_order_relaxed);
    // The legacy Slow detector does not expose calibrated confidence or
    // voicing values, so report them as unavailable rather than inventing a
    // percentage.  Pitch and target remain useful in this mode.
    meter.confidence = 0.0f;
    meter.voicing = 0.0f;
    meter.consensus = 0.0f;
    meter.detectorSupport = meter.detectedPitchHz > 0.0f ? 1 : 0;
    if (meter.detectedPitchHz > 0.0f && meter.targetPitchHz > 0.0f)
    {
        meter.correctionCents = 1200.0f * std::log2(
            meter.targetPitchHz / meter.detectedPitchHz);
    }
    meter.state = meter.detectedPitchHz > 0.0f
        ? ModernPitchEngine::TrackingState::stable
        : ModernPitchEngine::TrackingState::unvoiced;
    return meter;
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

            // Restore processing mode (with backward compatibility for liveModeEnabled)
            if (tree.hasProperty ("processingMode"))
            {
                int mode = juce::jlimit (0, 3,
                    static_cast<int> (tree.getProperty ("processingMode")));
                updateProcessingMode (mode);
            }
            else if (tree.hasProperty ("liveModeEnabled"))
            {
                // Backward compatibility: old sessions with liveModeEnabled bool
                bool wasLive = static_cast<int> (tree.getProperty ("liveModeEnabled")) != 0;
                int mode = wasLive ? 2 : 0; // map old Live to new Live mode
                updateProcessingMode (mode);
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
