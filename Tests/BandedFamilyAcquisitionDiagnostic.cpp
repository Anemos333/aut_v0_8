#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
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
constexpr int analysisLength = 480;

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
    if (!(measured > 0.0) || !(target > 0.0))
        return std::numeric_limits<double>::infinity();
    return 1200.0 * std::log2(measured / target);
}

struct Candidate
{
    double frequency = 0.0;
    float contrast = 0.0f;
    int band = -1;
    int divisor = 1;
    double parentFrequency = 0.0;
};

void run(double targetHz, double snrDb, std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker tracker;
    tracker.prepare(sr);
    tracker.setRange(55.0f, 1600.0f);

    Rng rng { seed };
    double phase = 0.0;
    const double amp = std::pow(10.0, -24.0 / 20.0);
    const double noiseAmp = amp / std::pow(10.0, snrDb / 20.0);

    ModernPitchEngine::PitchObservation o;
    for (int i = 0; i < analysisLength; ++i)
    {
        phase += 2.0 * pi * targetHz / sr;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;
        const float x = static_cast<float>(amp) * voice(phase)
                      + static_cast<float>(noiseAmp) * rng.next();
        tracker.processSample(x, o);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace ws {};
    static_cast<void>(tracker.measureCoordinate(
        tracker.fullRateRing_,
        tracker.fullRateWritePosition_,
        tracker.fullRateAvailableSamples_,
        sr,
        55.0f,
        1600.0f,
        analysisLength,
        ws));

    const auto sourceCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return -1.0f;
        double corr = 0.0, ea = 0.0, eb = 0.0;
        const int overlap = analysisLength - lag;
        for (int i = 0; i < overlap; ++i)
        {
            const double a = ws.frame[static_cast<std::size_t>(i)];
            const double b = ws.frame[static_cast<std::size_t>(i + lag)];
            corr += a * b;
            ea += a * a;
            eb += b * b;
        }
        const double den = std::sqrt(std::max(1.0e-20, ea * eb));
        return den > 0.0 ? static_cast<float>(corr / den) : -1.0f;
    };

    const auto refineTau = [&](int tau) noexcept
    {
        int bestTau = tau;
        float peak = sourceCorrelation(tau);
        for (int offset = -2; offset <= 2; ++offset)
        {
            const int t = tau + offset;
            const float p = sourceCorrelation(t);
            if (p > peak)
            {
                peak = p;
                bestTau = t;
            }
        }

        double refined = static_cast<double>(bestTau);
        if (bestTau > 2 && bestTau < analysisLength - 9)
        {
            const double left = sourceCorrelation(bestTau - 1);
            const double centre = sourceCorrelation(bestTau);
            const double right = sourceCorrelation(bestTau + 1);
            const double den = left - 2.0 * centre + right;
            if (std::abs(den) > 1.0e-12)
                refined += std::clamp(0.5 * (left - right) / den, -0.75, 0.75);
        }
        return sr / refined;
    };

    std::array<double, analysisLength> hann {};
    double residualSignalEnergy = 0.0;
    double residualWindowEnergy = 0.0;
    for (int i = 0; i < analysisLength; ++i)
    {
        hann[static_cast<std::size_t>(i)] =
            0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(i)
                               / static_cast<double>(analysisLength - 1));
        const double sample =
            static_cast<double>(ws.voiceResidualFrame[static_cast<std::size_t>(i)])
            * hann[static_cast<std::size_t>(i)];
        residualSignalEnergy += sample * sample;
        residualWindowEnergy += hann[static_cast<std::size_t>(i)]
                              * hann[static_cast<std::size_t>(i)];
    }

    const auto lineCoherence = [&](double frequencyHz) noexcept
    {
        if (!(frequencyHz > 0.0) || frequencyHz >= 0.45 * sr)
            return 0.0f;
        double real = 0.0, imag = 0.0;
        for (int i = 0; i < analysisLength; ++i)
        {
            const double sample =
                static_cast<double>(ws.voiceResidualFrame[static_cast<std::size_t>(i)])
                * hann[static_cast<std::size_t>(i)];
            const double phaseNow = 2.0 * pi * frequencyHz
                                  * static_cast<double>(i) / sr;
            real += sample * std::cos(phaseNow);
            imag -= sample * std::sin(phaseNow);
        }
        const double normaliser = std::max(
            1.0e-20, residualSignalEnergy * residualWindowEnergy);
        return std::clamp(static_cast<float>(std::sqrt(
            2.0 * (real * real + imag * imag) / normaliser)), 0.0f, 1.0f);
    };

    const auto harmonicContrast = [&](double fundamentalHz) noexcept
    {
        if (!(fundamentalHz >= 55.0 && fundamentalHz <= 1600.0))
            return 0.0f;

        float harmonicScore = lineCoherence(fundamentalHz);
        float interScore = 0.0f;
        float harmonicWeight = 1.0f;
        float interWeight = 0.0f;

        for (int harmonic = 2; harmonic <= 6; ++harmonic)
        {
            const double hf = fundamentalHz * static_cast<double>(harmonic);
            if (hf >= 0.45 * sr)
                break;
            const float weight = 1.0f
                               / std::sqrt(static_cast<float>(harmonic));
            harmonicScore += weight * lineCoherence(hf);
            harmonicWeight += weight;

            const double inter = fundamentalHz
                               * (static_cast<double>(harmonic) - 0.5);
            if (inter < 0.45 * sr)
            {
                interScore += weight * lineCoherence(inter);
                interWeight += weight;
            }
        }

        const float harmonicMean = harmonicScore
                                 / std::max(1.0e-6f, harmonicWeight);
        const float interMean = interWeight > 1.0e-6f
            ? interScore / interWeight : 0.0f;

        const float raw = harmonicMean - 0.78f * interMean;
        const float t = std::clamp((raw - 0.025f) / (0.30f - 0.025f),
                                   0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };

    constexpr std::array<std::array<double, 2>, 4> bands {{
        { 55.0, 230.0 },
        { 230.0, 460.0 },
        { 460.0, 900.0 },
        { 900.0, 1600.0 }
    }};

    std::array<Candidate, 24> candidates {};
    int candidateCount = 0;

    for (int band = 0; band < static_cast<int>(bands.size()); ++band)
    {
        const double low = bands[static_cast<std::size_t>(band)][0];
        const double high = bands[static_cast<std::size_t>(band)][1];

        int tauMin = std::max(
            2, static_cast<int>(std::floor(sr / high)));
        int tauMax = std::min(
            analysisLength - 16,
            static_cast<int>(std::ceil(sr / low)));

        if (tauMin > tauMax)
            continue;

        int bestTau = tauMin;
        float bestValue = ws.difference[static_cast<std::size_t>(tauMin)];
        for (int tau = tauMin + 1; tau <= tauMax; ++tau)
        {
            const float value = ws.difference[static_cast<std::size_t>(tau)];
            if (value < bestValue)
            {
                bestValue = value;
                bestTau = tau;
            }
        }

        const double parentHz = refineTau(bestTau);
        for (int divisor = 1; divisor <= 4; ++divisor)
        {
            const double candidateHz = parentHz / static_cast<double>(divisor);
            if (candidateHz < 55.0 || candidateHz > 1600.0)
                continue;

            bool duplicate = false;
            for (int i = 0; i < candidateCount; ++i)
            {
                if (std::abs(cents(candidates[static_cast<std::size_t>(i)].frequency,
                                   candidateHz)) < 18.0)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate || candidateCount >= static_cast<int>(candidates.size()))
                continue;

            auto& candidate = candidates[static_cast<std::size_t>(candidateCount++)];
            candidate.frequency = candidateHz;
            candidate.parentFrequency = parentHz;
            candidate.divisor = divisor;
            candidate.band = band;
            candidate.contrast = harmonicContrast(candidateHz);
        }
    }

    std::sort(candidates.begin(),
              candidates.begin() + candidateCount,
              [](const Candidate& a, const Candidate& b)
              {
                  return a.contrast > b.contrast;
              });

    const Candidate best = candidateCount > 0 ? candidates[0] : Candidate {};
    const double error = cents(best.frequency, targetHz);

    std::cout << std::fixed << std::setprecision(5)
              << "BANDED_FAMILY"
              << " hz=" << targetHz
              << " snr=" << snrDb
              << " selected=" << best.frequency
              << " cents=" << error
              << " abs_cents=" << std::abs(error)
              << " contrast=" << best.contrast
              << " band=" << best.band
              << " divisor=" << best.divisor
              << " parent=" << best.parentFrequency;

    const int show = std::min(4, candidateCount);
    for (int i = 0; i < show; ++i)
    {
        const auto& q = candidates[static_cast<std::size_t>(i)];
        std::cout << " c" << i << "_hz=" << q.frequency
                  << " c" << i << "_contrast=" << q.contrast;
    }

    std::cout << " pass_1p5="
              << ((candidateCount > 0 && std::abs(error) <= 1.5) ? 1 : 0)
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
