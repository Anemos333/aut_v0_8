#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

#include "F0WholeNoteProgressiveProbe.cpp"

namespace
{
constexpr int extendedMaxSamples = 1536; // 32 ms @ 48 kHz
using ExtendedSignal = std::array<double, extendedMaxSamples>;

struct UnresolvedCase
{
    int setIndex;
    std::uint32_t seed;
};

constexpr std::array<UnresolvedCase, 8> unresolved {{
    { 0, 1821285621u },
    { 0, 1518500249u },
    { 1, 2600822924u },
    { 1, 1249150122u },
    { 1, 1065670069u },
    { 2, 2614888103u },
    { 3, 2438529370u },
    { 5, 2240740374u }
}};

const VoiceProfile& strongSecondProfile()
{
    for (const auto& profile : profiles)
        if (std::string(profile.name) == "strong_second")
            return profile;
    return profiles[0];
}

ExtendedSignal makePrefixStableExtendedVoiceLike(const VoiceProfile& profile,
                                                   double baseF0,
                                                   double snrDb,
                                                   std::uint32_t seed)
{
    ExtendedSignal x {};
    Rng cleanRng { seed };
    std::array<double, 24> phaseOffset {};
    for (auto& p : phaseOffset)
        p = pi * cleanRng.next();

    double phase = 0.0;
    double jitterState = 0.0;
    double shimmerState = 0.0;
    double breathState = 0.0;
    double cleanEnergy = 0.0;

    auto renderCleanSample = [&](int n, Rng& rng) noexcept
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
        return attack * shimmer * voiced + profile.breath * highBreath;
    };

    // Reproduce the original 1024-sample clean prefix exactly.
    for (int n = 0; n < progressiveMaxSamples; ++n)
    {
        const double sample = renderCleanSample(n, cleanRng);
        x[static_cast<std::size_t>(n)] = sample;
        cleanEnergy += sample * sample;
    }

    // Fork only after the exact original clean prefix. One copy continues the
    // physical source state; the other reproduces the original noise stream.
    Rng tailCleanRng = cleanRng;
    Rng noiseRng = cleanRng;

    const double rms = std::sqrt(
        cleanEnergy / static_cast<double>(progressiveMaxSamples));
    const double noiseScale = rms / std::pow(10.0, snrDb / 20.0);
    double noiseColour = 0.0;

    for (int n = 0; n < progressiveMaxSamples; ++n)
    {
        const double w = noiseRng.next();
        noiseColour = 0.72 * noiseColour + 0.28 * w;
        x[static_cast<std::size_t>(n)] +=
            noiseScale * (0.72 * w + 0.28 * noiseColour);
    }

    // Continue source phase/modulation from sample 1024. The stochastic source
    // stream is forked so the already-validated noisy prefix cannot change.
    for (int n = progressiveMaxSamples; n < extendedMaxSamples; ++n)
    {
        const double clean = renderCleanSample(n, tailCleanRng);
        const double w = noiseRng.next();
        noiseColour = 0.72 * noiseColour + 0.28 * w;
        x[static_cast<std::size_t>(n)] =
            clean + noiseScale * (0.72 * w + 0.28 * noiseColour);
    }

    return x;
}

template <std::size_t N>
AlternationWitness measureExtendedAlternation(const std::array<double, N>& x,
                                               int sampleCount,
                                               int lag) noexcept
{
    if (lag <= 0)
        return {};
    const int cycles = sampleCount / lag;
    if (cycles < 4)
        return {};

    const int evenCycles = (cycles + 1) / 2;
    const int oddCycles = cycles / 2;
    if (evenCycles < 2 || oddCycles < 2)
        return {};

    double between = 0.0;
    double within = 0.0;
    double energy = 0.0;

    for (int p = 0; p < lag; ++p)
    {
        double evenMean = 0.0, oddMean = 0.0;
        int ne = 0, no = 0;
        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            const int index = cycle * lag + p;
            if (index >= sampleCount) break;
            const double s = x[static_cast<std::size_t>(index)];
            energy += s * s;
            if ((cycle & 1) == 0) { evenMean += s; ++ne; }
            else { oddMean += s; ++no; }
        }
        if (ne == 0 || no == 0) continue;
        evenMean /= static_cast<double>(ne);
        oddMean /= static_cast<double>(no);
        const double d = evenMean - oddMean;
        between += d * d;

        for (int cycle = 0; cycle < cycles; ++cycle)
        {
            const int index = cycle * lag + p;
            if (index >= sampleCount) break;
            const double s = x[static_cast<std::size_t>(index)];
            const double mean = ((cycle & 1) == 0) ? evenMean : oddMean;
            const double e = s - mean;
            within += e * e;
        }
    }

    const double normalizedBetween =
        between / std::max(1.0e-30, energy / static_cast<double>(cycles));
    const double normalizedWithin = within / std::max(1.0e-30, energy);
    const double score = normalizedBetween / std::max(1.0e-12, normalizedWithin);
    const double effectiveGroups =
        static_cast<double>(evenCycles * oddCycles)
        / static_cast<double>(evenCycles + oddCycles);
    return { true, score, normalizedBetween, normalizedWithin,
             score * effectiveGroups, cycles };
}

