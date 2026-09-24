#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

#include "F0PrimitiveAuthorityAblationProbe.cpp"

namespace
{
constexpr int longMaxSamples = 3072; // 64 ms @ 48 kHz
using LongSignal = std::array<double, longMaxSamples>;

LongSignal makeLongPrefixStableVoiceLike(const VoiceProfile& profile,
                                         double baseF0,
                                         double snrDb,
                                         std::uint32_t seed)
{
    LongSignal x {};
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
            if (hz >= 0.47 * sr) break;
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

    for (int n = 0; n < progressiveMaxSamples; ++n)
    {
        const double sample = renderCleanSample(n, cleanRng);
        x[static_cast<std::size_t>(n)] = sample;
        cleanEnergy += sample * sample;
    }

    Rng tailCleanRng = cleanRng;
    Rng noiseRng = cleanRng;
    const double rms = std::sqrt(cleanEnergy / static_cast<double>(progressiveMaxSamples));
    const double noiseScale = rms / std::pow(10.0, snrDb / 20.0);
    double noiseColour = 0.0;

    for (int n = 0; n < progressiveMaxSamples; ++n)
    {
        const double w = noiseRng.next();
        noiseColour = 0.72 * noiseColour + 0.28 * w;
        x[static_cast<std::size_t>(n)] +=
            noiseScale * (0.72 * w + 0.28 * noiseColour);
    }

    for (int n = progressiveMaxSamples; n < longMaxSamples; ++n)
    {
        const double clean = renderCleanSample(n, tailCleanRng);
        const double w = noiseRng.next();
        noiseColour = 0.72 * noiseColour + 0.28 * w;
        x[static_cast<std::size_t>(n)] =
            clean + noiseScale * (0.72 * w + 0.28 * noiseColour);
    }
    return x;
}

struct LongLowerEvidence
{
    bool observable = false;
    double lowHz = 0.0;
    double localRatio = 1.0;
    int lag = 0;
    int witnesses8 = 0;
};

double longProjectionPower(const LongSignal& x, int sampleCount, double hz)
{
    if (!(hz > 0.0) || hz >= 0.48 * sr)
        return 0.0;
    double re = 0.0, im = 0.0;
    for (int n = 0; n < sampleCount; ++n)
    {
        const double w = 0.5 - 0.5 * std::cos(
            2.0 * pi * static_cast<double>(n)
            / static_cast<double>(sampleCount - 1));
        const double p = 2.0 * pi * hz * static_cast<double>(n) / sr;
        const double s = x[static_cast<std::size_t>(n)] * w;
        re += s * std::cos(p);
        im -= s * std::sin(p);
    }
    return re * re + im * im;
}

LongLowerEvidence longExclusive(const LongSignal& x,
                                int sampleCount,
                                const RawEstimate& e)
{
    if (!e.valid || e.lag <= 0 || 0.5 * e.hz < minimumF0)
        return {};
    const int centre = 2 * e.lag;
    const int radius = std::max(2,
        static_cast<int>(std::ceil(0.05 * static_cast<double>(centre))));
    const int lo = std::max(e.lag + 2, centre - radius);
    const int hi = std::min(sampleCount - 40, centre + radius);
    if (lo > hi) return {};
    const int overlap = sampleCount - hi;
    if (overlap < 40) return {};

    auto mismatch = [&](int lag)
    {
        double diff = 0.0, energy = 0.0;
        for (int n = 0; n < overlap; ++n)
        {
            const double a = x[static_cast<std::size_t>(n)];
            const double b = x[static_cast<std::size_t>(n + lag)];
            const double d = a - b;
            diff += d * d;
            energy += a * a + b * b;
        }
        return diff / std::max(1.0e-30, energy);
    };

    const double shortMismatch = mismatch(e.lag);
    double best = std::numeric_limits<double>::infinity();
    int bestLag = 0;
    for (int lag = lo; lag <= hi; ++lag)
    {
        const double m = mismatch(lag);
        if (m < best)
        {
            best = m;
            bestLag = lag;
        }
    }
    if (bestLag <= 0) return {};
    const double lowHz = sr / static_cast<double>(bestLag);
    if (!(lowHz >= minimumF0) || !(lowHz < e.hz)) return {};

    int witnesses = 0;
    int measured = 0;
    for (int k : {1, 3, 5, 7})
    {
        const double hz = lowHz * static_cast<double>(k);
        if (hz >= 0.44 * sr) continue;
        const double centrePower = longProjectionPower(x, sampleCount, hz);
        std::array<double, 4> side {
            longProjectionPower(x, sampleCount, hz - 0.40 * lowHz),
            longProjectionPower(x, sampleCount, hz - 0.30 * lowHz),
            longProjectionPower(x, sampleCount, hz + 0.30 * lowHz),
            longProjectionPower(x, sampleCount, hz + 0.40 * lowHz)
        };
        std::sort(side.begin(), side.end());
        const double floor = 0.5 * (side[1] + side[2]);
        if (centrePower / std::max(1.0e-20, floor) >= 8.0)
            ++witnesses;
        ++measured;
    }
    if (measured < 3) return {};
    return {true, lowHz, best / std::max(1.0e-12, shortMismatch), bestLag, witnesses};
}

template <std::size_t N>
void examineSet(const char* setName,
                const std::array<std::uint32_t, N>& seeds)
{
    constexpr double truth = 246.9417;
    constexpr double snr = 3.0;
    constexpr std::array<int, 5> checkpoints {1536, 1920, 2304, 2688, 3072};
    const auto& profile = strongSecondProfile();

    int prefixFailures = 0;
    int ambiguousAt32 = 0;
    int resolvedBy40 = 0, resolvedBy48 = 0, resolvedBy56 = 0, resolvedBy64 = 0;
    int unresolvedAt64 = 0;

    for (auto seed : seeds)
    {
        const auto mixed = seed ^ static_cast<std::uint32_t>(truth * 97.0);
        const auto reference = makePrefixStableExtendedVoiceLike(profile, truth, snr, mixed);
        const auto x = makeLongPrefixStableVoiceLike(profile, truth, snr, mixed);
        double prefixDiff = 0.0;
        for (int n = 0; n < extendedMaxSamples; ++n)
            prefixDiff = std::max(prefixDiff,
                std::abs(reference[static_cast<std::size_t>(n)]
                       - x[static_cast<std::size_t>(n)]));
        if (prefixDiff > 1.0e-12)
            ++prefixFailures;

        bool tracked = false;
        bool resolved = false;
        int resolvedAt = 0;

        for (int n : checkpoints)
        {
            const auto raw = estimateRawNoLower(x, n);
            const auto d = longExclusive(x, n, raw);
            const std::string rawClass = raw.valid ? classify(raw.hz, truth) : "invalid";
            const bool strict = d.observable && d.witnesses8 >= 3 && d.localRatio <= 1.20;
            const bool ambiguous = d.observable && d.witnesses8 == 2 && d.localRatio <= 0.85;
            const bool strictCorrect = strict && std::string(classify(d.lowHz, truth)) == "correct";
            const bool rawCorrect = raw.valid && rawClass == "correct";

            if (n == 1536 && rawClass == "high" && ambiguous)
            {
                tracked = true;
                ++ambiguousAt32;
            }

            if (tracked && !resolved && (rawCorrect || strictCorrect))
            {
                resolved = true;
                resolvedAt = n;
            }

            if (tracked)
            {
                std::cout << std::fixed << std::setprecision(6)
                          << "AMBIGUOUS_EVOLUTION"
                          << " set=" << setName
                          << " seed=" << seed
                          << " samples=" << n
                          << " ms=" << (1000.0 * n / sr)
                          << " raw_hz=" << raw.hz
                          << " raw_class=" << rawClass
                          << " primitive_ratio=" << raw.primitiveRatio
                          << " alt_score=" << raw.alt.score
                          << " alt_sig=" << raw.alt.significance
                          << " low_hz=" << d.lowHz
                          << " local_ratio=" << d.localRatio
                          << " witnesses8=" << d.witnesses8
                          << " ambiguous=" << (ambiguous ? 1 : 0)
                          << " strict=" << (strict ? 1 : 0)
                          << '\n';
            }
        }

        if (tracked)
        {
            if (!resolved) ++unresolvedAt64;
            else if (resolvedAt <= 1920) ++resolvedBy40;
            else if (resolvedAt <= 2304) ++resolvedBy48;
            else if (resolvedAt <= 2688) ++resolvedBy56;
            else ++resolvedBy64;
        }
    }

    std::cout << "AMBIGUOUS_BOUND_SUMMARY"
              << " set=" << setName
              << " prefix_failures=" << prefixFailures
              << " ambiguous_at_32=" << ambiguousAt32
              << " resolved_by_40=" << resolvedBy40
              << " resolved_by_48=" << resolvedBy48
              << " resolved_by_56=" << resolvedBy56
              << " resolved_by_64=" << resolvedBy64
              << " unresolved_at_64=" << unresolvedAt64
              << '\n';
}
}

int main()
{
    constexpr std::array<std::uint32_t, 8> a {
        0x0d95748fu, 0x728eb658u, 0x718bcd58u, 0x82154aeeu,
        0x7b54a41du, 0xc25a59b5u, 0x9c30d539u, 0x2af26013u
    };
    constexpr std::array<std::uint32_t, 8> b {
        0xa4093822u, 0x299f31d0u, 0x082efa98u, 0xec4e6c89u,
        0x452821e6u, 0x38d01377u, 0xbe5466cfu, 0x34e90c6cu
    };
    constexpr std::array<std::uint32_t, 8> c {
        0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu,
        0x1f83d9abu, 0x5be0cd19u, 0xc1059ed8u, 0x367cd507u
    };
    examineSet("a", a);
    examineSet("b", b);
    examineSet("c", c);
    return 0;
}
