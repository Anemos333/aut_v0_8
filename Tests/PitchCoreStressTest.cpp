#include "PitchCore.h"

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
            const std::int16_t v = static_cast<std::int16_t>(
                p[0] | (p[1] << 8));
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
        for (int ch = 0; ch < channels; ++ch)
        {
            const std::size_t offset =
                (frame * static_cast<std::size_t>(channels)
                    + static_cast<std::size_t>(ch))
                * static_cast<std::size_t>(bytesPerSample);
            sum += sampleAt(offset);
        }
        out.mono[frame] = static_cast<float>(
            sum / static_cast<double>(channels));
    }
    return true;
}

const char* modeName(neumaton::pitch::LatencyMode mode)
{
    switch (mode)
    {
        case neumaton::pitch::LatencyMode::lowLatency128: return "low128";
        case neumaton::pitch::LatencyMode::live256: return "live256";
        case neumaton::pitch::LatencyMode::studio512: return "studio512";
    }
    return "?";
}

bool run(const std::string& path,
         const WavData& wav,
         neumaton::pitch::LatencyMode mode)
{
    neumaton::pitch::PitchCore core;
    core.prepare(wav.sampleRate, mode, 45.0f, 1600.0f);

    std::uint64_t gateClosed = 0;
    std::uint64_t stableSamples = 0;
    std::uint64_t transitionSamples = 0;
    std::uint64_t acquireSamples = 0;
    std::uint64_t measurements = 0;
    std::uint64_t closedGateStableChanges = 0;
    std::uint64_t ambiguousMeasurements = 0;
    std::uint64_t octaveLikeChanges = 0;
    double sumConfidence = 0.0;
    double sumPeriodicity = 0.0;
    float previousStableChangeHz = 0.0f;

    const auto start = std::chrono::steady_clock::now();
    for (float sample : wav.mono)
    {
        const auto r = core.processSample(sample);
        if (!r.gateOpen)
            ++gateClosed;

        switch (r.state)
        {
            case neumaton::pitch::TrackingState::acquire: ++acquireSamples; break;
            case neumaton::pitch::TrackingState::stable: ++stableSamples; break;
            case neumaton::pitch::TrackingState::transition: ++transitionSamples; break;
        }

        if (r.newMeasurement)
        {
            ++measurements;
            sumConfidence += r.confidence;
            sumPeriodicity += r.periodicity;
            if (r.octaveAmbiguous)
                ++ambiguousMeasurements;
        }

        if (r.stableChanged)
        {
            if (!r.gateOpen)
                ++closedGateStableChanges;

            if (previousStableChangeHz > 0.0f && r.stableHz > 0.0f)
            {
                const double cents = std::abs(
                    1200.0 * std::log2(r.stableHz / previousStableChangeHz));
                if (cents > 1110.0 && cents < 1290.0)
                    ++octaveLikeChanges;
            }
            previousStableChangeHz = r.stableHz;
        }
    }
    const auto stop = std::chrono::steady_clock::now();

    const double cpuSeconds = std::chrono::duration<double>(stop - start).count();
    const double audioSeconds = static_cast<double>(wav.mono.size())
        / static_cast<double>(wav.sampleRate);
    const double frames = static_cast<double>(std::max<std::size_t>(1, wav.mono.size()));
    const double measurementDenominator = static_cast<double>(
        std::max<std::uint64_t>(1, measurements));

    std::cout << path << '\t' << modeName(mode)
              << "\tdeclaredLatency=" << neumaton::pitch::declaredLatencySamples(mode)
              << "\trealtimeCpuRatio=" << cpuSeconds / std::max(1.0e-9, audioSeconds)
              << "\tgateClosed=" << 100.0 * gateClosed / frames << "%"
              << "\tstable=" << 100.0 * stableSamples / frames << "%"
              << "\ttransition=" << 100.0 * transitionSamples / frames << "%"
              << "\tacquire=" << 100.0 * acquireSamples / frames << "%"
              << "\tmeasurements=" << measurements
              << "\tambiguous=" << ambiguousMeasurements
              << "\toctaveLikeChanges=" << octaveLikeChanges
              << "\tmeanConfidence=" << sumConfidence / measurementDenominator
              << "\tmeanPeriodicity=" << sumPeriodicity / measurementDenominator
              << "\tclosedGateStableChanges=" << closedGateStableChanges
              << '\n';

    // Architectural invariant, not an acoustic guess: the gate may never
    // create a new Stable F0 while it says "do not measure".
    return closedGateStableChanges == 0;
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr
            << "usage: PitchCoreStressTest noisy_voice.wav [more_voice.wav ...]\n"
            << "Use real hostile vocal recordings; ideal sine fixtures are intentionally not supplied.\n";
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

        pass = run(argv[i], wav, neumaton::pitch::LatencyMode::lowLatency128) && pass;
        pass = run(argv[i], wav, neumaton::pitch::LatencyMode::live256) && pass;
        pass = run(argv[i], wav, neumaton::pitch::LatencyMode::studio512) && pass;
    }
    return pass ? 0 : 1;
}
