#include <JuceHeader.h>
#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int n = 480;
constexpr int half = n / 2;

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

double wrapPi(double x) noexcept
{
    while (x > pi) x -= 2.0 * pi;
    while (x < -pi) x += 2.0 * pi;
    return x;
}

template <typename Frame>
std::complex<double> projectHalf(const Frame& frame,
                                 int start,
                                 double frequencyHz) noexcept
{
    double real = 0.0;
    double imag = 0.0;
    for (int i = 0; i < half; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(
            2.0 * pi * static_cast<double>(i)
            / static_cast<double>(half - 1));
        const double x = static_cast<double>(
            frame[static_cast<std::size_t>(start + i)]) * w;
        const double phase = 2.0 * pi * frequencyHz
                           * static_cast<double>(i) / sr;
        real += x * std::cos(phase);
        imag -= x * std::sin(phase);
    }
    return {real, imag};
}

template <typename Frame>
double refinePhaseDelta(const Frame& frame,
                        double proposalHz,
                        double radiusCents) noexcept
{
    double weightedDeltaHz = 0.0;
    double weightSum = 0.0;
    constexpr int maxHarmonic = 8;
    constexpr double deltaTime = static_cast<double>(half) / sr;

    for (int harmonic = 1; harmonic <= maxHarmonic; ++harmonic)
    {
        const double harmonicHz = proposalHz * static_cast<double>(harmonic);
        if (harmonicHz >= 0.45 * sr)
            break;

        const auto first = projectHalf(frame, 0, harmonicHz);
        const auto second = projectHalf(frame, half, harmonicHz);

        const double mag1 = std::abs(first);
        const double mag2 = std::abs(second);
        const double weight = std::sqrt(std::max(0.0, mag1 * mag2))
                            / std::sqrt(static_cast<double>(harmonic));
        if (!(weight > 1.0e-12))
            continue;

        const double observedAdvance = std::arg(second)
                                     - std::arg(first);
        const double expectedAdvance = 2.0 * pi * harmonicHz * deltaTime;
        const double residualPhase = wrapPi(
            observedAdvance - expectedAdvance);

        const double deltaHz = residualPhase
            / (2.0 * pi * static_cast<double>(harmonic) * deltaTime);

        const double maxDeltaHz = proposalHz
            * (std::exp2(radiusCents / 1200.0) - 1.0);

        const double clampedDeltaHz = std::clamp(
            deltaHz, -maxDeltaHz, maxDeltaHz);

        weightedDeltaHz += weight * clampedDeltaHz;
        weightSum += weight;
    }

    if (!(weightSum > 0.0))
        return proposalHz;

    const double refined = proposalHz + weightedDeltaHz / weightSum;
    const double lower = shifted(proposalHz, -radiusCents);
    const double upper = shifted(proposalHz,  radiusCents);
    return std::clamp(refined, lower, upper);
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
        sr, 55.0f, 1600.0f, n, workspace));

    constexpr std::array<double, 8> proposalErrors {
        -20.0, -10.0, -5.0, -2.0, 2.0, 5.0, 10.0, 20.0
    };
    constexpr std::array<double, 4> radii { 2.0, 5.0, 10.0, 20.0 };

    for (double radius : radii)
    {
        for (double proposalError : proposalErrors)
        {
            if (std::abs(proposalError) > radius + 1.0e-9)
                continue;

            const double proposal = shifted(targetHz, proposalError);

            constexpr int repeats = 1;
            const auto sourceBegin = std::chrono::steady_clock::now();
            double sourceRefined = proposal;
            for (int rep = 0; rep < repeats; ++rep)
                sourceRefined = refinePhaseDelta(
                    workspace.frame, proposal, radius);
            const auto sourceEnd = std::chrono::steady_clock::now();

            const auto residualBegin = std::chrono::steady_clock::now();
            double residualRefined = proposal;
            for (int rep = 0; rep < repeats; ++rep)
                residualRefined = refinePhaseDelta(
                    workspace.voiceResidualFrame, proposal, radius);
            const auto residualEnd = std::chrono::steady_clock::now();

            const double sourceUs =
                std::chrono::duration<double, std::micro>(
                    sourceEnd - sourceBegin).count()
                / static_cast<double>(repeats);
            const double residualUs =
                std::chrono::duration<double, std::micro>(
                    residualEnd - residualBegin).count()
                / static_cast<double>(repeats);

            const double sourceError =
                centsError(sourceRefined, targetHz);
            const double residualError =
                centsError(residualRefined, targetHz);

            std::cout << std::fixed << std::setprecision(6)
                      << "SUBFRAME_PHASE_SECOND"
                      << " hz=" << targetHz
                      << " snr=" << snrDb
                      << " radius=" << radius
                      << " proposal_error=" << proposalError
                      << " source_error=" << sourceError
                      << " source_pass=" << (std::abs(sourceError) <= 1.5 ? 1 : 0)
                      << " source_improves="
                      << (std::abs(sourceError) < std::abs(proposalError) ? 1 : 0)
                      << " source_us=" << sourceUs
                      << " residual_error=" << residualError
                      << " residual_pass=" << (std::abs(residualError) <= 1.5 ? 1 : 0)
                      << " residual_improves="
                      << (std::abs(residualError) < std::abs(proposalError) ? 1 : 0)
                      << " residual_us=" << residualUs
                      << '\n';
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
