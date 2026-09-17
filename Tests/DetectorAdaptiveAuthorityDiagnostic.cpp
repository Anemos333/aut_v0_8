#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private
#include "VoiceEvidenceAnalyzer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
constexpr double sr = 48000.0;
constexpr double pi = 3.14159265358979323846;
constexpr int blockSize = 256;

struct Rng
{
    std::uint32_t s;
    float next() noexcept
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xffffu) / 32767.5f - 1.0f;
    }
};

struct VoiceProfile
{
    const char* name;
    double fundamental;
    double second;
    double third;
    double fourth;
};

constexpr VoiceProfile harmonic4 { "harmonic4", 1.0, 0.44, 0.23, 0.12 };
constexpr VoiceProfile legacy { "legacy", 1.0, 0.34, 0.18, 0.0 };
constexpr VoiceProfile strongSecond { "strong_second", 0.35, 1.0, 0.22, 0.08 };

double rms(const std::vector<float>& x)
{
    double e = 0.0;
    for (float v : x) e += static_cast<double>(v) * v;
    return std::sqrt(e / static_cast<double>(std::max<std::size_t>(1, x.size())));
}

void scaleRms(std::vector<float>& x, double target)
{
    const double r = rms(x);
    if (!(r > 0.0)) return;
    const double g = target / r;
    for (float& v : x) v = static_cast<float>(v * g);
}

bool near(float measured, double target, double cents = 45.0)
{
    return measured > 0.0f
        && std::abs(1200.0 * std::log2(static_cast<double>(measured) / target)) <= cents;
}

struct Result
{
    int validAfter = 0;
    int correctAfter = 0;
    int halfAfter = 0;
    int doubleAfter = 0;
    int otherAfter = 0;
    int firstCorrectSample = -1;
    int maximumOldRunSamples = 0;
    double lowerEvidencePreSum = 0.0;
    int lowerEvidencePreCount = 0;
    double lowerEvidencePostSum = 0.0;
    int lowerEvidencePostCount = 0;
    double bodyAuthorityPreSum = 0.0;
    int bodyAuthorityPreCount = 0;
    double bodyAuthorityPostSum = 0.0;
    int bodyAuthorityPostCount = 0;
};

std::vector<float> makeSignal(double firstHz,
                              double secondHz,
                              int changeSample,
                              double snrDb,
                              std::uint32_t seed,
                              const VoiceProfile& profile,
                              int sampleCount,
                              bool pureNoise = false)
{
    std::vector<float> voice(static_cast<std::size_t>(sampleCount));
    std::vector<float> noise(static_cast<std::size_t>(sampleCount));
    Rng rng { seed };
    float fast = 0.0f;
    float slow = 0.0f;
    double phase = 0.0;

    for (int i = 0; i < sampleCount; ++i)
    {
        const double hz = i < changeSample ? firstHz : secondHz;
        phase += 2.0 * pi * hz / sr;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
        if (!pureNoise)
        {
            voice[static_cast<std::size_t>(i)] = static_cast<float>(
                profile.fundamental * std::sin(phase)
                + profile.second * std::sin(2.0 * phase + 0.17)
                + profile.third * std::sin(3.0 * phase + 0.41)
                + profile.fourth * std::sin(4.0 * phase + 0.73));
        }

        const float white = rng.next();
        fast = 0.92f * fast + 0.08f * white;
        slow = 0.992f * slow + 0.008f * white;
        noise[static_cast<std::size_t>(i)] = 0.52f * white + 0.31f * fast + 0.17f * slow;
    }

    if (!pureNoise)
    {
        scaleRms(voice, std::pow(10.0, -42.0 / 20.0));
        scaleRms(noise, rms(voice) / std::pow(10.0, snrDb / 20.0));
        for (int i = 0; i < sampleCount; ++i)
            voice[static_cast<std::size_t>(i)] += noise[static_cast<std::size_t>(i)];
        return voice;
    }

    scaleRms(noise, std::pow(10.0, -42.0 / 20.0));
    return noise;
}

