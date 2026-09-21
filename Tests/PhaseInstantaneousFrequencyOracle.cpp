#include <JuceHeader.h>
#define private public
#include "../Source/SingleWetSpectralRenderer.h"
#undef private

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;

struct Rng
{
    std::uint32_t s;
    float next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xffffu) / 32767.5f - 1.0f;
    }
};

float voice(double p) noexcept
{
    return static_cast<float>(
          0.72 * std::sin(p)
        + 0.34 * std::sin(2.0 * p + 0.17)
        + 0.21 * std::sin(3.0 * p - 0.31)
        + 0.13 * std::sin(4.0 * p + 0.49)
        + 0.08 * std::sin(5.0 * p - 0.63));
}

double cents(double measured, double target)
{
    return 1200.0 * std::log2(measured / target);
}

void run(double target, double snr, std::uint32_t seed)
{
    SingleWetSpectralRenderer r;
    r.prepare(sr, 256);

    Rng rng { seed };
    double phase = 0.0;
    const double amp = std::pow(10.0, -24.0 / 20.0);
    const double noise = amp / std::pow(10.0, snr / 20.0);

    for (int i = 0; i < 480; ++i)
    {
        phase += 2.0 * pi * target / sr;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;

        const float x = static_cast<float>(amp) * voice(phase)
                      + static_cast<float>(noise) * rng.next();
        static_cast<void>(r.processSample(x, 0.0, 0.0f));
    }

    struct Estimate { int harmonic; int bin; double hz; double cents; float magnitude; };
    std::vector<Estimate> estimates;

    for (int h = 1; h <= 6; ++h)
    {
        const double harmonicHz = target * static_cast<double>(h);
        if (harmonicHz >= 0.45 * sr)
            break;

        const double nominalBin = harmonicHz * 256.0 / sr;
        int bin = static_cast<int>(std::lround(nominalBin));
        bin = std::clamp(bin, 1, 127);

        const double trueBin = r.trueSourceBins_[static_cast<std::size_t>(bin)];
        const double estimateHz = trueBin * sr / 256.0 / static_cast<double>(h);
        estimates.push_back({
            h,
            bin,
            estimateHz,
            cents(estimateHz, target),
            r.magnitudes_[static_cast<std::size_t>(bin)]
        });
    }

    double weightedHz = 0.0;
    double weight = 0.0;
    std::vector<double> centsValues;
    for (const auto& e : estimates)
    {
        const double w = std::max(0.0f, e.magnitude);
        weightedHz += w * e.hz;
        weight += w;
        centsValues.push_back(e.cents);
    }
    const double weighted = weight > 0.0 ? weightedHz / weight : 0.0;
    std::sort(centsValues.begin(), centsValues.end());
    const double medianCents = centsValues.empty()
        ? 0.0
        : centsValues[centsValues.size() / 2];

    std::cout << std::fixed << std::setprecision(5)
              << "PHASE_IF_ORACLE"
              << " hz=" << target
              << " snr=" << snr
              << " weighted_hz=" << weighted
              << " weighted_cents=" << cents(weighted, target)
              << " median_cents=" << medianCents;

    for (const auto& e : estimates)
    {
        std::cout << " h" << e.harmonic << "_bin=" << e.bin
                  << " h" << e.harmonic << "_hz=" << e.hz
                  << " h" << e.harmonic << "_cents=" << e.cents
                  << " h" << e.harmonic << "_mag=" << e.magnitude;
    }

    std::cout << " weighted_pass="
              << ((weighted > 0.0 && std::abs(cents(weighted,target)) <= 1.5) ? 1 : 0)
              << '\n';
}
}

int main()
{
    constexpr std::array<double, 6> frequencies {
        82.4069, 110.0, 220.0, 440.0, 660.0, 880.0
    };
    constexpr std::array<double, 3> snrs { 12.0, 6.0, 3.0 };
    constexpr std::array<std::uint32_t, 3> seeds {
        0x1234567u, 0x51f15e5du, 0x9e3779b9u
    };

    for (double hz : frequencies)
        for (double snr : snrs)
            for (auto seed : seeds)
                run(hz, snr, seed);
}
