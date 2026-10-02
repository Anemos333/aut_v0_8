#include "PitchCore.h"
#include "PitchCorrectionTrajectory.h"
#include "SinglePathPitchRenderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
struct WavData
{
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> mono;
};

std::uint16_t readU16(std::istream& in)
{
    unsigned char b[2] {};
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

std::uint32_t readU32(std::istream& in)
{
    unsigned char b[4] {};
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<std::uint32_t>(
        b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24));
}

bool readWav(const std::string& path, WavData& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;

    char id[4] {};
    in.read(id, 4);
    if (std::strncmp(id, "RIFF", 4) != 0)
        return false;
    static_cast<void>(readU32(in));
    in.read(id, 4);
    if (std::strncmp(id, "WAVE", 4) != 0)
        return false;

    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    std::uint32_t sampleRate = 0;
    std::vector<unsigned char> bytes;

    while (in)
    {
        if (!in.read(id, 4))
            break;
        const std::uint32_t size = readU32(in);
        const auto next = in.tellg()
            + static_cast<std::streamoff>(size + (size & 1u));

        if (std::strncmp(id, "fmt ", 4) == 0)
        {
            format = readU16(in);
            channels = readU16(in);
            sampleRate = readU32(in);
            static_cast<void>(readU32(in));
            static_cast<void>(readU16(in));
            bits = readU16(in);
        }
        else if (std::strncmp(id, "data", 4) == 0)
        {
            bytes.resize(size);
            in.read(reinterpret_cast<char*>(bytes.data()),
                    static_cast<std::streamsize>(size));
        }
        in.seekg(next);
    }

    if (channels == 0 || sampleRate == 0 || bits == 0 || bytes.empty())
        return false;

    const int bytesPerSample = bits / 8;
    if (bytesPerSample <= 0)
        return false;

    const std::size_t frames = bytes.size()
        / (static_cast<std::size_t>(channels) * bytesPerSample);
    out.sampleRate = static_cast<int>(sampleRate);
    out.channels = static_cast<int>(channels);
    out.mono.resize(frames);

    const auto sampleAt = [&](std::size_t byteOffset) -> float
    {
        const auto* p = bytes.data() + byteOffset;
        if (format == 3 && bits == 32)
        {
            float v = 0.0f;
            std::memcpy(&v, p, sizeof(float));
            return std::isfinite(v) ? v : 0.0f;
        }
        if (format == 1 && bits == 16)
        {
            const std::int16_t v = static_cast<std::int16_t>(p[0] | (p[1] << 8));
            return static_cast<float>(v / 32768.0);
        }
        if (format == 1 && bits == 24)
        {
            std::int32_t v = static_cast<std::int32_t>(
                p[0] | (p[1] << 8) | (p[2] << 16));
            if (v & 0x800000)
                v |= ~0xFFFFFF;
            return static_cast<float>(v / 8388608.0);
        }
        if (format == 1 && bits == 32)
        {
            std::int32_t v = 0;
            std::memcpy(&v, p, sizeof(v));
            return static_cast<float>(static_cast<double>(v) / 2147483648.0);
        }
        return 0.0f;
    };

    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        double sum = 0.0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const std::size_t offset =
                (frame * static_cast<std::size_t>(channels)
                    + static_cast<std::size_t>(channel))
                * static_cast<std::size_t>(bytesPerSample);
            sum += sampleAt(offset);
        }
        out.mono[frame] = static_cast<float>(sum / static_cast<double>(channels));
    }
    return true;
}

double percentile(std::vector<double> values, double p)
{
    if (values.empty())
        return 0.0;
    std::sort(values.begin(), values.end());
    const double position = std::clamp(p, 0.0, 1.0)
        * static_cast<double>(values.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(position));
    const auto hi = static_cast<std::size_t>(std::ceil(position));
    if (lo == hi)
        return values[lo];
    const double f = position - static_cast<double>(lo);
    return values[lo] + f * (values[hi] - values[lo]);
}

std::vector<float> render(const WavData& wav,
                          int latencySamples,
                          double correctionCents,
                          double& cpuRatio,
                          std::uint64_t& spliceCount)
{
    neumaton::render::SinglePathPitchRenderer renderer;
    renderer.prepare(wav.sampleRate, 1, latencySamples);

    std::vector<float> output(wav.mono.size(), 0.0f);
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < wav.mono.size(); ++i)
    {
        const float in[1] { wav.mono[i] };
        float out[1] {};
        renderer.processFrame(in, out, 1, correctionCents);
        output[i] = out[0];
    }
    const auto stop = std::chrono::steady_clock::now();

    const double audioSeconds = static_cast<double>(wav.mono.size())
        / static_cast<double>(wav.sampleRate);
    cpuRatio = std::chrono::duration<double>(stop - start).count()
        / std::max(1.0e-9, audioSeconds);
    spliceCount = renderer.spliceCount();
    return output;
}

struct PitchSummary
{
    std::vector<double> hz;
};

