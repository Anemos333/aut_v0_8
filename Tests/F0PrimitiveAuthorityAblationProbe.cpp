#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>

#include "F0WholeNoteExclusiveConjunctionHoldoutProbe.cpp"

namespace
{
struct RawEstimate
{
    bool valid = false;
    double hz = 0.0;
    double periodicity = 0.0;
    double primitiveRatio = 1.0;
    int lag = 0;
    AlternationWitness alt {};
};

template <std::size_t N>
RawEstimate estimateRawNoLower(const std::array<double, N>& x,
                               int sampleCount)
{
    std::array<double, N> y {};
    double mean = 0.0;
    for (int n = 0; n < sampleCount; ++n)
        mean += x[static_cast<std::size_t>(n)];
    mean /= static_cast<double>(sampleCount);
    for (int n = 0; n < sampleCount; ++n)
        y[static_cast<std::size_t>(n)] = x[static_cast<std::size_t>(n)] - mean;

    const int lagMin = std::max(2, static_cast<int>(std::floor(sr / maximumF0)));
    const int lagMax = std::min(sampleCount - 8,
        static_cast<int>(std::ceil(sr / minimumF0)));
    if (lagMin >= lagMax)
        return {};

    std::array<double, N> corr {};
    double strongest = -1.0;
    for (int lag = lagMin; lag <= lagMax; ++lag)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        const int overlap = sampleCount - lag;
        for (int n = 0; n < overlap; ++n)
        {
            const double a = y[static_cast<std::size_t>(n)];
            const double b = y[static_cast<std::size_t>(n + lag)];
            ab += a * b;
            aa += a * a;
            bb += b * b;
        }
        const double den = std::sqrt(std::max(1.0e-30, aa * bb));
        const double value = den > 0.0 ? ab / den : -1.0;
        corr[static_cast<std::size_t>(lag)] = value;
        strongest = std::max(strongest, value);
    }

