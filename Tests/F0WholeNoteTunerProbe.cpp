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


struct PrimitiveWitness
{
    bool observable = false;
    double lagMismatch = 1.0;
    double doubleLagMismatch = 1.0;
    double improvement = 0.0;
    double ratio = 1.0;
    int overlap = 0;
};

PrimitiveWitness measurePrimitiveWitness(const std::array<double, frameSize>& x,
                                         int lag) noexcept
{
    if (lag <= 0 || 2 * lag >= frameSize)
        return {};

    const int overlap = frameSize - 2 * lag;
    if (overlap < 40)
        return {};

    double mean = 0.0;
    for (double s : x) mean += s;
    mean /= static_cast<double>(frameSize);

    auto mismatch = [&](int testLag) noexcept
    {
        double diff = 0.0;
        double energy = 0.0;
        for (int n = 0; n < overlap; ++n)
        {
            const double a = x[static_cast<std::size_t>(n)] - mean;
            const double b = x[static_cast<std::size_t>(n + testLag)] - mean;
            const double d = a - b;
            diff += d * d;
            energy += a * a + b * b;
        }
        return diff / std::max(1.0e-30, energy);
    };

    const double one = mismatch(lag);
    const double two = mismatch(2 * lag);
    return {
        true,
        one,
        two,
        one - two,
        two / std::max(1.0e-12, one),
        overlap
    };
}

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

    // A period peak is meaningful only after the waveform has first
    // decorrelated from its zero-lag neighbourhood. Without this guard,
    // smooth/formant-dominated notes can make a tiny lag look like a
    // high-frequency period simply because adjacent samples are similar.
    constexpr double decorrelationThreshold = 0.35;
    bool decorrelated = false;

    for (int lag = lagMin + 1; lag < lagMax; ++lag)
    {
        const double c = corr[static_cast<std::size_t>(lag)];
        if (c <= decorrelationThreshold)
        {
            decorrelated = true;
            continue;
        }
        if (!decorrelated || c < acceptance)
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


WholeNoteEstimate applyPrimitiveValidator(const std::array<double, frameSize>& x,
                                          WholeNoteEstimate e) noexcept
{
    if (!e.valid)
        return e;

    const auto witness = measurePrimitiveWitness(x, e.lag);
    constexpr double octaveDownRatio = 0.65;

    if (!witness.observable || witness.ratio > octaveDownRatio)
        return e;

    const double loweredHz = 0.5 * e.hz;
    if (loweredHz < minimumF0)
        return e;

    e.hz = loweredHz;
    e.lag *= 2;
    return e;
}


WholeNoteEstimate applyDirectObservabilityGuard(WholeNoteEstimate e) noexcept
{
    if (!e.valid)
        return e;

    // A direct whole-wave repetition claim is structurally publishable only
    // when two candidate periods fit in the causal frame. If not, the
    // current time-domain estimator has insufficient direct repetition
    // evidence and must remain transition. A later estimator may recover
    // these cases using additional whole-note information.
    if (2 * e.lag >= frameSize)
        return {};

    return e;
}

#ifndef F0_WHOLE_NOTE_TUNER_NO_MAIN

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
    constexpr std::array<std::uint32_t, 6> holdoutSeeds {
        0x31415926u, 0x27182818u, 0x13579bdfu,
        0x2468ace0u, 0xc001d00du, 0x7f4a7c15u
    };
    constexpr std::array<std::uint32_t, 6> verificationSeeds {
        0x6c8e9cf5u, 0x5a827999u, 0x3c6ef372u,
        0xbb67ae85u, 0xa54ff53au, 0x510e527fu
    };

    int cases = 0;
    int valid = 0;
    int familyCorrect = 0;
    int wrongFamily = 0;
    int artificialLow = 0;
    int octaveHigh = 0;
    int precision = 0;
    int validatedCorrect = 0;
    int validatedWrong = 0;
    int validatedArtificialLow = 0;
    int validatedOctaveHigh = 0;
    int validatedPrecision = 0;
    int witnessObservable = 0;
    int octaveHighWitnessObservable = 0;
    int octaveHighDoubleBetter = 0;
    int correctDoubleBetter = 0;
    double octaveHighRatioSum = 0.0;
    double correctRatioSum = 0.0;
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
                    const auto validated = applyPrimitiveValidator(x, e);
                    const auto end = std::chrono::steady_clock::now();
                    micros.push_back(std::chrono::duration<double, std::micro>(
                        end - start).count());

                    ++cases;
                    double err = std::numeric_limits<double>::quiet_NaN();
                    PrimitiveWitness witness {};
                    if (e.valid)
                    {
                        witness = measurePrimitiveWitness(x, e.lag);
                        if (witness.observable) ++witnessObservable;
                        ++valid;
                        err = cents(e.hz, f0);
                        const double ae = std::abs(err);
                        worstCents = std::max(worstCents, ae);

                        if (ae <= 100.0) ++familyCorrect;
                        else ++wrongFamily;
                        if (e.hz < 0.75 * f0) ++artificialLow;
                        if (e.hz > 1.5 * f0)
                        {
                            ++octaveHigh;
                            if (witness.observable)
                            {
                                ++octaveHighWitnessObservable;
                                octaveHighRatioSum += witness.ratio;
                                if (witness.doubleLagMismatch < witness.lagMismatch)
                                    ++octaveHighDoubleBetter;
                            }
                        }
                        else if (ae <= 100.0 && witness.observable)
                        {
                            correctRatioSum += witness.ratio;
                            if (witness.doubleLagMismatch < witness.lagMismatch)
                                ++correctDoubleBetter;
                        }
                        if (ae <= 1.5) ++precision;
                    }

                    if (validated.valid)
                    {
                        const double ve = cents(validated.hz, f0);
                        const double ave = std::abs(ve);
                        if (ave <= 100.0) ++validatedCorrect;
                        else ++validatedWrong;
                        if (validated.hz < 0.75 * f0) ++validatedArtificialLow;
                        if (validated.hz > 1.5 * f0) ++validatedOctaveHigh;
                        if (ave <= 1.5) ++validatedPrecision;
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
                              << " witness_observable=" << (witness.observable ? 1 : 0)
                              << " mismatch_lag=" << witness.lagMismatch
                              << " mismatch_2lag=" << witness.doubleLagMismatch
                              << " witness_improvement=" << witness.improvement
                              << " witness_ratio=" << witness.ratio
                              << " witness_overlap=" << witness.overlap
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
              << " validated_correct=" << validatedCorrect
              << " validated_wrong=" << validatedWrong
              << " validated_artificial_low=" << validatedArtificialLow
              << " validated_octave_high=" << validatedOctaveHigh
              << " validated_precision_1_5c=" << validatedPrecision
              << " witness_observable=" << witnessObservable
              << " octave_high_witness_observable=" << octaveHighWitnessObservable
              << " octave_high_double_better=" << octaveHighDoubleBetter
              << " octave_high_mean_ratio="
              << (octaveHighWitnessObservable > 0
                    ? octaveHighRatioSum / static_cast<double>(octaveHighWitnessObservable)
                    : 0.0)
              << " correct_double_better=" << correctDoubleBetter
              << " correct_mean_ratio="
              << ((familyCorrect - octaveHigh) > 0
                    ? correctRatioSum / static_cast<double>(std::max(1, witnessObservable - octaveHighWitnessObservable))
                    : 0.0)
              << " worst_cents=" << worstCents
              << " mean_us=" << meanUs
              << " p95_us=" << p95Us
              << " max_us=" << maxUs
              << '\n';


    int holdoutCases = 0;
    int holdoutBaseCorrect = 0;
    int holdoutBaseWrong = 0;
    int holdoutValidatedCorrect = 0;
    int holdoutValidatedWrong = 0;
    int holdoutBaseLow = 0;
    int holdoutValidatedLow = 0;
    int holdoutBaseHigh = 0;
    int holdoutValidatedHigh = 0;
    int holdoutChanged = 0;
    int holdoutChangedCorrectToWrong = 0;

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : holdoutSeeds)
                {
                    const auto x = makeVoiceLikeFrame(
                        profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 97.0));
                    const auto base = estimateWholeNote(x);
                    const auto validated = applyPrimitiveValidator(x, base);
                    ++holdoutCases;

                    if (base.valid)
                    {
                        const double be = std::abs(cents(base.hz, f0));
                        if (be <= 100.0) ++holdoutBaseCorrect;
                        else ++holdoutBaseWrong;
                        if (base.hz < 0.75 * f0) ++holdoutBaseLow;
                        if (base.hz > 1.5 * f0) ++holdoutBaseHigh;
                    }

                    if (validated.valid)
                    {
                        const double ve = std::abs(cents(validated.hz, f0));
                        if (ve <= 100.0) ++holdoutValidatedCorrect;
                        else ++holdoutValidatedWrong;
                        if (validated.hz < 0.75 * f0) ++holdoutValidatedLow;
                        if (validated.hz > 1.5 * f0) ++holdoutValidatedHigh;
                    }

                    if (base.valid && validated.valid
                        && std::abs(base.hz - validated.hz) > 1.0e-9)
                    {
                        ++holdoutChanged;
                        const bool baseWasCorrect =
                            std::abs(cents(base.hz, f0)) <= 100.0;
                        const bool validatedIsWrong =
                            std::abs(cents(validated.hz, f0)) > 100.0;
                        if (baseWasCorrect && validatedIsWrong)
                            ++holdoutChangedCorrectToWrong;

                        const auto w = measurePrimitiveWitness(x, base.lag);
                        std::cout << std::fixed << std::setprecision(4)
                                  << "WHOLE_NOTE_HOLDOUT_CHANGE profile=" << profile.name
                                  << " hz=" << f0
                                  << " snr=" << snr
                                  << " seed=" << seed
                                  << " base_hz=" << base.hz
                                  << " validated_hz=" << validated.hz
                                  << " base_cents=" << cents(base.hz, f0)
                                  << " validated_cents=" << cents(validated.hz, f0)
                                  << " witness_ratio=" << w.ratio
                                  << " mismatch_lag=" << w.lagMismatch
                                  << " mismatch_2lag=" << w.doubleLagMismatch
                                  << '\n';
                    }
                }

    std::cout << "WHOLE_NOTE_HOLDOUT"
              << " cases=" << holdoutCases
              << " base_correct=" << holdoutBaseCorrect
              << " base_wrong=" << holdoutBaseWrong
              << " validated_correct=" << holdoutValidatedCorrect
              << " validated_wrong=" << holdoutValidatedWrong
              << " base_low=" << holdoutBaseLow
              << " validated_low=" << holdoutValidatedLow
              << " base_high=" << holdoutBaseHigh
              << " validated_high=" << holdoutValidatedHigh
              << " changed=" << holdoutChanged
              << " correct_to_wrong=" << holdoutChangedCorrectToWrong
              << '\n';


    int verificationCases = 0;
    int verificationBaseCorrect = 0;
    int verificationBaseWrong = 0;
    int verificationValidatedCorrect = 0;
    int verificationValidatedWrong = 0;
    int verificationBaseLow = 0;
    int verificationValidatedLow = 0;
    int verificationBaseHigh = 0;
    int verificationValidatedHigh = 0;
    int verificationChanged = 0;
    int verificationCorrectToWrong = 0;
    int verificationGuardedValid = 0;
    int verificationGuardedCorrect = 0;
    int verificationGuardedWrong = 0;
    int verificationGuardedHigh = 0;
    int verificationGuardedLow = 0;
    int verificationResidualOctaveHigh = 0;
    int verificationResidualArtificialLow = 0;
    int verificationResidualOther = 0;

    for (const auto& profile : profiles)
        for (double f0 : frequencies)
            for (double snr : snrs)
                for (auto seed : verificationSeeds)
                {
                    const auto x = makeVoiceLikeFrame(
                        profile, f0, snr,
                        seed ^ static_cast<std::uint32_t>(f0 * 97.0));
                    const auto base = estimateWholeNote(x);
                    const auto validated = applyPrimitiveValidator(x, base);
                    const auto guarded = applyDirectObservabilityGuard(validated);
                    ++verificationCases;

                    if (base.valid)
                    {
                        const double be = std::abs(cents(base.hz, f0));
                        if (be <= 100.0) ++verificationBaseCorrect;
                        else ++verificationBaseWrong;
                        if (base.hz < 0.75 * f0) ++verificationBaseLow;
                        if (base.hz > 1.5 * f0) ++verificationBaseHigh;
                    }

                    if (validated.valid)
                    {
                        const double signedVe = cents(validated.hz, f0);
                        const double ve = std::abs(signedVe);
                        if (ve <= 100.0) ++verificationValidatedCorrect;
                        else
                        {
                            ++verificationValidatedWrong;
                            const bool octaveHigh = validated.hz > 1.5 * f0;
                            const bool artificialLow = validated.hz < 0.75 * f0;
                            if (octaveHigh) ++verificationResidualOctaveHigh;
                            else if (artificialLow) ++verificationResidualArtificialLow;
                            else ++verificationResidualOther;

                            const auto w = measurePrimitiveWitness(x, validated.lag);
                            std::cout << std::fixed << std::setprecision(4)
                                      << "WHOLE_NOTE_VERIFICATION_ERROR profile=" << profile.name
                                      << " hz=" << f0
                                      << " snr=" << snr
                                      << " seed=" << seed
                                      << " base_hz=" << base.hz
                                      << " validated_hz=" << validated.hz
                                      << " cents=" << signedVe
                                      << " ratio_to_truth=" << (validated.hz / f0)
                                      << " class="
                                      << (octaveHigh ? "high" : (artificialLow ? "low" : "other"))
                                      << " witness_observable=" << (w.observable ? 1 : 0)
                                      << " witness_ratio=" << w.ratio
                                      << " mismatch_lag=" << w.lagMismatch
                                      << " mismatch_2lag=" << w.doubleLagMismatch
                                      << '\n';
                        }
                        if (validated.hz < 0.75 * f0) ++verificationValidatedLow;
                        if (validated.hz > 1.5 * f0) ++verificationValidatedHigh;
                    }

                    if (guarded.valid)
                    {
                        ++verificationGuardedValid;
                        const double ge = std::abs(cents(guarded.hz, f0));
                        if (ge <= 100.0) ++verificationGuardedCorrect;
                        else ++verificationGuardedWrong;
                        if (guarded.hz < 0.75 * f0) ++verificationGuardedLow;
                        if (guarded.hz > 1.5 * f0) ++verificationGuardedHigh;
                    }

                    if (base.valid && validated.valid
                        && std::abs(base.hz - validated.hz) > 1.0e-9)
                    {
                        ++verificationChanged;
                        const bool baseWasCorrect =
                            std::abs(cents(base.hz, f0)) <= 100.0;
                        const bool validatedIsWrong =
                            std::abs(cents(validated.hz, f0)) > 100.0;
                        if (baseWasCorrect && validatedIsWrong)
                            ++verificationCorrectToWrong;
                    }
                }

    std::cout << "WHOLE_NOTE_VERIFICATION"
              << " cases=" << verificationCases
              << " base_correct=" << verificationBaseCorrect
              << " base_wrong=" << verificationBaseWrong
              << " validated_correct=" << verificationValidatedCorrect
              << " validated_wrong=" << verificationValidatedWrong
              << " base_low=" << verificationBaseLow
              << " validated_low=" << verificationValidatedLow
              << " base_high=" << verificationBaseHigh
              << " validated_high=" << verificationValidatedHigh
              << " residual_high=" << verificationResidualOctaveHigh
              << " residual_low=" << verificationResidualArtificialLow
              << " residual_other=" << verificationResidualOther
              << " guarded_valid=" << verificationGuardedValid
              << " guarded_correct=" << verificationGuardedCorrect
              << " guarded_wrong=" << verificationGuardedWrong
              << " guarded_low=" << verificationGuardedLow
              << " guarded_high=" << verificationGuardedHigh
              << " changed=" << verificationChanged
              << " correct_to_wrong=" << verificationCorrectToWrong
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

#endif // F0_WHOLE_NOTE_TUNER_NO_MAIN
