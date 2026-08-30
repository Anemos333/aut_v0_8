#include "ModernPitchEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <numeric>
#include <vector>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

struct RenderResult
{
    ModernPitchEngine::Metering meter;
    std::vector<float> correctionHistory;
    bool finite = true;
};

bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

double standardDeviation(const std::vector<float>& values,
                         std::size_t skip)
{
    if (values.size() <= skip + 1)
        return 0.0;

    const auto first = values.begin() + static_cast<std::ptrdiff_t>(skip);
    const double mean = std::accumulate(first, values.end(), 0.0)
        / static_cast<double>(values.end() - first);
    double sum = 0.0;
    for (auto it = first; it != values.end(); ++it)
    {
        const double delta = static_cast<double>(*it) - mean;
        sum += delta * delta;
    }
    return std::sqrt(sum / static_cast<double>(values.end() - first));
}

RenderResult render(ModernPitchEngine::LatencyMode mode,
                    ModernPitchEngine::Parameters parameters,
                    const std::vector<double>& scale,
                    double rootFrequency,
                    double seconds,
                    const std::function<double(double)>& frequencyAt)
{
    ModernPitchEngine engine;
    engine.prepare(sampleRate, blockSize, 1, mode);

    juce::AudioBuffer<float> block(1, blockSize);
    RenderResult result;
    double phase = 0.0;
    const int totalSamples = static_cast<int>(std::lround(seconds * sampleRate));

    for (int produced = 0; produced < totalSamples; produced += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - produced);
        block.setSize(1, count, false, false, true);
        auto* data = block.getWritePointer(0);
        for (int sample = 0; sample < count; ++sample)
        {
            const double time = static_cast<double>(produced + sample) / sampleRate;
            const double frequency = frequencyAt(time);
            phase += twoPi * frequency / sampleRate;
            if (phase >= twoPi)
                phase -= twoPi;
            data[sample] = static_cast<float>(
                0.48 * std::sin(phase)
                + 0.16 * std::sin(2.0 * phase)
                + 0.07 * std::sin(3.0 * phase));
        }

        engine.process(block,
                       scale.empty() ? nullptr : scale.data(),
                       static_cast<int>(scale.size()),
                       rootFrequency,
                       parameters);

        for (int sample = 0; sample < count; ++sample)
            result.finite = result.finite && std::isfinite(data[sample]);

        result.meter = engine.getMetering();
        result.correctionHistory.push_back(result.meter.correctionCents);
    }

    return result;
}
}

