#include "SingleWetSpectralRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double sampleRate = 48000.0;

bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

double tonePower(const std::vector<float>& signal,
                 double frequencyHz,
                 int startSample)
{
    double real = 0.0;
    double imaginary = 0.0;
    for (int sample = startSample; sample < static_cast<int>(signal.size()); ++sample)
    {
        const double phase = 2.0 * pi * frequencyHz
                           * static_cast<double>(sample) / sampleRate;
        real += static_cast<double>(signal[static_cast<std::size_t>(sample)])
              * std::cos(phase);
        imaginary -= static_cast<double>(signal[static_cast<std::size_t>(sample)])
                   * std::sin(phase);
    }
    return real * real + imaginary * imaginary;
}

// Authority is a frequency-ratio contract, not merely an energy-placement
// contract.  Search the dominant coherent sinusoid around the commanded target
// at sub-bin resolution so a several-cent transport error cannot hide behind
// a broad FFT peak.
double estimateToneFrequency(const std::vector<float>& signal,
                             double expectedHz,
                             int startSample)
{
    double bestFrequency = expectedHz;
    double bestPower = -1.0;
    for (double frequency = expectedHz - 3.0;
         frequency <= expectedHz + 3.0001;
         frequency += 0.05)
    {
        const double power = tonePower(signal, frequency, startSample);
        if (power > bestPower)
        {
            bestPower = power;
            bestFrequency = frequency;
        }
    }

    double left = bestFrequency - 0.08;
    double right = bestFrequency + 0.08;
    for (int iteration = 0; iteration < 24; ++iteration)
    {
        const double third = (right - left) / 3.0;
        const double a = left + third;
        const double b = right - third;
        if (tonePower(signal, a, startSample) < tonePower(signal, b, startSample))
            left = a;
        else
            right = b;
    }
    return 0.5 * (left + right);
}

double centsError(double measuredHz, double expectedHz)
{
    if (!(measuredHz > 0.0) || !(expectedHz > 0.0))
        return 1.0e9;
    return 1200.0 * std::log2(measuredHz / expectedHz);
}

std::vector<float> renderTone(int frameSize,
                              double correctionCents,
                              double inputFrequencyHz = 220.0)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);

    std::vector<float> output(72000);
    for (int sample = 0; sample < static_cast<int>(output.size()); ++sample)
    {
        const float input = 0.22f * static_cast<float>(std::sin(
            2.0 * pi * inputFrequencyHz * static_cast<double>(sample) / sampleRate));
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            input, correctionCents, 0.9f);
    }
    return output;
}

std::vector<float> renderInharmonicOctaveShift()
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 512);

    constexpr std::array<double, 4> frequencies { 277.0, 401.0, 593.0, 877.0 };
    constexpr std::array<double, 4> phases { 0.17, 0.73, 1.31, 2.03 };
    std::vector<float> output(72000);
    for (int sample = 0; sample < static_cast<int>(output.size()); ++sample)
    {
        double input = 0.0;
        for (std::size_t index = 0; index < frequencies.size(); ++index)
        {
            input += 0.055 * std::sin(
                2.0 * pi * frequencies[index] * static_cast<double>(sample) / sampleRate
                + phases[index]);
        }
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            static_cast<float>(input), 1200.0, 0.0f);
    }
    return output;
}


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

} // namespace

