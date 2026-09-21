#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

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

double shifted(double hz, double cents) noexcept
{
    return hz * std::exp2(cents / 1200.0);
}

double centsError(double measured, double target) noexcept
{
    if (!(measured > 0.0) || !(target > 0.0))
        return std::numeric_limits<double>::infinity();
    return 1200.0 * std::log2(measured / target);
}

struct ScoreBank
{
    const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& source;
    const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& residual;
    const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& difference;
    std::array<double, n> hann {};
    double sourceEnergy = 0.0;
    double residualEnergy = 0.0;
    double windowEnergy = 0.0;

    ScoreBank(
        const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& s,
        const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& r,
        const std::array<float, ModernPitchEngine::MultiRatePitchTracker::maxAnalysisSize>& d)
        : source(s), residual(r), difference(d)
    {
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos(
                2.0 * pi * static_cast<double>(i) / static_cast<double>(n - 1));
            hann[static_cast<std::size_t>(i)] = w;
            const double sx = static_cast<double>(source[static_cast<std::size_t>(i)]) * w;
            const double rx = static_cast<double>(residual[static_cast<std::size_t>(i)]) * w;
            sourceEnergy += sx * sx;
            residualEnergy += rx * rx;
            windowEnergy += w * w;
        }
    }

    template <typename Frame>
    float line(const Frame& frame, double signalEnergy, double hz) const noexcept
    {
        if (!(hz > 0.0) || hz >= 0.45 * sr)
            return 0.0f;
        double re = 0.0, im = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double x = static_cast<double>(frame[static_cast<std::size_t>(i)])
                           * hann[static_cast<std::size_t>(i)];
            const double p = 2.0 * pi * hz * static_cast<double>(i) / sr;
            re += x * std::cos(p);
            im -= x * std::sin(p);
        }
        const double norm = std::max(1.0e-20, signalEnergy * windowEnergy);
        return std::clamp(static_cast<float>(
            std::sqrt(2.0 * (re * re + im * im) / norm)), 0.0f, 1.0f);
    }

    float harmonicEnergy(bool useResidual, double f0) const noexcept
    {
        const auto& frame = useResidual ? residual : source;
        const double energy = useResidual ? residualEnergy : sourceEnergy;
        float score = 0.0f;
        float weight = 0.0f;
        for (int harmonic = 1; harmonic <= 8; ++harmonic)
        {
            const double hz = f0 * static_cast<double>(harmonic);
            if (hz >= 0.45 * sr)
                break;
            const float w = 1.0f / std::sqrt(static_cast<float>(harmonic));
            score += w * line(frame, energy, hz);
            weight += w;
        }
        return score / std::max(1.0e-6f, weight);
    }

    float harmonicContrast(double f0) const noexcept
    {
        float harmonicScore = line(residual, residualEnergy, f0);
        float interScore = 0.0f;
        float harmonicWeight = 1.0f;
        float interWeight = 0.0f;

        for (int harmonic = 2; harmonic <= 6; ++harmonic)
        {
            const double hf = f0 * static_cast<double>(harmonic);
            if (hf >= 0.45 * sr)
                break;
            const float w = 1.0f / std::sqrt(static_cast<float>(harmonic));
            harmonicScore += w * line(residual, residualEnergy, hf);
            harmonicWeight += w;
            const double inter = f0 * (static_cast<double>(harmonic) - 0.5);
            if (inter < 0.45 * sr)
            {
                interScore += w * line(residual, residualEnergy, inter);
                interWeight += w;
            }
        }

        const float harmonicMean = harmonicScore / std::max(1.0e-6f, harmonicWeight);
        const float interMean = interWeight > 1.0e-6f ? interScore / interWeight : 0.0f;
        return harmonicMean - 0.78f * interMean;
    }

    float sourceCorrelation(double f0) const noexcept
    {
        const double tau = sr / f0;
        if (tau < 2.0 || tau >= static_cast<double>(n - 8))
            return -1.0f;

        const int lag = std::clamp(static_cast<int>(std::lround(tau)), 2, n - 9);
        double corr = 0.0, ea = 0.0, eb = 0.0;
        const int overlap = n - lag;
        for (int i = 0; i < overlap; ++i)
        {
            const double a = source[static_cast<std::size_t>(i)];
            const double b = source[static_cast<std::size_t>(i + lag)];
            corr += a * b;
            ea += a * a;
            eb += b * b;
        }
        const double den = std::sqrt(std::max(1.0e-20, ea * eb));
        return den > 0.0 ? static_cast<float>(corr / den) : -1.0f;
    }

    float cmndfAt(double f0) const noexcept
    {
        const double tau = sr / f0;
        if (tau < 2.0 || tau >= static_cast<double>(n - 8))
            return 1.0f;

        const int centre = std::clamp(static_cast<int>(std::floor(tau)), 2, n - 10);
        const double frac = tau - static_cast<double>(centre);
        const float a = difference[static_cast<std::size_t>(centre)];
        const float b = difference[static_cast<std::size_t>(centre + 1)];
        return static_cast<float>((1.0 - frac) * a + frac * b);
    }
};

