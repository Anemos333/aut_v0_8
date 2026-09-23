#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

#define main transition_discipline_reference_main
#include "F0TransitionDisciplineProbe.cpp"
#undef main

namespace
{
constexpr double residualJump = 2.0;
constexpr double energyCollapse = 0.25;

Sequence makeContinuousVoice(const VoiceProfile& profile,
                             double f0,
                             double snrDb,
                             std::uint32_t seed)
{
    Sequence x {};
    LocalRng rng { seed };
    std::array<double, 24> phases {};
    for (double& p : phases) p = pi * rng.next();

    double phase = 0.0, jitterState = 0.0, shimmerState = 0.0, breathState = 0.0;
    double cleanEnergy = 0.0;
    for (int n = 0; n < sequenceSamples; ++n)
    {
        const double t = static_cast<double>(n) / sr;
        jitterState = 0.86 * jitterState + 0.14 * rng.next();
        shimmerState = 0.93 * shimmerState + 0.07 * rng.next();
        const double vibrato = 0.0035 * std::sin(2.0 * pi * 5.2 * t + 0.37);
        const double instF0 = f0 * (1.0 + vibrato + profile.jitter * jitterState);
        phase += 2.0 * pi * instF0 / sr;

        double voiced = 0.0;
        for (int k = 1; k <= static_cast<int>(phases.size()); ++k)
        {
            const double hz = f0 * static_cast<double>(k);
            if (hz >= 0.47 * sr) break;
            double amp = 1.0 / std::pow(static_cast<double>(k), profile.tilt);
            const double env = 0.12
                + 1.10 * formantGain(hz, profile.f1, profile.bw1)
                + 0.85 * formantGain(hz, profile.f2, profile.bw2)
                + 0.58 * formantGain(hz, profile.f3, profile.bw3);
            amp *= env;
            if (k == 1) amp *= profile.fundamentalScale;
            if (k == 2) amp *= profile.secondScale;
            voiced += amp * std::sin(static_cast<double>(k) * phase
                                   + phases[static_cast<std::size_t>(k - 1)]);
        }

        const double shimmer = std::max(0.65, 1.0 + profile.shimmer * shimmerState);
        const double w = rng.next();
        const double highBreath = w - breathState;
        breathState = 0.86 * breathState + 0.14 * w;
        const double s = shimmer * voiced + profile.breath * highBreath;
        x[static_cast<std::size_t>(n)] = s;
        cleanEnergy += s * s;
    }

    const double rms = std::sqrt(cleanEnergy / static_cast<double>(sequenceSamples));
    const double noiseScale = rms / std::pow(10.0, snrDb / 20.0);
    double colour = 0.0;
    for (double& s : x)
    {
        const double w = rng.next();
        colour = 0.72 * colour + 0.28 * w;
        s += noiseScale * (0.72 * w + 0.28 * colour);
    }
    return x;
}

double residual(const Sequence& x, int endExclusive, double stableHz)
{
    const int lag = std::max(2, static_cast<int>(std::lround(sr / stableHz)));
    const int begin = endExclusive - analysisSamples;
    if (begin - lag < 0) return std::numeric_limits<double>::infinity();
    double diff = 0.0, energy = 0.0;
    for (int n = begin; n < endExclusive; ++n)
    {
        const double a = x[static_cast<std::size_t>(n)];
        const double b = x[static_cast<std::size_t>(n - lag)];
        const double d = a - b;
        diff += d * d;
        energy += a * a + b * b;
    }
    return diff / std::max(1.0e-30, energy);
}

double tailRms(const Sequence& x, int endExclusive, int count = 128)
{
    double e = 0.0;
    for (int n = endExclusive - count; n < endExclusive; ++n)
    {
        const double s = x[static_cast<std::size_t>(n)];
        e += s * s;
    }
    return std::sqrt(e / static_cast<double>(count));
}

bool gateAllowsAdaptive(const Sequence& x, int endExclusive)
{
    F0PeriodicGateV1 gate;
    gate.prepare(sr);
    bool measure = true;
    for (int n = endExclusive - analysisSamples; n < endExclusive; ++n)
        measure = gate.processSample(static_cast<float>(x[static_cast<std::size_t>(n)]));
    return measure;
}

struct State
{
    double stableHz = 0.0;
    double baselineResidual = 0.0;
    double baselineRms = 0.0;
};

bool transitionNow(const Sequence& x, int endExclusive, const State& state)
{
    const double r = residual(x, endExclusive, state.stableHz);
    const double rms = tailRms(x, endExclusive);
    const bool residualBreak = r >= residualJump * std::max(1.0e-6, state.baselineResidual);
    const bool energyBreak = rms <= energyCollapse * std::max(1.0e-9, state.baselineRms);
    return !gateAllowsAdaptive(x, endExclusive) || residualBreak || energyBreak;
}
}

