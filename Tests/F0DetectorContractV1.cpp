#include "F0DetectorV1.h"
#include "F0PeriodicGateV1.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr double epsilonCents = 1.5;

struct Rng
{
    std::uint32_t s;
    float next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xffffu) / 32767.5f - 1.0f;
    }
};

enum class Kind { normal, strongSecond, missingFundamental, sine };

float voice(Kind kind, double phase) noexcept
{
    if (kind == Kind::strongSecond)
        return static_cast<float>(0.35 * std::sin(phase)
                                + 1.00 * std::sin(2.0 * phase + 0.17)
                                + 0.22 * std::sin(3.0 * phase - 0.31)
                                + 0.08 * std::sin(4.0 * phase + 0.49));
    if (kind == Kind::missingFundamental)
        return static_cast<float>(0.75 * std::sin(2.0 * phase + 0.17)
                                + 0.45 * std::sin(3.0 * phase - 0.31)
                                + 0.28 * std::sin(4.0 * phase + 0.49)
                                + 0.16 * std::sin(5.0 * phase - 0.63));
    if (kind == Kind::sine)
        return static_cast<float>(std::sin(phase));
    return static_cast<float>(0.72 * std::sin(phase)
                            + 0.34 * std::sin(2.0 * phase + 0.17)
                            + 0.21 * std::sin(3.0 * phase - 0.31)
                            + 0.13 * std::sin(4.0 * phase + 0.49)
                            + 0.08 * std::sin(5.0 * phase - 0.63));
}

const char* kindName(Kind kind)
{
    switch (kind)
    {
        case Kind::normal: return "normal";
        case Kind::strongSecond: return "strong_second";
        case Kind::missingFundamental: return "missing_fundamental";
        case Kind::sine: return "sine";
    }
    return "unknown";
}

double cents(double measured, double target)
{
    if (!(measured > 0.0) || !(target > 0.0))
        return std::numeric_limits<double>::infinity();
    return 1200.0 * std::log2(measured / target);
}

struct CaseResult
{
    double firstCorrectMs = -1.0;
    int stableUpdates = 0;
    int wrongStable = 0;
    int octaveStable = 0;
    double worstCorrectFamilyCents = 0.0;
};

CaseResult runPeriodic(Kind kind, double hz, double snrDb, std::uint32_t seed)
{
    F0PeriodicGateV1 gate;
    F0DetectorV1 detector;
    gate.prepare(sr);
    detector.prepare(sr);

    Rng rng { seed };
    double phase = 0.0;
    const double amp = std::pow(10.0, -24.0 / 20.0);
    const double noiseAmp = amp / std::pow(10.0, snrDb / 20.0);
    CaseResult result;

    constexpr int samples = 4800;
    for (int i = 0; i < samples; ++i)
    {
        phase += 2.0 * pi * hz / sr;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;

        const float x = static_cast<float>(amp) * voice(kind, phase)
                      + static_cast<float>(noiseAmp) * rng.next();
        const bool measure = gate.processSample(x);
        const auto out = detector.processSample(x, measure);
        if (!out.stableUpdated)
            continue;

        ++result.stableUpdates;
        const double error = cents(out.stableHz, hz);
        const double absError = std::abs(error);
        if (absError <= 100.0)
        {
            result.worstCorrectFamilyCents = std::max(result.worstCorrectFamilyCents, absError);
            if (result.firstCorrectMs < 0.0 && absError <= epsilonCents)
                result.firstCorrectMs = 1000.0 * static_cast<double>(i + 1) / sr;
        }
        else
        {
            ++result.wrongStable;
            const double octaveResidual = std::min(
                std::abs(absError - 1200.0),
                std::abs(absError - 2400.0));
            if (octaveResidual < 40.0)
                ++result.octaveStable;
        }
    }
    return result;
}

int runNoise(bool coloured)
{
    F0PeriodicGateV1 gate;
    F0DetectorV1 detector;
    gate.prepare(sr);
    detector.prepare(sr);

    Rng rng { 0x51f15e5du };
    float fast = 0.0f;
    float slow = 0.0f;
    int stableUpdates = 0;
    const float amp = static_cast<float>(std::pow(10.0, -24.0 / 20.0));

    for (int i = 0; i < 24000; ++i)
    {
        const float white = rng.next();
        float x = white;
        if (coloured)
        {
            fast = 0.92f * fast + 0.08f * white;
            slow = 0.992f * slow + 0.008f * white;
            x = 0.52f * white + 0.31f * fast + 0.17f * slow;
        }
        x *= amp;
        const auto out = detector.processSample(x, gate.processSample(x));
        if (out.stableUpdated)
            ++stableUpdates;
    }
    return stableUpdates;
}

struct StepResult
{
    double firstNewStableMs = -1.0;
    int wrongStableDuringStep = 0;
};

