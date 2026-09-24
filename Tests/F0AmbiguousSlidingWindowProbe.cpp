#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "F0AmbiguousTransitionBoundProbe.cpp"

namespace
{
ExtendedSignal slice32(const LongSignal& x, int start)
{
    ExtendedSignal y {};
    for (int n = 0; n < extendedMaxSamples; ++n)
        y[static_cast<std::size_t>(n)] = x[static_cast<std::size_t>(start + n)];
    return y;
}

template <std::size_t N>
void runSliding(const char* setName, const std::array<std::uint32_t, N>& seeds)
{
    constexpr double truth = 246.9417;
    constexpr double snr = 3.0;
    constexpr std::array<int, 5> ends {1536, 1920, 2304, 2688, 3072};
    const auto& profile = strongSecondProfile();
    int tracked = 0;
    int resolved40 = 0, resolved48 = 0, resolved56 = 0, resolved64 = 0;
    int unresolved64 = 0;

    for (auto seed : seeds)
    {
        const auto mixed = seed ^ static_cast<std::uint32_t>(truth * 97.0);
        const auto x = makeLongPrefixStableVoiceLike(profile, truth, snr, mixed);
        bool isTracked = false;
        bool resolved = false;
        int resolvedEnd = 0;

        for (int end : ends)
        {
            const int start = end - extendedMaxSamples;
            const auto w = slice32(x, start);
            const auto raw = estimateRawNoLower(w, extendedMaxSamples);
            const auto d = measureExtendedExclusiveLowerFamily(
                w, extendedMaxSamples, asProgressive(raw));
            const std::string rawClass = raw.valid ? classify(raw.hz, truth) : "invalid";
            const bool strict = d.observable && d.witnesses8 >= 3 && d.localRatio <= 1.20;
            const bool strictCorrect = strict
                && std::string(classify(d.lowHz, truth)) == "correct";
            const bool ambiguous = d.observable && d.witnesses8 == 2 && d.localRatio <= 0.85;
            const bool rawCorrect = raw.valid && rawClass == "correct";

            if (end == 1536 && rawClass == "high" && ambiguous)
            {
                isTracked = true;
                ++tracked;
            }

            if (isTracked)
            {
                std::cout << std::fixed << std::setprecision(6)
                          << "SLIDING_AMBIGUOUS"
                          << " set=" << setName
                          << " seed=" << seed
                          << " start=" << start
                          << " end=" << end
                          << " end_ms=" << (1000.0 * end / sr)
                          << " raw_hz=" << raw.hz
                          << " raw_class=" << rawClass
                          << " low_hz=" << d.lowHz
                          << " local_ratio=" << d.localRatio
                          << " witnesses8=" << d.witnesses8
                          << " ambiguous=" << (ambiguous ? 1 : 0)
                          << " strict=" << (strict ? 1 : 0)
                          << '\n';
            }

            if (isTracked && !resolved && (rawCorrect || strictCorrect))
            {
                resolved = true;
                resolvedEnd = end;
            }
        }

        if (isTracked)
        {
            if (!resolved) ++unresolved64;
            else if (resolvedEnd <= 1920) ++resolved40;
            else if (resolvedEnd <= 2304) ++resolved48;
            else if (resolvedEnd <= 2688) ++resolved56;
            else ++resolved64;
        }
    }

    std::cout << "SLIDING_BOUND_SUMMARY"
              << " set=" << setName
              << " tracked=" << tracked
              << " resolved_by_40=" << resolved40
              << " resolved_by_48=" << resolved48
              << " resolved_by_56=" << resolved56
              << " resolved_by_64=" << resolved64
              << " unresolved_at_64=" << unresolved64
              << '\n';
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
    runSliding("a", a);
    runSliding("b", b);
    runSliding("c", c);
    return 0;
}
