#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "../Source/F0WholeNoteDetectorV1.h"
#include "F0WholeNoteExtendedEightProbe.cpp"

namespace
{
constexpr std::array<double, 12> sourceFrequencies {
    110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
    246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
};
constexpr std::array<double, 3> sourceSnrs { 18.0, 9.0, 3.0 };
constexpr std::array<std::uint32_t, 8> sourceSeeds {
    0x0d95748fu, 0x728eb658u, 0x718bcd58u, 0x82154aeeu,
    0x7b54a41du, 0xc25a59b5u, 0x9c30d539u, 0x2af26013u
};
constexpr std::array<int, 3> checkpoints { 1024, 1280, 1536 };

struct Stats
{
    int cases = 0;
    int valid = 0;
    int correct = 0;
    int high = 0;
    int low = 0;
    int otherWrong = 0;
    int exclusiveCorrections = 0;
};
}

int main()
{
    F0WholeNoteDetectorV1 detector;
    detector.prepare(48000.0, 55.0, 1600.0);

    std::array<Stats, checkpoints.size()> stats {};
    std::array<float, 1536> frame {};

    for (const auto& profile : profiles)
        for (double f0 : sourceFrequencies)
            for (double snr : sourceSnrs)
                for (auto seed : sourceSeeds)
                {
                    const auto mixedSeed = seed
                        ^ static_cast<std::uint32_t>(f0 * 97.0);
                    const auto x = makePrefixStableExtendedVoiceLike(
                        profile, f0, snr, mixedSeed);
                    for (int n = 0; n < 1536; ++n)
                        frame[static_cast<std::size_t>(n)] =
                            static_cast<float>(x[static_cast<std::size_t>(n)]);

                    for (std::size_t i = 0; i < checkpoints.size(); ++i)
                    {
                        auto& s = stats[i];
                        ++s.cases;
                        const auto a = detector.analyse(frame.data(), checkpoints[i]);
                        if (!a.valid)
                            continue;
                        ++s.valid;
                        const double error = std::abs(1200.0 * std::log2(a.hz / f0));
                        if (error <= 100.0) ++s.correct;
                        else if (a.hz > 1.5 * f0) ++s.high;
                        else if (a.hz < 0.75 * f0) ++s.low;
                        else ++s.otherWrong;
                        if (a.exclusiveLowerWitness)
                            ++s.exclusiveCorrections;
                    }
                }

    bool safe = true;
    for (std::size_t i = 0; i < checkpoints.size(); ++i)
    {
        const auto& s = stats[i];
        std::cout << "SOURCE_DETECTOR_CANDIDATE"
                  << " samples=" << checkpoints[i]
                  << " ms=" << std::fixed << std::setprecision(4)
                  << (1000.0 * static_cast<double>(checkpoints[i]) / 48000.0)
                  << " cases=" << s.cases
                  << " valid=" << s.valid
                  << " correct=" << s.correct
                  << " high=" << s.high
                  << " low=" << s.low
                  << " other_wrong=" << s.otherWrong
                  << " exclusive=" << s.exclusiveCorrections
                  << '\n';
        if (s.low != 0)
            safe = false;
    }

    // The fresh holdout established that by 32 ms all 1440 cases in this
    // corpus should be in the correct family with the frozen strict witness.
    if (stats.back().valid != 1440
        || stats.back().correct != 1440
        || stats.back().high != 0
        || stats.back().low != 0
        || stats.back().otherWrong != 0)
        safe = false;

    std::cout << "SOURCE_DETECTOR_CANDIDATE_SAFE="
              << (safe ? "PASS" : "FAIL") << '\n';
    return 0;
}
