#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int n = 480;

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
    return measured > 0.0
        ? 1200.0 * std::log2(measured / target)
        : std::numeric_limits<double>::infinity();
}

template <typename Array>
double refine(const Array& frame, double target)
{
    const double exactTau = sr / target;
    if (exactTau >= n - 8)
        return 0.0;

    const int centreTau = static_cast<int>(std::lround(exactTau));

    const auto corr = [&](int lag)
    {
        if (lag <= 0 || lag >= n - 8)
            return -1.0;
        double c = 0.0, ea = 0.0, eb = 0.0;
        const int overlap = n - lag;
        for (int i = 0; i < overlap; ++i)
        {
            const double a = frame[static_cast<std::size_t>(i)];
            const double b = frame[static_cast<std::size_t>(i + lag)];
            c += a * b;
            ea += a * a;
            eb += b * b;
        }
        return c / std::sqrt(std::max(1.0e-20, ea * eb));
    };

    int bestTau = centreTau;
    double best = corr(bestTau);
    for (int offset = -5; offset <= 5; ++offset)
    {
        const int tau = centreTau + offset;
        const double value = corr(tau);
        if (value > best)
        {
            best = value;
            bestTau = tau;
        }
    }

    double refinedTau = static_cast<double>(bestTau);
    if (bestTau > 1 && bestTau < n - 9)
    {
        const double left = corr(bestTau - 1);
        const double centre = corr(bestTau);
        const double right = corr(bestTau + 1);
        const double den = left - 2.0 * centre + right;
        if (std::abs(den) > 1.0e-12)
            refinedTau += std::clamp(
                0.5 * (left - right) / den, -0.75, 0.75);
    }

    return sr / refinedTau;
}

void run(double target, double snr, std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker t;
    t.prepare(sr);
    t.setRange(55.0f, 1600.0f);

    Rng rng { seed };
    double phase = 0.0;
    const double amp = std::pow(10.0, -24.0 / 20.0);
    const double noise = amp / std::pow(10.0, snr / 20.0);
    ModernPitchEngine::PitchObservation o;

    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * target / sr;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;
        const float x = static_cast<float>(amp) * voice(phase)
                      + static_cast<float>(noise) * rng.next();
        t.processSample(x, o);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws {};
    static_cast<void>(t.measureCoordinate(
        t.fullRateRing_, t.fullRateWritePosition_, t.fullRateAvailableSamples_,
        sr, 55.0f, 1600.0f, n, ws));

    const double sourceHz = refine(ws.frame, target);
    const double residualHz = refine(ws.voiceResidualFrame, target);

    std::cout << std::fixed << std::setprecision(5)
              << "LOCAL_CORR_ORACLE"
              << " hz=" << target
              << " snr=" << snr
              << " source=" << sourceHz
              << " source_cents=" << cents(sourceHz,target)
              << " residual=" << residualHz
              << " residual_cents=" << cents(residualHz,target)
              << " source_pass=" << ((sourceHz>0.0 && std::abs(cents(sourceHz,target))<=1.5)?1:0)
              << " residual_pass=" << ((residualHz>0.0 && std::abs(cents(residualHz,target))<=1.5)?1:0)
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

    for(double hz:frequencies)
        for(double snr:snrs)
            for(auto seed:seeds)
                run(hz,snr,seed);
}
