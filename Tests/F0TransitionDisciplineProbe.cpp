#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string_view>

#include "../Source/F0PeriodicGateV1.h"

#define F0_WHOLE_NOTE_TUNER_NO_MAIN
#define main progressive_probe_reference_main
#include "F0WholeNoteProgressiveProbe.cpp"
#undef main
#undef F0_WHOLE_NOTE_TUNER_NO_MAIN

namespace
{
constexpr int analysisSamples = 448;
constexpr int hopSamples = 32;
constexpr int sequenceSamples = 14400;
constexpr int eventStart = 5760;
constexpr int eventLength = 960;
constexpr int eventEnd = eventStart + eventLength;

using Frame = std::array<double, progressiveMaxSamples>;
using Sequence = std::array<double, sequenceSamples>;

enum class EventKind { click, dropout, whiteBurst, hissBurst, breathBurst, phaseBreak };

struct Decision
{
    bool stable = false;
    bool familyCorrect = false;
    double hz = 0.0;
};

struct LocalRng
{
    std::uint32_t s;
    double next() noexcept
    {
        s = 1664525u * s + 1013904223u;
        return 2.0 * (static_cast<double>(s) / 4294967295.0) - 1.0;
    }
};

Sequence makeStableSequence(double f0, std::uint32_t seed)
{
    Sequence x {};
    LocalRng rng { seed };
    std::array<double, 10> phase {};
    for (double& p : phase) p = pi * rng.next();

    double basePhase = 0.0;
    for (int n = 0; n < sequenceSamples; ++n)
    {
        const double t = static_cast<double>(n) / sr;
        const double vibrato = 0.0018 * std::sin(2.0 * pi * 5.1 * t + 0.4);
        basePhase += 2.0 * pi * f0 * (1.0 + vibrato) / sr;
        double s = 0.0;
        for (int k = 1; k <= static_cast<int>(phase.size()); ++k)
            s += (1.0 / std::pow(static_cast<double>(k), 1.15))
               * std::sin(static_cast<double>(k) * basePhase
                        + phase[static_cast<std::size_t>(k - 1)]);
        x[static_cast<std::size_t>(n)] = s + 0.006 * rng.next();
    }
    return x;
}

void injectEvent(Sequence& x, EventKind kind, std::uint32_t seed)
{
    LocalRng rng { seed };
    double colour = 0.0;
    if (kind == EventKind::click)
    {
        const int c = eventStart + eventLength / 2;
        x[static_cast<std::size_t>(c)] += 7.0;
        x[static_cast<std::size_t>(c + 1)] -= 4.0;
        x[static_cast<std::size_t>(c + 2)] += 2.0;
        return;
    }

    for (int n = eventStart; n < eventEnd; ++n)
    {
        const double pos = static_cast<double>(n - eventStart)
                         / static_cast<double>(eventLength - 1);
        const double env = std::sin(pi * std::clamp(pos, 0.0, 1.0));
        const double w = rng.next();
        colour = 0.90 * colour + 0.10 * w;
        switch (kind)
        {
            case EventKind::dropout:
                x[static_cast<std::size_t>(n)] *= 0.01;
                break;
            case EventKind::whiteBurst:
                x[static_cast<std::size_t>(n)] = 1.25 * env * w;
                break;
            case EventKind::hissBurst:
                x[static_cast<std::size_t>(n)] = 1.10 * env * (w - colour);
                break;
            case EventKind::breathBurst:
                x[static_cast<std::size_t>(n)] = 0.72 * env * (0.55 * w + 0.45 * colour);
                break;
            case EventKind::phaseBreak:
                x[static_cast<std::size_t>(n)] = 0.80 * env
                    * std::sin(2.0 * pi * 337.0 * static_cast<double>(n - eventStart) / sr + 1.7);
                break;
            case EventKind::click:
                break;
        }
    }
}

Frame extractFrame(const Sequence& x, int endExclusive)
{
    Frame f {};
    const int begin = endExclusive - analysisSamples;
    for (int n = 0; n < analysisSamples; ++n)
        f[static_cast<std::size_t>(n)] = x[static_cast<std::size_t>(begin + n)];
    return f;
}

Decision decide(const Frame& frame, double truthHz)
{
    neumaton::f0v1::PeriodicMeasurementGate gate;
    gate.prepare(sr);
    bool measure = true;
    for (int n = 0; n < analysisSamples; ++n)
        measure = gate.processSample(static_cast<float>(frame[static_cast<std::size_t>(n)]));

    const auto e = estimateProgressive(frame, analysisSamples);
    const bool stable = measure && e.valid && shouldPublishInsideNormalWindow(frame, e);
    const bool correct = stable && std::abs(cents(e.hz, truthHz)) <= 100.0;
    return { stable, correct, stable ? e.hz : 0.0 };
}

bool coreOverlapsEvent(int endExclusive, EventKind kind)
{
    const int begin = endExclusive - analysisSamples;
    if (kind == EventKind::click)
    {
        const int c = eventStart + eventLength / 2;
        return begin <= c && c < endExclusive;
    }
    const int overlap = std::max(0, std::min(endExclusive, eventEnd)
                                  - std::max(begin, eventStart));
    return overlap >= analysisSamples / 4;
}

bool cleanAfterEvent(int endExclusive)
{
    return endExclusive - analysisSamples >= eventEnd;
}
}