Result run(const char* name,
           double firstHz,
           double secondHz,
           int changeSample,
           double snrDb,
           std::uint32_t seed,
           const VoiceProfile& profile,
           bool pureNoise = false)
{
    constexpr int totalSamples = 43200; // 0.9 s
    auto signal = makeSignal(firstHz, secondHz, changeSample, snrDb, seed,
                             profile, totalSamples, pureNoise);

    auto tracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    tracker->prepare(sr);
    tracker->setRange(45.0f, 1600.0f);

    VoiceEvidenceAnalyzer analyzer;
    analyzer.prepare(sr, blockSize, 1);
    bool evidencePrimed = false;
    VoiceEvidenceAnalyzer::Context analyzerContext;
    ModernPitchEngine::PitchObservation latestObservation;

    Result result;
    int oldRun = 0;
    const int countStart = std::max(changeSample + static_cast<int>(0.060 * sr),
                                    static_cast<int>(0.180 * sr));

    for (int blockStart = 0; blockStart < totalSamples; blockStart += blockSize)
    {
        const int count = std::min(blockSize, totalSamples - blockStart);
        const auto priorEvidence = analyzer.getLatest();
        tracker->setVoiceAuthorityContext(
            evidencePrimed,
            priorEvidence.harmonicity,
            priorEvidence.breathiness,
            priorEvidence.voicedBodyEnergy,
            priorEvidence.spectralReliability,
            priorEvidence.eventStrength,
            priorEvidence.formantStability,
            priorEvidence.lowerFamilyEvidence);

        // Mirror LivePitchProcessor causality: the analyzer sees this input block
        // using the previous detector context, but its result is available only
        // to the NEXT block's tracker decision.
        juce::AudioBuffer<float> analysisBlock(1, count);
        for (int i = 0; i < count; ++i)
            analysisBlock.setSample(0, i, signal[static_cast<std::size_t>(blockStart + i)]);
        analyzer.analyse(analysisBlock, analyzerContext);
        evidencePrimed = true;

        const float bodyAuthority = tracker->voiceBodyAuthorityV67();
        const bool pre = blockStart < changeSample;
        if (evidencePrimed)
        {
            if (pre)
            {
                result.lowerEvidencePreSum += priorEvidence.lowerFamilyEvidence;
                ++result.lowerEvidencePreCount;
                result.bodyAuthorityPreSum += bodyAuthority;
                ++result.bodyAuthorityPreCount;
            }
            else
            {
                result.lowerEvidencePostSum += priorEvidence.lowerFamilyEvidence;
                ++result.lowerEvidencePostCount;
                result.bodyAuthorityPostSum += bodyAuthority;
                ++result.bodyAuthorityPostCount;
            }
        }

        for (int i = 0; i < count; ++i)
        {
            const int sampleIndex = blockStart + i;
            ModernPitchEngine::PitchObservation observation;
            if (tracker->processSample(signal[static_cast<std::size_t>(sampleIndex)], observation))
            {
                latestObservation = observation;
                analyzerContext.detectedPitchHz = observation.frequencyHz;
                analyzerContext.confidence = observation.confidence;
                analyzerContext.periodicity = observation.periodicity;
                analyzerContext.consensus = observation.consensus;
                analyzerContext.onsetStrength = observation.onsetStrength;
                analyzerContext.detectorSupport = observation.detectorSupport;
            }

            if (!latestObservation.valid || !(latestObservation.correctionFrequencyHz > 0.0f))
                continue;

            const float hz = latestObservation.correctionFrequencyHz;
            if (sampleIndex >= changeSample && near(hz, firstHz))
            {
                ++oldRun;
                result.maximumOldRunSamples = std::max(result.maximumOldRunSamples, oldRun);
            }
            else
            {
                oldRun = 0;
            }

            if (sampleIndex >= changeSample
                && result.firstCorrectSample < 0
                && near(hz, secondHz))
            {
                result.firstCorrectSample = sampleIndex;
            }

            if (sampleIndex < countStart)
                continue;

            ++result.validAfter;
            if (near(hz, secondHz)) ++result.correctAfter;
            else if (near(hz, secondHz * 0.5)) ++result.halfAfter;
            else if (near(hz, secondHz * 2.0)) ++result.doubleAfter;
            else ++result.otherAfter;
        }
    }

    const auto mean = [](double sum, int n) { return n > 0 ? sum / n : 0.0; };
    const double correctFraction = result.validAfter > 0
        ? static_cast<double>(result.correctAfter) / result.validAfter : 0.0;
    const double halfFraction = result.validAfter > 0
        ? static_cast<double>(result.halfAfter) / result.validAfter : 0.0;
    const double firstCorrectMs = result.firstCorrectSample >= 0
        ? 1000.0 * static_cast<double>(result.firstCorrectSample - changeSample) / sr : -1.0;
    const double maxOldMs = 1000.0 * static_cast<double>(result.maximumOldRunSamples) / sr;

    std::cout << std::fixed << std::setprecision(4)
              << "V67_AUTH case=" << name
              << " seed=" << seed
              << " profile=" << profile.name
              << " from=" << firstHz
              << " to=" << secondHz
              << " snr=" << snrDb
              << " valid_after=" << result.validAfter
              << " correct_fraction=" << correctFraction
              << " half_fraction=" << halfFraction
              << " other=" << result.otherAfter
              << " first_correct_ms=" << firstCorrectMs
              << " max_old_ms=" << maxOldMs
              << " lower_pre=" << mean(result.lowerEvidencePreSum, result.lowerEvidencePreCount)
              << " lower_post=" << mean(result.lowerEvidencePostSum, result.lowerEvidencePostCount)
              << " body_pre=" << mean(result.bodyAuthorityPreSum, result.bodyAuthorityPreCount)
              << " body_post=" << mean(result.bodyAuthorityPostSum, result.bodyAuthorityPostCount)
              << '\n';
    return result;
}
}

int main()
{
    const std::array<std::uint32_t, 4> seeds {
        0x1234567u, 0x9e3779b9u, 0x51f15e5du, 0xc001d00du
    };

    constexpr int noChange = 0;
    constexpr int transitionAt = 19200; // 400 ms

    for (const auto seed : seeds)
    {
        run("stable440_legacy", 440.0, 440.0, noChange, 6.0, seed, legacy);
        run("down440_220_legacy", 440.0, 220.0, transitionAt, 6.0, seed, legacy);
    }

    for (const auto seed : { seeds[0], seeds[3] })
    {
        run("stable220_legacy", 220.0, 220.0, noChange, 6.0, seed, legacy);
        run("up220_440_legacy", 220.0, 440.0, transitionAt, 6.0, seed, legacy);
        run("down220_110_strong2", 220.0, 110.0, transitionAt, 6.0, seed, strongSecond);
        run("stable440_harmonic4_snr3", 440.0, 440.0, noChange, 3.0, seed, harmonic4);
    }

    run("pure_noise", 220.0, 220.0, noChange, -999.0, seeds[0], legacy, true);
    return 0;
}
