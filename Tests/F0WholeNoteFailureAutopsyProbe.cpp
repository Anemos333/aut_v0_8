#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "../Source/F0WholeNoteDetectorV1.h"
#include "F0WholeNoteExclusiveConjunctionHoldoutProbe.cpp"

namespace
{
constexpr std::array<std::uint32_t, 8> sourceHoldoutSeeds {
    0x0d95748fu, 0x728eb658u, 0x718bcd58u, 0x82154aeeu,
    0x7b54a41du, 0xc25a59b5u, 0x9c30d539u, 0x2af26013u
};

constexpr std::array<std::uint32_t, 8> conjunctionSeeds {
    0xa4093822u, 0x299f31d0u, 0x082efa98u, 0xec4e6c89u,
    0x452821e6u, 0x38d01377u, 0xbe5466cfu, 0x34e90c6cu
};

constexpr std::array<double, 12> auditFrequencies {
    110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
    246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
};
constexpr std::array<double, 3> auditSnrs { 18.0, 9.0, 3.0 };
constexpr std::array<int, 3> auditCheckpoints { 1024, 1280, 1536 };

const char* errorClass(double hz, double truth) noexcept
{
    const double ae = std::abs(1200.0 * std::log2(hz / truth));
    if (ae <= 100.0) return "correct";
    if (hz > 1.5 * truth) return "high";
    if (hz < 0.75 * truth) return "low";
    return "other";
}

template <std::size_t N>
void runSeedSet(const char* setName,
                const std::array<std::uint32_t, N>& seeds,
                F0WholeNoteDetectorV1& detector)
{
    std::array<float, 1536> frame {};
    int wrong = 0;
    int high = 0;
    int low = 0;
    int other = 0;

    for (const auto& profile : profiles)
        for (double f0 : auditFrequencies)
            for (double snr : auditSnrs)
                for (auto seed : seeds)
                {
                    const auto mixedSeed = seed
                        ^ static_cast<std::uint32_t>(f0 * 97.0);
                    const auto x = makePrefixStableExtendedVoiceLike(
                        profile, f0, snr, mixedSeed);
                    for (int n = 0; n < 1536; ++n)
                        frame[static_cast<std::size_t>(n)] =
                            static_cast<float>(x[static_cast<std::size_t>(n)]);

                    for (int sampleCount : auditCheckpoints)
                    {
                        const auto a = detector.analyse(frame.data(), sampleCount);
                        if (!a.valid)
                            continue;

                        const char* cls = errorClass(a.hz, f0);
                        if (std::string(cls) == "correct")
                            continue;

                        ++wrong;
                        if (std::string(cls) == "high") ++high;
                        else if (std::string(cls) == "low") ++low;
                        else ++other;

                        auto raw = estimateExtended(x, sampleCount);
                        if (sampleCount >= 896)
                            raw = applyExtendedLongConsensus(x, sampleCount, raw);
                        const auto d = measureExtendedExclusiveLowerFamily(
                            x, sampleCount, raw);

                        const double centsError =
                            1200.0 * std::log2(a.hz / f0);
                        const double ratio = a.hz / f0;

                        std::cout << std::fixed << std::setprecision(6)
                                  << "F0_FAILURE"
                                  << " set=" << setName
                                  << " profile=" << profile.name
                                  << " truth=" << f0
                                  << " snr_db=" << snr
                                  << " seed=" << seed
                                  << " samples=" << sampleCount
                                  << " ms=" << (1000.0 * sampleCount / sr)
                                  << " hz=" << a.hz
                                  << " ratio=" << ratio
                                  << " cents=" << centsError
                                  << " class=" << cls
                                  << " periodicity=" << a.periodicity
                                  << " lag=" << a.lag
                                  << " primitive_lowered=" << (a.primitiveLowered ? 1 : 0)
                                  << " exclusive_lower=" << (a.exclusiveLowerWitness ? 1 : 0)
                                  << " raw_hz=" << raw.hz
                                  << " raw_primitive_ratio=" << raw.primitiveRatio
                                  << " raw_lowered=" << (raw.loweredPrimitive ? 1 : 0)
                                  << " lower_observable=" << (d.observable ? 1 : 0)
                                  << " lower_hz=" << d.lowHz
                                  << " lower_local_ratio=" << d.localRatio
                                  << " lower_w8=" << d.witnesses8
                                  << " lower_w12=" << d.witnesses12
                                  << " fscale=" << profile.fundamentalScale
                                  << " second_scale=" << profile.secondScale
                                  << " breath=" << profile.breath
                                  << " jitter=" << profile.jitter
                                  << " shimmer=" << profile.shimmer
                                  << " attack_ms=" << profile.attackMs
                                  << '\n';
                    }
                }

    std::cout << "F0_FAILURE_SUMMARY"
              << " set=" << setName
              << " wrong=" << wrong
              << " high=" << high
              << " low=" << low
              << " other=" << other
              << '\n';
}
}

int main()
{
    F0WholeNoteDetectorV1 detector;
    detector.prepare(48000.0, 55.0, 1600.0);

    runSeedSet("source_holdout", sourceHoldoutSeeds, detector);
    runSeedSet("conjunction_holdout", conjunctionSeeds, detector);
    return 0;
}