enum class Metric
{
    sourceHarmonicEnergy,
    residualHarmonicEnergy,
    harmonicContrast,
    sourceCorrelation,
    cmndf
};

const char* metricName(Metric metric)
{
    switch (metric)
    {
        case Metric::sourceHarmonicEnergy: return "source_harmonic";
        case Metric::residualHarmonicEnergy: return "residual_harmonic";
        case Metric::harmonicContrast: return "harmonic_contrast";
        case Metric::sourceCorrelation: return "source_corr";
        case Metric::cmndf: return "cmndf";
    }
    return "unknown";
}

double refine(const ScoreBank& bank,
              Metric metric,
              double proposal,
              double radiusCents)
{
    constexpr double stepCents = 0.25;
    double bestHz = proposal;
    double bestScore = -std::numeric_limits<double>::infinity();

    for (double offset = -radiusCents;
         offset <= radiusCents + 1.0e-9;
         offset += stepCents)
    {
        const double hz = shifted(proposal, offset);
        double score = 0.0;
        switch (metric)
        {
            case Metric::sourceHarmonicEnergy:
                score = bank.harmonicEnergy(false, hz);
                break;
            case Metric::residualHarmonicEnergy:
                score = bank.harmonicEnergy(true, hz);
                break;
            case Metric::harmonicContrast:
                score = bank.harmonicContrast(hz);
                break;
            case Metric::sourceCorrelation:
                score = bank.sourceCorrelation(hz);
                break;
            case Metric::cmndf:
                score = -static_cast<double>(bank.cmndfAt(hz));
                break;
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestHz = hz;
        }
    }
    return bestHz;
}

void run(double targetHz, double snrDb, std::uint32_t seed)
{
    ModernPitchEngine::MultiRatePitchTracker tracker;
    tracker.prepare(sr);
    tracker.setRange(55.0f, 1600.0f);

    Rng rng { seed };
    double phase = 0.0;
    const double amp = std::pow(10.0, -24.0 / 20.0);
    const double noiseAmp = amp / std::pow(10.0, snrDb / 20.0);
    ModernPitchEngine::PitchObservation observation;

    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * targetHz / sr;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;
        const float x = static_cast<float>(amp) * voice(phase)
                      + static_cast<float>(noiseAmp) * rng.next();
        tracker.processSample(x, observation);
    }

    ModernPitchEngine::MultiRatePitchTracker::AnalysisWorkspace workspace {};
    static_cast<void>(tracker.measureCoordinate(
        tracker.fullRateRing_,
        tracker.fullRateWritePosition_,
        tracker.fullRateAvailableSamples_,
        sr,
        55.0f,
        1600.0f,
        n,
        workspace));

    const ScoreBank bank(workspace.frame,
                         workspace.voiceResidualFrame,
                         workspace.difference);

    constexpr std::array<double, 8> proposalErrors {
        -20.0, -10.0, -5.0, -2.0, 2.0, 5.0, 10.0, 20.0
    };
    constexpr std::array<double, 4> radii { 2.0, 5.0, 10.0, 20.0 };
    constexpr std::array<Metric, 5> metrics {
        Metric::sourceHarmonicEnergy,
        Metric::residualHarmonicEnergy,
        Metric::harmonicContrast,
        Metric::sourceCorrelation,
        Metric::cmndf
    };

    for (Metric metric : metrics)
    {
        for (double radius : radii)
        {
            for (double proposalError : proposalErrors)
            {
                if (std::abs(proposalError) > radius + 1.0e-9)
                    continue;

                const double proposal = shifted(targetHz, proposalError);

                const auto begin = std::chrono::steady_clock::now();
                constexpr int repeats = 8;
                double refined = proposal;
                for (int rep = 0; rep < repeats; ++rep)
                    refined = refine(bank, metric, proposal, radius);
                const auto end = std::chrono::steady_clock::now();

                const double microseconds =
                    std::chrono::duration<double, std::micro>(end - begin).count()
                    / static_cast<double>(repeats);

                const double error = centsError(refined, targetHz);
                const bool improves = std::abs(error) < std::abs(proposalError) - 1.0e-9;
                const bool passes = std::abs(error) <= 1.5;

                std::cout << std::fixed << std::setprecision(5)
                          << "LOCAL_SECOND_OBSERVATION"
                          << " hz=" << targetHz
                          << " snr=" << snrDb
                          << " metric=" << metricName(metric)
                          << " radius=" << radius
                          << " proposal_error=" << proposalError
                          << " refined_error=" << error
                          << " improves=" << (improves ? 1 : 0)
                          << " pass_1p5=" << (passes ? 1 : 0)
                          << " us=" << microseconds
                          << '\n';
            }
        }
    }
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