PitchSummary estimateVoicePitch(const std::vector<float>& audio, int sampleRate)
{
    PitchSummary result;
    if (audio.size() < 4096)
        return result;

    constexpr int window = 4096;
    const int minimumLag = std::max(2, sampleRate / 600);
    const int maximumLag = std::min(window / 2 - 2, sampleRate / 70);
    const std::size_t first = std::min<std::size_t>(
        audio.size() - window, static_cast<std::size_t>(2 * sampleRate));
    const std::size_t last = audio.size() - window;
    const std::size_t span = last > first ? last - first : 0;
    constexpr int probes = 72;

    for (int probe = 0; probe < probes; ++probe)
    {
        const std::size_t start = first + (span * static_cast<std::size_t>(probe))
            / static_cast<std::size_t>(std::max(1, probes - 1));

        double mean = 0.0;
        double energy = 0.0;
        for (int i = 0; i < window; ++i)
            mean += audio[start + static_cast<std::size_t>(i)];
        mean /= window;
        for (int i = 0; i < window; ++i)
        {
            const double x = audio[start + static_cast<std::size_t>(i)] - mean;
            energy += x * x;
        }
        const double rms = std::sqrt(energy / window);
        if (rms < 0.008)
            continue;

        int bestLag = 0;
        double bestScore = -1.0;
        std::vector<std::pair<int, double>> peaks;
        for (int lag = minimumLag; lag <= maximumLag; ++lag)
        {
            double xy = 0.0;
            double xx = 0.0;
            double yy = 0.0;
            for (int i = lag; i < window; i += 2)
            {
                const double a = audio[start + static_cast<std::size_t>(i)] - mean;
                const double b = audio[start + static_cast<std::size_t>(i - lag)] - mean;
                xy += a * b;
                xx += a * a;
                yy += b * b;
            }
            const double score = xy / std::sqrt(std::max(1.0e-18, xx * yy));
            if (score > bestScore)
            {
                bestScore = score;
                bestLag = lag;
            }
        }

        if (bestLag <= 0 || bestScore < 0.45)
            continue;

        // Prefer the shortest strong member of the same periodic family. The
        // estimator is deliberately test-only and sees real voice, never an
        // ideal sinusoid fixture.
        int chosenLag = bestLag;
        for (int divisor = 4; divisor >= 2; --divisor)
        {
            const int candidate = static_cast<int>(std::lround(
                static_cast<double>(bestLag) / divisor));
            if (candidate < minimumLag || candidate > maximumLag)
                continue;

            double xy = 0.0;
            double xx = 0.0;
            double yy = 0.0;
            for (int i = candidate; i < window; i += 2)
            {
                const double a = audio[start + static_cast<std::size_t>(i)] - mean;
                const double b = audio[start + static_cast<std::size_t>(i - candidate)] - mean;
                xy += a * b;
                xx += a * a;
                yy += b * b;
            }
            const double score = xy / std::sqrt(std::max(1.0e-18, xx * yy));
            if (score >= 0.88 * bestScore)
            {
                chosenLag = candidate;
                break;
            }
        }

        result.hz.push_back(static_cast<double>(sampleRate) / chosenLag);
    }
    return result;
}

bool trajectoryInvariant()
{
    neumaton::pitch::ScaleQuantizer quantizer;
    double ratios[12] {};
    for (int i = 0; i < 12; ++i)
        ratios[i] = std::exp2(static_cast<double>(i) / 12.0);
    if (!quantizer.setScale(ratios, 12, 440.0, 2.0))
        return false;

    neumaton::render::PitchCorrectionTrajectory trajectory;
    trajectory.prepare(48000.0, 1200.0);

    neumaton::pitch::PitchResult stable;
    stable.state = neumaton::pitch::TrackingState::stable;
    stable.hasStable = true;
    stable.stableHz = 430.0f;
    static_cast<void>(trajectory.process(stable, quantizer, 1.0f, 0.0f, 0.0f));
    const double held = trajectory.desiredCorrectionCents();
    if (std::abs(held) < 1.0)
        return false;

    neumaton::pitch::PitchResult transition = stable;
    transition.state = neumaton::pitch::TrackingState::transition;
    transition.measuredHz = 900.0f; // must have no renderer authority
    transition.octaveAmbiguous = true;
    for (int i = 0; i < 1000; ++i)
        static_cast<void>(trajectory.process(transition, quantizer, 1.0f, 0.0f, 0.0f));

    return std::abs(trajectory.desiredCorrectionCents() - held) < 1.0e-9
        && std::abs(trajectory.currentCorrectionCents() - held) < 1.0e-9;
}

