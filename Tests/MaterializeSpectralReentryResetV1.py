#!/usr/bin/env python3
from __future__ import annotations

import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


def git_blob(path: str) -> str:
    return subprocess.run(
        ["git", "hash-object", str(ROOT / path)],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()


renderer_path = ROOT / "Source/SingleWetSpectralRenderer.cpp"
renderer = renderer_path.read_text(encoding="utf-8")

renderer = replace_once(
    renderer,
    """    const std::int64_t frameStartSample = frameEndSample - frameSize_ + 1;\n    for (int index = 0; index < frameSize_; ++index)\n    {\n        const float input = readInputSample(frameStartSample + index);\n        fftBuffer_[static_cast<std::size_t>(index)] = Complex(\n            input * window_[static_cast<std::size_t>(index)], 0.0f);\n    }\n    fft(fftBuffer_, false);\n""",
    """    const std::int64_t frameStartSample = frameEndSample - frameSize_ + 1;\n    double frameEnergy = 0.0;\n    for (int index = 0; index < frameSize_; ++index)\n    {\n        const float input = readInputSample(frameStartSample + index);\n        frameEnergy += static_cast<double>(input) * static_cast<double>(input);\n        fftBuffer_[static_cast<std::size_t>(index)] = Complex(\n            input * window_[static_cast<std::size_t>(index)], 0.0f);\n    }\n    fft(fftBuffer_, false);\n\n    // SILENCE_REENTRY_RESEEDS_PHASE_AND_ENVELOPE_V1: silence is not a second\n    // audio path and never changes correction authority. It only invalidates\n    // phase/envelope history that cannot meaningfully describe the next physical\n    // emission. Keep OLA/output state intact: no mute, no dry crossfade, no gap.\n    // The threshold is deliberately below normal recorded noise so an ordinary\n    // weak vowel or breath is never itself declared silence.\n    const double frameRms = std::sqrt(frameEnergy\n        / static_cast<double>(std::max(1, frameSize_)));\n    constexpr double reentrySilenceRms = 2.0e-5;\n    const bool reentrySilenceFrame = frameRms <= reentrySilenceRms;\n    if (reentrySilenceFrame)\n    {\n        phaseResetPending_ = true;\n        envelopeInitialised_ = false;\n        envelopeFrameCounter_ = 0;\n    }\n""",
    "accumulate frame energy and arm reentry reset",
)

renderer = replace_once(
    renderer,
    """    if (!envelopeInitialised_\n        || ++envelopeFrameCounter_ >= envelopeUpdateInterval_)\n    {\n        envelopeFrameCounter_ = 0;\n        calculateEnvelope(positiveBins);\n    }\n\n    const bool resetAnalysis = phaseResetPending_ || !analysisPhaseInitialised_;\n    phaseResetPending_ = false;\n""",
    """    // Do not learn a spectral envelope from silence. The first energetic\n    // frame after the gap becomes the fresh envelope reference instead of being\n    // warped through the previous vowel/formant history.\n    if (!reentrySilenceFrame\n        && (!envelopeInitialised_\n            || ++envelopeFrameCounter_ >= envelopeUpdateInterval_))\n    {\n        envelopeFrameCounter_ = 0;\n        calculateEnvelope(positiveBins);\n    }\n\n    const bool resetAnalysis = phaseResetPending_ || !analysisPhaseInitialised_;\n    // Keep the reset armed for the complete silent interval. It is consumed only\n    // by the first energetic frame, where analysis and synthesis phases are both\n    // seeded from that frame. No accumulation ring is cleared.\n    if (!reentrySilenceFrame)\n        phaseResetPending_ = false;\n""",
    "consume reentry reset only on energetic frame",
)

renderer_path.write_text(renderer, encoding="utf-8")


test_path = ROOT / "Tests/SingleWetSpectralRendererTest.cpp"
test = test_path.read_text(encoding="utf-8")
test = replace_once(
    test,
    "#include <cmath>\n#include <iostream>\n#include <vector>\n",
    "#include <cmath>\n#include <cstdint>\n#include <iostream>\n#include <vector>\n",
    "add cstdint",
)

helpers = r'''

double rangeEnergy(const std::vector<float>& signal, int startSample, int sampleCount)
{
    const int start = std::clamp(startSample, 0, static_cast<int>(signal.size()));
    const int end = std::clamp(start + sampleCount, start, static_cast<int>(signal.size()));
    double energy = 0.0;
    for (int sample = start; sample < end; ++sample)
    {
        const double value = signal[static_cast<std::size_t>(sample)];
        energy += value * value;
    }
    return energy;
}

double rangeRms(const std::vector<float>& signal, int startSample, int sampleCount)
{
    const int start = std::clamp(startSample, 0, static_cast<int>(signal.size()));
    const int end = std::clamp(start + sampleCount, start, static_cast<int>(signal.size()));
    const int count = std::max(1, end - start);
    return std::sqrt(rangeEnergy(signal, start, count) / static_cast<double>(count));
}

double differenceRms(const std::vector<float>& signal, int startSample, int sampleCount)
{
    const int start = std::clamp(startSample + 1, 1, static_cast<int>(signal.size()));
    const int end = std::clamp(startSample + sampleCount, start, static_cast<int>(signal.size()));
    double energy = 0.0;
    int count = 0;
    for (int sample = start; sample < end; ++sample)
    {
        const double delta = static_cast<double>(signal[static_cast<std::size_t>(sample)])
            - static_cast<double>(signal[static_cast<std::size_t>(sample - 1)]);
        energy += delta * delta;
        ++count;
    }
    return std::sqrt(energy / static_cast<double>(std::max(1, count)));
}

int longestNearZeroRun(const std::vector<float>& signal,
                       int startSample,
                       int sampleCount,
                       float threshold)
{
    const int start = std::clamp(startSample, 0, static_cast<int>(signal.size()));
    const int end = std::clamp(start + sampleCount, start, static_cast<int>(signal.size()));
    int longest = 0;
    int current = 0;
    for (int sample = start; sample < end; ++sample)
    {
        if (std::abs(signal[static_cast<std::size_t>(sample)]) <= threshold)
        {
            ++current;
            longest = std::max(longest, current);
        }
        else
        {
            current = 0;
        }
    }
    return longest;
}

double tonePowerRange(const std::vector<float>& signal,
                      double frequencyHz,
                      int startSample,
                      int sampleCount)
{
    const int start = std::clamp(startSample, 0, static_cast<int>(signal.size()));
    const int end = std::clamp(start + sampleCount, start, static_cast<int>(signal.size()));
    double real = 0.0;
    double imaginary = 0.0;
    for (int sample = start; sample < end; ++sample)
    {
        const double phase = 2.0 * pi * frequencyHz
            * static_cast<double>(sample) / sampleRate;
        const double value = signal[static_cast<std::size_t>(sample)];
        real += value * std::cos(phase);
        imaginary -= value * std::sin(phase);
    }
    return real * real + imaginary * imaginary;
}

double maximumCoherentFraction(const std::vector<float>& signal,
                               int startSample,
                               int sampleCount)
{
    const double energy = rangeEnergy(signal, startSample, sampleCount);
    const double normaliser = std::max(1.0e-20,
        energy * static_cast<double>(std::max(1, sampleCount)));
    double maximum = 0.0;
    for (double frequency = 160.0; frequency <= 8000.0; frequency += 41.0)
    {
        maximum = std::max(maximum,
            tonePowerRange(signal, frequency, startSample, sampleCount) / normaliser);
    }
    return maximum;
}

float deterministicNoise(std::uint32_t& state)
{
    state = 1664525u * state + 1013904223u;
    const double unit = static_cast<double>((state >> 8) & 0x00ffffffu)
        / static_cast<double>(0x01000000u);
    return static_cast<float>(2.0 * unit - 1.0);
}

struct ReentryRender
{
    std::vector<float> output;
    int firstStart = 0;
    int secondStart = 0;
    int latency = 0;
};

ReentryRender renderRepeatedNoiseBurst(bool filtered)
{
    constexpr int frameSize = 512;
    constexpr int burstSamples = 12000;
    constexpr int silenceSamples = 7200;
    constexpr int flushSamples = 2048;

    std::vector<float> burst(static_cast<std::size_t>(burstSamples));
    std::uint32_t randomState = 0x31415926u;
    float lowpass = 0.0f;
    for (int sample = 0; sample < burstSamples; ++sample)
    {
        const float white = deterministicNoise(randomState);
        if (filtered)
        {
            lowpass += 0.085f * (white - lowpass);
            burst[static_cast<std::size_t>(sample)] = 0.12f * (white - lowpass);
        }
        else
        {
            burst[static_cast<std::size_t>(sample)] = 0.10f * white;
        }
    }

    ReentryRender result;
    result.firstStart = 0;
    result.secondStart = burstSamples + silenceSamples;
    result.latency = frameSize;
    const int totalSamples = result.secondStart + burstSamples + flushSamples;
    result.output.resize(static_cast<std::size_t>(totalSamples));

    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        float input = 0.0f;
        if (sample < burstSamples)
            input = burst[static_cast<std::size_t>(sample)];
        else if (sample >= result.secondStart
                 && sample < result.secondStart + burstSamples)
            input = burst[static_cast<std::size_t>(sample - result.secondStart)];

        result.output[static_cast<std::size_t>(sample)] = renderer.processSample(
            input, 430.0, 0.92f);
    }
    return result;
}

struct ToneReentryRender
{
    std::vector<float> output;
    int postStart = 0;
    int latency = 0;
    double sourceHz = 277.0;
    double targetHz = 440.0;
};

ToneReentryRender renderToneAfterSilence()
{
    constexpr int frameSize = 512;
    constexpr int firstToneSamples = 9600;
    constexpr int silenceSamples = 7200;
    constexpr int postToneSamples = 30000;
    constexpr int flushSamples = 2048;
    constexpr int rampSamples = 240;

    ToneReentryRender result;
    result.postStart = firstToneSamples + silenceSamples;
    result.latency = frameSize;
    const int totalSamples = result.postStart + postToneSamples + flushSamples;
    result.output.resize(static_cast<std::size_t>(totalSamples));

    const double correctionCents = 1200.0 * std::log2(result.targetHz / result.sourceHz);
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        float input = 0.0f;
        if (sample < firstToneSamples)
        {
            input = 0.18f * static_cast<float>(std::sin(
                2.0 * pi * 220.0 * static_cast<double>(sample) / sampleRate));
        }
        else if (sample >= result.postStart
                 && sample < result.postStart + postToneSamples)
        {
            const int local = sample - result.postStart;
            const double ramp = local < rampSamples
                ? 0.5 - 0.5 * std::cos(pi * static_cast<double>(local)
                    / static_cast<double>(rampSamples))
                : 1.0;
            input = static_cast<float>(0.20 * ramp * std::sin(
                2.0 * pi * result.sourceHz * static_cast<double>(local) / sampleRate));
        }
        result.output[static_cast<std::size_t>(sample)] = renderer.processSample(
            input, correctionCents, 0.95f);
    }
    return result;
}
'''

test = replace_once(
    test,
    "} // namespace\n\nint main()",
    helpers + "\n} // namespace\n\nint main()",
    "insert reentry helpers",
)

new_tests = r'''

    // SILENCE_REENTRY_RESEEDS_PHASE_AND_ENVELOPE_V1: the exact same aperiodic
    // material rendered at startup and after a true silence must have equivalent
    // energy and spectral roughness. Re-entry may reset analysis history, never
    // add a second periodic component, mute the signal, or create a dry copy.
    for (const bool filtered : { false, true })
    {
        const auto reentry = renderRepeatedNoiseBurst(filtered);
        const int analysisOffset = reentry.latency + 256;
        const int windowSamples = 3072;
        const int firstWindow = reentry.firstStart + analysisOffset;
        const int secondWindow = reentry.secondStart + analysisOffset;
        const double firstRms = rangeRms(reentry.output, firstWindow, windowSamples);
        const double secondRms = rangeRms(reentry.output, secondWindow, windowSamples);
        const double levelDeltaDb = 20.0 * std::log10(
            std::max(1.0e-12, secondRms) / std::max(1.0e-12, firstRms));
        const double firstRoughness = differenceRms(
            reentry.output, firstWindow, windowSamples) / std::max(1.0e-12, firstRms);
        const double secondRoughness = differenceRms(
            reentry.output, secondWindow, windowSamples) / std::max(1.0e-12, secondRms);
        const double coherentFraction = maximumCoherentFraction(
            reentry.output, secondWindow, windowSamples);
        const int silentRun = longestNearZeroRun(
            reentry.output, secondWindow, windowSamples, 1.0e-7f);

        std::cerr << (filtered ? "filtered" : "white")
                  << "_reentry_level_delta_db=" << levelDeltaDb
                  << " roughness_delta=" << std::abs(secondRoughness - firstRoughness)
                  << " coherent_fraction=" << coherentFraction
                  << " longest_zero_run=" << silentRun << '\n';

        success &= check(std::abs(levelDeltaDb) < 2.0,
                         filtered ? "filtered_noise_reentry_has_no_energy_burst_or_hole"
                                  : "pure_noise_reentry_has_no_energy_burst_or_hole");
        success &= check(std::abs(secondRoughness - firstRoughness) < 0.22,
                         filtered ? "filtered_noise_reentry_keeps_one_broadband_texture"
                                  : "pure_noise_reentry_keeps_one_broadband_texture");
        success &= check(coherentFraction < 0.035,
                         filtered ? "filtered_noise_does_not_acquire_periodic_copy"
                                  : "pure_noise_does_not_acquire_periodic_copy");
        success &= check(silentRun < 64,
                         filtered ? "filtered_noise_reentry_never_inserts_silence"
                                  : "pure_noise_reentry_never_inserts_silence");
    }

    // A real periodic emission after silence must re-enter on the commanded
    // correction ratio, with no dry/source copy, click, or hidden response gap.
    const auto toneReentry = renderToneAfterSilence();
    const int postOutputStart = toneReentry.postStart + toneReentry.latency;
    const int steadyStart = postOutputStart + 6000;
    const double measuredReentryHz = estimateToneFrequency(
        toneReentry.output, toneReentry.targetHz, steadyStart);
    const double reentryCentsError = centsError(measuredReentryHz, toneReentry.targetHz);
    const double targetReentryPower = tonePower(
        toneReentry.output, toneReentry.targetHz, steadyStart);
    const double dryReentryPower = tonePower(
        toneReentry.output, toneReentry.sourceHz, steadyStart);

    double maximumAttackStep = 0.0;
    for (int sample = postOutputStart + 1;
         sample < std::min(postOutputStart + 1800,
                           static_cast<int>(toneReentry.output.size()));
         ++sample)
    {
        maximumAttackStep = std::max(maximumAttackStep,
            std::abs(static_cast<double>(toneReentry.output[static_cast<std::size_t>(sample)])
                   - static_cast<double>(toneReentry.output[static_cast<std::size_t>(sample - 1)])));
    }

    double minimumActiveWindowRms = 1.0e9;
    constexpr int activeWindow = 240;
    for (int offset = 240; offset < 3840; offset += activeWindow)
    {
        minimumActiveWindowRms = std::min(minimumActiveWindowRms,
            rangeRms(toneReentry.output, postOutputStart + offset, activeWindow));
    }

    std::cerr << "periodic_reentry_error_cents=" << reentryCentsError
              << " target_dry_ratio="
              << targetReentryPower / std::max(1.0e-20, dryReentryPower)
              << " max_attack_step=" << maximumAttackStep
              << " min_active_window_rms=" << minimumActiveWindowRms << '\n';
    success &= check(std::abs(reentryCentsError) < 0.35,
                     "periodic_reentry_respects_commanded_lock_ratio");
    success &= check(targetReentryPower > 1000.0 * dryReentryPower,
                     "periodic_reentry_has_no_source_or_dry_copy");
    success &= check(maximumAttackStep < 0.12,
                     "periodic_reentry_has_no_click");
    success &= check(minimumActiveWindowRms > 0.002,
                     "periodic_reentry_has_no_interruption_when_signal_exists");
'''

test = replace_once(
    test,
    "\n    return success ? 0 : 1;\n}",
    new_tests + "\n    return success ? 0 : 1;\n}",
    "insert reentry regression tests",
)

test_path.write_text(test, encoding="utf-8")


# Freeze the exact final beta DSP baseline in the existing release contract.
contract_path = ROOT / "Tests/release_readiness_contract.py"
contract = contract_path.read_text(encoding="utf-8")
source_paths = [
    "Source/SingleWetSpectralRenderer.cpp",
    "Source/SingleWetSpectralRenderer.h",
    "Source/ModernPitchEngine.cpp",
    "Source/ModernPitchEngine.h",
    "Source/LivePitchProcessor.h",
    "Source/PluginProcessor.cpp",
    "Source/PluginProcessor.h",
]
manifest = "AUDIO_BASELINE = {\n" + "".join(
    f'    "{path}": "{git_blob(path)}",\n' for path in source_paths
) + "}"
contract, count = re.subn(
    r"AUDIO_BASELINE = \{.*?\n\}",
    manifest,
    contract,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit("release baseline manifest: expected 1 match")
contract_path.write_text(contract, encoding="utf-8")


# The default branch becomes the beta candidate after validation. Ensure its
# push runs the existing cross-platform release-readiness/package workflow.
release_path = ROOT / ".github/workflows/release-readiness.yml"
release = release_path.read_text(encoding="utf-8")
release = replace_once(
    release,
    """    branches:\n      - single-wet-quality-reconstruction\n      - release-readiness-hardening\n""",
    """    branches:\n      - main\n      - beta-restricted\n      - single-wet-quality-reconstruction\n      - release-readiness-hardening\n""",
    "release readiness push branches",
)
release_path.write_text(release, encoding="utf-8")

print("spectral_reentry_reset_materialized=PASS")
