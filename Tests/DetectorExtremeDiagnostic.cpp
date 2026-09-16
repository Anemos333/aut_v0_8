#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int detectorHop = 32;
constexpr double pi = 3.14159265358979323846;

struct XorShift32
{
    std::uint32_t state = 0x9e3779b9u;

    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<float>(state & 0xffffu) / 32767.5f - 1.0f;
    }
};

double rms(const std::vector<float>& x)
{
    double sum = 0.0;
    for (float v : x)
        sum += static_cast<double>(v) * static_cast<double>(v);
    return x.empty() ? 0.0 : std::sqrt(sum / static_cast<double>(x.size()));
}

void scaleToRms(std::vector<float>& x, double targetRms)
{
    const double current = rms(x);
    if (!(current > 0.0) || !(targetRms >= 0.0))
        return;
    const double gain = targetRms / current;
    for (auto& v : x)
        v = static_cast<float>(static_cast<double>(v) * gain);
}

std::vector<float> makeVoice(double hz,
                             int samples,
                             double rmsDbfs,
                             bool missingFundamental = false,
                             bool breathy = false)
{
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    XorShift32 breathRng { 0x31415926u };
    double phase = 0.0;
    float breathHpMemory = 0.0f;
    float previousWhite = 0.0f;

    for (int i = 0; i < samples; ++i)
    {
        phase += 2.0 * pi * hz / sampleRate;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;

        double value = 0.0;
        if (!missingFundamental)
            value += std::sin(phase);
        else
            value += 0.045 * std::sin(phase);
        value += 0.44 * std::sin(2.0 * phase + 0.17);
        value += 0.23 * std::sin(3.0 * phase + 0.41);
        value += 0.12 * std::sin(4.0 * phase + 0.73);

        if (breathy)
        {
            const float white = breathRng.next();
            const float hp = white - previousWhite + 0.82f * breathHpMemory;
            previousWhite = white;
            breathHpMemory = hp;
            value += 0.70 * static_cast<double>(hp);
        }
        out[static_cast<std::size_t>(i)] = static_cast<float>(value);
    }

    scaleToRms(out, std::pow(10.0, rmsDbfs / 20.0));
    return out;
}

std::vector<float> makeColoredNoise(int samples, std::uint32_t seed)
{
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    XorShift32 rng { seed };
    float lpFast = 0.0f;
    float lpSlow = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        const float white = rng.next();
        lpFast = 0.92f * lpFast + 0.08f * white;
        lpSlow = 0.992f * lpSlow + 0.008f * white;
        out[static_cast<std::size_t>(i)] = 0.52f * white + 0.31f * lpFast + 0.17f * lpSlow;
    }
    return out;
}

std::vector<float> mixAtSnr(const std::vector<float>& voice,
                            double snrDb,
                            std::uint32_t seed,
                            bool addHum = false,
                            bool addThunder = false)
{
    std::vector<float> noise = makeColoredNoise(static_cast<int>(voice.size()), seed);
    const double voiceRms = rms(voice);
    const double targetNoiseRms = voiceRms / std::pow(10.0, snrDb / 20.0);
    scaleToRms(noise, targetNoiseRms);

    std::vector<float> mixed = voice;
    double humPhase = 0.0;
    for (std::size_t i = 0; i < mixed.size(); ++i)
    {
        double extra = 0.0;
        if (addHum)
        {
            humPhase += 2.0 * pi * 50.0 / sampleRate;
            if (humPhase >= 2.0 * pi)
                humPhase -= 2.0 * pi;
            extra += 0.70 * targetNoiseRms * std::sin(humPhase)
                   + 0.35 * targetNoiseRms * std::sin(2.0 * humPhase + 0.2);
        }
        if (addThunder)
        {
            const double t = static_cast<double>(i) / sampleRate;
            const double start = 0.24;
            if (t >= start && t < start + 0.16)
            {
                const double u = t - start;
                const double env = std::exp(-u * 18.0);
                extra += 3.5 * targetNoiseRms * env
                       * (0.75 * std::sin(2.0 * pi * 43.0 * u)
                          + 0.25 * std::sin(2.0 * pi * 87.0 * u + 0.7));
            }
        }
        mixed[i] += noise[i] + static_cast<float>(extra);
    }
    return mixed;
}

