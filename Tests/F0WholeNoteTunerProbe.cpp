#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <vector>

#define main harmonic_probe_reference_main
#include "F0HarmonicFamilyProbe.cpp"
#undef main

namespace
{
struct VoiceProfile
{
    const char* name;
    double f1, bw1;
    double f2, bw2;
    double f3, bw3;
    double tilt;
    double breath;
    double jitter;
    double shimmer;
    double fundamentalScale;
    double secondScale;
    double attackMs;
};

constexpr std::array<VoiceProfile, 5> profiles {{
    { "vowel_a",           750.0, 110.0, 1200.0, 170.0, 2600.0, 260.0, 1.15, 0.025, 0.0005, 0.018, 1.00, 1.00, 0.4 },
    { "vowel_i_breathy",   300.0,  80.0, 2250.0, 210.0, 3000.0, 280.0, 1.35, 0.100, 0.0015, 0.045, 1.00, 1.00, 0.7 },
    { "strong_second",     520.0, 100.0, 1550.0, 190.0, 2700.0, 260.0, 1.10, 0.040, 0.0010, 0.030, 0.24, 2.20, 0.5 },
    { "missing_fund",      600.0, 115.0, 1350.0, 180.0, 2450.0, 250.0, 1.20, 0.050, 0.0012, 0.035, 0.00, 1.20, 0.5 },
    { "rough_onset",       430.0, 120.0, 1750.0, 220.0, 2900.0, 300.0, 1.05, 0.075, 0.0030, 0.070, 0.75, 1.15, 1.8 }
}};

double formantGain(double hz, double centre, double bandwidth) noexcept
{
    const double d = (hz - centre) / std::max(1.0, bandwidth);
    return 1.0 / (1.0 + d * d);
}

std::array<double, frameSize> makeVoiceLikeFrame(const VoiceProfile& profile,
                                                  double baseF0,
                                                  double snrDb,
                                                  std::uint32_t seed)
{
    std::array<double, frameSize> x {};
    Rng rng { seed };
    std::array<double, 24> phaseOffset {};
    for (auto& p : phaseOffset)
        p = pi * rng.next();

    double phase = 0.0;
    double jitterState = 0.0;
    double shimmerState = 0.0;
    double breathState = 0.0;
    double cleanEnergy = 0.0;

    for (int n = 0; n < frameSize; ++n)
    {
        const double t = static_cast<double>(n) / sr;
        const double vibrato = 0.0035 * std::sin(2.0 * pi * 5.2 * t + 0.37);
        jitterState = 0.86 * jitterState + 0.14 * rng.next();
        shimmerState = 0.93 * shimmerState + 0.07 * rng.next();

        const double instF0 = baseF0
            * (1.0 + vibrato + profile.jitter * jitterState);
        phase += 2.0 * pi * instF0 / sr;

        double voiced = 0.0;
        for (int k = 1; k <= static_cast<int>(phaseOffset.size()); ++k)
        {
            const double hz = baseF0 * static_cast<double>(k);
            if (hz >= 0.47 * sr)
                break;

            double amp = 1.0 / std::pow(static_cast<double>(k), profile.tilt);
            const double envelope =
                0.12
                + 1.10 * formantGain(hz, profile.f1, profile.bw1)
                + 0.85 * formantGain(hz, profile.f2, profile.bw2)
                + 0.58 * formantGain(hz, profile.f3, profile.bw3);
            amp *= envelope;

            if (k == 1) amp *= profile.fundamentalScale;
            if (k == 2) amp *= profile.secondScale;

            voiced += amp * std::sin(static_cast<double>(k) * phase
                                   + phaseOffset[static_cast<std::size_t>(k - 1)]);
        }

        const double shimmer = std::max(0.65, 1.0 + profile.shimmer * shimmerState);
        const double attackSamples = std::max(1.0, profile.attackMs * 0.001 * sr);
        const double attack = std::min(1.0, (static_cast<double>(n) + 1.0) / attackSamples);

        const double w = rng.next();
        const double highBreath = w - breathState;
        breathState = 0.86 * breathState + 0.14 * w;

        const double sample = attack * shimmer * voiced
                            + profile.breath * highBreath;
        x[static_cast<std::size_t>(n)] = sample;
        cleanEnergy += sample * sample;
    }

    const double rms = std::sqrt(cleanEnergy / static_cast<double>(frameSize));
    const double noiseScale = rms / std::pow(10.0, snrDb / 20.0);
    double noiseColour = 0.0;
    for (double& s : x)
    {
        const double w = rng.next();
        noiseColour = 0.72 * noiseColour + 0.28 * w;
        s += noiseScale * (0.72 * w + 0.28 * noiseColour);
    }
    return x;
}

struct WholeNoteEstimate
{
    bool valid = false;
    double hz = 0.0;
    double periodicity = 0.0;
    int lag = 0;
};

WholeNoteEstimate estimateWholeNote(const std::array<double, frameSize>& x)
{
    std::array<double, frameSize> y {};
    double mean = 0.0;
    for (double s : x) mean += s;
    mean /= static_cast<double>(frameSize);
    for (int i = 0; i < frameSize; ++i)
        y[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(i)] - mean;

    const int lagMin = std::max(2, static_cast<int>(std::floor(sr / maximumF0)));
    const int lagMax = std::min(frameSize - 8,
                                static_cast<int>(std::ceil(sr / minimumF0)));

    std::array<double, frameSize> corr {};
    double strongest = -1.0;

    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        double ab = 0.0;
        double aa = 0.0;
        double bb = 0.0;
        const int overlap = frameSize - lag;

        for (int n = 0; n < overlap; ++n)
        {
            const double a = y[static_cast<std::size_t>(n)];
            const double b = y[static_cast<std::size_t>(n + lag)];
            ab += a * b;
            aa += a * a;
            bb += b * b;
        }

        const double den = std::sqrt(std::max(1.0e-30, aa * bb));
        const double c = den > 0.0 ? ab / den : -1.0;
        corr[static_cast<std::size_t>(lag)] = c;
        strongest = std::max(strongest, c);
    }