int main()
{
    bool success = true;

    ModernPitchEngine::Parameters base;
    base.amount = 1.0f;
    base.retuneTimeMs = 0.0f;
    base.humanize = 0.0f;
    base.formantPreservation = 0.9f;
    base.detectorSensitivity = 0.9f;
    base.minimumPitchHz = 70.0f;
    base.maximumPitchHz = 1200.0f;
    base.maximumCorrectionSemitones = 12.0f;

    const std::vector<double> unison { 1.0 };
    std::vector<double> chromatic;
    for (int degree = 0; degree < 12; ++degree)
        chromatic.push_back(std::exp2(static_cast<double>(degree) / 12.0));

    const auto steady458 = [](double) { return 458.0; };
    const auto unisonTarget = render(
        ModernPitchEngine::LatencyMode::live, base,
        unison, 440.0, 4.0, steady458);
    const auto chromaticTarget = render(
        ModernPitchEngine::LatencyMode::live, base,
        chromatic, 440.0, 4.0, steady458);
    success &= check(unisonTarget.finite && chromaticTarget.finite,
                     "scale_renders_are_finite");
    success &= check(std::abs(unisonTarget.meter.targetPitchHz
                              - chromaticTarget.meter.targetPitchHz) > 15.0f,
                     "scale_selector_changes_target_pitch");

    const auto shiftedRootTarget = render(
        ModernPitchEngine::LatencyMode::live, base,
        unison, 466.1637615, 4.0, steady458);
    success &= check(std::abs(unisonTarget.meter.targetPitchHz
                              - shiftedRootTarget.meter.targetPitchHz) > 20.0f,
                     "root_selector_changes_target_pitch");

    // Amount remains correction depth, never dry/wet.
    auto amountZero = base;
    amountZero.amount = 0.0f;
    const auto noCorrection = render(
        ModernPitchEngine::LatencyMode::live, amountZero,
        unison, 440.0, 4.0, steady458);
    success &= check(std::abs(noCorrection.meter.correctionCents) < 0.1f
                     && std::abs(unisonTarget.meter.correctionCents) > 8.0f,
                     "amount_changes_correction_depth");

    // Scale Lock itself must change target hold, not merely expose sub-controls.
    const double semitone = std::exp2(1.0 / 12.0);
    const std::vector<double> twoNoteScale { 1.0, semitone };
    const auto boundaryStep = [](double seconds)
    {
        return seconds < 2.5 ? 450.0 : 456.0;
    };
    auto unlocked = base;
    unlocked.scaleLock = false;
    auto locked = base;
    locked.scaleLock = true;
    locked.hardLockActive = true;
    locked.lockHysteresis = 80.0f;
    const auto unlockedResult = render(
        ModernPitchEngine::LatencyMode::quality, unlocked,
        twoNoteScale, 440.0, 5.0, boundaryStep);
    const auto lockedResult = render(
        ModernPitchEngine::LatencyMode::quality, locked,
        twoNoteScale, 440.0, 5.0, boundaryStep);
    success &= check(std::abs(unlockedResult.meter.targetPitchHz
                              - lockedResult.meter.targetPitchHz) > 15.0f,
                     "scale_lock_switch_changes_target_hold");

    // Response must retain a clearly audible range while Scale Lock is active.
    auto lockedFast = locked;
    lockedFast.lockHysteresis = 0.0f;
    lockedFast.retuneTimeMs = 0.0f;
    auto lockedSlow = lockedFast;
    lockedSlow.retuneTimeMs = 500.0f;
    const auto steady452 = [](double) { return 452.0; };
    const auto fastResponse = render(
        ModernPitchEngine::LatencyMode::quality, lockedFast,
        unison, 440.0, 0.40, steady452);
    const auto slowResponse = render(
        ModernPitchEngine::LatencyMode::quality, lockedSlow,
        unison, 440.0, 0.40, steady452);
    success &= check(std::abs(fastResponse.meter.correctionCents)
                     > std::abs(slowResponse.meter.correctionCents) + 1.5f,
                     "scale_lock_response_has_audible_range");

    // Humanize outside Scale Lock must preserve same-note motion instead of
    // merely changing hidden classifier thresholds.
    const auto vibratoInput = [](double seconds)
    {
        const double cents = 22.0 * std::sin(twoPi * 5.0 * seconds);
        return 440.0 * std::exp2(cents / 1200.0);
    };
    auto robot = base;
    robot.humanize = 0.0f;
    robot.preserveVibrato = 0.70f;
    auto human = robot;
    human.humanize = 1.0f;
    const auto robotResult = render(
        ModernPitchEngine::LatencyMode::live, robot,
        unison, 440.0, 6.0, vibratoInput);
    const auto humanResult = render(
        ModernPitchEngine::LatencyMode::live, human,
        unison, 440.0, 6.0, vibratoInput);
    const auto skip = robotResult.correctionHistory.size() / 2;
    const double robotDeviation = standardDeviation(
        robotResult.correctionHistory, skip);
    const double humanDeviation = standardDeviation(
        humanResult.correctionHistory, skip);
    std::cerr << "robot_correction_stddev=" << robotDeviation << '\n'
              << "human_correction_stddev=" << humanDeviation << '\n'
              << "fast_locked_cents=" << fastResponse.meter.correctionCents << '\n'
              << "slow_locked_cents=" << slowResponse.meter.correctionCents << '\n';
    success &= check(humanDeviation + 0.35 < robotDeviation,
                     "humanize_preserves_same_note_vibrato");

    // Mode is deliberately a latency/reconstruction profile, not a correction
    // quality control. All modes must hit the same target while reporting their
    // distinct frame latency.
    const std::vector<std::pair<ModernPitchEngine::LatencyMode, int>> modes {
        { ModernPitchEngine::LatencyMode::quality, 512 },
        { ModernPitchEngine::LatencyMode::live, 256 },
        { ModernPitchEngine::LatencyMode::ultraLive, 128 }
    };
    for (const auto& [mode, latency] : modes)
    {
        ModernPitchEngine engine;
        engine.prepare(sampleRate, blockSize, 1, mode);
        success &= check(engine.getLatencySamples() == latency,
                         latency == 512 ? "quality_mode_latency_profile"
                         : latency == 256 ? "live_mode_latency_profile"
                                          : "experimental_mode_latency_profile");
    }

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
