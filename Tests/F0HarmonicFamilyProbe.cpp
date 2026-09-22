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
    double primitiveScore = 0.0;
};

double primitiveHarmonicScore(const std::array<double, frameSize>& x, double f0) noexcept
{
    double odd = 0.0;
    double even = 0.0;
    for (int k = 1; k <= 8; ++k)
    {
        const double hz = f0 * static_cast<double>(k);
        if (hz >= 0.48 * sr)
            break;
        const double p = spectralPower(x, hz);
        if ((k & 1) != 0) odd += p;
        else even += p;
    }
    return odd / std::max(1.0e-18, odd + even);
}

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
    return { hz, harmonicFit(x, hz), primitiveHarmonicScore(x, hz) };
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
    double candidateHz = 0.0;
    double runnerUpHz = 0.0;
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
        coarse.push_back({ h, harmonicFit(x, h), primitiveHarmonicScore(x, h) });
    std::sort(coarse.begin(), coarse.end(), [](const Candidate& a, const Candidate& b) { return a.fit > b.fit; });
    if (coarse.size() > 8) coarse.resize(8);

    std::vector<Candidate> refined;
    refined.reserve(coarse.size());
    for (const auto& c : coarse)
        refined.push_back(refineFamily(x, c.hz));
    std::sort(refined.begin(), refined.end(), [](const Candidate& a, const Candidate& b) { return a.fit > b.fit; });

    if (refined.empty() || refined.front().fit < 0.45)
        return {};

    constexpr double primitiveThreshold = 0.08;
    auto primitiveIt = std::find_if(refined.begin(), refined.end(),
        [](const Candidate& c) { return c.primitiveScore >= primitiveThreshold; });
    if (primitiveIt == refined.end())
        return {};

    Candidate best = *primitiveIt;

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
    double runnerUpHz = 0.0;
    for (const auto& c : refined)
    {
        if (c.primitiveScore < primitiveThreshold || sameFamilyNeighbour(c.hz, best.hz))
            continue;
        if (c.fit > runnerUp)
        {
            runnerUp = c.fit;
            runnerUpHz = c.hz;
        }
    }

    if (runnerUp >= best.fit - 0.0050)
        return { false, 0.0, best.fit, runnerUp, best.hz, runnerUpHz };

    return { true, best.hz, best.fit, runnerUp, best.hz, runnerUpHz };
}


double phaseOracleRefine(const std::array<double, frameSize>& x, double centre) noexcept
{
    constexpr int windowLength = 144;
    constexpr int firstStart = 24;
    constexpr int secondStart = frameSize - firstStart - windowLength;
    constexpr double deltaSamples =
        static_cast<double>(secondStart - firstStart);

    struct HarmonicPhase
    {
        double deltaHz = 0.0;
        double weight = 0.0;
    };

    std::array<HarmonicPhase, modelHarmonics> values {};
    double strongest = 0.0;

    for (int k = 1; k <= modelHarmonics; ++k)
    {
        const double hz = centre * static_cast<double>(k);
        if (hz >= 0.48 * sr)
            continue;

        double reA = 0.0, imA = 0.0;
        double reB = 0.0, imB = 0.0;
        for (int j = 0; j < windowLength; ++j)
        {
            const double w = 0.5 - 0.5 * std::cos(
                2.0 * pi * static_cast<double>(j)
                / static_cast<double>(windowLength - 1));

            const int na = firstStart + j;
            const int nb = secondStart + j;
            const double aa = 2.0 * pi * hz * static_cast<double>(na) / sr;
            const double ab = 2.0 * pi * hz * static_cast<double>(nb) / sr;

            const double xa = x[static_cast<std::size_t>(na)] * w;
            const double xb = x[static_cast<std::size_t>(nb)] * w;
            reA += xa * std::cos(aa);
            imA -= xa * std::sin(aa);
            reB += xb * std::cos(ab);
            imB -= xb * std::sin(ab);
        }

        const double powerA = reA * reA + imA * imA;
        const double powerB = reB * reB + imB * imB;
        const double coherentPower = std::sqrt(std::max(0.0, powerA * powerB));
        strongest = std::max(strongest, coherentPower);

        const double crossRe = reB * reA + imB * imA;
        const double crossIm = imB * reA - reB * imA;
        const double phase = std::atan2(crossIm, crossRe);
        const double deltaHz = phase * sr
                             / (2.0 * pi * static_cast<double>(k) * deltaSamples);

        values[static_cast<std::size_t>(k - 1)] =
            { deltaHz, coherentPower * static_cast<double>(k * k) };
    }

    if (!(strongest > 1.0e-18))
        return centre;

    double weighted = 0.0;
    double totalWeight = 0.0;
    for (int k = 1; k <= modelHarmonics; ++k)
    {
        const auto& v = values[static_cast<std::size_t>(k - 1)];
        const double rawPower = v.weight / static_cast<double>(k * k);
        if (rawPower < 0.035 * strongest)
            continue;
        if (std::abs(v.deltaHz) > std::max(30.0, 0.08 * centre))
            continue;

        weighted += v.deltaHz * v.weight;
        totalWeight += v.weight;
    }

    if (!(totalWeight > 0.0))
        return centre;

    return centre + weighted / totalWeight;
}

double cents(double measured, double target) noexcept
{
    return 1200.0 * std::log2(measured / target);
}