    // Tuner-style primitive-period rule:
    // use the shortest strong local repetition of the entire waveform.
    // A longer multiple is not allowed to manufacture a lower F0.
    const double acceptance = std::max(0.55, strongest - 0.08);
    int bestLag = 0;
    double bestCorr = -1.0;

    for (int lag = lagMin + 1; lag < lagMax; ++lag)
    {
        const double c = corr[static_cast<std::size_t>(lag)];
        if (c < acceptance)
            continue;
        if (c < corr[static_cast<std::size_t>(lag - 1)]
            || c < corr[static_cast<std::size_t>(lag + 1)])
            continue;

        bestLag = lag;
        bestCorr = c;
        break;
    }

    if (bestLag == 0)
    {
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            const double c = corr[static_cast<std::size_t>(lag)];
            if (c > bestCorr)
            {
                bestCorr = c;
                bestLag = lag;
            }
        }
    }

    if (bestLag <= 0 || bestCorr < 0.48)
        return {};

    double refinedLag = static_cast<double>(bestLag);
    if (bestLag > lagMin && bestLag < lagMax)
    {
        const double left = corr[static_cast<std::size_t>(bestLag - 1)];
        const double centre = corr[static_cast<std::size_t>(bestLag)];
        const double right = corr[static_cast<std::size_t>(bestLag + 1)];
        const double den = left - 2.0 * centre + right;
        if (std::abs(den) > 1.0e-12)
        {
            const double delta = 0.5 * (left - right) / den;
            if (std::abs(delta) <= 1.0)
                refinedLag += delta;
        }
    }

    const double hz = sr / refinedLag;
    if (!(hz >= minimumF0 && hz <= maximumF0))
        return {};

    return { true, hz, bestCorr, bestLag };
}
}

int main()
{
    constexpr std::array<double, 12> frequencies {
        110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
        246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
    };
    constexpr std::array<double, 3> snrs { 18.0, 9.0, 3.0 };
    constexpr std::array<std::uint32_t, 6> seeds {
        0x01234567u, 0x9e3779b9u, 0x243f6a88u,
        0xb7e15162u, 0xdeadbeefu, 0xa5a5a5a5u
    };

    int cases = 0;
    int valid = 0;
    int familyCorrect = 0;
    int wrongFamily = 0;
    int artificialLow = 0;
    int octaveHigh = 0;
    int precision = 0;
    double worstCents = 0.0;
    std::vector<double> micros;

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    const auto x = makeVoiceLikeFrame(
                        profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 97.0));

                    const auto start = std::chrono::steady_clock::now();
                    const auto e = estimateWholeNote(x);
                    const auto end = std::chrono::steady_clock::now();
                    micros.push_back(std::chrono::duration<double, std::micro>(
                        end - start).count());

                    ++cases;
                    double err = std::numeric_limits<double>::quiet_NaN();
                    if (e.valid)
                    {
                        ++valid;
                        err = cents(e.hz, f0);
                        const double ae = std::abs(err);
                        worstCents = std::max(worstCents, ae);

                        if (ae <= 100.0) ++familyCorrect;
                        else ++wrongFamily;
                        if (e.hz < 0.75 * f0) ++artificialLow;
                        if (e.hz > 1.5 * f0) ++octaveHigh;
                        if (ae <= 1.5) ++precision;
                    }

                    std::cout << std::fixed << std::setprecision(4)
                              << "WHOLE_NOTE_CASE profile=" << profile.name
                              << " hz=" << f0
                              << " snr=" << snr
                              << " seed=" << seed
                              << " valid=" << (e.valid ? 1 : 0)
                              << " estimate=" << e.hz
                              << " cents=" << err
                              << " periodicity=" << e.periodicity
                              << " lag=" << e.lag
                              << '\n';
                }

    std::sort(micros.begin(), micros.end());
    const double meanUs = micros.empty() ? 0.0
        : std::accumulate(micros.begin(), micros.end(), 0.0)
          / static_cast<double>(micros.size());
    const double p95Us = micros.empty() ? 0.0
        : micros[static_cast<std::size_t>(
            0.95 * static_cast<double>(micros.size() - 1))];
    const double maxUs = micros.empty() ? 0.0 : micros.back();

    std::cout << std::fixed << std::setprecision(4)
              << "WHOLE_NOTE_SUMMARY"
              << " cases=" << cases
              << " valid=" << valid
              << " family_correct=" << familyCorrect
              << " wrong_family=" << wrongFamily
              << " artificial_low=" << artificialLow
              << " octave_high=" << octaveHigh
              << " precision_1_5c=" << precision
              << " worst_cents=" << worstCents
              << " mean_us=" << meanUs
              << " p95_us=" << p95Us
              << " max_us=" << maxUs
              << '\n';

    const bool structuralSafe =
        wrongFamily == 0
        && artificialLow == 0
        && octaveHigh == 0
        && familyCorrect == cases;

    std::cout << "WHOLE_NOTE_STRUCTURAL_SAFE="
              << (structuralSafe ? "PASS" : "FAIL") << '\n';
    return 0;
}