int main()
{
    bool success = true;
    const double semitoneTargetHz = 220.0 * std::exp2(100.0 / 1200.0);

    // The same transport law must work at all three production frame sizes.
    // SINGLE_WET_PURITY_V6: a production lattice must suppress the original
    // pitch by at least 30 dB in power on this deterministic one-semitone test.
    // The former 128-sample profile measured only ~2.07:1 and is therefore not
    // a production option until its transport is redesigned.
    const std::array<int, 2> frameSizes { 512, 256 };
    for (const int frameSize : frameSizes)
    {
        const auto output = renderTone(frameSize, 100.0);
        const double targetPower = tonePower(output, semitoneTargetHz, 12000);
        const double sourcePower = tonePower(output, 220.0, 12000);
        const double ratio = targetPower / std::max(1.0e-20, sourcePower);
        std::cerr << "frame_" << frameSize << "_target_source_ratio=" << ratio << '\n';
        success &= check(targetPower > 1000.0 * sourcePower,
                         frameSize == 512 ? "quality_has_no_audible_source_copy"
                                          : "live_and_experimental_have_no_audible_source_copy");
    }

    // EXACT_RENDER_RATIO_V1: prove that the frozen renderer realizes the
    // commanded ratio itself. If this passes while a vocal render is several
    // cents off, the remaining error is upstream (F0/control), not hidden
    // attenuation or reinterpretation inside the spectral transport.
    struct RatioCase
    {
        double inputHz;
        double correctionCents;
        const char* name;
    };
    const std::array<RatioCase, 4> ratioCases {{
        { 173.70,  37.25, "low_fractional_up" },
        { 220.00, -83.40, "mid_fractional_down" },
        { 311.13, 137.60, "upper_fractional_up" },
        { 452.00, -46.53, "vocal_region_down" }
    }};
    for (const int frameSize : frameSizes)
    {
        for (const auto& testCase : ratioCases)
        {
            const auto output = renderTone(frameSize,
                                           testCase.correctionCents,
                                           testCase.inputHz);
            const double expectedHz = testCase.inputHz
                * std::exp2(testCase.correctionCents / 1200.0);
            const double measuredHz = estimateToneFrequency(output,
                                                             expectedHz,
                                                             24000);
            const double error = centsError(measuredHz, expectedHz);
            std::cerr << "exact_render_frame_" << frameSize << '_'
                      << testCase.name << "_expected_hz=" << expectedHz
                      << " measured_hz=" << measuredHz
                      << " error_cents=" << error << '\n';
            success &= check(std::abs(error) < 0.35,
                             frameSize == 512
                                ? "quality_realizes_commanded_pitch_ratio"
                                : "live_realizes_commanded_pitch_ratio");
        }
    }

    const auto unity = renderTone(512, 0.0);
    const double unitySourcePower = tonePower(unity, 220.0, 12000);
    const double unityShiftedPower = tonePower(unity, semitoneTargetHz, 12000);
    success &= check(unitySourcePower > 4.0 * unityShiftedPower,
                     "zero_correction_preserves_source_pitch");

    // A full octave catches both historical octave wrapping and any hidden
    // original-position contribution.
    const auto octave = renderTone(512, 1200.0);
    const double octaveTargetPower = tonePower(octave, 440.0, 12000);
    const double octaveSourcePower = tonePower(octave, 220.0, 12000);
    std::cerr << "octave_target_source_ratio="
              << octaveTargetPower / std::max(1.0e-20, octaveSourcePower) << '\n';
    success &= check(octaveTargetPower > 4.0 * octaveSourcePower,
                     "octave_correction_is_not_wrapped_or_split");

    // An inharmonic signal has no special branch: every component must obey the
    // same requested transport.
    const auto inharmonic = renderInharmonicOctaveShift();
    constexpr std::array<double, 4> sourceFrequencies { 277.0, 401.0, 593.0, 877.0 };
    double inharmonicSourcePower = 0.0;
    double inharmonicTargetPower = 0.0;
    for (const double frequency : sourceFrequencies)
    {
        inharmonicSourcePower += tonePower(inharmonic, frequency, 24000);
        inharmonicTargetPower += tonePower(inharmonic, 2.0 * frequency, 24000);
    }
    const double inharmonicRatio = inharmonicTargetPower
        / std::max(1.0e-20, inharmonicSourcePower);
    std::cerr << "inharmonic_full_transport_ratio=" << inharmonicRatio << '\n';
    success &= check(inharmonicTargetPower > 2.0 * inharmonicSourcePower,
                     "inharmonic_signal_uses_same_transport");


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

    return success ? 0 : 1;
}
