#include "ModernPitchEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

struct MeterPoint
{
    double seconds = 0.0;
    ModernPitchEngine::Metering meter;
};

struct RenderResult
{
    double outputFrequencyHz = 0.0;
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    bool finite = true;
    ModernPitchEngine::Metering finalMeter;
    std::vector<MeterPoint> history;
    std::vector<float> capture;
};

[[nodiscard]] double estimateFrequency(const std::vector<float>& signal,
                                       double minimumHz = 350.0,
                                       double maximumHz = 560.0)
{
    if (signal.size() < 512)
        return 0.0;

    const int minimumLag = std::max(
        2, static_cast<int>(std::floor(sampleRate / maximumHz)));
    const int maximumLag = std::min(
        static_cast<int>(signal.size() / 3),
        static_cast<int>(std::ceil(sampleRate / minimumHz)));

    double bestCorrelation = -2.0;
    int bestLag = minimumLag;
    for (int lag = minimumLag; lag <= maximumLag; ++lag)
    {
        double xy = 0.0;
        double xx = 0.0;
        double yy = 0.0;
        for (std::size_t index = static_cast<std::size_t>(lag);
             index < signal.size();
             ++index)
        {
            const double a = signal[index];
            const double b = signal[index - static_cast<std::size_t>(lag)];
            xy += a * b;
            xx += a * a;
            yy += b * b;
        }

        if (xx <= 1.0e-16 || yy <= 1.0e-16)
            continue;
        const double correlation = xy / std::sqrt(xx * yy);
        if (correlation > bestCorrelation)
        {
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }
    return sampleRate / static_cast<double>(bestLag);
}

[[nodiscard]] const MeterPoint& pointNear(const RenderResult& result,
                                          double seconds)
{
    auto iterator = std::min_element(
        result.history.begin(), result.history.end(),
        [seconds](const MeterPoint& a, const MeterPoint& b)
        {
            return std::abs(a.seconds - seconds) < std::abs(b.seconds - seconds);
        });
    return *iterator;
}

[[nodiscard]] double standardDeviation(const RenderResult& result,
                                       double startSeconds)
{
    std::vector<double> values;
    for (const auto& point : result.history)
    {
        if (point.seconds >= startSeconds)
            values.push_back(point.meter.correctionCents);
    }
    if (values.size() < 2)
        return 0.0;
    const double mean = std::accumulate(values.begin(), values.end(), 0.0)
        / static_cast<double>(values.size());
    double sum = 0.0;
    for (double value : values)
        sum += (value - mean) * (value - mean);
    return std::sqrt(sum / static_cast<double>(values.size()));
}

RenderResult render(ModernPitchEngine::LatencyMode mode,
                    ModernPitchEngine::Parameters parameters,
                    const std::vector<double>& scale,
                    double rootFrequency,
                    double seconds,
                    const std::function<double(double)>& frequencyAt,
                    bool provideHostTransport = false)
{
    const int totalSamples = static_cast<int>(std::lround(seconds * sampleRate));
    const int captureSamples = std::min(totalSamples,
        static_cast<int>(1.25 * sampleRate));

    auto engine = std::make_unique<ModernPitchEngine>();
    engine->prepare(sampleRate, blockSize, 2, mode);

    juce::AudioBuffer<float> block(2, blockSize);
    RenderResult result;
    result.capture.reserve(static_cast<std::size_t>(captureSamples));
    double phase = 0.0;

    for (int produced = 0; produced < totalSamples; produced += blockSize)
    {
        const int samplesThisBlock = std::min(blockSize, totalSamples - produced);
        block.setSize(2, samplesThisBlock, false, false, true);

        float* left = block.getWritePointer(0);
        float* right = block.getWritePointer(1);
        for (int sample = 0; sample < samplesThisBlock; ++sample)
        {
            const double time = static_cast<double>(produced + sample) / sampleRate;
            const double frequency = frequencyAt(time);
            phase += 2.0 * pi * frequency / sampleRate;
            if (phase >= twoPi)
                phase -= twoPi;
            left[sample] = static_cast<float>(
                0.53 * std::sin(phase) + 0.12 * std::sin(2.0 * phase));
            right[sample] = static_cast<float>(
                0.31 * std::sin(phase + 0.31)
                + 0.21 * std::sin(2.0 * phase + 0.73));
        }

        CreativeTempo::HostPosition host;
        if (provideHostTransport)
        {
            host.bpm = 120.0;
            host.ppqAtBlockStart = static_cast<double>(produced)
                * host.bpm / (60.0 * sampleRate);
            host.timeInSamples = produced;
            host.numberOfSamples = samplesThisBlock;
            host.hasBpm = true;
            host.hasPpq = true;
            host.hasTimeInSamples = true;
            host.isPlaying = true;
        }

        engine->process(block,
                        scale.empty() ? nullptr : scale.data(),
                        static_cast<int>(scale.size()),
                        rootFrequency,
                        parameters,
                        host);

        for (int channel = 0; channel < 2; ++channel)
        {
            const float* data = block.getReadPointer(channel);
            for (int sample = 0; sample < samplesThisBlock; ++sample)
            {
                const float value = data[sample];
                result.finite = result.finite && std::isfinite(value);
                if (channel == 0)
                    result.leftEnergy += static_cast<double>(value) * value;
                else
                    result.rightEnergy += static_cast<double>(value) * value;
            }
        }

        const int captureStart = totalSamples - captureSamples;
        for (int sample = 0; sample < samplesThisBlock; ++sample)
        {
            if (produced + sample >= captureStart)
                result.capture.push_back(block.getSample(0, sample));
        }

        result.history.push_back({
            static_cast<double>(produced + samplesThisBlock) / sampleRate,
            engine->getMetering()
        });
    }

    result.finalMeter = engine->getMetering();
    result.outputFrequencyHz = estimateFrequency(result.capture);
    return result;
}

bool check(bool condition, const std::string& name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

} // namespace

int main()
{
    bool success = true;

    ModernPitchEngine::Parameters base;
    base.amount = 1.0f;
    base.retuneTimeMs = 0.0f;
    base.humanize = 0.0f;
    base.formantPreservation = 0.0f;
    base.detectorSensitivity = 0.85f;
    base.minimumPitchHz = 70.0f;
    base.maximumPitchHz = 1200.0f;
    base.maximumCorrectionSemitones = 12.0f;
    base.stereoMode = ModernPitchEngine::StereoMode::linkedMidSide;

    const std::vector<double> unison { 1.0 };
    const auto steady452 = [](double) { return 452.0; };

    std::vector<double> modeFrequencies;
    const std::array<ModernPitchEngine::LatencyMode, 3> modes {
        ModernPitchEngine::LatencyMode::ultraLive,
        ModernPitchEngine::LatencyMode::live,
        ModernPitchEngine::LatencyMode::quality
    };
    const std::array<int, 3> expectedLatencies { 256, 256, 512 };

    for (std::size_t index = 0; index < modes.size(); ++index)
    {
        const auto result = render(modes[index], base, unison, 440.0, 5.0, steady452);
        modeFrequencies.push_back(result.outputFrequencyHz);
        success &= check(result.finite, "mode_finite_" + std::to_string(index));
        success &= check(result.leftEnergy > 1.0 && result.rightEnergy > 1.0,
                         "mode_both_channels_alive_" + std::to_string(index));
        success &= check(std::abs(result.outputFrequencyHz - 440.0) < 6.0,
                         "mode_target_frequency_" + std::to_string(index));
        success &= check(std::abs(result.finalMeter.targetPitchHz - 440.0f) < 1.0f,
                         "mode_meter_target_" + std::to_string(index));

        auto engine = std::make_unique<ModernPitchEngine>();
        engine->prepare(sampleRate, blockSize, 2, modes[index]);
        success &= check(engine->getLatencySamples() == expectedLatencies[index],
                         "mode_latency_" + std::to_string(index));
    }
    const auto [minimumModeFrequency, maximumModeFrequency] = std::minmax_element(
        modeFrequencies.begin(), modeFrequencies.end());
    success &= check(*maximumModeFrequency - *minimumModeFrequency < 1.5,
                     "same_pitch_quality_across_modes");

    auto amountOff = base;
    amountOff.amount = 0.0f;
    const auto dryTrajectory = render(
        ModernPitchEngine::LatencyMode::live, amountOff,
        unison, 440.0, 5.0, steady452);
    const auto fullTrajectory = render(
        ModernPitchEngine::LatencyMode::live, base,
        unison, 440.0, 5.0, steady452);
    success &= check(std::abs(dryTrajectory.outputFrequencyHz - 452.0) < 6.0,
                     "amount_zero_keeps_pitch");
    success &= check(std::abs(fullTrajectory.outputFrequencyHz
                              - dryTrajectory.outputFrequencyHz) > 7.0,
                     "amount_changes_audio_without_dry_wet_mix");

    auto human = base;
    human.humanize = 1.0f;
    const auto humanResult = render(
        ModernPitchEngine::LatencyMode::live, human,
        unison, 440.0, 5.0, steady452);
    success &= check(std::abs(humanResult.finalMeter.correctionCents
                              - fullTrajectory.finalMeter.correctionCents) > 6.0f,
                     "humanize_changes_correction_window");

    auto slowSpeed = base;
    slowSpeed.retuneTimeMs = 500.0f;
    const auto fastResult = render(
        ModernPitchEngine::LatencyMode::live, base,
        unison, 440.0, 0.32, steady452);
    const auto slowResult = render(
        ModernPitchEngine::LatencyMode::live, slowSpeed,
        unison, 440.0, 0.32, steady452);
    success &= check(std::abs(fastResult.finalMeter.correctionCents)
                     > std::abs(slowResult.finalMeter.correctionCents) + 1.0f,
                     "speed_changes_continuous_retune");

    const double semitone = std::exp2(1.0 / 12.0);
    const std::vector<double> twoNoteScale { 1.0, semitone };
    const auto boundaryStep = [](double seconds)
    {
        return seconds < 2.5 ? 450.0 : 456.0;
    };
    auto lowHysteresis = base;
    lowHysteresis.scaleLock = true;
    lowHysteresis.hardLockActive = true;
    lowHysteresis.lockHysteresis = 0.0f;
    lowHysteresis.retuneTimeMs = 0.0f;
    auto highHysteresis = lowHysteresis;
    highHysteresis.lockHysteresis = 80.0f;
    const auto lowHysteresisResult = render(
        ModernPitchEngine::LatencyMode::quality, lowHysteresis,
        twoNoteScale, 440.0, 5.0, boundaryStep);
    const auto highHysteresisResult = render(
        ModernPitchEngine::LatencyMode::quality, highHysteresis,
        twoNoteScale, 440.0, 5.0, boundaryStep);
    success &= check(std::abs(lowHysteresisResult.finalMeter.targetPitchHz
                              - highHysteresisResult.finalMeter.targetPitchHz) > 15.0f,
                     "lock_hysteresis_changes_target_identity");

    const auto vibratoInput = [](double seconds)
    {
        const double cents = 20.0 * std::sin(twoPi * 5.0 * seconds);
        return 440.0 * std::exp2(cents / 1200.0);
    };
    auto vibratoOff = highHysteresis;
    vibratoOff.vibratoPreserve = 0.0f;
    vibratoOff.humanize = 0.0f;
    auto vibratoOn = vibratoOff;
    vibratoOn.vibratoPreserve = 1.0f;
    const auto vibratoOffResult = render(
        ModernPitchEngine::LatencyMode::live, vibratoOff,
        unison, 440.0, 6.0, vibratoInput);
    const auto vibratoOnResult = render(
        ModernPitchEngine::LatencyMode::live, vibratoOn,
        unison, 440.0, 6.0, vibratoInput);
    const double offDeviation = standardDeviation(vibratoOffResult, 3.0);
    const double onDeviation = standardDeviation(vibratoOnResult, 3.0);
    success &= check(onDeviation + 0.35 < offDeviation,
                     "vibrato_preserve_changes_locked_trajectory");

    CreativeTempo::Controller tempoController;
    tempoController.prepare(sampleRate);
    CreativeTempo::HostPosition host;
    host.bpm = 120.0;
    host.ppqAtBlockStart = 0.10;
    host.timeInSamples = 0;
    host.numberOfSamples = blockSize;
    host.hasBpm = true;
    host.hasPpq = true;
    host.hasTimeInSamples = true;
    host.isPlaying = true;

    CreativeTempo::Settings tempo;
    tempo.mode = CreativeTempo::Mode::tempoGlide;
    tempo.division = CreativeTempo::Division::note128;
    tempo.glideFraction = 0.05f;
    tempoController.beginBlock(host, tempo, blockSize);
    const float shortGlide = tempoController.getGlideTimeMs();
    tempo.division = CreativeTempo::Division::note8;
    tempo.glideFraction = 1.0f;
    tempoController.beginBlock(host, tempo, blockSize);
    const float longGlide = tempoController.getGlideTimeMs();
    success &= check(longGlide > shortGlide * 20.0f,
                     "tempo_division_and_glide_length_change_trajectory");

    tempoController.reset();
    tempo.mode = CreativeTempo::Mode::glideLock;
    tempo.division = CreativeTempo::Division::note8;
    tempo.lockStrength = 1.0f;
    tempo.smartOnset = false;
    tempoController.beginBlock(host, tempo, blockSize);
    static_cast<void>(tempoController.processSample(0.0, 0.0, 1, 0.0f,
                                                    true, 0, tempo, 35.0f));
    const auto locked = tempoController.processSample(0.0, 100.0, 2, 0.0f,
                                                      true, 1, tempo, 35.0f);
    success &= check(locked.waitingForGrid,
                     "tempo_lock_strength_holds_target");

    tempoController.reset();
    tempo.lockStrength = 0.0f;
    tempoController.beginBlock(host, tempo, blockSize);
    static_cast<void>(tempoController.processSample(0.0, 0.0, 1, 0.0f,
                                                    true, 0, tempo, 35.0f));
    const auto unlocked = tempoController.processSample(0.0, 100.0, 2, 0.0f,
                                                        true, 1, tempo, 35.0f);
    success &= check(!unlocked.waitingForGrid,
                     "tempo_lock_strength_zero_releases");

    tempoController.reset();
    tempo.lockStrength = 1.0f;
    tempo.smartOnset = true;
    host.ppqAtBlockStart = 0.499;
    tempoController.beginBlock(host, tempo, blockSize);
    static_cast<void>(tempoController.processSample(0.0, 0.0, 1, 0.0f,
                                                    true, 0, tempo, 35.0f));
    const auto smart = tempoController.processSample(0.0, 100.0, 2, 0.9f,
                                                     true, 1, tempo, 35.0f);
    success &= check(!smart.waitingForGrid,
                     "tempo_smart_onset_releases_near_grid");

    std::cerr << "full_output_hz=" << fullTrajectory.outputFrequencyHz << '\n'
              << "amount_zero_output_hz=" << dryTrajectory.outputFrequencyHz << '\n'
              << "fast_correction_cents=" << fastResult.finalMeter.correctionCents << '\n'
              << "slow_correction_cents=" << slowResult.finalMeter.correctionCents << '\n'
              << "low_hysteresis_target_hz="
              << lowHysteresisResult.finalMeter.targetPitchHz << '\n'
              << "high_hysteresis_target_hz="
              << highHysteresisResult.finalMeter.targetPitchHz << '\n'
              << "vibrato_off_stddev=" << offDeviation << '\n'
              << "vibrato_on_stddev=" << onDeviation << '\n'
              << "short_glide_ms=" << shortGlide << '\n'
              << "long_glide_ms=" << longGlide << '\n';

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
