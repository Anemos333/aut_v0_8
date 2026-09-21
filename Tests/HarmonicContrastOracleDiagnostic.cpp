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
    return 1200.0 * std::log2(measured / target);
}

struct Contrast
{
    const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& data;
    std::array<double, n> hann {};
    double signalEnergy = 0.0;
    double windowEnergy = 0.0;

    explicit Contrast(
        const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& input)
        : data(input)
    {
        for (int i = 0; i < n; ++i)
        {
            hann[static_cast<std::size_t>(i)] =
                0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i)
                                    / static_cast<double>(n - 1));
            const double x =
                static_cast<double>(data[static_cast<std::size_t>(i)])
                * hann[static_cast<std::size_t>(i)];
            signalEnergy += x * x;
            windowEnergy += hann[static_cast<std::size_t>(i)]
                          * hann[static_cast<std::size_t>(i)];
        }
    }

    float line(double hz) const noexcept
    {
        if (!(hz > 0.0) || hz >= 0.45 * sr)
            return 0.0f;

        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double x =
                static_cast<double>(data[static_cast<std::size_t>(i)])
                * hann[static_cast<std::size_t>(i)];
            const double p = 2.0 * pi * hz * static_cast<double>(i) / sr;
            re += x * std::cos(p);
            im -= x * std::sin(p);
        }

        const double norm = std::max(1.0e-20, signalEnergy * windowEnergy);
        return std::clamp(static_cast<float>(
            std::sqrt(2.0 * (re * re + im * im) / norm)), 0.0f, 1.0f);
    }

    float harmonic(double f0) const noexcept
    {
        if (!(f0 >= 55.0 && f0 <= 1600.0))
            return 0.0f;

        float hs = line(f0);
        float is = 0.0f;
        float hw = 1.0f;
        float iw = 0.0f;

        for (int h = 2; h <= 6; ++h)
        {
            const double hf = f0 * static_cast<double>(h);
            if (hf >= 0.45 * sr)
                break;

            const float w = 1.0f / std::sqrt(static_cast<float>(h));
            hs += w * line(hf);
            hw += w;

            const double inter = f0 * (static_cast<double>(h) - 0.5);
            if (inter < 0.45 * sr)
            {
                is += w * line(inter);
                iw += w;
            }
        }

        const float hm = hs / std::max(1.0e-6f, hw);
        const float im = iw > 1.0e-6f ? is / iw : 0.0f;
        const float raw = hm - 0.78f * im;
        const float t = std::clamp((raw - 0.025f) / 0.275f, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
};

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

    const Contrast residual(ws.voiceResidualFrame);
    const Contrast source(ws.frame);

    const auto scan = [&](const Contrast& c)
    {
        double bestHz = target;
        float best = -1.0f;
        for (int cent = -120; cent <= 120; ++cent)
        {
            const double hz = target * std::exp2(static_cast<double>(cent) / 1200.0);
            const float score = c.harmonic(hz);
            if (score > best)
            {
                best = score;
                bestHz = hz;
            }
        }
        return std::pair<double,float>{bestHz,best};
    };

    const auto [resHz,resScore] = scan(residual);
    const auto [srcHz,srcScore] = scan(source);

    const float resTrue = residual.harmonic(target);
    const float resHalf = residual.harmonic(target * 0.5);
    const float resDouble = target * 2.0 <= 1600.0
        ? residual.harmonic(target * 2.0) : 0.0f;
    const float srcTrue = source.harmonic(target);
    const float srcHalf = source.harmonic(target * 0.5);
    const float srcDouble = target * 2.0 <= 1600.0
        ? source.harmonic(target * 2.0) : 0.0f;

    std::cout << std::fixed << std::setprecision(5)
              << "CONTRAST_ORACLE"
              << " hz=" << target
              << " snr=" << snr
              << " residual_best=" << resHz
              << " residual_cents=" << cents(resHz,target)
              << " residual_score=" << resScore
              << " residual_true=" << resTrue
              << " residual_half=" << resHalf
              << " residual_double=" << resDouble
              << " source_best=" << srcHz
              << " source_cents=" << cents(srcHz,target)
              << " source_score=" << srcScore
              << " source_true=" << srcTrue
              << " source_half=" << srcHalf
              << " source_double=" << srcDouble
              << " residual_pass=" << (std::abs(cents(resHz,target)) <= 1.5 ? 1 : 0)
              << " source_pass=" << (std::abs(cents(srcHz,target)) <= 1.5 ? 1 : 0)
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
