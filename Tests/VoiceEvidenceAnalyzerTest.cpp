#include "VoiceEvidenceAnalyzer.h"

#include <cmath>
#include <cstdint>
#include <iostream>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

VoiceEvidenceAnalyzer::Evidence runTone(VoiceEvidenceAnalyzer& analyzer,
                                        float fundamentalGain,
                                        float secondGain,
                                        int blocks)
{
    juce::AudioBuffer<float> buffer(1, blockSize);
    VoiceEvidenceAnalyzer::Context context;
    context.detectedPitchHz = 220.0f;
    context.confidence = 0.96f;
    context.periodicity = 0.96f;
    context.consensus = 0.92f;
    context.detectorSupport = 4;

    double phase = 0.0;
    VoiceEvidenceAnalyzer::Evidence result;
    for (int block = 0; block < blocks; ++block)
    {
        float* data = buffer.getWritePointer(0);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            phase += twoPi * 220.0 / sampleRate;
            if (phase >= twoPi)
                phase -= twoPi;
            data[sample] = fundamentalGain * static_cast<float>(std::sin(phase))
                         + secondGain * static_cast<float>(std::sin(2.0 * phase));
        }
        result = analyzer.analyse(buffer, context);
    }
    return result;
}

VoiceEvidenceAnalyzer::Evidence runNoise(VoiceEvidenceAnalyzer& analyzer,
                                         int blocks)
{
    juce::AudioBuffer<float> buffer(1, blockSize);
    VoiceEvidenceAnalyzer::Context context;
    context.detectedPitchHz = 220.0f;
    context.confidence = 0.18f;
    context.periodicity = 0.08f;
    context.consensus = 0.12f;
    context.detectorSupport = 1;

    std::uint32_t state = 0x12345678u;
    VoiceEvidenceAnalyzer::Evidence result;
    for (int block = 0; block < blocks; ++block)
    {
        float* data = buffer.getWritePointer(0);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            state = 1664525u * state + 1013904223u;
            const float uniform = static_cast<float>(state & 0x00ffffffu)
                                / static_cast<float>(0x01000000u);
            data[sample] = 0.34f * (2.0f * uniform - 1.0f);
        }
        result = analyzer.analyse(buffer, context);
    }
    return result;
}
} // namespace

int main()
{
    bool success = true;

    VoiceEvidenceAnalyzer analyzer;
    analyzer.prepare(sampleRate, blockSize, 1);

    const auto normal = runTone(analyzer, 0.68f, 0.16f, 80);
    analyzer.reset();
    const auto secondDominant = runTone(analyzer, 0.16f, 0.72f, 80);

    success &= check(secondDominant.secondHarmonicDominance
                     > normal.secondHarmonicDominance + 0.20f,
                     "second_harmonic_detector_responds");
    success &= check(secondDominant.harmonicity > 0.45f,
                     "second_harmonic_material_remains_voiced");

    analyzer.reset();
    const auto tonal = runTone(analyzer, 0.62f, 0.18f, 80);
    analyzer.reset();
    const auto noisy = runNoise(analyzer, 80);
    success &= check(noisy.breathiness > tonal.breathiness + 0.15f,
                     "breathiness_distinguishes_noise_from_voiced_tone");
    success &= check(tonal.spectralReliability > noisy.spectralReliability + 0.15f,
                     "spectral_reliability_tracks_voiced_evidence");

    analyzer.reset();
    juce::AudioBuffer<float> buffer(1, blockSize);
    buffer.clear();
    VoiceEvidenceAnalyzer::Context context;
    context.detectedPitchHz = 220.0f;
    context.confidence = 0.90f;
    context.periodicity = 0.90f;
    context.consensus = 0.90f;
    context.detectorSupport = 4;
    for (int i = 0; i < 12; ++i)
        static_cast<void>(analyzer.analyse(buffer, context));

    float* data = buffer.getWritePointer(0);
    double phase = 0.0;
    for (int sample = 0; sample < blockSize; ++sample)
    {
        phase += twoPi * 220.0 / sampleRate;
        data[sample] = 0.85f * static_cast<float>(std::sin(phase));
    }
    const float firstSampleBefore = data[0];
    const float lastSampleBefore = data[blockSize - 1];
    const auto event = analyzer.analyse(buffer, context);
    success &= check(event.eventStrength > 0.35f,
                     "event_detector_responds_to_attack");
    success &= check(data[0] == firstSampleBefore
                     && data[blockSize - 1] == lastSampleBefore,
                     "analyzer_never_modifies_audio");

    const auto published = analyzer.getLatest();
    success &= check(std::abs(published.eventStrength - event.eventStrength) < 1.0e-6f,
                     "published_evidence_is_coherent");

    return success ? 0 : 1;
}