double optimisticSineFrequencyStdHz(double snrDb) noexcept
{
    const double linearSnr = std::pow(10.0, snrDb / 10.0);
    const double n = static_cast<double>(frameSize);
    const double omegaVariance =
        12.0 / (linearSnr * n * (n * n - 1.0));
    return std::sqrt(omegaVariance) * sr / (2.0 * pi);
}

double hzStdToCents(double stdHz, double hz) noexcept
{
    return (1200.0 / std::log(2.0)) * stdHz / hz;
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
    int vocalCases = 0;
    int vocalValid = 0;
    int vocalPrecision = 0;
    int observabilityCases = 0;
    int observabilityValid = 0;
    int familyCorrect = 0;
    int precision = 0;
    int artificialLow = 0;
    int octaveHigh = 0;
    int wrongFamily = 0;
    int oraclePrecision = 0;
    int phaseOraclePrecision = 0;
    double oracleWorstCents = 0.0;
    double phaseOracleWorstCents = 0.0;
    double worstAcceptedCents = 0.0;

    for (Kind kind : kinds)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    const auto x = makeFrame(kind, f0, snr, seed);
                    const auto e = estimateFamily(x);
                    const auto oracle = refineFamily(x, f0);
                    const double oracleError = std::abs(cents(oracle.hz, f0));
                    const double phaseOracleHz = phaseOracleRefine(x, f0);
                    const double phaseOracleError = std::abs(cents(phaseOracleHz, f0));
                    if (oracleError <= 1.5) ++oraclePrecision;
                    if (phaseOracleError <= 1.5) ++phaseOraclePrecision;
                    oracleWorstCents = std::max(oracleWorstCents, oracleError);
                    phaseOracleWorstCents = std::max(phaseOracleWorstCents, phaseOracleError);
                    ++cases;
                    const bool productCriticalVocal =
                        kind != Kind::sine && f0 >= 110.0;
                    if (productCriticalVocal) ++vocalCases;
                    else ++observabilityCases;
                    double err = std::numeric_limits<double>::quiet_NaN();
                    if (e.valid)
                    {
                        ++valid;
                        if (productCriticalVocal) ++vocalValid;
                        else ++observabilityValid;
                        err = cents(e.hz, f0);
                        const double ae = std::abs(err);
                        if (ae <= 100.0) ++familyCorrect;
                        else ++wrongFamily;
                        if (ae <= 1.5)
                        {
                            ++precision;
                            if (productCriticalVocal) ++vocalPrecision;
                        }
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
                              << " candidate_hz=" << e.candidateHz
                              << " runner_up_hz=" << e.runnerUpHz
                              << " oracle_hz=" << oracle.hz
                              << " oracle_cents=" << oracleError
                              << " phase_oracle_hz=" << phaseOracleHz
                              << " phase_oracle_cents=" << phaseOracleError
                              << '\n';
                }

    const int whiteHallucinations = noiseHallucinations(false);
    const int colouredHallucinations = noiseHallucinations(true);

    std::cout << std::fixed << std::setprecision(4)
              << "HARMONIC_FAMILY_SUMMARY"
              << " cases=" << cases
              << " valid=" << valid
              << " vocal_cases=" << vocalCases
              << " vocal_valid=" << vocalValid
              << " vocal_precision_1_5c=" << vocalPrecision
              << " observability_cases=" << observabilityCases
              << " observability_valid_9ms=" << observabilityValid
              << " family_correct=" << familyCorrect
              << " precision_1_5c=" << precision
              << " artificial_low=" << artificialLow
              << " octave_high=" << octaveHigh
              << " wrong_family=" << wrongFamily
              << " oracle_precision_1_5c=" << oraclePrecision
              << " oracle_worst_cents=" << oracleWorstCents
              << " phase_oracle_precision_1_5c=" << phaseOraclePrecision
              << " phase_oracle_worst_cents=" << phaseOracleWorstCents
              << " worst_accepted_cents=" << worstAcceptedCents
              << " white_hallucinations=" << whiteHallucinations
              << " coloured_hallucinations=" << colouredHallucinations
              << '\n';

    const bool familySafety = artificialLow == 0
                           && octaveHigh == 0
                           && wrongFamily == 0
                           && whiteHallucinations == 0
                           && colouredHallucinations == 0;
    const bool vocalPromotionReady = familySafety
                                  && vocalValid == vocalCases
                                  && vocalPrecision == vocalCases;
    const bool research120Ready = familySafety
                               && valid == cases
                               && precision == cases
                               && worstAcceptedCents <= 1.5;

    std::cout << "HARMONIC_FAMILY_SAFETY=" << (familySafety ? "PASS" : "FAIL") << '\n';
    std::cout << "F0_V1_VOCAL_PROMOTION_READY="
              << (vocalPromotionReady ? "PASS" : "FAIL") << '\n';
    std::cout << "F0_RESEARCH_120_READY="
              << (research120Ready ? "PASS" : "FAIL") << '\n';

    for (double f0 : frequencies)
        for (double snrDb : snrs)
        {
            const double stdHz = optimisticSineFrequencyStdHz(snrDb);
            std::cout << std::fixed << std::setprecision(4)
                      << "SINE_CRLB_APPROX hz=" << f0
                      << " snr=" << snrDb
                      << " std_hz=" << stdHz
                      << " std_cents=" << hzStdToCents(stdHz, f0)
                      << '\n';
        }
    return 0;
}