int main()
{
    constexpr std::array<double, 8> frequencies {
        110.0, 146.8324, 196.0, 220.0, 246.9417, 329.6276, 440.0, 659.2551
    };
    constexpr std::array<double, 2> normalSnrs { 18.0, 9.0 };
    constexpr std::array<std::uint32_t, 8> normalSeeds {
        0x5a827999u, 0x6ed9eba1u, 0x8f1bbcdcu, 0xca62c1d6u,
        0x3f84d5b5u, 0xb5470917u, 0x9216d5d9u, 0x8979fb1bu
    };

    int normalFrames = 0, normalFalseTransition = 0;
    double worstNormalRatio = 0.0;
    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : normalSnrs)
                for (auto seed : normalSeeds)
                {
                    const auto x = makeContinuousVoice(profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 53.0));
                    State state { f0, residual(x, 2400, f0), tailRms(x, 2400) };
                    bool validState = std::isfinite(state.baselineResidual);
                    if (!validState) continue;

                    for (int end = 2528; end <= 12000; end += 128)
                    {
                        ++normalFrames;
                        const double r = residual(x, end, state.stableHz);
                        const double ratio = r / std::max(1.0e-6, state.baselineResidual);
                        worstNormalRatio = std::max(worstNormalRatio, ratio);
                        if (transitionNow(x, end, state))
                            ++normalFalseTransition;
                        else
                        {
                            state.baselineResidual = r;
                            state.baselineRms = tailRms(x, end);
                        }
                    }
                }

    constexpr std::array<EventKind, 6> kinds {
        EventKind::click, EventKind::dropout, EventKind::whiteBurst,
        EventKind::hissBurst, EventKind::breathBurst, EventKind::phaseBreak
    };
    constexpr std::array<std::uint32_t, 8> eventSeeds {
        0xd1310ba6u, 0x98dfb5acu, 0x2ffd72dbu, 0xd01adfb7u,
        0xb8e1afedu, 0x6a267e96u, 0xba7c9045u, 0xf12c7f99u
    };

    std::array<int, 6> core {}, caught {};
    int lateCleanFrames = 0, lateFalseTransition = 0;
    double worstRecoveryMs = 0.0;

    for (std::size_t k = 0; k < kinds.size(); ++k)
        for (auto seed : eventSeeds)
        {
            constexpr double f0 = 220.0;
            auto x = makeContinuousVoice(profiles[0], f0, 9.0, seed);
            State state { f0, residual(x, eventStart - 512, f0), tailRms(x, eventStart - 512) };
            injectEvent(x, kinds[k], seed ^ 0xa5a5a5a5u);
            bool recovered = false;
            int recoveryEnd = -1;

            for (int end = eventStart; end <= eventEnd + analysisSamples + 960; end += hopSamples)
            {
                const bool trans = transitionNow(x, end, state);
                if (coreOverlapsEvent(end, kinds[k]))
                {
                    ++core[k];
                    if (trans) ++caught[k];
                }

                if (!trans)
                {
                    if (!recovered && end >= eventEnd)
                    {
                        recovered = true;
                        recoveryEnd = end;
                    }
                    state.baselineResidual = residual(x, end, state.stableHz);
                    state.baselineRms = tailRms(x, end);
                }

                const int onePeriod = static_cast<int>(std::ceil(sr / f0));
                if (end - analysisSamples >= eventEnd + onePeriod)
                {
                    ++lateCleanFrames;
                    if (trans) ++lateFalseTransition;
                }
            }

            if (!recovered)
                worstRecoveryMs = std::numeric_limits<double>::infinity();
            else if (std::isfinite(worstRecoveryMs))
                worstRecoveryMs = std::max(worstRecoveryMs,
                    1000.0 * static_cast<double>(std::max(0, recoveryEnd - eventEnd)) / sr);
        }

    int totalCore = 0, totalCaught = 0;
    for (std::size_t k = 0; k < kinds.size(); ++k)
    {
        totalCore += core[k];
        totalCaught += caught[k];
        std::cout << "ADAPTIVE_TRANSITION_EVENT kind=" << k
                  << " core=" << core[k] << " caught=" << caught[k] << '\n';
    }

    std::cout << std::fixed << std::setprecision(6)
              << "ADAPTIVE_TRANSITION_NORMAL frames=" << normalFrames
              << " false_transition=" << normalFalseTransition
              << " worst_ratio=" << worstNormalRatio << '\n';
    std::cout << "ADAPTIVE_TRANSITION_SUMMARY core=" << totalCore
              << " caught=" << totalCaught
              << " missed=" << (totalCore - totalCaught)
              << " late_clean_frames=" << lateCleanFrames
              << " late_false_transition=" << lateFalseTransition
              << " worst_recovery_ms=" << worstRecoveryMs << '\n';

    const bool ready = normalFalseTransition == 0
                    && totalCore == totalCaught
                    && lateFalseTransition == 0;
    std::cout << "F0_ADAPTIVE_TRANSITION_READY=" << (ready ? "PASS" : "FAIL") << '\n';
    return 0;
}
