#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "F0PrimitiveAuthorityAblationProbe.cpp"

namespace
{
struct PolicyStats
{
    int cases = 0;
    int correct = 0;
    int high = 0;
    int low = 0;
    int other = 0;
    int transition = 0;
    int transitionTruthCorrect = 0;
    int transitionTruthHigh = 0;
    int exactLower = 0;
    int altStrongLower = 0;
    int strictLower = 0;
};

template <std::size_t N>
void runPolicy(const char* name, const std::array<std::uint32_t, N>& seeds)
{
    constexpr std::array<double, 12> freqs {
        110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
        246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
    };
    constexpr std::array<double, 3> snrs {18.0, 9.0, 3.0};
    constexpr std::array<int, 3> cps {1024, 1280, 1536};
    std::array<PolicyStats, cps.size()> stats {};

    for (const auto& profile : profiles)
        for (double f0 : freqs)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    const auto mixed = seed ^ static_cast<std::uint32_t>(f0 * 97.0);
                    const auto x = makePrefixStableExtendedVoiceLike(profile, f0, snr, mixed);
                    for (std::size_t ci = 0; ci < cps.size(); ++ci)
                    {
                        const int n = cps[ci];
                        auto& s = stats[ci];
                        ++s.cases;
                        const auto raw = estimateRawNoLower(x, n);
                        if (!raw.valid) continue;

                        double outHz = raw.hz;
                        bool resolved = false;

                        if (raw.primitiveRatio <= 0.65 && 0.5 * raw.hz >= minimumF0)
                        {
                            outHz *= 0.5;
                            resolved = true;
                            ++s.exactLower;
                        }
                        else if (raw.alt.observable && raw.alt.score >= 3.0
                                 && 0.5 * raw.hz >= minimumF0)
                        {
                            outHz *= 0.5;
                            resolved = true;
                            ++s.altStrongLower;
                        }

                        const auto d = measureExtendedExclusiveLowerFamily(
                            x, n, asProgressive(raw));
                        if (!resolved && d.observable
                            && d.witnesses8 >= 3 && d.localRatio <= 1.20)
                        {
                            outHz = d.lowHz;
                            resolved = true;
                            ++s.strictLower;
                        }

                        if (!resolved && d.observable
                            && d.witnesses8 == 2 && d.localRatio <= 0.85)
                        {
                            ++s.transition;
                            const std::string truthClass = classify(raw.hz, f0);
                            if (truthClass == "correct") ++s.transitionTruthCorrect;
                            if (truthClass == "high") ++s.transitionTruthHigh;
                            continue;
                        }

                        const std::string cls = classify(outHz, f0);
                        if (cls == "correct") ++s.correct;
                        else if (cls == "high") ++s.high;
                        else if (cls == "low") ++s.low;
                        else ++s.other;
                    }
                }

    for (std::size_t ci = 0; ci < cps.size(); ++ci)
    {
        const auto& s = stats[ci];
        std::cout << "CANDIDATE_POLICY"
                  << " set=" << name
                  << " samples=" << cps[ci]
                  << " ms=" << std::fixed << std::setprecision(4)
                  << (1000.0 * cps[ci] / sr)
                  << " cases=" << s.cases
                  << " correct=" << s.correct
                  << " high=" << s.high
                  << " low=" << s.low
                  << " other=" << s.other
                  << " transition=" << s.transition
                  << " transition_correct=" << s.transitionTruthCorrect
                  << " transition_high=" << s.transitionTruthHigh
                  << " exact_lower=" << s.exactLower
                  << " altstrong_lower=" << s.altStrongLower
                  << " strict_lower=" << s.strictLower
                  << '\n';
    }
}
}

int main()
{
    constexpr std::array<std::uint32_t, 8> a {
        0x0d95748fu, 0x728eb658u, 0x718bcd58u, 0x82154aeeu,
        0x7b54a41du, 0xc25a59b5u, 0x9c30d539u, 0x2af26013u
    };
    constexpr std::array<std::uint32_t, 8> b {
        0xa4093822u, 0x299f31d0u, 0x082efa98u, 0xec4e6c89u,
        0x452821e6u, 0x38d01377u, 0xbe5466cfu, 0x34e90c6cu
    };
    constexpr std::array<std::uint32_t, 8> c {
        0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu,
        0x1f83d9abu, 0x5be0cd19u, 0xc1059ed8u, 0x367cd507u
    };
    runPolicy("a", a);
    runPolicy("b", b);
    runPolicy("c", c);
    return 0;
}
