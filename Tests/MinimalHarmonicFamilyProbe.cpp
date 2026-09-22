#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr double twoPi = 2.0 * pi;
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

enum class Kind { normal, strongSecond, missingFundamental, onsetBurst, whiteNoise };

const char* name(Kind k) noexcept
{
    switch (k)
    {
        case Kind::normal: return "normal";
        case Kind::strongSecond: return "strong_second";
        case Kind::missingFundamental: return "missing_fundamental";
        case Kind::onsetBurst: return "onset_burst";
        case Kind::whiteNoise: return "white_noise";
    }
    return "unknown";
}

float body(Kind kind, double p) noexcept
{
    if (kind == Kind::strongSecond)
        return static_cast<float>(
              0.16 * std::sin(p)
            + 1.00 * std::sin(2.0 * p + 0.17)
            + 0.38 * std::sin(3.0 * p - 0.31)
            + 0.18 * std::sin(4.0 * p + 0.49)
            + 0.09 * std::sin(5.0 * p - 0.63));

    if (kind == Kind::missingFundamental)
        return static_cast<float>(
              0.90 * std::sin(2.0 * p + 0.17)
            + 0.58 * std::sin(3.0 * p - 0.31)
            + 0.34 * std::sin(4.0 * p + 0.49)
            + 0.20 * std::sin(5.0 * p - 0.63)
            + 0.12 * std::sin(6.0 * p + 0.27));

    return static_cast<float>(
          0.72 * std::sin(p)
        + 0.34 * std::sin(2.0 * p + 0.17)
        + 0.21 * std::sin(3.0 * p - 0.31)
        + 0.13 * std::sin(4.0 * p + 0.49)
        + 0.08 * std::sin(5.0 * p - 0.63));
}

float clamp01(float x) noexcept { return std::clamp(x, 0.0f, 1.0f); }

float smoothStep(float lo, float hi, float x) noexcept
{
    if (hi <= lo)
        return x >= hi ? 1.0f : 0.0f;
    const float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

struct Probe
{
    std::array<float, n> residual {};
    std::array<double, n> hann {};
    double signalEnergy = 0.0;
    double windowEnergy = 0.0;

    explicit Probe(const std::array<float, n>& input)
    {
        std::array<float, n> frame = input;
        double mean = 0.0;
        for (float x : frame)
            mean += static_cast<double>(x);
        mean /= static_cast<double>(n);
        for (auto& x : frame)
            x -= static_cast<float>(mean);

        double num = 0.0;
        double den = 0.0;
        for (int i = 1; i < n; ++i)
        {
            const double cur = frame[static_cast<std::size_t>(i)];
            const double prev = frame[static_cast<std::size_t>(i - 1)];
            num += cur * prev;
            den += prev * prev;
        }
        const float predictor = static_cast<float>(
            std::clamp(num / std::max(1.0e-20, den), -0.92, 0.92));

        residual[0] = frame[0];
        for (int i = 1; i < n; ++i)
            residual[static_cast<std::size_t>(i)] =
                frame[static_cast<std::size_t>(i)]
                - predictor * frame[static_cast<std::size_t>(i - 1)];

        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(
                twoPi * static_cast<double>(i) / static_cast<double>(n - 1));
            hann[static_cast<std::size_t>(i)] = w;
            const double y = static_cast<double>(
                residual[static_cast<std::size_t>(i)]) * w;
            signalEnergy += y * y;
            windowEnergy += w * w;
        }
    }

    float line(double hz) const noexcept
    {
        if (!(hz > 0.0) || hz >= 0.48 * sr)
            return 0.0f;

        double re = 0.0;
        double im = 0.0;
        const double cps = hz / sr;
        for (int i = 0; i < n; ++i)
        {
            const double sample = static_cast<double>(
                residual[static_cast<std::size_t>(i)])
                * hann[static_cast<std::size_t>(i)];
            const double phase = twoPi * cps * static_cast<double>(i);
            re += sample * std::cos(phase);
            im -= sample * std::sin(phase);
        }
        const double norm = std::max(1.0e-20, signalEnergy * windowEnergy);
        return clamp01(static_cast<float>(
            std::sqrt(2.0 * (re * re + im * im) / norm)));
    }

    float harmonicContrast(double f0) const noexcept
    {
        if (!(f0 > 0.0))
            return 0.0f;

        float harmonicScore = line(f0);
        float interScore = 0.0f;
        float harmonicWeight = 1.0f;
        float interWeight = 0.0f;

        for (int h = 2; h <= 6; ++h)
        {
            const double hf = f0 * static_cast<double>(h);
            if (hf >= 0.45 * sr)
                break;
            const float w = 1.0f / std::sqrt(static_cast<float>(h));
            harmonicScore += w * line(hf);
            harmonicWeight += w;

            const double inter = f0 * (static_cast<double>(h) - 0.5);
            if (inter < 0.45 * sr)
            {
                interScore += w * line(inter);
                interWeight += w;
            }
        }

        const float harmonicMean = harmonicScore / std::max(1.0e-6f, harmonicWeight);
        const float interMean = interWeight > 1.0e-6f ? interScore / interWeight : 0.0f;
        return smoothStep(0.025f, 0.30f, harmonicMean - 0.78f * interMean);
    }
};

