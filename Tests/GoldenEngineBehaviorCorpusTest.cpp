#include <JuceHeader.h>
#include "../Source/ModernPitchEngine.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
constexpr double kSampleRate = 48000.0;
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kTwoPi = 2.0 * kPi;

std::uint32_t bits(float value) noexcept
{
    std::uint32_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::uint64_t bits(double value) noexcept
{
    std::uint64_t result = 0;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

struct TraceHash
{
    void byte(std::uint8_t value) noexcept
    {
        hash ^= static_cast<std::uint64_t>(value);
        hash *= 1099511628211ull;
    }

    void u32(std::uint32_t value) noexcept
    {
        for (int shift = 0; shift < 32; shift += 8)
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }

    void u64(std::uint64_t value) noexcept
    {
        for (int shift = 0; shift < 64; shift += 8)
            byte(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }

    void f32(float value) noexcept { u32(bits(value)); }
    void f64(double value) noexcept { u64(bits(value)); }
    void integer(int value) noexcept { u32(static_cast<std::uint32_t>(value)); }
    void boolean(bool value) noexcept { byte(value ? 1u : 0u); }

    void text(std::string_view value) noexcept
    {
        for (char c : value)
            byte(static_cast<std::uint8_t>(c));
        byte(0xffu);
    }

    std::uint64_t hash = 1469598103934665603ull;
};

std::array<double, 12> chromaticScale()
{
    std::array<double, 12> scale {};
    for (int degree = 0; degree < 12; ++degree)
        scale[static_cast<std::size_t>(degree)] =
            std::exp2(static_cast<double>(degree) / 12.0);
    return scale;
}

enum class SignalKind
{
    longVowel,
    octaveTrap,
    noteTransitions,
    glide,
    consonantInterruptions,
    softAuthority,
    scaleLockOff
};

struct Scenario
{
    const char* name = "";
    SignalKind kind = SignalKind::longVowel;
    bool scaleLock = true;
    float retuneMs = 8.0f;
    float transitionMs = 35.0f;
    float humanize = 0.20f;
    float vibratoPreserve = 0.0f;
    float detectorSensitivity = 0.70f;
};

constexpr std::array<Scenario, 7> kScenarios {{
    { "long_vowel_amplitude_motion", SignalKind::longVowel, true, 8.0f, 35.0f, 0.20f, 0.0f, 0.70f },
    { "upward_octave_trap", SignalKind::octaveTrap, true, 8.0f, 35.0f, 0.20f, 0.0f, 0.70f },
    { "legato_note_transitions", SignalKind::noteTransitions, true, 8.0f, 35.0f, 0.20f, 0.0f, 0.70f },
    { "continuous_glide", SignalKind::glide, true, 8.0f, 35.0f, 0.20f, 0.0f, 0.70f },
    { "consonant_interruptions", SignalKind::consonantInterruptions, true, 8.0f, 35.0f, 0.20f, 0.0f, 0.70f },
    { "soft_authority_no_unity_escape", SignalKind::softAuthority, true, 55.0f, 95.0f, 0.62f, 0.72f, 0.70f },
    { "scale_lock_off_reference", SignalKind::scaleLockOff, false, 55.0f, 95.0f, 0.62f, 0.72f, 0.70f },
}};

struct SignalState
{
    double phase = 0.0;
    std::uint32_t noise = 0x7f4a7c15u;
};

double baseFrequency(SignalKind kind, double t) noexcept
{
    switch (kind)
    {
        case SignalKind::longVowel:
        case SignalKind::octaveTrap:
        case SignalKind::consonantInterruptions:
            return 452.0;

        case SignalKind::softAuthority:
        case SignalKind::scaleLockOff:
            return 452.0 + 7.0 * std::sin(kTwoPi * 0.28 * t);

        case SignalKind::noteTransitions:
            if (t < 0.90) return 452.0;
            if (t < 1.70) return 486.0;
            if (t < 2.45) return 466.0;
            return 452.0;

        case SignalKind::glide:
        {
            const double cycle = std::fmod(t, 1.60) / 1.60;
            const double triangle = cycle < 0.5 ? cycle * 2.0 : 2.0 - cycle * 2.0;
            return 430.0 + 55.0 * triangle;
        }
    }
    return 452.0;
}

float nextSignalSample(const Scenario& scenario,
                       SignalState& state,
                       std::uint64_t absoluteSample) noexcept
{
    const double t = static_cast<double>(absoluteSample) / kSampleRate;
    const double hz = baseFrequency(scenario.kind, t);
    const double vibratoCents =
        scenario.kind == SignalKind::softAuthority
        || scenario.kind == SignalKind::scaleLockOff
        || scenario.kind == SignalKind::longVowel
        ? 34.0 * std::sin(kTwoPi * 5.15 * t)
        : 11.0 * std::sin(kTwoPi * 4.7 * t);
    const double instantaneousHz = hz * std::exp2(vibratoCents / 1200.0);

    state.phase += kTwoPi * instantaneousHz / kSampleRate;
    state.phase -= kTwoPi * std::floor(state.phase / kTwoPi);

    double amplitude = 0.20;
    if (scenario.kind == SignalKind::longVowel)
    {
        amplitude *= 0.62
            + 0.28 * (0.5 + 0.5 * std::sin(kTwoPi * 0.63 * t))
            + 0.10 * (0.5 + 0.5 * std::sin(kTwoPi * 2.1 * t));
    }

    double fundamental = 1.0;
    double second = 0.58;
    double third = 0.31;
    double fourth = 0.17;

    if (scenario.kind == SignalKind::octaveTrap)
    {
        // Constant physical pitch. During the middle of the note the fundamental
        // becomes weak while 2F0 dominates: the classic upward-octave trap.
        const bool trap = t > 0.85 && t < 2.35;
        fundamental = trap ? 0.13 : 1.0;
        second = trap ? 1.38 : 0.58;
        third = trap ? 0.44 : 0.31;
        fourth = trap ? 0.26 : 0.17;
    }

    const double vowel =
          fundamental * std::sin(state.phase)
        + second * std::sin(2.0 * state.phase + 0.19)
        + third * std::sin(3.0 * state.phase - 0.31)
        + fourth * std::sin(4.0 * state.phase + 0.47)
        + 0.09 * std::sin(5.0 * state.phase - 0.12);

    state.noise ^= state.noise << 13;
    state.noise ^= state.noise >> 17;
    state.noise ^= state.noise << 5;
    const double white = (static_cast<int>(state.noise & 0xffffu) - 32768)
                       / 32768.0;

    double consonant = 0.0;
    if (scenario.kind == SignalKind::consonantInterruptions)
    {
        const double local = std::fmod(t, 0.72);
        if (local > 0.48 && local < 0.545)
            consonant = 0.16 * white;
    }

    return static_cast<float>(amplitude * vowel + consonant + 0.0015 * white);
}

void hashMeter(TraceHash& hash,
               const ModernPitchEngine::Metering& meter) noexcept
{
    hash.f32(meter.detectedPitchHz);
    hash.f32(meter.targetPitchHz);
    hash.f32(meter.confidence);
    hash.f32(meter.voicing);
    hash.f32(meter.harmonicity);
    hash.f32(meter.consensus);
    hash.f32(meter.correctionCents);
    hash.f32(meter.wetMix);
    hash.f32(meter.transitionBlend);
    hash.integer(meter.detectorSupport);
    hash.integer(meter.octaveState);
    hash.integer(meter.pendingOctaveObservations);
    hash.integer(static_cast<int>(meter.state));
}

std::uint64_t runScenario(const Scenario& scenario, int blockSize)
{
    ModernPitchEngine engine;
    engine.prepare(kSampleRate, blockSize, 1, ModernPitchEngine::LatencyMode::live);

    ModernPitchEngine::Parameters parameters;
    parameters.amount = 1.0f;
    parameters.retuneTimeMs = scenario.retuneMs;
    parameters.transitionTimeMs = scenario.transitionMs;
    parameters.humanize = scenario.humanize;
    parameters.preserveVibrato = scenario.vibratoPreserve;
    parameters.vibratoPreserve = scenario.vibratoPreserve;
    parameters.formantPreservation = 0.90f;
    parameters.transientProtection = 0.85f;
    parameters.detectorSensitivity = scenario.detectorSensitivity;
    parameters.maximumCorrectionSemitones = 12.0f;
    parameters.minimumPitchHz = 45.0f;
    parameters.maximumPitchHz = 1600.0f;
    parameters.scaleLock = scenario.scaleLock;
    parameters.lockHysteresis = 24.0f;

    // Keep voice-analysis context strong and deterministic. Detector coordinate
    // changes remain the only possible source of musical divergence here.
    parameters.voiceEvidenceValid = true;
    parameters.voiceHarmonicity = 0.88f;
    parameters.voiceBreathiness = 0.10f;
    parameters.voiceBodyEnergy = 0.90f;
    parameters.voiceSpectralReliability = 0.88f;
    parameters.voiceEventStrength = 0.08f;
    parameters.voiceFormantStability = 0.90f;
    parameters.voiceLowerFamilyEvidence = 0.08f;

    const auto scale = chromaticScale();
    juce::AudioBuffer<float> buffer(1, blockSize);
    SignalState signal;
    TraceHash hash;
    hash.text(scenario.name);
    hash.integer(blockSize);

    constexpr std::uint64_t durationSamples =
        static_cast<std::uint64_t>(kSampleRate * 3.20);
    std::uint64_t absoluteSample = 0;
    int blockIndex = 0;

    while (absoluteSample < durationSamples)
    {
        const int count = static_cast<int>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(blockSize), durationSamples - absoluteSample));
        buffer.setSize(1, count, false, false, true);
        float* out = buffer.getWritePointer(0);
        for (int i = 0; i < count; ++i)
            out[i] = nextSignalSample(scenario, signal, absoluteSample + static_cast<std::uint64_t>(i));

        engine.process(buffer,
                       scale.data(),
                       static_cast<int>(scale.size()),
                       440.0,
                       parameters);

        const float* rendered = buffer.getReadPointer(0);
        for (int i = 0; i < count; ++i)
        {
            if (!std::isfinite(rendered[i]))
            {
                std::cerr << "NONFINITE_AUDIO scenario=" << scenario.name
                          << " block=" << blockSize
                          << " sample=" << (absoluteSample + static_cast<std::uint64_t>(i))
                          << '\n';
                std::exit(2);
            }
            hash.f32(rendered[i]);
        }

        const auto meter = engine.getMetering();
        hash.integer(blockIndex);
        hashMeter(hash, meter);

        absoluteSample += static_cast<std::uint64_t>(count);
        ++blockIndex;
    }

    return hash.hash;
}

} // namespace

int main()
{
    TraceHash global;
    for (const int blockSize : { 64, 256 })
    {
        for (const auto& scenario : kScenarios)
        {
            const auto scenarioHash = runScenario(scenario, blockSize);
            global.text(scenario.name);
            global.integer(blockSize);
            global.u64(scenarioHash);
            std::cout << "GOLDEN_ENGINE_SCENARIO_HASH"
                      << " name=" << scenario.name
                      << " block=" << blockSize
                      << " hash=" << std::hex << std::setw(16) << std::setfill('0')
                      << scenarioHash << std::dec << '\n';
        }
    }

    std::cout << "GOLDEN_ENGINE_BEHAVIOR_CORPUS=PASS scenarios="
              << (kScenarios.size() * 2) << '\n';
    std::cout << "GOLDEN_ENGINE_CORPUS_HASH="
              << std::hex << std::setw(16) << std::setfill('0')
              << global.hash << std::dec << '\n';
    return 0;
}
