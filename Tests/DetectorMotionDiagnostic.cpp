#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
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

void scaleToRms(std::vector<float>& x, double target)
{
    const double current = rms(x);
    if (!(current > 0.0))
        return;
    const double gain = target / current;
    for (auto& v : x)
        v = static_cast<float>(static_cast<double>(v) * gain);
}

std::vector<float> makeSegment(double hz, int samples, double dbfs)
{
    std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        phase += 2.0 * pi * hz / sampleRate;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;
        const double v = std::sin(phase)
                       + 0.44 * std::sin(2.0 * phase + 0.17)
                       + 0.23 * std::sin(3.0 * phase + 0.41)
                       + 0.12 * std::sin(4.0 * phase + 0.73);
        out[static_cast<std::size_t>(i)] = static_cast<float>(v);
    }
    scaleToRms(out, std::pow(10.0, dbfs / 20.0));
    return out;
}

std::vector<float> addNoise(const std::vector<float>& voice,
                            double snrDb,
                            std::uint32_t seed)
{
    std::vector<float> noise(voice.size(), 0.0f);
    XorShift32 rng { seed };
    float lpFast = 0.0f;
    float lpSlow = 0.0f;
    for (std::size_t i = 0; i < noise.size(); ++i)
    {
        const float white = rng.next();
        lpFast = 0.92f * lpFast + 0.08f * white;
        lpSlow = 0.992f * lpSlow + 0.008f * white;
        noise[i] = 0.52f * white + 0.31f * lpFast + 0.17f * lpSlow;
    }
    const double targetNoise = rms(voice) / std::pow(10.0, snrDb / 20.0);
    scaleToRms(noise, targetNoise);

    std::vector<float> mixed = voice;
    for (std::size_t i = 0; i < mixed.size(); ++i)
        mixed[i] += noise[i];
    return mixed;
}

bool nearHz(float measured, double expected, double cents = 45.0)
{
    if (!(measured > 0.0f) || !(expected > 0.0))
        return false;
    return std::abs(1200.0 * std::log2(static_cast<double>(measured) / expected)) <= cents;
}

struct TransitionMetrics
{
    double firstCorrectMs = -1.0;
    double stableCorrectMs = -1.0;
    double maxOldLockMs = 0.0;
    int oldLockDecisions = 0;
    int validAfterChange = 0;
    int correctAfterChange = 0;
};

TransitionMetrics runTransition(double fromHz,
                                double toHz,
                                double snrDb,
                                std::uint32_t seed)
{
    constexpr int segmentSamples = 12000; // 250 ms each.
    auto first = makeSegment(fromHz, segmentSamples, -36.0);
    auto second = makeSegment(toHz, segmentSamples, -36.0);
    std::vector<float> voice;
    voice.reserve(segmentSamples * 2);
    voice.insert(voice.end(), first.begin(), first.end());
    voice.insert(voice.end(), second.begin(), second.end());
    auto mixed = addNoise(voice, snrDb, seed);

    auto tracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    tracker->prepare(sampleRate);
    tracker->setRange(45.0f, 1600.0f);

    TransitionMetrics m;
    int correctRun = 0;
    int oldRun = 0;
    int maxOldRun = 0;
    int firstCorrectSample = -1;
    int stableSample = -1;

    for (int sample = 0; sample < static_cast<int>(mixed.size()); ++sample)
    {
        ModernPitchEngine::PitchObservation o;
        if (!tracker->processSample(mixed[static_cast<std::size_t>(sample)], o)
            || sample < segmentSamples || !o.valid
            || !(o.correctionFrequencyHz > 0.0f))
        {
            continue;
        }

        ++m.validAfterChange;
        if (nearHz(o.correctionFrequencyHz, toHz))
        {
            ++m.correctAfterChange;
            if (firstCorrectSample < 0)
                firstCorrectSample = sample;
            ++correctRun;
            if (stableSample < 0 && correctRun >= 4)
                stableSample = sample - 3 * detectorHop;
        }
        else
        {
            correctRun = 0;
        }

        if (nearHz(o.correctionFrequencyHz, fromHz))
        {
            ++m.oldLockDecisions;
            ++oldRun;
            maxOldRun = std::max(maxOldRun, oldRun);
        }
        else
        {
            oldRun = 0;
        }
    }

    const auto relativeMs = [](int sample)
    {
        return sample < 0 ? -1.0
            : 1000.0 * static_cast<double>(sample - segmentSamples) / sampleRate;
    };
    m.firstCorrectMs = relativeMs(firstCorrectSample);
    m.stableCorrectMs = relativeMs(stableSample);
    m.maxOldLockMs = 1000.0 * detectorHop * static_cast<double>(maxOldRun) / sampleRate;
    return m;
}

void printTransition(double fromHz,
                     double toHz,
                     double snrDb,
                     std::uint32_t seed)
{
    const auto m = runTransition(fromHz, toHz, snrDb, seed);
    const double correctFraction = m.validAfterChange > 0
        ? static_cast<double>(m.correctAfterChange) / static_cast<double>(m.validAfterChange)
        : 0.0;
    std::cout << std::fixed << std::setprecision(3)
              << "MOTION_STEP from=" << fromHz
              << " to=" << toHz
              << " snr=" << snrDb
              << " seed=" << seed
              << " first_correct_ms=" << m.firstCorrectMs
              << " stable_correct_ms=" << m.stableCorrectMs
              << " max_old_lock_ms=" << m.maxOldLockMs
              << " old_lock_decisions=" << m.oldLockDecisions
              << " valid_after=" << m.validAfterChange
              << " correct_fraction=" << correctFraction
              << '\n';
}
}

int main()
{
    for (std::uint32_t seed : {0x01234567u, 0x9e3779b9u, 0x51f15e5du, 0xc001d00du})
    {
        printTransition(220.0, 246.9416506, 6.0, seed);
        printTransition(246.9416506, 196.0, 6.0, seed ^ 0xa5a5a5a5u);
        printTransition(196.0, 261.6255653, 6.0, seed ^ 0x3c6ef372u);
    }

    // +3 dB remains diagnostic rather than a release gate: it is deliberately
    // close to the measured ambiguity boundary and helps detect regressions.
    printTransition(220.0, 246.9416506, 3.0, 0xabcdef01u);
    return 0;
}
