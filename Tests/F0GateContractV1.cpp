#include "F0PeriodicGateV1.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;

struct Rng
{
    std::uint32_t s;
    float next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xffffu) / 32767.5f - 1.0f;
    }
};

enum class PeriodicKind { normal, strongSecond, missingFundamental, sine };

float periodicSample(PeriodicKind kind, double phase) noexcept
{
    if (kind == PeriodicKind::strongSecond)
        return static_cast<float>(0.35 * std::sin(phase)
                                + 1.00 * std::sin(2.0 * phase + 0.17)
                                + 0.22 * std::sin(3.0 * phase - 0.31)
                                + 0.08 * std::sin(4.0 * phase + 0.49));
    if (kind == PeriodicKind::missingFundamental)
        return static_cast<float>(0.75 * std::sin(2.0 * phase + 0.17)
                                + 0.45 * std::sin(3.0 * phase - 0.31)
                                + 0.28 * std::sin(4.0 * phase + 0.49)
                                + 0.16 * std::sin(5.0 * phase - 0.63));
    if (kind == PeriodicKind::sine)
        return static_cast<float>(std::sin(phase));
    return static_cast<float>(0.72 * std::sin(phase)
                            + 0.34 * std::sin(2.0 * phase + 0.17)
                            + 0.21 * std::sin(3.0 * phase - 0.31)
                            + 0.13 * std::sin(4.0 * phase + 0.49)
                            + 0.08 * std::sin(5.0 * phase - 0.63));
}

double frequencyAt(int index)
{
    constexpr int count = 24;
    const double t = static_cast<double>(index) / static_cast<double>(count - 1);
    return 55.0 * std::pow(1560.0 / 55.0, t);
}

bool runPeriodicCase(PeriodicKind kind, double hz, double snrDb, std::uint32_t seed,
                     bool addBurst)
{
    F0PeriodicGateV1 gate;
    gate.prepare(sr);
    Rng rng { seed };
    const double amp = std::pow(10.0, -24.0 / 20.0);
    const double noiseAmp = amp / std::pow(10.0, snrDb / 20.0);
    double phase = 0.37 * static_cast<double>(seed & 7u);
    constexpr int samples = 1440;

    for (int i = 0; i < samples; ++i)
    {
        phase += 2.0 * pi * hz / sr;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;
        float x = static_cast<float>(amp) * periodicSample(kind, phase)
                + static_cast<float>(noiseAmp) * rng.next();
        if (addBurst && i >= 480 && i < 624)
            x += static_cast<float>(amp * 4.0) * rng.next();
        if (!gate.processSample(x))
            return false;
    }
    return true;
}

enum class NoiseKind { silence, white, coloured, breath, hiss };

bool finalNoiseDecision(NoiseKind kind, std::uint32_t seed)
{
    F0PeriodicGateV1 gate;
    gate.prepare(sr);
    Rng rng { seed };
    const double amp = std::pow(10.0, -24.0 / 20.0);
    float fast = 0.0f;
    float slow = 0.0f;
    float previous = 0.0f;
    bool decision = true;

    for (int i = 0; i < 2400; ++i)
    {
        const float white = rng.next();
        float x = 0.0f;
        if (kind == NoiseKind::white)
            x = white;
        else if (kind == NoiseKind::coloured)
        {
            fast = 0.92f * fast + 0.08f * white;
            slow = 0.992f * slow + 0.008f * white;
            x = 0.52f * white + 0.31f * fast + 0.17f * slow;
        }
        else if (kind == NoiseKind::breath)
        {
            fast = 0.70f * fast + 0.30f * white;
            x = 0.80f * (white - fast) + 0.20f * white;
        }
        else if (kind == NoiseKind::hiss)
        {
            x = white - 0.92f * previous;
            previous = white;
        }
        decision = gate.processSample(static_cast<float>(amp) * x);
    }
    return decision;
}
}

int main()
{
    int periodicCases = 0;
    int periodicFalseNegatives = 0;
    int burstCases = 0;
    int burstFalseNegatives = 0;

    constexpr std::array<double, 4> snrs { 12.0, 6.0, 3.0, 0.0 };
    constexpr std::array<PeriodicKind, 4> kinds {
        PeriodicKind::normal,
        PeriodicKind::strongSecond,
        PeriodicKind::missingFundamental,
        PeriodicKind::sine
    };

    for (auto kind : kinds)
        for (int fi = 0; fi < 24; ++fi)
            for (double snr : snrs)
                for (std::uint32_t seed = 1; seed <= 12; ++seed)
                {
                    ++periodicCases;
                    if (!runPeriodicCase(kind, frequencyAt(fi), snr, seed, false))
                        ++periodicFalseNegatives;
                }

    for (int fi = 0; fi < 24; ++fi)
        for (double snr : snrs)
            for (std::uint32_t seed = 1; seed <= 12; ++seed)
            {
                ++burstCases;
                if (!runPeriodicCase(PeriodicKind::normal, frequencyAt(fi), snr, seed, true))
                    ++burstFalseNegatives;
            }

    auto countBlocked = [](NoiseKind kind)
    {
        int blocked = 0;
        for (std::uint32_t seed = 1; seed <= 32; ++seed)
            blocked += finalNoiseDecision(kind, seed) ? 0 : 1;
        return blocked;
    };

    const int silenceBlocked = countBlocked(NoiseKind::silence);
    const int whiteBlocked = countBlocked(NoiseKind::white);
    const int colouredBlocked = countBlocked(NoiseKind::coloured);
    const int breathBlocked = countBlocked(NoiseKind::breath);
    const int hissBlocked = countBlocked(NoiseKind::hiss);

    std::cout << "F0_GATE_V1"
              << " periodic_cases=" << periodicCases
              << " periodic_false_negatives=" << periodicFalseNegatives
              << " burst_cases=" << burstCases
              << " burst_false_negatives=" << burstFalseNegatives
              << " silence_blocked=" << silenceBlocked << "/32"
              << " white_blocked=" << whiteBlocked << "/32"
              << " coloured_blocked=" << colouredBlocked << "/32"
              << " breath_blocked=" << breathBlocked << "/32"
              << " hiss_blocked=" << hissBlocked << "/32"
              << '\n';

    const bool pass = periodicFalseNegatives == 0
                   && burstFalseNegatives == 0
                   && silenceBlocked == 32
                   && hissBlocked >= 30;
    std::cout << "F0_GATE_V1_RESULT=" << (pass ? "PASS" : "FAIL") << '\n';
    return pass ? 0 : 1;
}