StepResult runStep(double fromHz, double toHz)
{
    F0PeriodicGateV1 gate;
    F0DetectorV1 detector;
    gate.prepare(sr);
    detector.prepare(sr);
    Rng rng { 0x1234567u };
    double phase = 0.0;
    const float amp = static_cast<float>(std::pow(10.0, -24.0 / 20.0));
    const float noise = amp / static_cast<float>(std::pow(10.0, 6.0 / 20.0));
    constexpr int stepSample = 4800;
    StepResult result;

    for (int i = 0; i < 9600; ++i)
    {
        const double hz = i < stepSample ? fromHz : toHz;
        phase += 2.0 * pi * hz / sr;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;
        const float x = amp * voice(Kind::normal, phase) + noise * rng.next();
        const auto out = detector.processSample(x, gate.processSample(x));
        if (!out.stableUpdated || i < stepSample)
            continue;

        const double toError = std::abs(cents(out.stableHz, toHz));
        const double fromError = std::abs(cents(out.stableHz, fromHz));
        if (toError <= epsilonCents && result.firstNewStableMs < 0.0)
            result.firstNewStableMs = 1000.0 * static_cast<double>(i - stepSample + 1) / sr;
        else if (toError > 100.0 && fromError > 100.0)
            ++result.wrongStableDuringStep;
    }
    return result;
}
}

int main()
{
    constexpr std::array<double, 5> frequencies { 82.4069, 110.0, 220.0, 440.0, 880.0 };
    constexpr std::array<double, 3> snrs { 12.0, 6.0, 3.0 };
    constexpr std::array<std::uint32_t, 3> seeds { 0x1234567u, 0x51f15e5du, 0x9e3779b9u };
    constexpr std::array<Kind, 3> kinds { Kind::normal, Kind::strongSecond, Kind::missingFundamental };

    int cases = 0;
    int acquiredUnder10 = 0;
    int octaveErrors = 0;
    int wrongStable = 0;
    int precisionCases = 0;
    double worstCents = 0.0;

    for (auto kind : kinds)
        for (double hz : frequencies)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    const auto r = runPeriodic(kind, hz, snr, seed);
                    ++cases;
                    if (r.firstCorrectMs >= 0.0 && r.firstCorrectMs < 10.0)
                        ++acquiredUnder10;
                    octaveErrors += r.octaveStable;
                    wrongStable += r.wrongStable;
                    if (r.stableUpdates > 0 && r.wrongStable == 0)
                    {
                        ++precisionCases;
                        worstCents = std::max(worstCents, r.worstCorrectFamilyCents);
                    }

                    std::cout << std::fixed << std::setprecision(4)
                              << "F0_V1_CASE kind=" << kindName(kind)
                              << " hz=" << hz
                              << " snr=" << snr
                              << " seed=" << seed
                              << " acquire_ms=" << r.firstCorrectMs
                              << " stable_updates=" << r.stableUpdates
                              << " wrong_stable=" << r.wrongStable
                              << " octave_stable=" << r.octaveStable
                              << " worst_family_cents=" << r.worstCorrectFamilyCents
                              << '\n';
                }

    const int whiteHallucinations = runNoise(false);
    const int colouredHallucinations = runNoise(true);
    const auto stepA = runStep(220.0, 246.94165);
    const auto stepB = runStep(440.0, 660.0);

    const bool pass = acquiredUnder10 == cases
                   && octaveErrors == 0
                   && wrongStable == 0
                   && worstCents <= epsilonCents
                   && whiteHallucinations == 0
                   && colouredHallucinations == 0
                   && stepA.firstNewStableMs >= 0.0 && stepA.firstNewStableMs < 10.0
                   && stepB.firstNewStableMs >= 0.0 && stepB.firstNewStableMs < 10.0
                   && stepA.wrongStableDuringStep == 0
                   && stepB.wrongStableDuringStep == 0;

    std::cout << std::fixed << std::setprecision(4)
              << "F0_V1_SUMMARY"
              << " cases=" << cases
              << " acquired_under_10ms=" << acquiredUnder10
              << " octave_errors=" << octaveErrors
              << " wrong_stable=" << wrongStable
              << " precision_cases=" << precisionCases
              << " worst_cents=" << worstCents
              << " white_hallucinations=" << whiteHallucinations
              << " coloured_hallucinations=" << colouredHallucinations
              << " step_220_246_ms=" << stepA.firstNewStableMs
              << " step_440_660_ms=" << stepB.firstNewStableMs
              << " step_wrong=" << (stepA.wrongStableDuringStep + stepB.wrongStableDuringStep)
              << '\n';
    std::cout << "F0_DETECTOR_V1_RESULT=" << (pass ? "PASS" : "FAIL") << '\n';
    return pass ? 0 : 1;
}
