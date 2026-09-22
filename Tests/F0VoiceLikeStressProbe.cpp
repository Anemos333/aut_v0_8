#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
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
}


struct SpacingEstimate
{
    bool valid = false;
    double hz = 0.0;
    double score = -1.0e30;
    int support = 0;
    double rmsCents = 1.0e30;
};

struct SparsePartial
{
    double hz = 0.0;
    double power = 0.0;
};

double goertzelPower(const std::array<double, frameSize>& x, double hz) noexcept
{
    const double omega = 2.0 * pi * hz / sr;
    const double coeff = 2.0 * std::cos(omega);
    double s1 = 0.0;
    double s2 = 0.0;
    for (int n = 0; n < frameSize; ++n)
    {
        const double w = 0.5 - 0.5 * std::cos(
            2.0 * pi * static_cast<double>(n)
            / static_cast<double>(frameSize - 1));
        const double s0 = x[static_cast<std::size_t>(n)] * w + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

double refineSparsePeak(const std::array<double, frameSize>& x, double centre) noexcept
{
    double lo = std::max(60.0, centre - 28.0);
    double hi = std::min(6000.0, centre + 28.0);
    constexpr double phi = 0.6180339887498948482;
    double a = hi - phi * (hi - lo);
    double b = lo + phi * (hi - lo);
    double pa = goertzelPower(x, a);
    double pb = goertzelPower(x, b);
    for (int i = 0; i < 9; ++i)
    {
        if (pa > pb)
        {
            hi = b; b = a; pb = pa;
            a = hi - phi * (hi - lo);
            pa = goertzelPower(x, a);
        }
        else
        {
            lo = a; a = b; pa = pb;
            b = lo + phi * (hi - lo);
            pb = goertzelPower(x, b);
        }
    }
    return 0.5 * (lo + hi);
}

std::vector<SparsePartial> extractSparsePartials(const std::array<double, frameSize>& x)
{
    struct GridPeak { double hz; double power; };
    std::vector<GridPeak> grid;

    constexpr double step = 20.0;
    double previous = goertzelPower(x, 80.0);
    double current = goertzelPower(x, 100.0);
    for (double hz = 120.0; hz <= 5000.0; hz += step)
    {
        const double next = goertzelPower(x, hz);
        if (current >= previous && current >= next)
            grid.push_back({ hz - step, current });
        previous = current;
        current = next;
    }

    std::sort(grid.begin(), grid.end(),
        [](const GridPeak& a, const GridPeak& b) { return a.power > b.power; });
    if (grid.size() > 12)
        grid.resize(12);

    std::vector<SparsePartial> partials;
    partials.reserve(grid.size());
    for (const auto& p : grid)
    {
        const double hz = refineSparsePeak(x, p.hz);
        partials.push_back({ hz, goertzelPower(x, hz) });
    }
    std::sort(partials.begin(), partials.end(),
        [](const SparsePartial& a, const SparsePartial& b) { return a.hz < b.hz; });
    return partials;
}

struct SpacingScore
{
    bool usable = false;
    double refinedHz = 0.0;
    double score = -1.0e30;
    int support = 0;
    double rmsCents = 1.0e30;
};

SpacingScore scoreSpacingCandidate(const std::vector<SparsePartial>& partials,
                                   double candidate) noexcept
{
    if (!(candidate >= minimumF0 && candidate <= maximumF0) || partials.size() < 2)
        return {};

    double maxPower = 0.0;
    for (const auto& p : partials)
        maxPower = std::max(maxPower, p.power);
    if (!(maxPower > 0.0))
        return {};

    struct Match { int h; double hz; double weight; };
    std::array<Match, 12> matches {};
    int count = 0;
    int gcdIndex = 0;
    double coverageWeight = 0.0;
    double totalWeight = 0.0;

    for (const auto& p : partials)
    {
        const double baseWeight = std::sqrt(std::max(0.0, p.power / maxPower));
        totalWeight += baseWeight;

        const int h = static_cast<int>(std::llround(p.hz / candidate));
        if (h < 1 || h > 24)
            continue;

        const double predicted = candidate * static_cast<double>(h);
        const double errCents = 1200.0 * std::log2(p.hz / predicted);
        if (std::abs(errCents) > 42.0)
            continue;

        bool duplicateH = false;
        for (int i = 0; i < count; ++i)
            if (matches[static_cast<std::size_t>(i)].h == h)
                duplicateH = true;
        if (duplicateH || count >= static_cast<int>(matches.size()))
            continue;

        matches[static_cast<std::size_t>(count++)] = { h, p.hz, baseWeight };
        gcdIndex = gcdIndex == 0 ? h : std::gcd(gcdIndex, h);
        coverageWeight += baseWeight;
    }

    if (count < 2 || gcdIndex != 1)
        return {};

    double numerator = 0.0;
    double denominator = 0.0;
    for (int i = 0; i < count; ++i)
    {
        const auto& m = matches[static_cast<std::size_t>(i)];
        numerator += m.weight * static_cast<double>(m.h) * m.hz;
        denominator += m.weight * static_cast<double>(m.h * m.h);
    }
    if (!(denominator > 0.0))
        return {};

    const double refined = numerator / denominator;

    double err2 = 0.0;
    double errWeight = 0.0;
    int refinedGcd = 0;
    int refinedCount = 0;
    for (const auto& p : partials)
    {
        const int h = static_cast<int>(std::llround(p.hz / refined));
        if (h < 1 || h > 24)
            continue;
        const double predicted = refined * static_cast<double>(h);
        const double ec = 1200.0 * std::log2(p.hz / predicted);
        if (std::abs(ec) > 42.0)
            continue;

        const double w = std::sqrt(std::max(0.0, p.power / maxPower));
        err2 += w * ec * ec;
        errWeight += w;
        refinedGcd = refinedGcd == 0 ? h : std::gcd(refinedGcd, h);
        ++refinedCount;
    }

    if (refinedCount < 2 || refinedGcd != 1 || !(errWeight > 0.0))
        return {};

    const double rms = std::sqrt(err2 / errWeight);
    const double coverage = coverageWeight / std::max(1.0e-12, totalWeight);
    const double score =
        2.2 * coverage
        + 0.11 * static_cast<double>(refinedCount)
        - 0.012 * rms
        + 0.00015 * refined; // tiny primitive-family preference on near ties

    return { true, refined, score, refinedCount, rms };
}

SpacingEstimate estimateByPartialSpacing(const std::array<double, frameSize>& x)
{
    const auto partials = extractSparsePartials(x);
    if (partials.size() < 2)
        return {};

    std::vector<double> hypotheses;
    hypotheses.reserve(partials.size() * 12);
    for (const auto& p : partials)
    {
        for (int h = 1; h <= 16; ++h)
        {
            const double f = p.hz / static_cast<double>(h);
            if (f < minimumF0 || f > maximumF0)
                continue;

            bool duplicate = false;
            for (double old : hypotheses)
                if (std::abs(old - f) < 1.0)
                    duplicate = true;
            if (!duplicate)
                hypotheses.push_back(f);
        }
    }

    SpacingScore best {};
    SpacingScore runner {};
    for (double h : hypotheses)
    {
        const auto s = scoreSpacingCandidate(partials, h);
        if (!s.usable)
            continue;

        if (!best.usable || s.score > best.score)
        {
            runner = best;
            best = s;
        }
        else if (std::abs(1200.0 * std::log2(s.refinedHz / best.refinedHz)) > 80.0
              && (!runner.usable || s.score > runner.score))
        {
            runner = s;
        }
    }

    if (!best.usable)
        return {};

    const double margin = runner.usable ? best.score - runner.score : 1.0e9;
    if (best.support < 2 || best.rmsCents > 24.0 || margin < 0.055)
        return { false, 0.0, best.score, best.support, best.rmsCents };

    return { true, best.refinedHz, best.score, best.support, best.rmsCents };
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
    int baseValid = 0;
    int baseCorrect = 0;
    int baseWrong = 0;
    int baseArtificialLow = 0;
    int rescuedValid = 0;
    int rescuedCorrect = 0;
    int rescuedWrong = 0;
    int rescuedArtificialLow = 0;
    int rescuedPrecision = 0;
    int spacingValid = 0;
    int spacingCorrect = 0;
    int spacingWrong = 0;
    int spacingArtificialLow = 0;
    double worstRescuedCents = 0.0;
    std::vector<double> micros;
    std::vector<double> spacingMicros;
    micros.reserve(profiles.size() * frequencies.size() * snrs.size() * seeds.size());
    spacingMicros.reserve(micros.capacity());

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    const auto x = makeVoiceLikeFrame(profile, f0, snr,
                                                      seed ^ static_cast<std::uint32_t>(f0 * 97.0));
                    const auto start = std::chrono::steady_clock::now();
                    const auto base = estimateFamily(x);
                    const auto rescued = applyPredictiveAmbiguityRescue(x, base);
                    const auto end = std::chrono::steady_clock::now();
                    micros.push_back(std::chrono::duration<double, std::micro>(end - start).count());

                    const auto spacingStart = std::chrono::steady_clock::now();
                    const auto spacing = estimateByPartialSpacing(x);
                    const auto spacingEnd = std::chrono::steady_clock::now();
                    spacingMicros.push_back(
                        std::chrono::duration<double, std::micro>(spacingEnd - spacingStart).count());
                    ++cases;

                    if (base.valid)
                    {
                        ++baseValid;
                        const double err = cents(base.hz, f0);
                        if (std::abs(err) <= 100.0) ++baseCorrect;
                        else ++baseWrong;
                        if (base.hz < 0.75 * f0) ++baseArtificialLow;
                    }

                    double spacingErr = std::numeric_limits<double>::quiet_NaN();
                    if (spacing.valid)
                    {
                        ++spacingValid;
                        spacingErr = cents(spacing.hz, f0);
                        if (std::abs(spacingErr) <= 100.0) ++spacingCorrect;
                        else ++spacingWrong;
                        if (spacing.hz < 0.75 * f0) ++spacingArtificialLow;
                    }

                    double rescuedErr = std::numeric_limits<double>::quiet_NaN();
                    if (rescued.valid)
                    {
                        ++rescuedValid;
                        rescuedErr = cents(rescued.hz, f0);
                        const double ae = std::abs(rescuedErr);
                        if (ae <= 100.0) ++rescuedCorrect;
                        else ++rescuedWrong;
                        if (rescued.hz < 0.75 * f0) ++rescuedArtificialLow;
                        if (ae <= 1.5) ++rescuedPrecision;
                        worstRescuedCents = std::max(worstRescuedCents, ae);
                    }

                    std::cout << std::fixed << std::setprecision(4)
                              << "VOICE_STRESS_CASE profile=" << profile.name
                              << " hz=" << f0
                              << " snr=" << snr
                              << " seed=" << seed
                              << " base_valid=" << (base.valid ? 1 : 0)
                              << " base_hz=" << base.hz
                              << " rescued_valid=" << (rescued.valid ? 1 : 0)
                              << " rescued_hz=" << rescued.hz
                              << " rescued_cents=" << rescuedErr
                              << '\n';
                }

    std::sort(micros.begin(), micros.end());
    const double meanUs = micros.empty() ? 0.0
        : std::accumulate(micros.begin(), micros.end(), 0.0) / static_cast<double>(micros.size());
    const double p95Us = micros.empty() ? 0.0
        : micros[static_cast<std::size_t>(0.95 * static_cast<double>(micros.size() - 1))];
    const double maxUs = micros.empty() ? 0.0 : micros.back();

    std::sort(spacingMicros.begin(), spacingMicros.end());
    const double spacingMeanUs = spacingMicros.empty() ? 0.0
        : std::accumulate(spacingMicros.begin(), spacingMicros.end(), 0.0)
          / static_cast<double>(spacingMicros.size());
    const double spacingP95Us = spacingMicros.empty() ? 0.0
        : spacingMicros[static_cast<std::size_t>(
            0.95 * static_cast<double>(spacingMicros.size() - 1))];
    const double spacingMaxUs = spacingMicros.empty() ? 0.0 : spacingMicros.back();

    std::cout << std::fixed << std::setprecision(4)
              << "VOICE_STRESS_SUMMARY"
              << " cases=" << cases
              << " base_valid=" << baseValid
              << " base_correct=" << baseCorrect
              << " base_wrong=" << baseWrong
              << " base_artificial_low=" << baseArtificialLow
              << " rescued_valid=" << rescuedValid
              << " rescued_correct=" << rescuedCorrect
              << " rescued_wrong=" << rescuedWrong
              << " rescued_artificial_low=" << rescuedArtificialLow
              << " rescued_precision_1_5c=" << rescuedPrecision
              << " spacing_valid=" << spacingValid
              << " spacing_correct=" << spacingCorrect
              << " spacing_wrong=" << spacingWrong
              << " spacing_artificial_low=" << spacingArtificialLow
              << " worst_rescued_cents=" << worstRescuedCents
              << " mean_us=" << meanUs
              << " p95_us=" << p95Us
              << " max_us=" << maxUs
              << '\n';

    const bool structuralSafe =
        rescuedWrong == 0
        && rescuedArtificialLow == 0
        && rescuedCorrect == cases;

    std::cout << "VOICE_STRESS_STRUCTURAL_SAFE="
              << (structuralSafe ? "PASS" : "FAIL") << '\n';
    return 0;
}