    const double acceptance = std::max(0.55, strongest - 0.08);
    constexpr double decorrelationThreshold = 0.35;
    bool decorrelated = false;
    int bestLag = 0;
    double bestCorr = -1.0;
    for (int lag = lagMin + 1; lag < lagMax; ++lag)
    {
        const double value = corr[static_cast<std::size_t>(lag)];
        if (value <= decorrelationThreshold)
        {
            decorrelated = true;
            continue;
        }
        if (!decorrelated || value < acceptance)
            continue;
        if (value < corr[static_cast<std::size_t>(lag - 1)]
            || value < corr[static_cast<std::size_t>(lag + 1)])
            continue;
        bestLag = lag;
        bestCorr = value;
        break;
    }
    if (bestLag == 0)
    {
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            const double value = corr[static_cast<std::size_t>(lag)];
            if (value > bestCorr)
            {
                bestCorr = value;
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

    double primitiveRatio = 1.0;
    if (2 * bestLag < sampleCount)
    {
        const int overlap = sampleCount - 2 * bestLag;
        if (overlap >= 40)
        {
            auto mismatch = [&](int lag)
            {
                double diff = 0.0, energy = 0.0;
                for (int n = 0; n < overlap; ++n)
                {
                    const double a = y[static_cast<std::size_t>(n)];
                    const double b = y[static_cast<std::size_t>(n + lag)];
                    const double d = a - b;
                    diff += d * d;
                    energy += a * a + b * b;
                }
                return diff / std::max(1.0e-30, energy);
            };
            primitiveRatio = mismatch(2 * bestLag)
                           / std::max(1.0e-12, mismatch(bestLag));
        }
    }

    RawEstimate r;
    r.valid = true;
    r.hz = sr / refinedLag;
    r.periodicity = bestCorr;
    r.primitiveRatio = primitiveRatio;
    r.lag = bestLag;
    r.alt = measureExtendedAlternation(x, sampleCount, bestLag);
    return r;
}

ProgressiveEstimate asProgressive(const RawEstimate& r)
{
    ProgressiveEstimate e;
    e.valid = r.valid;
    e.hz = r.hz;
    e.periodicity = r.periodicity;
    e.primitiveRatio = r.primitiveRatio;
    e.lag = r.lag;
    e.loweredPrimitive = false;
    return e;
}

const char* classify(double hz, double truth)
{
    const double ae = std::abs(1200.0 * std::log2(hz / truth));
    if (ae <= 100.0) return "correct";
    if (hz > 1.5 * truth) return "high";
    if (hz < 0.75 * truth) return "low";
    return "other";
}

struct Stats
{
    int cases = 0;
    int rawCorrect = 0, rawHigh = 0, rawLow = 0, rawOther = 0;
    int exactCorrect = 0, exactHigh = 0;
    int altStrongCorrect = 0, altStrongHigh = 0;
    int altLongCorrect = 0, altLongHigh = 0;
    int strictCorrectFlagged = 0, strictHighCorrected = 0, strictWrongLower = 0;
    int vetoCorrect = 0, vetoHigh = 0, vetoOther = 0;
    int publishCorrect = 0, publishHigh = 0, publishLow = 0, publishOther = 0;
};

template <std::size_t SeedN>
void runSet(const char* name,
            const std::array<std::uint32_t, SeedN>& seeds)
{
    constexpr std::array<double, 12> freqs {
        110.0, 123.4708, 146.8324, 164.8138, 196.0, 220.0,
        246.9417, 293.6648, 329.6276, 440.0, 659.2551, 880.0
    };
    constexpr std::array<double, 3> snrs { 18.0, 9.0, 3.0 };
    constexpr std::array<int, 3> cps { 1024, 1280, 1536 };
    std::array<Stats, cps.size()> stats {};

    for (const auto& profile : profiles)
        for (double f0 : freqs)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    const auto mixed = seed ^ static_cast<std::uint32_t>(f0 * 97.0);
                    const auto x = makePrefixStableExtendedVoiceLike(profile, f0, snr, mixed);
                    for (std::size_t ci = 0; ci < cps.size(); ++ci)
                    {
                        const int n = cps[ci];
                        auto& s = stats[ci];
                        ++s.cases;
                        const auto raw = estimateRawNoLower(x, n);
                        if (!raw.valid)
                            continue;

                        const std::string rawClass = classify(raw.hz, f0);
                        if (rawClass == "correct") ++s.rawCorrect;
                        else if (rawClass == "high") ++s.rawHigh;
                        else if (rawClass == "low") ++s.rawLow;
                        else ++s.rawOther;

                        const bool exact = raw.primitiveRatio <= 0.65
                                        && 0.5 * raw.hz >= minimumF0;
                        const bool altStrong = raw.alt.observable
                                            && raw.alt.score >= 3.0
                                            && 0.5 * raw.hz >= minimumF0;
                        const bool altLong = n >= 896
                                          && raw.alt.observable
                                          && raw.primitiveRatio <= 0.85
                                          && raw.alt.significance >= 1.0
                                          && 0.5 * raw.hz >= minimumF0;
                        if (exact)
                        {
                            if (rawClass == "correct") ++s.exactCorrect;
                            if (rawClass == "high") ++s.exactHigh;
                        }
                        if (altStrong)
                        {
                            if (rawClass == "correct") ++s.altStrongCorrect;
                            if (rawClass == "high") ++s.altStrongHigh;
                        }
                        if (altLong)
                        {
                            if (rawClass == "correct") ++s.altLongCorrect;
                            if (rawClass == "high") ++s.altLongHigh;
                        }

                        const auto d = measureExtendedExclusiveLowerFamily(
                            x, n, asProgressive(raw));
                        const bool strictLower = n >= 1024
                            && d.observable
                            && d.witnesses8 >= 3
                            && d.localRatio <= 1.20;

                        if (strictLower)
                        {
                            if (rawClass == "correct")
                                ++s.strictCorrectFlagged;
                            const std::string lowerClass = classify(d.lowHz, f0);
                            if (rawClass == "high" && lowerClass == "correct")
                                ++s.strictHighCorrected;
                            else if (lowerClass != "correct")
                                ++s.strictWrongLower;
                            continue;
                        }

                        // Proposed publication veto: temporal evidence strongly
                        // supports a doubled period, but exclusive lower-family
                        // evidence is suggestive rather than sufficient to publish.
                        const bool ambiguousTwo = d.observable
                            && d.localRatio <= 0.85
                            && d.witnesses8 == 2;

                        // Legacy direct-lowering witnesses lose authority.  For
                        // diagnosis we also treat a legacy request to lower as a
                        // conflict/veto rather than as permission to publish F0/2.
                        const bool legacyConflict = exact || altStrong || altLong;
                        const bool veto = ambiguousTwo || legacyConflict;
                        if (veto)
                        {
                            if (rawClass == "correct") ++s.vetoCorrect;
                            else if (rawClass == "high") ++s.vetoHigh;
                            else ++s.vetoOther;
                            continue;
                        }

                        if (rawClass == "correct") ++s.publishCorrect;
                        else if (rawClass == "high") ++s.publishHigh;
                        else if (rawClass == "low") ++s.publishLow;
                        else ++s.publishOther;
                    }
                }

    for (std::size_t ci = 0; ci < cps.size(); ++ci)
    {
        const auto& s = stats[ci];
        std::cout << "PRIMITIVE_ABLATION"
                  << " set=" << name
                  << " samples=" << cps[ci]
                  << " ms=" << std::fixed << std::setprecision(4)
                  << (1000.0 * cps[ci] / sr)
                  << " cases=" << s.cases
                  << " raw_correct=" << s.rawCorrect
                  << " raw_high=" << s.rawHigh
                  << " raw_low=" << s.rawLow
                  << " raw_other=" << s.rawOther
                  << " exact_correct=" << s.exactCorrect
                  << " exact_high=" << s.exactHigh
                  << " altstrong_correct=" << s.altStrongCorrect
                  << " altstrong_high=" << s.altStrongHigh
                  << " altlong_correct=" << s.altLongCorrect
                  << " altlong_high=" << s.altLongHigh
                  << " strict_correct_flagged=" << s.strictCorrectFlagged
                  << " strict_high_corrected=" << s.strictHighCorrected
                  << " strict_wrong_lower=" << s.strictWrongLower
                  << " veto_correct=" << s.vetoCorrect
                  << " veto_high=" << s.vetoHigh
                  << " veto_other=" << s.vetoOther
                  << " publish_correct=" << s.publishCorrect
                  << " publish_high=" << s.publishHigh
                  << " publish_low=" << s.publishLow
                  << " publish_other=" << s.publishOther
                  << '\n';
    }
}

void printArtificialLowReason()
{
    constexpr double truth = 220.0;
    constexpr double snr = 9.0;
    constexpr std::uint32_t seed = 137296536u;
    const auto& p = strongSecondProfile();
    const auto mixed = seed ^ static_cast<std::uint32_t>(truth * 97.0);
    const auto x = makePrefixStableExtendedVoiceLike(p, truth, snr, mixed);
    const auto raw = estimateRawNoLower(x, 1024);
    const auto d = measureExtendedExclusiveLowerFamily(x, 1024, asProgressive(raw));
    std::cout << std::fixed << std::setprecision(6)
              << "ARTIFICIAL_LOW_REASON"
              << " truth=" << truth
              << " raw_hz=" << raw.hz
              << " lag=" << raw.lag
              << " periodicity=" << raw.periodicity
              << " primitive_ratio=" << raw.primitiveRatio
              << " exact=" << ((raw.primitiveRatio <= 0.65) ? 1 : 0)
              << " alt_score=" << raw.alt.score
              << " alt_significance=" << raw.alt.significance
              << " alt_strong=" << ((raw.alt.observable && raw.alt.score >= 3.0) ? 1 : 0)
              << " alt_long=" << ((raw.alt.observable && raw.primitiveRatio <= 0.85
                                      && raw.alt.significance >= 1.0) ? 1 : 0)
              << " lower_observable=" << (d.observable ? 1 : 0)
              << " lower_hz=" << d.lowHz
              << " local_ratio=" << d.localRatio
              << " witnesses8=" << d.witnesses8
              << '\n';
}
}

int main()
{
    constexpr std::array<std::uint32_t, 8> setA {
        0x0d95748fu, 0x728eb658u, 0x718bcd58u, 0x82154aeeu,
        0x7b54a41du, 0xc25a59b5u, 0x9c30d539u, 0x2af26013u
    };
    constexpr std::array<std::uint32_t, 8> setB {
        0xa4093822u, 0x299f31d0u, 0x082efa98u, 0xec4e6c89u,
        0x452821e6u, 0x38d01377u, 0xbe5466cfu, 0x34e90c6cu
    };
    constexpr std::array<std::uint32_t, 8> setC {
        0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu,
        0x1f83d9abu, 0x5be0cd19u, 0xc1059ed8u, 0x367cd507u
    };

    printArtificialLowReason();
    runSet("a", setA);
    runSet("b", setB);
    runSet("c", setC);
    return 0;
}
