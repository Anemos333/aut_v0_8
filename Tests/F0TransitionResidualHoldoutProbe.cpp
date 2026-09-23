#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

#define main progressive_probe_reference_main
#include "F0WholeNoteProgressiveProbe.cpp"
#undef main

namespace
{
template <std::size_t N>
double residualScore(const std::array<double, N>& x,
                     int endExclusive,
                     int window,
                     double stableHz)
{
    if (!(stableHz > 0.0)) return std::numeric_limits<double>::infinity();
    const int lag = std::max(2, static_cast<int>(std::lround(sr / stableHz)));
    const int begin = endExclusive - window;
    if (begin - lag < 0 || endExclusive > static_cast<int>(N))
        return std::numeric_limits<double>::infinity();
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

double percentile(std::vector<double> v, double p)
{
    if (v.empty()) return -1.0;
    std::sort(v.begin(), v.end());
    const std::size_t i = static_cast<std::size_t>(
        std::floor(p * static_cast<double>(v.size() - 1)));
    return v[i];
}
}

int main()
{
    constexpr std::array<double, 12> frequencies {
        110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
        246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
    };
    constexpr std::array<double, 3> snrs { 18.0, 9.0, 3.0 };
    constexpr std::array<std::uint32_t, 8> seeds {
        0x0f1e2d3cu, 0x4b5a6978u, 0x8796a5b4u, 0xc3d2e1f0u,
        0x55aa33ccu, 0xa5c35a3cu, 0x7f4a7c15u, 0x6c8e9cf5u
    };

    std::array<std::vector<double>, 3> baseScores;
    std::array<std::vector<double>, 3> currentScores;
    std::array<std::vector<double>, 3> ratios;
    std::array<std::vector<double>, 3> deltas;
    std::array<int, 3> cases {};
    std::array<int, 3> priorWrong {};

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (std::size_t si = 0; si < snrs.size(); ++si)
                for (auto seed : seeds)
                {
                    const auto x = makeProgressiveVoiceLike(
                        profile, f0, snrs[si],
                        seed ^ static_cast<std::uint32_t>(f0 * 97.0));
                    auto prior = estimateProgressive(x, 896);
                    if (!prior.valid || std::abs(cents(prior.hz, f0)) > 100.0)
                    {
                        ++priorWrong[si];
                        continue;
                    }
                    const double a = residualScore(x, 896, 448, prior.hz);
                    const double b = residualScore(x, 1024, 448, prior.hz);
                    if (!std::isfinite(a) || !std::isfinite(b)) continue;
                    ++cases[si];
                    baseScores[si].push_back(a);
                    currentScores[si].push_back(b);
                    ratios[si].push_back(b / std::max(1.0e-9, a));
                    deltas[si].push_back(b - a);
                }

    std::cout << std::fixed << std::setprecision(6);
    for (std::size_t si = 0; si < snrs.size(); ++si)
    {
        std::cout << "RESIDUAL_HOLDOUT snr=" << snrs[si]
                  << " cases=" << cases[si]
                  << " prior_wrong=" << priorWrong[si]
                  << " base_p50=" << percentile(baseScores[si], 0.50)
                  << " base_p95=" << percentile(baseScores[si], 0.95)
                  << " base_max=" << percentile(baseScores[si], 1.00)
                  << " current_p50=" << percentile(currentScores[si], 0.50)
                  << " current_p95=" << percentile(currentScores[si], 0.95)
                  << " current_max=" << percentile(currentScores[si], 1.00)
                  << " ratio_p95=" << percentile(ratios[si], 0.95)
                  << " ratio_p99=" << percentile(ratios[si], 0.99)
                  << " ratio_max=" << percentile(ratios[si], 1.00)
                  << " delta_p95=" << percentile(deltas[si], 0.95)
                  << " delta_p99=" << percentile(deltas[si], 0.99)
                  << " delta_max=" << percentile(deltas[si], 1.00)
                  << '\n';
    }
    return 0;
}
