#include "PitchCore.h"

#include <algorithm>
#include <array>
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
    const double fraction = position - static_cast<double>(lo);
    return values[lo] + fraction * (values[hi] - values[lo]);
}

int integerFamilyJump(float a, float b)
{
    if (!(a > 0.0f) || !(b > 0.0f))
        return 0;
    const double ratio = std::max(a, b) / std::min(a, b);
    int bestFamily = 0;
    double bestCents = std::numeric_limits<double>::max();
    for (int family = 2; family <= 4; ++family)
    {
        const double cents = std::abs(1200.0 * std::log2(
            ratio / static_cast<double>(family)));
        if (cents < bestCents)
        {
            bestCents = cents;
            bestFamily = family;
        }
    }
    return bestCents < 85.0 ? bestFamily : 0;
}

bool run(const std::string& path,
         const WavData& wav,
         neumaton::pitch::LatencyMode mode)
{
    constexpr float minimumPitchHz = 45.0f;
    constexpr float maximumPitchHz = 1600.0f;

    neumaton::pitch::PitchCore core;
    core.prepare(wav.sampleRate, mode, minimumPitchHz, maximumPitchHz);

    std::uint64_t gateClosed = 0;
    std::uint64_t stableSamples = 0;
    std::uint64_t transitionSamples = 0;
    std::uint64_t acquireSamples = 0;
    std::uint64_t measurements = 0;
    std::uint64_t closedGateStableChanges = 0;
    std::uint64_t invalidPublishedPitch = 0;
    std::uint64_t ambiguousMeasurements = 0;
    std::array<std::uint64_t, 5> integerFamilyJumps {};
    double sumConfidence = 0.0;
    double sumPeriodicity = 0.0;
    float previousStableChangeHz = 0.0f;

    std::uint64_t openTransitionRun = 0;
    std::uint64_t longestOpenTransitionRun = 0;
    std::uint64_t openTransitionRunsOver20ms = 0;
    std::uint64_t openTransitionRunsOver50ms = 0;

    bool previousGateOpen = true;
    std::uint64_t closedRun = 0;
    bool waitingForStableAfterReopen = false;
    std::uint64_t samplesSinceReopen = 0;
    std::vector<double> reopenToStableMs;
    const std::uint64_t meaningfulClosedRun = static_cast<std::uint64_t>(
        std::max(1.0, 0.005 * static_cast<double>(wav.sampleRate)));

    const auto finishTransitionRun = [&]()
    {
        if (openTransitionRun == 0)
            return;
        longestOpenTransitionRun = std::max(longestOpenTransitionRun,
                                             openTransitionRun);
        const double milliseconds = 1000.0 * static_cast<double>(openTransitionRun)
            / static_cast<double>(wav.sampleRate);
        if (milliseconds > 20.0)
            ++openTransitionRunsOver20ms;
        if (milliseconds > 50.0)
            ++openTransitionRunsOver50ms;
        openTransitionRun = 0;
    };

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

        if (r.state == neumaton::pitch::TrackingState::transition && r.gateOpen)
            ++openTransitionRun;
        else
            finishTransitionRun();

        if (!r.gateOpen)
        {
            ++closedRun;
            waitingForStableAfterReopen = false;
            samplesSinceReopen = 0;
        }
        else
        {
            if (!previousGateOpen && closedRun >= meaningfulClosedRun)
            {
                waitingForStableAfterReopen = true;
                samplesSinceReopen = 0;
            }
            closedRun = 0;

            if (waitingForStableAfterReopen)
            {
                ++samplesSinceReopen;
                if (r.state == neumaton::pitch::TrackingState::stable && r.hasStable)
                {
                    reopenToStableMs.push_back(
                        1000.0 * static_cast<double>(samplesSinceReopen)
                        / static_cast<double>(wav.sampleRate));
                    waitingForStableAfterReopen = false;
                }
            }
        }
        previousGateOpen = r.gateOpen;

        if (r.hasStable
            && (!std::isfinite(r.stableHz)
                || r.stableHz < minimumPitchHz
                || r.stableHz > maximumPitchHz))
        {
            ++invalidPublishedPitch;
        }
        if (r.newMeasurement
            && (!std::isfinite(r.measuredHz)
                || r.measuredHz < minimumPitchHz
                || r.measuredHz > maximumPitchHz))
        {
            ++invalidPublishedPitch;
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
                const int family = integerFamilyJump(previousStableChangeHz, r.stableHz);
                if (family >= 2 && family <= 4)
                    ++integerFamilyJumps[static_cast<std::size_t>(family)];
            }
            previousStableChangeHz = r.stableHz;
        }
    }
    finishTransitionRun();
    const auto stop = std::chrono::steady_clock::now();

    const double cpuSeconds = std::chrono::duration<double>(stop - start).count();
    const double audioSeconds = static_cast<double>(wav.mono.size())
        / static_cast<double>(wav.sampleRate);
    const double frames = static_cast<double>(std::max<std::size_t>(1, wav.mono.size()));
    const double measurementDenominator = static_cast<double>(
        std::max<std::uint64_t>(1, measurements));
    const double longestTransitionMs = 1000.0
        * static_cast<double>(longestOpenTransitionRun)
        / static_cast<double>(wav.sampleRate);

    std::cout << path << '\t' << modeName(mode)
              << "\tdeclaredLatency=" << neumaton::pitch::declaredLatencySamples(mode)
              << "\trealtimeCpuRatio=" << cpuSeconds / std::max(1.0e-9, audioSeconds)
              << "\tgateClosed=" << 100.0 * gateClosed / frames << "%"
              << "\tstable=" << 100.0 * stableSamples / frames << "%"
              << "\ttransition=" << 100.0 * transitionSamples / frames << "%"
              << "\tacquire=" << 100.0 * acquireSamples / frames << "%"
              << "\tmeasurements=" << measurements
              << "\tambiguous=" << ambiguousMeasurements
              << "\tfamily2x=" << integerFamilyJumps[2]
              << "\tfamily3x=" << integerFamilyJumps[3]
              << "\tfamily4x=" << integerFamilyJumps[4]
              << "\tlongestOpenTransitionMs=" << longestTransitionMs
              << "\topenTransitionRunsOver20ms=" << openTransitionRunsOver20ms
              << "\topenTransitionRunsOver50ms=" << openTransitionRunsOver50ms
              << "\tgateReopenEvents=" << reopenToStableMs.size()
              << "\tgateReopenToStableP50Ms=" << percentile(reopenToStableMs, 0.50)
              << "\tgateReopenToStableP95Ms=" << percentile(reopenToStableMs, 0.95)
              << "\tgateReopenToStableMaxMs=" << percentile(reopenToStableMs, 1.0)
              << "\tmeanConfidence=" << sumConfidence / measurementDenominator
              << "\tmeanPeriodicity=" << sumPeriodicity / measurementDenominator
              << "\tclosedGateStableChanges=" << closedGateStableChanges
              << "\tinvalidPublishedPitch=" << invalidPublishedPitch
              << '\n';

    // These are architectural invariants. Family jumps and transition lengths
    // are diagnostics because real hostile recordings have no automatic pitch
    // ground truth and may contain genuine large intervals.
    return closedGateStableChanges == 0 && invalidPublishedPitch == 0;
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
