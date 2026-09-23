#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#define main progressive_probe_reference_main
#include "F0WholeNoteProgressiveProbe.cpp"
#undef main

namespace
{
constexpr std::array<double, 12> kFrequencies {
    110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
    246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
};
constexpr std::array<double, 3> kSnrs { 18.0, 9.0, 3.0 };
constexpr std::array<int, 6> kCheckpoints { 448, 576, 672, 768, 896, 1024 };

constexpr std::array<std::array<std::uint32_t, 6>, 6> kSeedSets {{
    {{ 0x6c8e9cf5u, 0x5a827999u, 0x3c6ef372u, 0xbb67ae85u, 0xa54ff53au, 0x510e527fu }},
    {{ 0x8f1bbcdcu, 0xca62c1d6u, 0x9b05688cu, 0x1f83d9abu, 0x4a7484aau, 0x3f84d5b5u }},
    {{ 0x5be0cd19u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u }},
    {{ 0xcbbb9d5du, 0x629a292au, 0x9159015au, 0x152fecd8u, 0x67332667u, 0x8eb44a87u }},
    {{ 0xd1310ba6u, 0x98dfb5acu, 0x2ffd72dbu, 0xd01adfb7u, 0xb8e1afedu, 0x6a267e96u }},
    {{ 0xba7c9045u, 0xf12c7f99u, 0x24a19947u, 0xb3916cf7u, 0x0801f2e2u, 0x858efc16u }}
}};

bool acceptedFamilyAt(const std::array<double, progressiveMaxSamples>& x,
                      int sampleCount,
                      double truth,
                      ProgressiveEstimate* outEstimate = nullptr)
{
    auto e = estimateProgressive(x, sampleCount);
    if (!e.valid)
        return false;

    if (sampleCount == 448
        && (!shouldPublishInsideNormalWindow(x, e)
            || hasUnresolvedPrimitiveAtNormalWindow(x, e)))
        return false;

    if (sampleCount >= 896)
        e = applyLongPrimitiveConsensus(x, sampleCount, e);

    if (outEstimate != nullptr)
        *outEstimate = e;
    return e.valid && std::abs(cents(e.hz, truth)) <= 100.0;
}

void printCheckpoint(const std::array<double, progressiveMaxSamples>& x,
                     int sampleCount,
                     double truth)
{
    const auto raw = estimateProgressive(x, sampleCount);
    auto final = raw;
    const bool normalAllowed = sampleCount != 448
        || (raw.valid && shouldPublishInsideNormalWindow(x, raw));
    const bool primitiveHeld = sampleCount == 448
        && raw.valid && hasUnresolvedPrimitiveAtNormalWindow(x, raw);

    if (sampleCount >= 896 && final.valid)
        final = applyLongPrimitiveConsensus(x, sampleCount, final);

    const auto alt = raw.valid
        ? measureCycleAlternation(x, sampleCount, raw.lag)
        : AlternationWitness {};

    std::cout << std::fixed << std::setprecision(4)
              << " NEVER_STEP samples=" << sampleCount
              << " ms=" << (1000.0 * sampleCount / sr)
              << " raw_valid=" << (raw.valid ? 1 : 0)
              << " raw_hz=" << raw.hz
              << " raw_cents=" << (raw.valid ? cents(raw.hz, truth) : 0.0)
              << " periodicity=" << raw.periodicity
              << " primitive_ratio=" << raw.primitiveRatio
              << " lowered=" << (raw.loweredPrimitive ? 1 : 0)
              << " alt_observable=" << (alt.observable ? 1 : 0)
              << " alt_score=" << alt.score
              << " alt_significance=" << alt.significance
              << " normal_allowed=" << (normalAllowed ? 1 : 0)
              << " primitive_held=" << (primitiveHeld ? 1 : 0)
              << " final_valid=" << (final.valid ? 1 : 0)
              << " final_hz=" << final.hz
              << " final_cents=" << (final.valid ? cents(final.hz, truth) : 0.0)
              << '\n';
}
}

int main()
{
    int total = 0;
    int never = 0;

    for (std::size_t setIndex = 0; setIndex < kSeedSets.size(); ++setIndex)
        for (const auto& profile : profiles)
            for (double f0 : kFrequencies)
                for (double snr : kSnrs)
                    for (auto seed : kSeedSets[setIndex])
                    {
                        ++total;
                        const auto mixedSeed =
                            seed ^ static_cast<std::uint32_t>(f0 * 97.0);
                        const auto x = makeProgressiveVoiceLike(
                            profile, f0, snr, mixedSeed);

                        bool familyFound = false;
                        for (int sampleCount : kCheckpoints)
                            if (acceptedFamilyAt(x, sampleCount, f0))
                            {
                                familyFound = true;
                                break;
                            }

                        if (familyFound)
                            continue;

                        ++never;
                        std::cout << "NEVER_FAMILY_CASE"
                                  << " set=" << setIndex
                                  << " profile=" << profile.name
                                  << " hz=" << f0
                                  << " snr=" << snr
                                  << " seed=" << seed
                                  << " mixed_seed=" << mixedSeed
                                  << '\n';
                        for (int sampleCount : kCheckpoints)
                            printCheckpoint(x, sampleCount, f0);
                    }

    std::cout << "NEVER_FAMILY_SUMMARY total=" << total
              << " never=" << never << '\n';
    return 0;
}
