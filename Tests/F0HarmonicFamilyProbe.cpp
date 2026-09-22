#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <vector>

namespace
{
constexpr double sr = 48000.0;
constexpr int frameSize = 432; // 9 ms
constexpr double pi = 3.14159265358979323846;
constexpr double minimumF0 = 55.0;
constexpr double maximumF0 = 1600.0;
constexpr int modelHarmonics = 5;

enum class Kind { normal, strongSecond, missingFundamental, sine };

struct Rng
{
    std::uint32_t s;
    double next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<double>(s & 0xffffu) / 32767.5 - 1.0;
    }
};

double synth(Kind kind, double phase) noexcept
{
    if (kind == Kind::strongSecond)
        return 0.35 * std::sin(phase)
             + 1.00 * std::sin(2.0 * phase + 0.17)
             + 0.22 * std::sin(3.0 * phase - 0.31)
             + 0.08 * std::sin(4.0 * phase + 0.49);
    if (kind == Kind::missingFundamental)
        return 0.75 * std::sin(2.0 * phase + 0.17)
             + 0.45 * std::sin(3.0 * phase - 0.31)
             + 0.28 * std::sin(4.0 * phase + 0.49)
             + 0.16 * std::sin(5.0 * phase - 0.63);
    if (kind == Kind::sine)
        return std::sin(phase);
    return 0.72 * std::sin(phase)
         + 0.34 * std::sin(2.0 * phase + 0.17)
         + 0.21 * std::sin(3.0 * phase - 0.31)
         + 0.13 * std::sin(4.0 * phase + 0.49)
         + 0.08 * std::sin(5.0 * phase - 0.63);
}

const char* kindName(Kind kind) noexcept
{
    switch (kind)
    {
        case Kind::normal: return "normal";
        case Kind::strongSecond: return "strong_second";
        case Kind::missingFundamental: return "missing_fundamental";
        case Kind::sine: return "sine";
    }
    return "unknown";
}

std::array<double, frameSize> makeFrame(Kind kind, double f0, double snrDb, std::uint32_t seed)
{
    std::array<double, frameSize> x {};
    Rng rng { seed };

    double cleanEnergy = 0.0;
    for (int n = 0; n < frameSize; ++n)
    {
        const double phase = 2.0 * pi * f0 * static_cast<double>(n + 1) / sr;
        x[static_cast<std::size_t>(n)] = synth(kind, phase);
        cleanEnergy += x[static_cast<std::size_t>(n)] * x[static_cast<std::size_t>(n)];
    }

    const double cleanRms = std::sqrt(cleanEnergy / frameSize);
    const double noiseScale = cleanRms / std::pow(10.0, snrDb / 20.0);
    for (double& s : x)
        s += noiseScale * rng.next();
    return x;
}

double hann(int n) noexcept
{
    return 0.5 - 0.5 * std::cos(2.0 * pi * static_cast<double>(n) / static_cast<double>(frameSize - 1));
}

double spectralPower(const std::array<double, frameSize>& x, double hz) noexcept
{
    const double w = 2.0 * pi * hz / sr;
    double re = 0.0;
    double im = 0.0;
    for (int n = 0; n < frameSize; ++n)
    {
        const double a = w * static_cast<double>(n);
        const double s = x[static_cast<std::size_t>(n)] * hann(n);
        re += s * std::cos(a);
        im -= s * std::sin(a);
    }
    return re * re + im * im;
}

double refinePartial(const std::array<double, frameSize>& x, double centre) noexcept
{
    double lo = std::max(40.0, centre - 24.0);
    double hi = std::min(6000.0, centre + 24.0);
    constexpr double phi = 0.6180339887498948482;
    double c = hi - phi * (hi - lo);
    double d = lo + phi * (hi - lo);
    double fc = spectralPower(x, c);
    double fd = spectralPower(x, d);
    for (int i = 0; i < 11; ++i)
    {
        if (fc > fd)
        {
            hi = d; d = c; fd = fc;
            c = hi - phi * (hi - lo); fc = spectralPower(x, c);
        }
        else
        {
            lo = c; c = d; fc = fd;
            d = lo + phi * (hi - lo); fd = spectralPower(x, d);
        }
    }
    return 0.5 * (lo + hi);
}

bool solveLinear(double a[12][13], int n) noexcept
{
    for (int col = 0; col < n; ++col)
    {
        int pivot = col;
        for (int row = col + 1; row < n; ++row)
            if (std::abs(a[row][col]) > std::abs(a[pivot][col]))
                pivot = row;

        if (std::abs(a[pivot][col]) < 1.0e-10)
            return false;
        if (pivot != col)
            for (int j = col; j <= n; ++j)
                std::swap(a[pivot][j], a[col][j]);

        const double inv = 1.0 / a[col][col];
        for (int j = col; j <= n; ++j)
            a[col][j] *= inv;

        for (int row = 0; row < n; ++row)
        {
            if (row == col) continue;
            const double factor = a[row][col];
            for (int j = col; j <= n; ++j)
                a[row][j] -= factor * a[col][j];
        }
    }
    return true;
}

