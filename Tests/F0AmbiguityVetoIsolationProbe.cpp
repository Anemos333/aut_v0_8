#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "F0PrimitiveAuthorityAblationProbe.cpp"

namespace
{
struct VetoStats
{
    int cases = 0;
    int rawCorrect = 0, rawHigh = 0, rawOther = 0;
    int strictCorrectFlagged = 0, strictHighCorrected = 0;
    int ambiguousCorrect = 0, ambiguousHigh = 0, ambiguousOther = 0;
    int publishCorrect = 0, publishHigh = 0, publishOther = 0;
};

template <std::size_t N>
void runVetoSet(const char* name, const std::array<std::uint32_t, N>& seeds)
{
    constexpr std::array<double, 12> freqs {
        110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
        246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
    };
    constexpr std::array<double, 3> snrs {18.0, 9.0, 3.0};
    constexpr std::array<int, 3> cps {1024, 1280, 1536};
    std::array<VetoStats, cps.size()> stats {};

    for (const auto& profile : profiles)
        for (double f0 : freqs)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    const auto mixed = seed ^ static_cast<std::uint32_t>(f0 * 97.0);
                    const auto x = makePrefixStableExtendedVoiceLike(profile, f0, snr, mixed);
                    for (std::size_t ci = 0; ci < cps.size(); ++ci)
                    {
                        auto& s = stats[ci];
                        ++s.cases;
                        const int n = cps[ci];
                        const auto raw = estimateRawNoLower(x, n);
                        if (!raw.valid) continue;
                        const std::string cls = classify(raw.hz, f0);
                        if (cls == "correct") ++s.rawCorrect;
                        else if (cls == "high") ++s.rawHigh;
                        else ++s.rawOther;

                        const auto d = measureExtendedExclusiveLowerFamily(
                            x, n, asProgressive(raw));
                        const bool strict = d.observable
                            && d.witnesses8 >= 3
                            && d.localRatio <= 1.20;
                        if (strict)
                        {
                            if (cls == "correct") ++s.strictCorrectFlagged;
                            if (cls == "high"
                                && std::string(classify(d.lowHz, f0)) == "correct")
                                ++s.strictHighCorrected;
                            continue;
                        }

                        const bool ambiguous = d.observable
                            && d.witnesses8 == 2
                            && d.localRatio <= 0.85;
                        if (ambiguous)
                        {
                            if (cls == "correct") ++s.ambiguousCorrect;
                            else if (cls == "high") ++s.ambiguousHigh;
                            else ++s.ambiguousOther;
                            continue;
                        }

                        if (cls == "correct") ++s.publishCorrect;
                        else if (cls == "high") ++s.publishHigh;
                        else ++s.publishOther;
                    }
                }

    for (std::size_t ci = 0; ci < cps.size(); ++ci)
    {
        const auto& s = stats[ci];
        std::cout << "AMBIGUITY_VETO"
                  << " set=" << name
                  << " samples=" << cps[ci]
                  << " ms=" << std::fixed << std::setprecision(4)
                  << (1000.0 * cps[ci] / sr)
                  << " cases=" << s.cases
                  << " raw_correct=" << s.rawCorrect
                  << " raw_high=" << s.rawHigh
                  << " raw_other=" << s.rawOther
                  << " strict_correct_flagged=" << s.strictCorrectFlagged
                  << " strict_high_corrected=" << s.strictHighCorrected
                  << " ambiguous_correct=" << s.ambiguousCorrect
                  << " ambiguous_high=" << s.ambiguousHigh
                  << " ambiguous_other=" << s.ambiguousOther
                  << " publish_correct=" << s.publishCorrect
                  << " publish_high=" << s.publishHigh
                  << " publish_other=" << s.publishOther
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
    runVetoSet("a", a);
    runVetoSet("b", b);
    runVetoSet("c", c);
    return 0;
}