int main()
{
    constexpr std::array<double, 7> normalFrequencies {
        110.0, 146.8324, 196.0, 246.9417, 329.6276, 440.0, 659.2551
    };
    constexpr std::array<std::uint32_t, 6> normalSeeds {
        0x10203040u, 0x55667788u, 0x13572468u,
        0x89abcdefu, 0xcafebabeu, 0x0badf00du
    };

    int normalFrames = 0, normalFalseTransition = 0, normalWrongStable = 0;
    for (double f0 : normalFrequencies)
        for (auto seed : normalSeeds)
        {
            const auto x = makeStableSequence(f0, seed ^ static_cast<std::uint32_t>(f0 * 31.0));
            for (int end = 2400; end <= 12000; end += 192)
            {
                const auto d = decide(extractFrame(x, end), f0);
                ++normalFrames;
                if (!d.stable) ++normalFalseTransition;
                else if (!d.familyCorrect) ++normalWrongStable;
            }
        }

    std::cout << "TRANSITION_NORMAL_SUMMARY frames=" << normalFrames
              << " false_transition=" << normalFalseTransition
              << " wrong_stable=" << normalWrongStable << '\n';

    constexpr std::array<EventKind, 6> eventKinds {
        EventKind::click, EventKind::dropout, EventKind::whiteBurst,
        EventKind::hissBurst, EventKind::breathBurst, EventKind::phaseBreak
    };
    constexpr std::array<std::uint32_t, 5> transientSeeds {
        0x11112222u, 0x33334444u, 0x55556666u, 0x77778888u, 0x9999aaaau
    };

    int transientCoreFrames = 0, transientStableLeak = 0, transientWrongStableLeak = 0;
    int postCleanFrames = 0, postFalseTransition = 0, postWrongStable = 0;
    double worstRecoveryMs = 0.0;

    for (EventKind kind : eventKinds)
        for (auto seed : transientSeeds)
        {
            constexpr double f0 = 220.0;
            auto x = makeStableSequence(f0, seed);
            injectEvent(x, kind, seed ^ 0xa5a5a5a5u);
            bool recoveryFound = false;
            int recoveryEnd = -1;

            for (int end = eventStart - 256;
                 end <= eventEnd + analysisSamples + 768;
                 end += hopSamples)
            {
                if (end < analysisSamples || end > sequenceSamples) continue;
                const auto d = decide(extractFrame(x, end), f0);
                if (coreOverlapsEvent(end, kind))
                {
                    ++transientCoreFrames;
                    if (d.stable)
                    {
                        ++transientStableLeak;
                        if (!d.familyCorrect) ++transientWrongStableLeak;
                    }
                }
                if (cleanAfterEvent(end))
                {
                    ++postCleanFrames;
                    if (!d.stable) ++postFalseTransition;
                    else if (!d.familyCorrect) ++postWrongStable;
                    if (!recoveryFound && d.stable && d.familyCorrect)
                    {
                        recoveryFound = true;
                        recoveryEnd = end;
                    }
                }
            }

            if (recoveryFound)
            {
                const double ms = 1000.0 * static_cast<double>(
                    std::max(0, recoveryEnd - (eventEnd + analysisSamples))) / sr;
                worstRecoveryMs = std::max(worstRecoveryMs, ms);
            }
            else
                worstRecoveryMs = std::numeric_limits<double>::infinity();
        }

    std::cout << std::fixed << std::setprecision(4)
              << "TRANSIENT_DISCIPLINE_SUMMARY core_frames=" << transientCoreFrames
              << " stable_leak=" << transientStableLeak
              << " wrong_stable_leak=" << transientWrongStableLeak
              << " post_clean_frames=" << postCleanFrames
              << " post_false_transition=" << postFalseTransition
              << " post_wrong_stable=" << postWrongStable
              << " worst_recovery_ms=" << worstRecoveryMs << '\n';

    constexpr std::array<std::uint32_t, 8> hardSeeds {
        1821285621u, 1518500249u, 2600822924u, 1249150122u,
        1065670069u, 2614888103u, 2438529370u, 2240740374u
    };
    constexpr std::array<int, 6> checkpoints { 448, 576, 672, 768, 896, 1024 };

    int hardCases = 0, hardResolved = 0, hardWrongPublished = 0, hardUnresolved = 0;
    double hardWorstExitMs = 0.0;
    for (auto seed : hardSeeds)
    {
        const auto x = makeProgressiveVoiceLike(
            profiles[2], 246.9417, 3.0,
            seed ^ static_cast<std::uint32_t>(246.9417 * 97.0));
        ++hardCases;
        bool resolved = false, wrongPublished = false;
        for (int n : checkpoints)
        {
            const auto e = estimateProgressive(x, n);
            if (!e.valid) continue;
            if (n == 448 && !shouldPublishInsideNormalWindow(x, e)) continue;
            const double ae = std::abs(cents(e.hz, 246.9417));
            if (ae <= 100.0)
            {
                resolved = true;
                hardWorstExitMs = std::max(hardWorstExitMs, 1000.0 * static_cast<double>(n) / sr);
                break;
            }
            wrongPublished = true;
        }
        if (resolved) ++hardResolved;
        else if (wrongPublished) ++hardWrongPublished;
        else ++hardUnresolved;
    }

    std::cout << "HARD_TRANSITION_SUMMARY cases=" << hardCases
              << " resolved=" << hardResolved
              << " wrong_published=" << hardWrongPublished
              << " unresolved=" << hardUnresolved
              << " worst_exit_ms=" << hardWorstExitMs << '\n';

    const bool normalReady = normalFalseTransition == 0 && normalWrongStable == 0;
    const bool transientReady = transientStableLeak == 0
                             && transientWrongStableLeak == 0
                             && postFalseTransition == 0
                             && postWrongStable == 0;
    const bool hardExitReady = hardWrongPublished == 0 && hardUnresolved == 0;
    std::cout << "F0_TRANSITION_NORMAL_READY=" << (normalReady ? "PASS" : "FAIL") << '\n';
    std::cout << "F0_TRANSIENT_DISCIPLINE_READY=" << (transientReady ? "PASS" : "FAIL") << '\n';
    std::cout << "F0_HARD_PERIODIC_EXIT_READY=" << (hardExitReady ? "PASS" : "FAIL") << '\n';
    std::cout << "F0_TRANSITION_DISCIPLINE_READY="
              << ((normalReady && transientReady && hardExitReady) ? "PASS" : "FAIL") << '\n';
    return 0;
}