double harmonicFit(const std::array<double, frameSize>& x, double f0) noexcept
{
    if (!(f0 >= minimumF0 && f0 <= maximumF0))
        return -1.0;

    constexpr int columns = 1 + 2 * modelHarmonics;
    double normal[12][13] {};
    double total = 0.0;
    double mean = 0.0;
    for (double s : x) mean += s;
    mean /= frameSize;
    for (double s : x)
    {
        const double d = s - mean;
        total += d * d;
    }
    if (total < 1.0e-12)
        return -1.0;

    for (int n = 0; n < frameSize; ++n)
    {
        std::array<double, columns> basis {};
        basis[0] = 1.0;
        for (int k = 1; k <= modelHarmonics; ++k)
        {
            const double a = 2.0 * pi * f0 * static_cast<double>(k * n) / sr;
            basis[static_cast<std::size_t>(2 * k - 1)] = std::sin(a);
            basis[static_cast<std::size_t>(2 * k)] = std::cos(a);
        }

        for (int i = 0; i < columns; ++i)
        {
            normal[i][columns] += basis[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(n)];
            for (int j = 0; j < columns; ++j)
                normal[i][j] += basis[static_cast<std::size_t>(i)] * basis[static_cast<std::size_t>(j)];
        }
    }

    for (int i = 0; i < columns; ++i)
        normal[i][i] += 1.0e-7;

    if (!solveLinear(normal, columns))
        return -1.0;

    double residual = 0.0;
    for (int n = 0; n < frameSize; ++n)
    {
        double y = normal[0][columns];
        for (int k = 1; k <= modelHarmonics; ++k)
        {
            const double a = 2.0 * pi * f0 * static_cast<double>(k * n) / sr;
            y += normal[2 * k - 1][columns] * std::sin(a);
            y += normal[2 * k][columns] * std::cos(a);
        }
        const double e = x[static_cast<std::size_t>(n)] - y;
        residual += e * e;
    }
    return 1.0 - residual / total;
}

struct Candidate
{
    double hz = 0.0;
    double fit = -1.0;
};

Candidate refineFamily(const std::array<double, frameSize>& x, double centre) noexcept
{
    const double span = std::max(3.0, 0.045 * centre);
    double lo = std::max(minimumF0, centre - span);
    double hi = std::min(maximumF0, centre + span);
    constexpr double phi = 0.6180339887498948482;
    double c = hi - phi * (hi - lo);
    double d = lo + phi * (hi - lo);
    double fc = harmonicFit(x, c);
    double fd = harmonicFit(x, d);
    for (int i = 0; i < 10; ++i)
    {
        if (fc > fd)
        {
            hi = d; d = c; fd = fc;
            c = hi - phi * (hi - lo); fc = harmonicFit(x, c);
        }
        else
        {
            lo = c; c = d; fc = fd;
            d = lo + phi * (hi - lo); fd = harmonicFit(x, d);
        }
    }
    const double hz = 0.5 * (lo + hi);
    return { hz, harmonicFit(x, hz) };
}

bool sameFamilyNeighbour(double a, double b) noexcept
{
    const double ratio = std::max(a, b) / std::min(a, b);
    return ratio < 1.075;
}

struct Estimate
{
    bool valid = false;
    double hz = 0.0;
    double fit = -1.0;
    double runnerUpFit = -1.0;
};

Estimate estimateFamily(const std::array<double, frameSize>& x)
{
    struct Peak { double power; double hz; };
    std::vector<Peak> peaks;
    double previous = spectralPower(x, 60.0);
    double current = spectralPower(x, 80.0);

    for (double hz = 100.0; hz <= 5000.0; hz += 20.0)
    {
        const double next = spectralPower(x, hz);
        if (current >= previous && current >= next)
            peaks.push_back({ current, hz - 20.0 });
        previous = current;
        current = next;
    }

    if (peaks.empty())
        peaks.push_back({ spectralPower(x, 60.0), 60.0 });

    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.power > b.power; });
    if (peaks.size() > 6) peaks.resize(6);

    std::vector<double> hypotheses;
    for (const auto& p : peaks)
    {
        const double partial = refinePartial(x, p.hz);
        for (int divisor = 1; divisor <= 12; ++divisor)
        {
            const double f = partial / static_cast<double>(divisor);
            if (f < minimumF0 || f > maximumF0)
                continue;
            bool duplicate = false;
            for (double old : hypotheses)
                if (std::abs(old - f) < 1.5)
                    duplicate = true;
            if (!duplicate)
                hypotheses.push_back(f);
        }
    }

    std::vector<Candidate> coarse;
    coarse.reserve(hypotheses.size());
    for (double h : hypotheses)
        coarse.push_back({ h, harmonicFit(x, h) });
    std::sort(coarse.begin(), coarse.end(), [](const Candidate& a, const Candidate& b) { return a.fit > b.fit; });
    if (coarse.size() > 8) coarse.resize(8);

    std::vector<Candidate> refined;
    refined.reserve(coarse.size());
    for (const auto& c : coarse)
        refined.push_back(refineFamily(x, c.hz));
    std::sort(refined.begin(), refined.end(), [](const Candidate& a, const Candidate& b) { return a.fit > b.fit; });

    if (refined.empty() || refined.front().fit < 0.45)
        return {};

    Candidate best = refined.front();

    // Primitive-family guard: never keep F/n when a higher primitive model
    // explains essentially the same samples. This is deliberately asymmetric:
    // ambiguity is allowed to withhold stable, never to manufacture bass.
    for (int multiplier = 2; multiplier <= 4; ++multiplier)
    {
        const double higher = best.hz * static_cast<double>(multiplier);
        if (higher > maximumF0)
            break;
        const Candidate test = refineFamily(x, higher);
        if (test.fit >= best.fit - 0.004)
            best = test;
    }

    double runnerUp = -1.0;
    for (const auto& c : refined)
    {
        if (sameFamilyNeighbour(c.hz, best.hz))
            continue;
        runnerUp = std::max(runnerUp, c.fit);
    }

    if (runnerUp >= best.fit - 0.0050)
        return { false, 0.0, best.fit, runnerUp };

    return { true, best.hz, best.fit, runnerUp };
}

