#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>

#define F0_WHOLE_NOTE_TUNER_NO_MAIN
#include "F0WholeNoteTunerProbe.cpp"
#undef F0_WHOLE_NOTE_TUNER_NO_MAIN

namespace
{
constexpr int progressiveMaxSamples = 1024;

std::array<double, progressiveMaxSamples>
makeProgressiveVoiceLike(const VoiceProfile& profile,
                         double baseF0,
                         double snrDb,
                         std::uint32_t seed)
{
    std::array<double, progressiveMaxSamples> x {};
    Rng rng { seed };
    std::array<double, 24> phaseOffset {};
    for (auto& p : phaseOffset)
        p = pi * rng.next();

    double phase = 0.0;
    double jitterState = 0.0;
    double shimmerState = 0.0;
    double breathState = 0.0;
    double cleanEnergy = 0.0;

    for (int n = 0; n < progressiveMaxSamples; ++n)
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

    const double rms = std::sqrt(cleanEnergy / static_cast<double>(progressiveMaxSamples));
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


struct AlternationWitness
{
    bool observable = false;
    double score = 0.0;
    double between = 0.0;
    double within = 0.0;
    int cycles = 0;
};

AlternationWitness measureCycleAlternation(
    const std::array<double, progressiveMaxSamples>& x,
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
            if ((cycle & 1) == 0)
            {
                evenMean += s;
                ++ne;
            }
            else
            {
                oddMean += s;
                ++no;
            }
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
            const double mean = ((cycle & 1) == 0) ? evenMean : oddMean;
            const double e = s - mean;
            within += e * e;
        }
    }

    const double normalizedBetween =
        between / std::max(1.0e-30, energy / static_cast<double>(cycles));
    const double normalizedWithin =
        within / std::max(1.0e-30, energy);
    const double score =
        normalizedBetween / std::max(1.0e-12, normalizedWithin);

    return { true, score, normalizedBetween, normalizedWithin, cycles };
}

struct ProgressiveEstimate
{
    bool valid = false;
    double hz = 0.0;
    double periodicity = 0.0;
    double primitiveRatio = 1.0;
    int lag = 0;
};

