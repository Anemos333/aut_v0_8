#include <JuceHeader.h>
#include "LivePitchProcessor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 128;
constexpr int totalSamples = 48000 * 2;
constexpr double pi = 3.1415926535897932384626433832795;

struct PassResult
{
    std::vector<float> audio;
    std::vector<float> pitch;
};

std::array<double, 12> makeScale()
{
    std::array<double, 12> scale {};
    for (int i = 0; i < 12; ++i)
        scale[static_cast<std::size_t>(i)] = std::exp2(static_cast<double>(i) / 12.0);
    return scale;
}

std::vector<float> makeInput()
{
    std::vector<float> input(static_cast<std::size_t>(totalSamples), 0.0f);
    std::uint32_t noise = 0x7a31d4b9u;
    double phase = 0.0;
    for (int i = 0; i < totalSamples; ++i)
    {
        const double t = static_cast<double>(i) / sampleRate;
        double f0 = 220.0;
        if (t >= 0.45 && t < 0.90) f0 = 246.94165;
        else if (t >= 0.90 && t < 1.30) f0 = 196.0;
        else if (t >= 1.30 && t < 1.65) f0 = 440.0;
        else if (t >= 1.65) f0 = 220.0;

        phase += 2.0 * pi * f0 / sampleRate;
        if (phase > 2.0 * pi)
            phase -= 2.0 * pi;

        double voiced = 0.0;
        for (int h = 1; h <= 7; ++h)
            voiced += std::sin(phase * static_cast<double>(h)) / static_cast<double>(h);

        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        const double n = (static_cast<double>(noise & 0xffffu) / 32767.5) - 1.0;
        const double envelope = std::min(1.0, std::min(t / 0.02, (2.0 - t) / 0.02));
        input[static_cast<std::size_t>(i)] = static_cast<float>(
            std::clamp(envelope * (0.14 * voiced + 0.004 * n), -0.95, 0.95));
    }
    return input;
}

void configure(LivePitchProcessor& processor)
{
    processor.prepare(sampleRate, blockSize, 1, ModernPitchEngine::LatencyMode::quality);
    processor.setAdvancedParameters(
        35.0f, 0.0f, 0.20f, 0.90f, 0.85f, 0.70f,
        12.0f, 45.0f, 1600.0f,
        LivePitchProcessor::StereoMode::linkedMidSide);
    processor.setScaleLockParameters(false, 24.0f, 0.0f);
    CreativeTempo::Settings tempo;
    tempo.mode = CreativeTempo::Mode::off;
    processor.setTempoSettings(tempo);
}

PassResult runPass(LivePitchProcessor& processor,
                   const std::vector<float>& input,
                   bool restartTimeline)
{
    const auto scale = makeScale();
    PassResult result;
    result.audio.reserve(input.size());
    result.pitch.reserve((input.size() + blockSize - 1) / blockSize);

    std::int64_t timeline = restartTimeline ? 0 : 1000000;
    for (int offset = 0; offset < static_cast<int>(input.size()); offset += blockSize)
    {
        const int count = std::min(blockSize, static_cast<int>(input.size()) - offset);
        juce::AudioBuffer<float> block(1, count);
        std::copy_n(input.data() + offset, count, block.getWritePointer(0));

        CreativeTempo::HostPosition host;
        host.numberOfSamples = count;
        host.hasTimeInSamples = true;
        host.timeInSamples = timeline + offset;
        host.isPlaying = true;
        host.isLooping = false;
        processor.setTempoHostPosition(host);
        processor.process(block, scale.data(), static_cast<int>(scale.size()),
                          440.0, 0.0f, 1.0f);

        const float* out = block.getReadPointer(0);
        result.audio.insert(result.audio.end(), out, out + count);
        result.pitch.push_back(processor.getDetectedPitchHz());
    }
    return result;
}

struct Diff
{
    double rms = 0.0;
    double peak = 0.0;
    int pitchMismatches = 0;
};

Diff compare(const PassResult& a, const PassResult& b)
{
    Diff d;
    const std::size_t n = std::min(a.audio.size(), b.audio.size());
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double delta = static_cast<double>(a.audio[i]) - static_cast<double>(b.audio[i]);
        sum += delta * delta;
        d.peak = std::max(d.peak, std::abs(delta));
    }
    d.rms = n > 0 ? std::sqrt(sum / static_cast<double>(n)) : 0.0;

    const std::size_t p = std::min(a.pitch.size(), b.pitch.size());
    for (std::size_t i = 0; i < p; ++i)
    {
        const float x = a.pitch[i];
        const float y = b.pitch[i];
        if (x <= 0.0f && y <= 0.0f)
            continue;
        if (x <= 0.0f || y <= 0.0f)
        {
            ++d.pitchMismatches;
            continue;
        }
        const double cents = std::abs(1200.0 * std::log2(static_cast<double>(x) / static_cast<double>(y)));
        if (cents > 1.0)
            ++d.pitchMismatches;
    }
    return d;
}
}

int main()
{
    const auto input = makeInput();

    LivePitchProcessor freshA;
    LivePitchProcessor freshB;
    configure(freshA);
    configure(freshB);
    const auto a = runPass(freshA, input, true);
    const auto b = runPass(freshB, input, true);
    const Diff fresh = compare(a, b);

    LivePitchProcessor replay;
    configure(replay);
    const auto first = runPass(replay, input, true);
    const auto second = runPass(replay, input, true);
    const Diff repeated = compare(first, second);

    std::cout << "REPLAY_DETERMINISM fresh_rms=" << fresh.rms
              << " fresh_peak=" << fresh.peak
              << " fresh_pitch_mismatch=" << fresh.pitchMismatches
              << " repeated_rms=" << repeated.rms
              << " repeated_peak=" << repeated.peak
              << " repeated_pitch_mismatch=" << repeated.pitchMismatches
              << '\n';

    const bool freshDeterministic = fresh.rms < 1.0e-9
                                 && fresh.peak < 1.0e-7
                                 && fresh.pitchMismatches == 0;
    const bool replayDiffers = repeated.rms > 1.0e-6
                            || repeated.peak > 1.0e-4
                            || repeated.pitchMismatches > 0;

    std::cout << "FRESH_INSTANCE_DETERMINISM=" << (freshDeterministic ? "PASS" : "FAIL") << '\n';
    std::cout << "REPLAY_STATE_DEPENDENCE_PRESENT=" << (replayDiffers ? "YES" : "NO") << '\n';
    return freshDeterministic ? 0 : 1;
}