struct Metrics
{
    int firstValidSample = -1;
    int firstCorrectSample = -1;
    int stableCorrectSample = -1;
    int validDecisions = 0;
    int correctDecisions = 0;
    int halfOctaveErrors = 0;
    int doubleOctaveErrors = 0;
    int otherErrors = 0;
    int maxInvalidHopRunAfterLock = 0;
    double meanAbsCents = 0.0;
};

bool nearHz(float measured, double expected, double toleranceCents = 45.0)
{
    if (!(measured > 0.0f) || !(expected > 0.0))
        return false;
    return std::abs(1200.0 * std::log2(static_cast<double>(measured) / expected)) <= toleranceCents;
}

Metrics measure(const std::vector<float>& signal, double expectedHz)
{
    auto tracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    tracker->prepare(sampleRate);
    tracker->setRange(45.0f, 1600.0f);

    Metrics m;
    int correctRun = 0;
    int invalidHopRun = 0;
    double centsSum = 0.0;

    for (int sample = 0; sample < static_cast<int>(signal.size()); ++sample)
    {
        ModernPitchEngine::PitchObservation observation;
        if (!tracker->processSample(signal[static_cast<std::size_t>(sample)], observation))
            continue;

        if (!observation.valid || !(observation.correctionFrequencyHz > 0.0f))
        {
            correctRun = 0;
            if (m.stableCorrectSample >= 0)
            {
                ++invalidHopRun;
                m.maxInvalidHopRunAfterLock = std::max(m.maxInvalidHopRunAfterLock, invalidHopRun);
            }
            continue;
        }

        invalidHopRun = 0;
        if (m.firstValidSample < 0)
            m.firstValidSample = sample;
        ++m.validDecisions;

        const double measured = static_cast<double>(observation.correctionFrequencyHz);
        const double cents = std::abs(1200.0 * std::log2(measured / expectedHz));
        centsSum += cents;

        if (cents <= 45.0)
        {
            ++m.correctDecisions;
            if (m.firstCorrectSample < 0)
                m.firstCorrectSample = sample;
            ++correctRun;
            if (m.stableCorrectSample < 0 && correctRun >= 4)
                m.stableCorrectSample = sample - 3 * detectorHop;
        }
        else
        {
            correctRun = 0;
            if (nearHz(observation.correctionFrequencyHz, expectedHz * 0.5))
                ++m.halfOctaveErrors;
            else if (nearHz(observation.correctionFrequencyHz, expectedHz * 2.0))
                ++m.doubleOctaveErrors;
            else
                ++m.otherErrors;
        }
    }

    if (m.validDecisions > 0)
        m.meanAbsCents = centsSum / static_cast<double>(m.validDecisions);
    return m;
}

void printMetrics(const std::string& name,
                  double expectedHz,
                  double voiceDbfs,
                  double snrDb,
                  const Metrics& m)
{
    const auto ms = [](int sample) -> double
    {
        return sample < 0 ? -1.0 : 1000.0 * static_cast<double>(sample) / sampleRate;
    };
    const double correctFraction = m.validDecisions > 0
        ? static_cast<double>(m.correctDecisions) / static_cast<double>(m.validDecisions)
        : 0.0;

    std::cout << std::fixed << std::setprecision(3)
              << "DETECTOR_STRESS case=" << name
              << " hz=" << expectedHz
              << " voice_dbfs=" << voiceDbfs
              << " snr_db=" << snrDb
              << " first_valid_ms=" << ms(m.firstValidSample)
              << " first_correct_ms=" << ms(m.firstCorrectSample)
              << " stable_lock_ms=" << ms(m.stableCorrectSample)
              << " valid=" << m.validDecisions
              << " correct_fraction=" << correctFraction
              << " mean_abs_cents=" << m.meanAbsCents
              << " half_oct=" << m.halfOctaveErrors
              << " double_oct=" << m.doubleOctaveErrors
              << " other=" << m.otherErrors
              << " max_invalid_after_lock_ms="
              << (1000.0 * detectorHop * static_cast<double>(m.maxInvalidHopRunAfterLock) / sampleRate)
              << '\n';
}

