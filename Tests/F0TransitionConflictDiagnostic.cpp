#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0TransitionAdaptiveStateProbe.cpp"

namespace
{
constexpr double diagnosticJump = 1.90;
constexpr std::array<double, 4> conflictCents { 100.0, 200.0, 300.0, 600.0 };

struct BreakParts
{
    bool gate = false;
    bool energy = false;
    bool residualJump = false;
};

BreakParts parts(const Sequence& x, int endExclusive, const State& state)
{
    const double r = residual(x, endExclusive, state.stableHz);
    const double rms = tailRms(x, endExclusive);
    return {
        !gateAllowsAdaptive(x, endExclusive),
        rms <= energyCollapse * std::max(1.0e-9, state.baselineRms),
        r >= diagnosticJump * std::max(1.0e-6, state.baselineResidual)
    };
}

bool rawBreak(const BreakParts& p) { return p.gate || p.energy || p.residualJump; }

std::array<bool, conflictCents.size()> candidateConflict(const Sequence& x,
                                                         int endExclusive,
                                                         double stableHz)
{
    std::array<bool, conflictCents.size()> out {};
    const auto frame = extractFrame(x, endExclusive);
    const auto e = estimateProgressive(frame, analysisSamples);
    if (!e.valid || !shouldPublishInsideNormalWindow(frame, e))
        return out;

    const double distance = std::abs(cents(e.hz, stableHz));
    for (std::size_t i = 0; i < conflictCents.size(); ++i)
        out[i] = distance >= conflictCents[i];
    return out;
}
}

int main()
{
    constexpr std::array<double, 6> frequencies {
        123.4708, 164.8138, 220.0, 293.6648, 440.0, 659.2551
    };
    constexpr std::array<double, 2> snrs { 18.0, 9.0 };
    constexpr std::array<std::uint32_t, 6> normalSeeds {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu,
        0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u
    };

    int normalFrames = 0, normalRawBreak = 0;
    int normalGate = 0, normalEnergy = 0, normalResidual = 0;
    std::array<int, conflictCents.size()> normalConflict {};

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : normalSeeds)
                {
                    const auto x = makeContinuousVoice(profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 61.0));
                    State state { f0, residual(x, 2400, f0), tailRms(x, 2400) };
                    if (!std::isfinite(state.baselineResidual)) continue;
                    for (int end = 2528; end <= 12000; end += 128)
                    {
                        ++normalFrames;
                        const auto p = parts(x, end, state);
                        const bool broken = rawBreak(p);
                        if (broken)
                        {
                            ++normalRawBreak;
                            if (p.gate) ++normalGate;
                            if (p.energy) ++normalEnergy;
                            if (p.residualJump) ++normalResidual;
                            std::cout << "NORMAL_BREAK profile=" << profile.name
                                      << " f0=" << f0 << " snr=" << snr
                                      << " seed=" << seed << " end=" << end
                                      << " gate=" << p.gate
                                      << " energy=" << p.energy
                                      << " residual=" << p.residualJump
                                      << " ratio=" << residual(x, end, state.stableHz)
                                          / std::max(1.0e-6, state.baselineResidual)
                                      << '\n';
                        }

                        const auto conflicts = candidateConflict(x, end, state.stableHz);
                        for (std::size_t i = 0; i < conflicts.size(); ++i)
                            if (conflicts[i]) ++normalConflict[i];

                        if (!broken)
                        {
                            state.baselineResidual = residual(x, end, state.stableHz);
                            state.baselineRms = tailRms(x, end);
                        }
                    }
                }

    constexpr std::array<EventKind, 6> kinds {
        EventKind::click, EventKind::dropout, EventKind::whiteBurst,
        EventKind::hissBurst, EventKind::breathBurst, EventKind::phaseBreak
    };
    constexpr std::array<double, 3> eventFreqs { 164.8138, 220.0, 329.6276 };
    constexpr std::array<double, 2> eventSnrs { 18.0, 9.0 };
    constexpr std::array<std::uint32_t, 4> eventSeeds {
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u
    };

    std::array<int, 6> missed {};
    std::array<std::array<int, conflictCents.size()>, 6> conflictCaught {};

    for (std::size_t ki = 0; ki < kinds.size(); ++ki)
        for (const auto& profile : profiles)
            for (double f0 : eventFreqs)
                for (double snr : eventSnrs)
                    for (auto seed : eventSeeds)
                    {
                        auto x = makeContinuousVoice(profile, f0, snr,
                            seed ^ static_cast<std::uint32_t>(f0 * 71.0));
                        State state { f0, residual(x, eventStart - 512, f0),
                                          tailRms(x, eventStart - 512) };
                        if (!std::isfinite(state.baselineResidual)) continue;
                        injectEvent(x, kinds[ki], seed ^ 0x3c6ef372u);

                        for (int end = eventStart;
                             end <= eventEnd + analysisSamples;
                             end += hopSamples)
                        {
                            const auto p = parts(x, end, state);
                            const bool broken = rawBreak(p);
                            if (coreOverlapsEvent(end, kinds[ki]) && !broken)
                            {
                                ++missed[ki];
                                const auto conflicts = candidateConflict(x, end, state.stableHz);
                                for (std::size_t i = 0; i < conflicts.size(); ++i)
                                    if (conflicts[i]) ++conflictCaught[ki][i];
                            }
                            if (!broken)
                            {
                                state.baselineResidual = residual(x, end, state.stableHz);
                                state.baselineRms = tailRms(x, end);
                            }
                        }
                    }

    std::cout << "CONFLICT_NORMAL frames=" << normalFrames
              << " raw_break=" << normalRawBreak
              << " gate=" << normalGate
              << " energy=" << normalEnergy
              << " residual=" << normalResidual;
    for (std::size_t i = 0; i < conflictCents.size(); ++i)
        std::cout << " c" << static_cast<int>(conflictCents[i])
                  << "=" << normalConflict[i];
    std::cout << '\n';

    for (std::size_t ki = 0; ki < kinds.size(); ++ki)
    {
        std::cout << "CONFLICT_EVENT kind=" << ki << " missed=" << missed[ki];
        for (std::size_t i = 0; i < conflictCents.size(); ++i)
            std::cout << " c" << static_cast<int>(conflictCents[i])
                      << "=" << conflictCaught[ki][i];
        std::cout << '\n';
    }
    return 0;
}
