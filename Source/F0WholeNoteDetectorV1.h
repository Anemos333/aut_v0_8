#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

// Standalone V1 whole-note F0 analysis core.
//
// This file deliberately contains no renderer, ScaleQuantizer, confidence
// export, dry/wet logic or ModernPitchEngine dependency.  It answers only the
// physical question owned by the detector: which primitive periodic family is
// supported by the current monophonic analysis window?
//
// Promotion status: candidate core for real-voice validation.  The stateful
// stable/transition publisher remains separate until this core has survived
// recorded-voice and hostile-real-world tests.
class F0WholeNoteDetectorV1 final
{
public:
    struct Analysis
    {
        bool valid = false;
        double hz = 0.0;
        double periodicity = 0.0;
        int lag = 0;
        int samples = 0;
        bool primitiveLowered = false;
        bool exclusiveLowerWitness = false;
    };

    void prepare(double sampleRate,
                 double minimumHz = 55.0,
                 double maximumHz = 1600.0) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate)
            ? std::max(8000.0, sampleRate) : 48000.0;
        minimumHz_ = std::max(20.0, minimumHz);
        maximumHz_ = std::max(minimumHz_ + 1.0, maximumHz);
    }

    [[nodiscard]] Analysis analyse(const float* samples,
                                   int sampleCount) const noexcept
    {
        if (samples == nullptr || sampleCount < 96
            || sampleCount > maxAnalysisSamples)
            return {};

        std::array<double, maxAnalysisSamples> x {};
        double mean = 0.0;
        double energy = 0.0;
        for (int n = 0; n < sampleCount; ++n)
        {
            double s = static_cast<double>(samples[n]);
            if (!std::isfinite(s) || std::fpclassify(s) == FP_SUBNORMAL)
                s = 0.0;
            x[static_cast<std::size_t>(n)] = s;
            mean += s;
            energy += s * s;
        }
        mean /= static_cast<double>(sampleCount);

        const double rms = std::sqrt(
            energy / static_cast<double>(sampleCount));
        if (!(rms > 1.0e-7))
            return {};

        for (int n = 0; n < sampleCount; ++n)
            x[static_cast<std::size_t>(n)] -= mean;

        const int lagMin = std::max(
            2, static_cast<int>(std::floor(sampleRate_ / maximumHz_)));
        const int lagMax = std::min(
            sampleCount - 8,
            static_cast<int>(std::ceil(sampleRate_ / minimumHz_)));
        if (lagMin >= lagMax || lagMax >= maxAnalysisSamples)
            return {};

        std::array<double, maxAnalysisSamples> corr {};
        double strongest = -1.0;
        for (int lag = lagMin; lag <= lagMax; ++lag)
        {
            const int overlap = sampleCount - lag;
            double ab = 0.0;
            double aa = 0.0;
            double bb = 0.0;
            for (int n = 0; n < overlap; ++n)
            {
                const double a = x[static_cast<std::size_t>(n)];
                const double b = x[static_cast<std::size_t>(n + lag)];
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

        // A tuner-style period peak is meaningful only after the waveform has
        // first decorrelated from the zero-lag neighbourhood.  This blocks the
        // tiny-lag/high-frequency false periods produced by smooth waveforms.
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

        double hz = sampleRate_ / refinedLag;
        int finalLag = bestLag;
        bool primitiveLowered = false;
        bool exclusiveLower = false;
        double primitiveRatio = 1.0;

        if (2 * bestLag < sampleCount)
        {
            const int overlap = sampleCount - 2 * bestLag;
            if (overlap >= 40)
            {
                const double one = mismatch(x, sampleCount, overlap, bestLag);
                const double two = mismatch(x, sampleCount, overlap, 2 * bestLag);
                primitiveRatio = two / std::max(1.0e-12, one);
                if (primitiveRatio <= 0.65 && 0.5 * hz >= minimumHz_)
                {
                    hz *= 0.5;
                    finalLag *= 2;
                    primitiveLowered = true;
                }
            }
        }

        if (!primitiveLowered && 0.5 * hz >= minimumHz_)
        {
            const auto alt = alternation(x, sampleCount, bestLag);
            if (alt.observable && alt.score >= 3.0)
            {
                hz *= 0.5;
                finalLag *= 2;
                primitiveLowered = true;
            }
        }

        // With more causal evidence, two independent whole-wave witnesses may
        // agree that the short period is a half-period.  This remains the same
        // validator; it is not a second F0 search.
        if (!primitiveLowered
            && sampleCount >= samplesForMs(18.667)
            && 0.5 * hz >= minimumHz_)
        {
            const auto alt = alternation(x, sampleCount, bestLag);
            if (alt.observable && primitiveRatio <= 0.85
                && alt.significance >= 1.0)
            {
                hz *= 0.5;
                finalLag *= 2;
                primitiveLowered = true;
            }
        }

        // Strict lower-family safety belt.  The candidate below is not searched
        // globally: it is confined to a small neighbourhood around exactly the
        // doubled period.  It may speak only when >=3 odd harmonics that the
        // high family cannot explain each stand >=8x above their local floor.
        // The threshold was frozen before the fresh extended holdout.
        if (!primitiveLowered
            && sampleCount >= samplesForMs(21.333)
            && 0.5 * hz >= minimumHz_)
        {
            const auto lower = exclusiveLowerFamily(x, sampleCount, bestLag);
            if (lower.observable && lower.witnesses8 >= 3)
            {
                hz = lower.hz;
                finalLag = lower.lag;
                primitiveLowered = true;
                exclusiveLower = true;
            }
        }

        if (!(hz >= minimumHz_ && hz <= maximumHz_) || !std::isfinite(hz))
            return {};

        if (2 * finalLag >= sampleCount)
            return {};

        return {
            true,
            hz,
            bestCorr,
            finalLag,
            sampleCount,
            primitiveLowered,
            exclusiveLower
        };
    }

    [[nodiscard]] static constexpr int maximumAnalysisSamples() noexcept
    {
        return maxAnalysisSamples;
    }

private:
    struct Alternation
    {
        bool observable = false;
        double score = 0.0;
        double significance = 0.0;
    };

    struct LowerFamily
    {
        bool observable = false;
        double hz = 0.0;
        int lag = 0;
        int witnesses8 = 0;
    };

    static constexpr int maxAnalysisSamples = 1536; // 32 ms @ 48 kHz
    static constexpr double pi = 3.141592653589793238462643383279502884;

    [[nodiscard]] int samplesForMs(double milliseconds) const noexcept
    {
        return static_cast<int>(std::lround(
            0.001 * milliseconds * sampleRate_));
    }

    [[nodiscard]] static double mismatch(
        const std::array<double, maxAnalysisSamples>& x,
        int sampleCount,
        int overlap,
        int lag) noexcept
    {
        if (lag <= 0 || lag >= sampleCount || overlap <= 0
            || overlap + lag > sampleCount)
            return std::numeric_limits<double>::infinity();

        double diff = 0.0;
        double energy = 0.0;
        for (int n = 0; n < overlap; ++n)
        {
            const double a = x[static_cast<std::size_t>(n)];
            const double b = x[static_cast<std::size_t>(n + lag)];
            const double d = a - b;
            diff += d * d;
            energy += a * a + b * b;
        }
        return diff / std::max(1.0e-30, energy);
    }

    [[nodiscard]] static Alternation alternation(
        const std::array<double, maxAnalysisSamples>& x,
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

        for (int phase = 0; phase < lag; ++phase)
        {
            double evenMean = 0.0;
            double oddMean = 0.0;
            int ne = 0;
            int no = 0;
            for (int cycle = 0; cycle < cycles; ++cycle)
            {
                const int index = cycle * lag + phase;
                if (index >= sampleCount)
                    break;
                const double s = x[static_cast<std::size_t>(index)];
                energy += s * s;
                if ((cycle & 1) == 0) { evenMean += s; ++ne; }
                else { oddMean += s; ++no; }
            }
            if (ne == 0 || no == 0)
                continue;
            evenMean /= static_cast<double>(ne);
            oddMean /= static_cast<double>(no);
            const double d = evenMean - oddMean;
            between += d * d;

            for (int cycle = 0; cycle < cycles; ++cycle)
            {
                const int index = cycle * lag + phase;
                if (index >= sampleCount)
                    break;
                const double s = x[static_cast<std::size_t>(index)];
                const double m = ((cycle & 1) == 0) ? evenMean : oddMean;
                const double e = s - m;
                within += e * e;
            }
        }

        const double normBetween = between
            / std::max(1.0e-30, energy / static_cast<double>(cycles));
        const double normWithin = within / std::max(1.0e-30, energy);
        const double score = normBetween / std::max(1.0e-12, normWithin);
        const double groups = static_cast<double>(evenCycles * oddCycles)
            / static_cast<double>(evenCycles + oddCycles);
        return { true, score, score * groups };
    }

    [[nodiscard]] static double projectionPower(
        const std::array<double, maxAnalysisSamples>& x,
        int sampleCount,
        double sampleRate,
        double hz) noexcept
    {
        if (!(hz > 0.0) || hz >= 0.48 * sampleRate)
            return 0.0;

        double re = 0.0;
        double im = 0.0;
        for (int n = 0; n < sampleCount; ++n)
        {
            const double w = 0.5 - 0.5 * std::cos(
                2.0 * pi * static_cast<double>(n)
                / static_cast<double>(sampleCount - 1));
            const double p = 2.0 * pi * hz * static_cast<double>(n)
                           / sampleRate;
            const double s = x[static_cast<std::size_t>(n)] * w;
            re += s * std::cos(p);
            im -= s * std::sin(p);
        }
        return re * re + im * im;
    }

    [[nodiscard]] LowerFamily exclusiveLowerFamily(
        const std::array<double, maxAnalysisSamples>& x,
        int sampleCount,
        int shortLag) const noexcept
    {
        if (shortLag <= 0)
            return {};

        const int centre = 2 * shortLag;
        const int radius = std::max(2,
            static_cast<int>(std::ceil(0.05 * static_cast<double>(centre))));
        const int lo = std::max(shortLag + 2, centre - radius);
        const int hi = std::min(sampleCount - 40, centre + radius);
        if (lo > hi)
            return {};

        const int overlap = sampleCount - hi;
        if (overlap < 40)
            return {};

        double bestMismatch = std::numeric_limits<double>::infinity();
        int bestLag = 0;
        for (int lag = lo; lag <= hi; ++lag)
        {
            const double m = mismatch(x, sampleCount, overlap, lag);
            if (m < bestMismatch)
            {
                bestMismatch = m;
                bestLag = lag;
            }
        }
        if (bestLag <= 0)
            return {};

        const double lowHz = sampleRate_ / static_cast<double>(bestLag);
        if (!(lowHz >= minimumHz_))
            return {};

        int witnesses = 0;
        int measured = 0;
        for (int k : { 1, 3, 5, 7 })
        {
            const double hz = lowHz * static_cast<double>(k);
            if (hz >= 0.44 * sampleRate_)
                continue;

            const double centrePower = projectionPower(
                x, sampleCount, sampleRate_, hz);
            std::array<double, 4> side {
                projectionPower(x, sampleCount, sampleRate_, hz - 0.40 * lowHz),
                projectionPower(x, sampleCount, sampleRate_, hz - 0.30 * lowHz),
                projectionPower(x, sampleCount, sampleRate_, hz + 0.30 * lowHz),
                projectionPower(x, sampleCount, sampleRate_, hz + 0.40 * lowHz)
            };
            std::sort(side.begin(), side.end());
            const double floor = 0.5 * (side[1] + side[2]);
            const double ratio = centrePower / std::max(1.0e-20, floor);
            if (ratio >= 8.0)
                ++witnesses;
            ++measured;
        }

        if (measured < 3)
            return {};
        return { true, lowHz, bestLag, witnesses };
    }

    double sampleRate_ = 48000.0;
    double minimumHz_ = 55.0;
    double maximumHz_ = 1600.0;
};
