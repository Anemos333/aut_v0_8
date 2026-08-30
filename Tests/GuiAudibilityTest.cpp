#include "ModernPitchEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
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
    bool finite = true;
};

bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
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

    // Response changes only trajectory time. This intentionally mirrors the
    // already-proven core-engine invariant instead of inventing a new response
    // curve just to make the control look more dramatic.
    const auto steady452 = [](double) { return 452.0; };
    auto fastResponseParameters = base;
    fastResponseParameters.retuneTimeMs = 0.0f;
    auto slowResponseParameters = base;
    slowResponseParameters.retuneTimeMs = 500.0f;
    const auto fastResponse = render(
        ModernPitchEngine::LatencyMode::live, fastResponseParameters,
        unison, 440.0, 0.32, steady452);
    const auto slowResponse = render(
        ModernPitchEngine::LatencyMode::live, slowResponseParameters,
        unison, 440.0, 0.32, steady452);
    success &= check(std::abs(fastResponse.meter.correctionCents)
                     > std::abs(slowResponse.meter.correctionCents) + 1.0f,
                     "response_changes_continuous_retune");

    // Humanize already has an audible meaning in the engine: it widens the
    // same-note human window and therefore changes the actual correction cents.
    auto robot = base;
    robot.humanize = 0.0f;
    auto human = base;
    human.humanize = 1.0f;
    const auto robotResult = render(
        ModernPitchEngine::LatencyMode::live, robot,
        unison, 440.0, 5.0, steady452);
    const auto humanResult = render(
        ModernPitchEngine::LatencyMode::live, human,
        unison, 440.0, 5.0, steady452);
    success &= check(std::abs(robotResult.meter.correctionCents
                              - humanResult.meter.correctionCents) > 6.0f,
                     "humanize_changes_correction_window");

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

    // Mode is a reconstruction/latency profile, never a pitch-quality control.
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

    std::cerr << "fast_response_cents=" << fastResponse.meter.correctionCents << '\n'
              << "slow_response_cents=" << slowResponse.meter.correctionCents << '\n'
              << "robot_correction_cents=" << robotResult.meter.correctionCents << '\n'
              << "human_correction_cents=" << humanResult.meter.correctionCents << '\n';

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