bool runFile(const std::string& path, const WavData& wav, int latencySamples)
{
    double identityCpu = 0.0;
    std::uint64_t identitySplices = 0;
    const auto identity = render(wav, latencySamples, 0.0,
                                 identityCpu, identitySplices);

    double maxIdentityError = 0.0;
    std::uint64_t nonFinite = 0;
    std::vector<double> identityDerivative;
    identityDerivative.reserve(identity.size());
    for (std::size_t i = 0; i < identity.size(); ++i)
    {
        if (!std::isfinite(identity[i]))
            ++nonFinite;
        if (i >= static_cast<std::size_t>(latencySamples))
        {
            const double expected = wav.mono[i - static_cast<std::size_t>(latencySamples)];
            maxIdentityError = std::max(maxIdentityError,
                std::abs(static_cast<double>(identity[i]) - expected));
        }
        if (i > 0)
            identityDerivative.push_back(std::abs(
                static_cast<double>(identity[i]) - identity[i - 1]));
    }

    double upCpu = 0.0;
    double downCpu = 0.0;
    std::uint64_t upSplices = 0;
    std::uint64_t downSplices = 0;
    const auto up = render(wav, latencySamples, 300.0, upCpu, upSplices);
    const auto down = render(wav, latencySamples, -300.0, downCpu, downSplices);

    std::vector<double> upDerivative;
    std::vector<double> downDerivative;
    upDerivative.reserve(up.size());
    downDerivative.reserve(down.size());
    for (std::size_t i = 1; i < up.size(); ++i)
    {
        if (!std::isfinite(up[i]) || !std::isfinite(down[i]))
            ++nonFinite;
        upDerivative.push_back(std::abs(static_cast<double>(up[i]) - up[i - 1]));
        downDerivative.push_back(std::abs(static_cast<double>(down[i]) - down[i - 1]));
    }

    const auto sourcePitch = estimateVoicePitch(identity, wav.sampleRate);
    const auto upPitch = estimateVoicePitch(up, wav.sampleRate);
    const auto downPitch = estimateVoicePitch(down, wav.sampleRate);

    double upShift = 0.0;
    double downShift = 0.0;
    if (!sourcePitch.hz.empty() && !upPitch.hz.empty())
        upShift = 1200.0 * std::log2(
            percentile(upPitch.hz, 0.50) / percentile(sourcePitch.hz, 0.50));
    if (!sourcePitch.hz.empty() && !downPitch.hz.empty())
        downShift = 1200.0 * std::log2(
            percentile(downPitch.hz, 0.50) / percentile(sourcePitch.hz, 0.50));

    std::cout << path
              << "\tlatency=" << latencySamples
              << "\tidentityMaxError=" << maxIdentityError
              << "\tidentitySplices=" << identitySplices
              << "\tidentityCpu=" << identityCpu
              << "\tupCpu=" << upCpu
              << "\tdownCpu=" << downCpu
              << "\tupSplices=" << upSplices
              << "\tdownSplices=" << downSplices
              << "\tupShiftMedianCents=" << upShift
              << "\tdownShiftMedianCents=" << downShift
              << "\tidentityDerivativeP999=" << percentile(identityDerivative, 0.999)
              << "\tupDerivativeP999=" << percentile(upDerivative, 0.999)
              << "\tdownDerivativeP999=" << percentile(downDerivative, 0.999)
              << "\tnonFinite=" << nonFinite
              << '\n';

    bool pitchTransportPass = true;
    if (sourcePitch.hz.size() >= 12 && upPitch.hz.size() >= 12)
        pitchTransportPass = pitchTransportPass && std::abs(upShift - 300.0) <= 35.0;
    if (sourcePitch.hz.size() >= 12 && downPitch.hz.size() >= 12)
        pitchTransportPass = pitchTransportPass && std::abs(downShift + 300.0) <= 35.0;

    return maxIdentityError <= 1.0e-7
        && identitySplices == 0
        && nonFinite == 0
        && pitchTransportPass;
}
}

int main(int argc, char** argv)
{
    if (!trajectoryInvariant())
    {
        std::cerr << "FAIL transition did not hold the last Stable correction\n";
        return 1;
    }

    if (argc < 2)
    {
        std::cerr
            << "usage: SinglePathPitchRendererTest hostile_voice.wav [more_voice.wav ...]\n"
            << "Pitch validation uses real voice only; no ideal sine fixture is supplied.\n";
        return 2;
    }

    bool pass = true;
    for (int i = 1; i < argc; ++i)
    {
        WavData wav;
        if (!readWav(argv[i], wav))
        {
            std::cerr << "cannot read " << argv[i] << '\n';
            pass = false;
            continue;
        }

        pass = runFile(argv[i], wav,
            neumaton::pitch::declaredLatencySamples(
                neumaton::pitch::LatencyMode::lowLatency128)) && pass;
        pass = runFile(argv[i], wav,
            neumaton::pitch::declaredLatencySamples(
                neumaton::pitch::LatencyMode::live256)) && pass;
        pass = runFile(argv[i], wav,
            neumaton::pitch::declaredLatencySamples(
                neumaton::pitch::LatencyMode::studio512)) && pass;
    }

    return pass ? 0 : 1;
}