template <std::size_t N>
ProgressiveEstimate estimateExtended(const std::array<double, N>& x,
                                     int sampleCount,
                                     bool requireDirectPublication = true)
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
            ab += a * b; aa += a * a; bb += b * b;
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
        if (value <= decorrelationThreshold) { decorrelated = true; continue; }
        if (!decorrelated || value < acceptance) continue;
        if (value < corr[static_cast<std::size_t>(lag - 1)]
            || value < corr[static_cast<std::size_t>(lag + 1)]) continue;
        bestLag = lag; bestCorr = value; break;
    }
    if (bestLag == 0)
        for (int lag = lagMin; lag <= lagMax; ++lag)
            if (corr[static_cast<std::size_t>(lag)] > bestCorr)
            { bestCorr = corr[static_cast<std::size_t>(lag)]; bestLag = lag; }

    if (bestLag <= 0 || bestCorr < 0.48) return {};

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
            if (std::abs(delta) <= 1.0) refinedLag += delta;
        }
    }

    double hz = sr / refinedLag;
    int finalLag = bestLag;
    double primitiveRatio = 1.0;
    bool loweredPrimitive = false;

    if (2 * bestLag < sampleCount)
    {
        const int overlap = sampleCount - 2 * bestLag;
        if (overlap >= 40)
        {
            auto mismatch = [&](int testLag)
            {
                double diff = 0.0, energy = 0.0;
                for (int n = 0; n < overlap; ++n)
                {
                    const double a = y[static_cast<std::size_t>(n)];
                    const double b = y[static_cast<std::size_t>(n + testLag)];
                    const double d = a - b;
                    diff += d * d; energy += a * a + b * b;
                }
                return diff / std::max(1.0e-30, energy);
            };
            const double one = mismatch(bestLag);
            const double two = mismatch(2 * bestLag);
            primitiveRatio = two / std::max(1.0e-12, one);
            if (primitiveRatio <= 0.65 && 0.5 * hz >= minimumF0)
            { hz *= 0.5; finalLag *= 2; loweredPrimitive = true; }
        }
    }

    if (!loweredPrimitive && 0.5 * hz >= minimumF0)
    {
        const auto alt = measureExtendedAlternation(x, sampleCount, bestLag);
        if (alt.observable && alt.score >= 3.0)
        { hz *= 0.5; finalLag *= 2; loweredPrimitive = true; }
    }

    if (!(hz >= minimumF0 && hz <= maximumF0)) return {};
    if (requireDirectPublication && 2 * finalLag >= sampleCount) return {};
    return { true, hz, bestCorr, primitiveRatio, finalLag, loweredPrimitive };
}

template <std::size_t N>
ProgressiveEstimate applyExtendedLongConsensus(const std::array<double, N>& x,
                                                int sampleCount,
                                                ProgressiveEstimate e)
{
    if (!e.valid || e.loweredPrimitive || sampleCount < 896)
        return e;
    const auto alt = measureExtendedAlternation(x, sampleCount, e.lag);
    if (alt.observable && e.primitiveRatio <= 0.85
        && alt.significance >= 1.0 && 0.5 * e.hz >= minimumF0)
    {
        e.hz *= 0.5;
        e.lag *= 2;
        e.loweredPrimitive = true;
    }
    return e;
}
}

int main()
{
    constexpr double truth = 246.9417;
    constexpr double snr = 3.0;
    constexpr std::array<int, 7> extraCheckpoints {
        1024, 1120, 1152, 1248, 1280, 1408, 1536
    };
    const auto& profile = strongSecondProfile();

    int prefixFailures = 0;
    int recovered = 0;

    for (const auto& item : unresolved)
    {
        const auto mixedSeed = item.seed
            ^ static_cast<std::uint32_t>(truth * 97.0);
        const auto base = makeProgressiveVoiceLike(profile, truth, snr, mixedSeed);
        const auto extended = makePrefixStableExtendedVoiceLike(
            profile, truth, snr, mixedSeed);

        double maxPrefixDiff = 0.0;
        for (int n = 0; n < progressiveMaxSamples; ++n)
            maxPrefixDiff = std::max(maxPrefixDiff,
                std::abs(base[static_cast<std::size_t>(n)]
                       - extended[static_cast<std::size_t>(n)]));
        if (maxPrefixDiff > 1.0e-12) ++prefixFailures;

        std::cout << std::fixed << std::setprecision(6)
                  << "EXTENDED_CASE set=" << item.setIndex
                  << " seed=" << item.seed
                  << " prefix_max_diff=" << maxPrefixDiff << '\n';

        bool caseRecovered = false;
        for (int sampleCount : extraCheckpoints)
        {
            auto e = estimateExtended(extended, sampleCount);
            if (sampleCount >= 896)
                e = applyExtendedLongConsensus(extended, sampleCount, e);
            const auto alt = e.valid
                ? measureExtendedAlternation(extended, sampleCount, e.lag)
                : AlternationWitness {};
            const double error = e.valid ? cents(e.hz, truth) : 0.0;
            const bool correct = e.valid && std::abs(error) <= 100.0;
            if (correct) caseRecovered = true;

            std::cout << " EXTENDED_STEP samples=" << sampleCount
                      << " ms=" << (1000.0 * sampleCount / sr)
                      << " valid=" << (e.valid ? 1 : 0)
                      << " hz=" << e.hz
                      << " cents=" << error
                      << " periodicity=" << e.periodicity
                      << " primitive_ratio=" << e.primitiveRatio
                      << " lowered=" << (e.loweredPrimitive ? 1 : 0)
                      << " alt_score=" << alt.score
                      << " alt_significance=" << alt.significance
                      << " correct=" << (correct ? 1 : 0)
                      << '\n';
        }
        if (caseRecovered) ++recovered;
    }

    std::cout << "EXTENDED_EIGHT_SUMMARY cases=" << unresolved.size()
              << " prefix_failures=" << prefixFailures
              << " recovered_by_32ms=" << recovered << '\n';
    return prefixFailures == 0 ? 0 : 2;
}