ProgressiveEstimate estimateProgressive(
    const std::array<double, progressiveMaxSamples>& x,
    int sampleCount)
{
    std::array<double, progressiveMaxSamples> y {};
    double mean = 0.0;
    for (int n = 0; n < sampleCount; ++n)
        mean += x[static_cast<std::size_t>(n)];
    mean /= static_cast<double>(sampleCount);

    for (int n = 0; n < sampleCount; ++n)
        y[static_cast<std::size_t>(n)] =
            x[static_cast<std::size_t>(n)] - mean;

    const int lagMin =
        std::max(2, static_cast<int>(std::floor(sr / maximumF0)));
    const int lagMax =
        std::min(sampleCount - 8,
                 static_cast<int>(std::ceil(sr / minimumF0)));

    std::array<double, progressiveMaxSamples> corr {};
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
                double diff = 0.0;
                double energy = 0.0;
                for (int n = 0; n < overlap; ++n)
                {
                    const double a = y[static_cast<std::size_t>(n)];
                    const double b = y[static_cast<std::size_t>(n + testLag)];
                    const double d = a - b;
                    diff += d * d;
                    energy += a * a + b * b;
                }
                return diff / std::max(1.0e-30, energy);
            };

            const double one = mismatch(bestLag);
            const double two = mismatch(2 * bestLag);
            primitiveRatio = two / std::max(1.0e-12, one);

            if (primitiveRatio <= 0.65 && 0.5 * hz >= minimumF0)
            {
                hz *= 0.5;
                finalLag *= 2;
                loweredPrimitive = true;
            }
        }
    }

    // Independent whole-wave primitive witness: if candidate-length cycles
    // alternate A/B/A/B with a very strong parity signature, the candidate
    // is the half-period. This witness is allowed to lower exactly once.
    if (!loweredPrimitive && 0.5 * hz >= minimumF0)
    {
        const auto alt = measureCycleAlternation(x, sampleCount, bestLag);
        if (alt.observable && alt.score >= 3.0)
        {
            hz *= 0.5;
            finalLag *= 2;
            loweredPrimitive = true;
        }
    }

    if (!(hz >= minimumF0 && hz <= maximumF0))
        return {};

    // Direct publication requires two periods of the final claimed F0.
    if (2 * finalLag >= sampleCount)
        return {};

    return { true, hz, bestCorr, primitiveRatio, finalLag };
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
        0x6c8e9cf5u, 0x5a827999u, 0x3c6ef372u,
        0xbb67ae85u, 0xa54ff53au, 0x510e527fu
    };
    constexpr std::array<int, 6> checkpoints {
        448, 576, 672, 768, 896, 1024
    };
    constexpr std::array<std::uint32_t, 6> alternationVerificationSeeds {
        0x8f1bbcdcu, 0xca62c1d6u, 0x9b05688cu,
        0x1f83d9abu, 0x4a7484aau, 0x3f84d5b5u
    };
    constexpr std::array<std::uint32_t, 6> integratedVerificationSeeds {
        0x5be0cd19u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u
    };

    int cases = 0;
    int firstCorrect = 0;
    int firstWrong = 0;
    int neverPublished = 0;
    int artificialLow = 0;
    int octaveHigh = 0;
    std::array<int, checkpoints.size()> firstAt {};
    std::array<int, checkpoints.size()> correctAt {};

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : seeds)
                {
                    ++cases;
                    const auto x = makeProgressiveVoiceLike(
                        profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 97.0));

                    bool published = false;
                    for (std::size_t i = 0; i < checkpoints.size(); ++i)
                    {
                        const auto e = estimateProgressive(x, checkpoints[i]);
                        if (!e.valid)
                            continue;

                        ++firstAt[i];
                        const double error = cents(e.hz, f0);
                        const double ae = std::abs(error);

                        if (ae <= 100.0)
                        {
                            ++firstCorrect;
                            ++correctAt[i];
                        }
                        else
                        {
                            ++firstWrong;
                            if (e.hz < 0.75 * f0) ++artificialLow;
                            if (e.hz > 1.5 * f0) ++octaveHigh;

                            std::cout << std::fixed << std::setprecision(4)
                                      << "PROGRESSIVE_WRONG_EVOLUTION profile=" << profile.name
                                      << " hz=" << f0
                                      << " snr=" << snr
                                      << " seed=" << seed
                                      << " first_samples=" << checkpoints[i]
                                      << " first_hz=" << e.hz
                                      << " first_cents=" << error;

                            for (std::size_t j = i + 1; j < checkpoints.size(); ++j)
                            {
                                const auto later = estimateProgressive(x, checkpoints[j]);
                                std::cout << " s" << checkpoints[j] << "_valid="
                                          << (later.valid ? 1 : 0)
                                          << " s" << checkpoints[j] << "_hz=" << later.hz;
                                if (later.valid)
                                    std::cout << " s" << checkpoints[j] << "_cents="
                                              << cents(later.hz, f0);
                            }
                            std::cout << '\n';
                        }

                        const auto alternation =
                            measureCycleAlternation(x, checkpoints[i], e.lag);

                        std::cout << std::fixed << std::setprecision(4)
                                  << "PROGRESSIVE_FIRST profile=" << profile.name
                                  << " hz=" << f0
                                  << " snr=" << snr
                                  << " seed=" << seed
                                  << " samples=" << checkpoints[i]
                                  << " ms="
                                  << (1000.0 * checkpoints[i] / sr)
                                  << " estimate=" << e.hz
                                  << " cents=" << error
                                  << " periodicity=" << e.periodicity
                                  << " primitive_ratio=" << e.primitiveRatio
                                  << " alt_observable=" << (alternation.observable ? 1 : 0)
                                  << " alt_score=" << alternation.score
                                  << " alt_between=" << alternation.between
                                  << " alt_within=" << alternation.within
                                  << " alt_cycles=" << alternation.cycles
                                  << '\n';
                        published = true;
                        break;
                    }

                    if (!published)
                        ++neverPublished;
                }


    int altVerifyCases = 0;
    int altVerifyBaseValid = 0;
    int altVerifyBaseCorrect = 0;
    int altVerifyBaseWrong = 0;
    int altVerifyCorrectedCorrect = 0;
    int altVerifyCorrectedWrong = 0;
    int altVerifyChanged = 0;
    int altVerifyCorrectToWrong = 0;
    int altVerifyArtificialLow = 0;
    int altVerifyOctaveHigh = 0;

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : alternationVerificationSeeds)
                {
                    ++altVerifyCases;
                    const auto x = makeProgressiveVoiceLike(
                        profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 97.0));
                    const auto base = estimateProgressive(x, 448);
                    if (!base.valid)
                        continue;

                    ++altVerifyBaseValid;
                    const double baseError = std::abs(cents(base.hz, f0));
                    if (baseError <= 100.0) ++altVerifyBaseCorrect;
                    else ++altVerifyBaseWrong;

                    double correctedHz = base.hz;
                    const auto alt = measureCycleAlternation(x, 448, base.lag);
                    (void) alt;

                    const double correctedError =
                        std::abs(cents(correctedHz, f0));
                    if (correctedError <= 100.0)
                        ++altVerifyCorrectedCorrect;
                    else
                        ++altVerifyCorrectedWrong;

                    if (baseError <= 100.0 && correctedError > 100.0)
                        ++altVerifyCorrectToWrong;
                    if (correctedHz < 0.75 * f0)
                        ++altVerifyArtificialLow;
                    if (correctedHz > 1.5 * f0)
                        ++altVerifyOctaveHigh;
                }


    int integratedCases = 0;
    int integratedFirstCorrect = 0;
    int integratedFirstWrong = 0;
    int integratedNever = 0;
    int integratedLow = 0;
    int integratedHigh = 0;

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : integratedVerificationSeeds)
                {
                    ++integratedCases;
                    const auto x = makeProgressiveVoiceLike(
                        profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 97.0));

                    bool published = false;
                    for (int sampleCount : checkpoints)
                    {
                        const auto e = estimateProgressive(x, sampleCount);
                        if (!e.valid)
                            continue;

                        const double ae = std::abs(cents(e.hz, f0));
                        if (ae <= 100.0) ++integratedFirstCorrect;
                        else
                        {
                            ++integratedFirstWrong;
                            if (e.hz < 0.75 * f0) ++integratedLow;
                            if (e.hz > 1.5 * f0) ++integratedHigh;
                        }
                        published = true;
                        break;
                    }
                    if (!published) ++integratedNever;
                }

    std::cout << "INTEGRATED_VERIFY"
              << " cases=" << integratedCases
              << " first_correct=" << integratedFirstCorrect
              << " first_wrong=" << integratedFirstWrong
              << " never=" << integratedNever
              << " artificial_low=" << integratedLow
              << " octave_high=" << integratedHigh
              << '\n';

    std::cout << "ALTERNATION_VERIFY"
              << " cases=" << altVerifyCases
              << " base_valid=" << altVerifyBaseValid
              << " base_correct=" << altVerifyBaseCorrect
              << " base_wrong=" << altVerifyBaseWrong
              << " corrected_correct=" << altVerifyCorrectedCorrect
              << " corrected_wrong=" << altVerifyCorrectedWrong
              << " changed=" << altVerifyChanged
              << " correct_to_wrong=" << altVerifyCorrectToWrong
              << " artificial_low=" << altVerifyArtificialLow
              << " octave_high=" << altVerifyOctaveHigh
              << '\n';

    std::cout << "PROGRESSIVE_SUMMARY"
              << " cases=" << cases
              << " first_correct=" << firstCorrect
              << " first_wrong=" << firstWrong
              << " never_published=" << neverPublished
              << " artificial_low=" << artificialLow
              << " octave_high=" << octaveHigh;

    for (std::size_t i = 0; i < checkpoints.size(); ++i)
        std::cout << " first_" << checkpoints[i] << "=" << firstAt[i]
                  << " correct_" << checkpoints[i] << "=" << correctAt[i];

    std::cout << '\n';
    return 0;
}