void runSteady(const std::string& name,
               double hz,
               double voiceDbfs,
               double snrDb,
               bool missingFundamental = false,
               bool breathy = false,
               bool hum = false,
               bool thunder = false)
{
    constexpr int samples = 24000; // 500 ms: long enough to expose stuck acquire.
    auto voice = makeVoice(hz, samples, voiceDbfs, missingFundamental, breathy);
    auto mixed = mixAtSnr(voice, snrDb, 0x1234567u + static_cast<std::uint32_t>(hz * 17.0), hum, thunder);
    const Metrics m = measure(mixed, hz);
    printMetrics(name, hz, voiceDbfs, snrDb, m);
}

void runPureNoise()
{
    constexpr int samples = 24000;
    auto noise = makeColoredNoise(samples, 0xdeadbeefu);
    scaleToRms(noise, std::pow(10.0, -24.0 / 20.0));
    const Metrics m = measure(noise, 220.0);
    printMetrics("pure_colored_noise", 220.0, -999.0, -999.0, m);
}

void runNoteChange()
{
    constexpr int halfSamples = 12000;
    constexpr int totalSamples = halfSamples * 2;
    auto first = makeVoice(220.0, halfSamples, -36.0, false, false);
    auto second = makeVoice(246.9416506, halfSamples, -36.0, false, false);
    std::vector<float> voice;
    voice.reserve(totalSamples);
    voice.insert(voice.end(), first.begin(), first.end());
    voice.insert(voice.end(), second.begin(), second.end());
    auto mixed = mixAtSnr(voice, 3.0, 0xabcdef01u, false, false);

    auto tracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    tracker->prepare(sampleRate);
    tracker->setRange(45.0f, 1600.0f);
    int firstNewCorrect = -1;
    int stableNew = -1;
    int run = 0;
    int halfOct = 0;
    int doubleOct = 0;
    int other = 0;
    for (int sample = 0; sample < totalSamples; ++sample)
    {
        ModernPitchEngine::PitchObservation o;
        if (!tracker->processSample(mixed[static_cast<std::size_t>(sample)], o) || !o.valid)
            continue;
        if (sample < halfSamples)
            continue;

        if (nearHz(o.correctionFrequencyHz, 246.9416506))
        {
            if (firstNewCorrect < 0)
                firstNewCorrect = sample;
            ++run;
            if (stableNew < 0 && run >= 4)
                stableNew = sample - 3 * detectorHop;
        }
        else
        {
            run = 0;
            if (nearHz(o.correctionFrequencyHz, 246.9416506 * 0.5)) ++halfOct;
            else if (nearHz(o.correctionFrequencyHz, 246.9416506 * 2.0)) ++doubleOct;
            else ++other;
        }
    }
    const auto relativeMs = [](int sample)
    {
        return sample < 0 ? -1.0 : 1000.0 * static_cast<double>(sample - halfSamples) / sampleRate;
    };
    std::cout << std::fixed << std::setprecision(3)
              << "DETECTOR_CHANGE case=220_to_246p94_snr3"
              << " first_new_correct_ms=" << relativeMs(firstNewCorrect)
              << " stable_new_lock_ms=" << relativeMs(stableNew)
              << " half_oct=" << halfOct
              << " double_oct=" << doubleOct
              << " other=" << other << '\n';
}
}

int main()
{
    // SNR sweep across low/mid/high vocal fundamentals at a deliberately quiet
    // absolute voice level. This is diagnostic only: no production threshold is
    // changed and no relaxed fallback is introduced.
    for (double hz : {110.0, 220.0, 440.0})
        for (double snrDb : {12.0, 6.0, 3.0, 0.0, -3.0})
            runSteady("colored_noise", hz, -42.0, snrDb);

    // Absolute-level sweep separates fixed detector floors from SNR effects.
    for (double voiceDbfs : {-24.0, -42.0, -54.0, -60.0})
        runSteady("quiet_voice", 220.0, voiceDbfs, 6.0);

    runSteady("missing_fundamental", 220.0, -42.0, 3.0, true, false);
    runSteady("breathy_voice", 220.0, -42.0, 3.0, false, true);
    runSteady("mains_hum", 220.0, -42.0, 3.0, false, false, true, false);
    runSteady("thunder_transient", 220.0, -42.0, 6.0, false, false, false, true);
    runPureNoise();
    runNoteChange();
    return 0;
}