double cents(double measured, double target) noexcept
{
    return 1200.0 * std::log2(measured / target);
}

int noiseHallucinations(bool coloured)
{
    int hallucinated = 0;
    for (std::uint32_t seed = 1; seed <= 16; ++seed)
    {
        Rng rng { 0x9e3779b9u * seed + 17u };
        std::array<double, frameSize> x {};
        double fast = 0.0;
        double slow = 0.0;
        for (double& s : x)
        {
            const double w = rng.next();
            if (coloured)
            {
                fast = 0.92 * fast + 0.08 * w;
                slow = 0.992 * slow + 0.008 * w;
                s = 0.52 * w + 0.31 * fast + 0.17 * slow;
            }
            else s = w;
        }
        if (estimateFamily(x).valid)
            ++hallucinated;
    }
    return hallucinated;
}
}

int main()
{
    constexpr std::array<Kind, 4> kinds { Kind::normal, Kind::strongSecond, Kind::missingFundamental, Kind::sine };
    constexpr std::array<double, 5> frequencies { 82.4069, 110.0, 220.0, 440.0, 880.0 };
    constexpr std::array<double, 3> snrs { 12.0, 6.0, 3.0 };
    constexpr std::array<std::uint32_t, 2> seeds { 0x1234567u, 0x9e3779b9u };

    int cases = 0;
    int valid = 0;
    int familyCorrect = 0;
    int precision = 0;
    int artificialLow = 0;
    int octaveHigh = 0;
    int wrongFamily = 0;
    double worstAcceptedCents = 0.0;

    for (Kind kind : kinds)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    const auto x = makeFrame(kind, f0, snr, seed);
                    const auto e = estimateFamily(x);
                    ++cases;
                    double err = std::numeric_limits<double>::quiet_NaN();
                    if (e.valid)
                    {
                        ++valid;
                        err = cents(e.hz, f0);
                        const double ae = std::abs(err);
                        if (ae <= 100.0) ++familyCorrect;
                        else ++wrongFamily;
                        if (ae <= 1.5) ++precision;
                        worstAcceptedCents = std::max(worstAcceptedCents, ae);
                        if (e.hz < 0.75 * f0) ++artificialLow;
                        if (e.hz > 1.5 * f0) ++octaveHigh;
                    }

                    std::cout << std::fixed << std::setprecision(4)
                              << "HARMONIC_FAMILY_CASE kind=" << kindName(kind)
                              << " hz=" << f0
                              << " snr=" << snr
                              << " seed=" << seed
                              << " valid=" << (e.valid ? 1 : 0)
                              << " estimate=" << e.hz
                              << " cents=" << err
                              << " fit=" << e.fit
                              << " runner_up=" << e.runnerUpFit
                              << '\n';
                }

    const int whiteHallucinations = noiseHallucinations(false);
    const int colouredHallucinations = noiseHallucinations(true);

    std::cout << std::fixed << std::setprecision(4)
              << "HARMONIC_FAMILY_SUMMARY"
              << " cases=" << cases
              << " valid=" << valid
              << " family_correct=" << familyCorrect
              << " precision_1_5c=" << precision
              << " artificial_low=" << artificialLow
              << " octave_high=" << octaveHigh
              << " wrong_family=" << wrongFamily
              << " worst_accepted_cents=" << worstAcceptedCents
              << " white_hallucinations=" << whiteHallucinations
              << " coloured_hallucinations=" << colouredHallucinations
              << '\n';

    const bool familySafety = artificialLow == 0
                           && octaveHigh == 0
                           && wrongFamily == 0
                           && whiteHallucinations == 0
                           && colouredHallucinations == 0;
    std::cout << "HARMONIC_FAMILY_SAFETY=" << (familySafety ? "PASS" : "FAIL") << '\n';
    return 0;
}