std::array<float,n> makeFrame(double f0, double snrDb, Kind kind, std::uint32_t seed)
{
    std::array<float,n> out {};
    Rng rng { seed };
    const double amp = std::pow(10.0, -24.0 / 20.0);
    const double noiseAmp = amp / std::pow(10.0, snrDb / 20.0);
    double phase = 0.0;

    for (int i = 0; i < n; ++i)
    {
        if (kind == Kind::whiteNoise)
        {
            out[static_cast<std::size_t>(i)] =
                static_cast<float>(amp) * rng.next();
            continue;
        }

        phase += twoPi * f0 / sr;
        if (phase >= twoPi)
            phase -= twoPi;

        float x = static_cast<float>(amp) * body(kind, phase)
                + static_cast<float>(noiseAmp) * rng.next();

        if (kind == Kind::onsetBurst && i < static_cast<int>(0.003 * sr))
            x += static_cast<float>(3.5 * amp) * rng.next();

        out[static_cast<std::size_t>(i)] = x;
    }
    return out;
}

void run(double f0, double snrDb, Kind kind, std::uint32_t seed)
{
    const auto frame = makeFrame(f0, snrDb, kind, seed);
    Probe p(frame);

    const auto begin = std::chrono::steady_clock::now();
    const float trueScore = p.harmonicContrast(f0);
    const float doubleScore = p.harmonicContrast(2.0 * f0);
    const float halfScore = f0 >= 110.0 ? p.harmonicContrast(0.5 * f0) : -1.0f;
    const float tripleScore = p.harmonicContrast(3.0 * f0);
    const auto end = std::chrono::steady_clock::now();

    double winnerHz = f0;
    float winner = trueScore;
    auto consider = [&](double hz, float score)
    {
        if (score > winner)
        {
            winner = score;
            winnerHz = hz;
        }
    };
    consider(2.0 * f0, doubleScore);
    if (halfScore >= 0.0f) consider(0.5 * f0, halfScore);
    consider(3.0 * f0, tripleScore);

    const double us = std::chrono::duration<double,std::micro>(end - begin).count();
    const bool trueWins = std::abs(winnerHz - f0) < 1.0e-9;

    std::cout << std::fixed << std::setprecision(6)
              << "HARMONIC_FAMILY_PROBE"
              << " kind=" << name(kind)
              << " hz=" << f0
              << " snr=" << snrDb
              << " seed=" << seed
              << " true=" << trueScore
              << " double=" << doubleScore
              << " half=" << halfScore
              << " triple=" << tripleScore
              << " margin_double=" << (trueScore - doubleScore)
              << " winner_hz=" << winnerHz
              << " true_wins=" << (trueWins ? 1 : 0)
              << " us=" << us
              << "\n";
}
}

int main()
{
    constexpr std::array<double,6> freqs {82.4069,110.0,220.0,440.0,660.0,880.0};
    constexpr std::array<double,4> snrs {12.0,6.0,3.0,0.0};
    constexpr std::array<std::uint32_t,3> seeds {
        0x1234567u,0x51f15e5du,0x9e3779b9u
    };
    constexpr std::array<Kind,4> voiced {
        Kind::normal, Kind::strongSecond, Kind::missingFundamental, Kind::onsetBurst
    };

    for (auto seed : seeds)
        for (auto kind : voiced)
            for (double hz : freqs)
                for (double snr : snrs)
                    run(hz, snr, kind, seed);

    for (auto seed : seeds)
        run(220.0, 0.0, Kind::whiteNoise, seed);
}
