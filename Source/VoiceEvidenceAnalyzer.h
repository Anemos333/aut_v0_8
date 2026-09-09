#pragma once

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// Analysis-only companion for the clean ModernPitchEngine path.
//
// This class never renders, delays, mixes or returns audio. It extracts the
// useful evidence that existed in the legacy spectral renderer and publishes
// only control information. The audible signal remains exclusively inside the
// clean full-signal ModernPitchEngine transport/LPC path.
class VoiceEvidenceAnalyzer final
{
public:
    struct Context
    {
        float detectedPitchHz = 0.0f;
        float confidence = 0.0f;
        float periodicity = 0.0f;
        float consensus = 0.0f;
        float onsetStrength = 0.0f;
        int detectorSupport = 0;
    };

    struct Evidence
    {
        float harmonicity = 0.0f;
        float breathiness = 0.0f;
        float eventStrength = 0.0f;
        float spectralReliability = 0.0f;
        float formantStability = 0.0f;
        float secondHarmonicDominance = 0.0f;
        float voicedBodyEnergy = 0.0f;
        float polyphonyRisk = 0.0f;
    };

    void prepare(double sampleRate, int maximumBlockSize, int maximumChannels) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate)
                                                : 48000.0;
        maximumBlockSize_ = std::max(1, maximumBlockSize);
        maximumChannels_ = std::clamp(maximumChannels, 1, 8);

        const auto onePole = [this](double cutoffHz) noexcept
        {
            const double safeCutoff = std::clamp(cutoffHz, 20.0, 0.45 * sampleRate_);
            return static_cast<float>(1.0 - std::exp(-2.0 * pi * safeCutoff / sampleRate_));
        };

        lowCoefficient_ = onePole(900.0);
        bodyCoefficient_ = onePole(3600.0);
        fastEnergyCoefficient_ = static_cast<float>(
            1.0 - std::exp(-1.0 / (0.0020 * sampleRate_)));
        slowEnergyCoefficient_ = static_cast<float>(
            1.0 - std::exp(-1.0 / (0.040 * sampleRate_)));
        reset();
    }

    void reset() noexcept
    {
        lowState_ = 0.0f;
        bodyState_ = 0.0f;
        fastEnergy_ = 0.0f;
        slowEnergy_ = 0.0f;
        previousSample_ = 0.0f;
        previousBandRatios_ = { 0.34f, 0.46f, 0.20f };
        bandRatiosInitialised_ = false;
        smoothed_ = {};
        publish(smoothed_);
    }

    [[nodiscard]] Evidence analyse(const juce::AudioBuffer<float>& input,
                                   const Context& context) noexcept
    {
        const int channels = std::min({ input.getNumChannels(), maximumChannels_, 8 });
        const int samples = std::min(input.getNumSamples(), maximumBlockSize_);
        if (channels <= 0 || samples <= 0)
        {
            decayToSilence(samples);
            publish(smoothed_);
            return smoothed_;
        }

        int analysisChannel = 0;
        double bestChannelEnergy = -1.0;
        for (int channel = 0; channel < channels; ++channel)
        {
            const float* data = input.getReadPointer(channel);
            double energy = 0.0;
            for (int sample = 0; sample < samples; ++sample)
            {
                const float value = sanitise(data[sample]);
                energy += static_cast<double>(value) * value;
            }
            if (energy > bestChannelEnergy)
            {
                bestChannelEnergy = energy;
                analysisChannel = channel;
            }
        }

        const float* data = input.getReadPointer(analysisChannel);
        Probe halfProbe;
        Probe fundamentalProbe;
        Probe secondProbe;
        Probe thirdProbe;
        const float referencePitch = std::isfinite(context.detectedPitchHz)
            ? context.detectedPitchHz : 0.0f;
        const bool pitchUsable = referencePitch >= 38.0f
                              && referencePitch <= static_cast<float>(sampleRate_ * 0.20);
        if (pitchUsable)
        {
            halfProbe.prepare(0.5 * static_cast<double>(referencePitch), sampleRate_);
            fundamentalProbe.prepare(static_cast<double>(referencePitch), sampleRate_);
            secondProbe.prepare(2.0 * static_cast<double>(referencePitch), sampleRate_);
            thirdProbe.prepare(3.0 * static_cast<double>(referencePitch), sampleRate_);
        }

        double totalEnergy = 0.0;
        double lowEnergy = 0.0;
        double midEnergy = 0.0;
        double highEnergy = 0.0;
        int zeroCrossings = 0;
        float peakEvent = std::clamp(context.onsetStrength, 0.0f, 1.0f);

        for (int sample = 0; sample < samples; ++sample)
        {
            const float x = sanitise(data[sample]);
            if ((x >= 0.0f) != (previousSample_ >= 0.0f)
                && std::abs(x - previousSample_) > 1.0e-4f)
            {
                ++zeroCrossings;
            }
            previousSample_ = x;

            lowState_ += lowCoefficient_ * (x - lowState_);
            bodyState_ += bodyCoefficient_ * (x - bodyState_);
            const float low = lowState_;
            const float mid = bodyState_ - lowState_;
            const float high = x - bodyState_;

            const double x2 = static_cast<double>(x) * x;
            totalEnergy += x2;
            lowEnergy += static_cast<double>(low) * low;
            midEnergy += static_cast<double>(mid) * mid;
            highEnergy += static_cast<double>(high) * high;

            fastEnergy_ += fastEnergyCoefficient_ * (static_cast<float>(x2) - fastEnergy_);
            slowEnergy_ += slowEnergyCoefficient_ * (static_cast<float>(x2) - slowEnergy_);
            const float energyRatio = fastEnergy_ / std::max(1.0e-10f, slowEnergy_);
            peakEvent = std::max(peakEvent,
                                 smoothStep(1.55f, 4.8f, energyRatio));

            if (pitchUsable)
            {
                halfProbe.push(x);
                fundamentalProbe.push(x);
                secondProbe.push(x);
                thirdProbe.push(x);
            }
        }

        const double safeEnergy = std::max(1.0e-12, totalEnergy);
        const float rms = static_cast<float>(
            std::sqrt(safeEnergy / static_cast<double>(samples)));
        const float lowRatio = clamp01(static_cast<float>(lowEnergy / safeEnergy));
        const float midRatio = clamp01(static_cast<float>(midEnergy / safeEnergy));
        const float highRatio = clamp01(static_cast<float>(highEnergy / safeEnergy));
        const float zcRate = static_cast<float>(zeroCrossings)
                           / static_cast<float>(std::max(1, samples));
        const float zcNoise = smoothStep(0.055f, 0.34f, zcRate);

        float halfEnergy = 0.0f;
        float fundamentalEnergy = 0.0f;
        float secondEnergy = 0.0f;
        float thirdEnergy = 0.0f;
        if (pitchUsable)
        {
            halfEnergy = halfProbe.rmsEnergy(samples);
            fundamentalEnergy = fundamentalProbe.rmsEnergy(samples);
            secondEnergy = secondProbe.rmsEnergy(samples);
            thirdEnergy = thirdProbe.rmsEnergy(samples);
        }

        const float tonalFraction = clamp01(
            (halfEnergy + fundamentalEnergy + secondEnergy + thirdEnergy)
            / std::max(1.0e-9f, static_cast<float>(safeEnergy / samples)));
        const float meterPeriodicity = clamp01(context.periodicity);
        const float meterConfidence = clamp01(context.confidence);
        const float meterConsensus = clamp01(context.consensus);
        const float harmonicityRaw = clamp01(
            0.46f * meterPeriodicity
          + 0.24f * std::sqrt(tonalFraction)
          + 0.18f * meterConfidence
          + 0.12f * (1.0f - zcNoise));

        const float airScore = smoothStep(0.09f, 0.46f, std::sqrt(highRatio));
        const float breathRaw = clamp01(
            (0.50f * airScore
           + 0.28f * zcNoise
           + 0.22f * (1.0f - harmonicityRaw))
            * (1.0f - 0.62f * peakEvent));

        const std::array<float, 3> currentRatios { lowRatio, midRatio, highRatio };
        float formantStabilityRaw = 1.0f;
        if (bandRatiosInitialised_)
        {
            float delta = 0.0f;
            for (std::size_t i = 0; i < currentRatios.size(); ++i)
                delta += std::abs(currentRatios[i] - previousBandRatios_[i]);
            formantStabilityRaw = std::exp(-5.5f * delta)
                                * (1.0f - 0.55f * peakEvent);
        }
        previousBandRatios_ = currentRatios;
        bandRatiosInitialised_ = true;

        const float secondRatio = secondEnergy
            / std::max(1.0e-9f, fundamentalEnergy);
        const float familySupport = clamp01(
            (fundamentalEnergy + secondEnergy + thirdEnergy)
            / std::max(1.0e-9f,
                static_cast<float>(safeEnergy / samples)));
        const float secondHarmonicRaw = pitchUsable
            ? smoothStep(1.15f, 3.50f, secondRatio)
                * smoothStep(0.03f, 0.30f, familySupport)
            : 0.0f;

        const float support = clamp01(static_cast<float>(context.detectorSupport) / 3.0f);
        const float subharmonicConflict = halfEnergy
            / std::max(1.0e-9f, fundamentalEnergy + secondEnergy + thirdEnergy);
        const float competingFamily = smoothStep(0.18f, 0.62f, subharmonicConflict);
        const float polyphonyRaw = clamp01(
            0.38f * (1.0f - support)
          + 0.32f * (1.0f - meterConsensus)
          + 0.30f * competingFamily);

        const float formantStability = clamp01(formantStabilityRaw);
        const float reliabilityRaw = clamp01(
            0.30f * harmonicityRaw
          + 0.24f * meterConfidence
          + 0.18f * meterConsensus
          + 0.18f * formantStability
          + 0.10f * (1.0f - breathRaw)
          - 0.24f * polyphonyRaw);
        const float voicedBodyRaw = clamp01(
            std::sqrt(std::max(0.0f, lowRatio + midRatio))
            * smoothStep(0.0008f, 0.010f, rms));

        Evidence target;
        target.harmonicity = harmonicityRaw;
        target.breathiness = breathRaw;
        target.eventStrength = peakEvent;
        target.spectralReliability = reliabilityRaw;
        target.formantStability = formantStability;
        target.secondHarmonicDominance = secondHarmonicRaw;
        target.voicedBodyEnergy = voicedBodyRaw;
        target.polyphonyRisk = polyphonyRaw;

        const float blockSeconds = static_cast<float>(samples / sampleRate_);
        smoothEvidence(target, blockSeconds);
        publish(smoothed_);
        return smoothed_;
    }

    [[nodiscard]] Evidence getLatest() const noexcept
    {
        Evidence result;
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const std::uint32_t before = sequence_.load(std::memory_order_acquire);
            if ((before & 1u) != 0u)
                continue;
            result.harmonicity = harmonicity_.load(std::memory_order_relaxed);
            result.breathiness = breathiness_.load(std::memory_order_relaxed);
            result.eventStrength = eventStrength_.load(std::memory_order_relaxed);
            result.spectralReliability = spectralReliability_.load(std::memory_order_relaxed);
            result.formantStability = formantStability_.load(std::memory_order_relaxed);
            result.secondHarmonicDominance = secondHarmonicDominance_.load(std::memory_order_relaxed);
            result.voicedBodyEnergy = voicedBodyEnergy_.load(std::memory_order_relaxed);
            result.polyphonyRisk = polyphonyRisk_.load(std::memory_order_relaxed);
            const std::uint32_t after = sequence_.load(std::memory_order_acquire);
            if (before == after && (after & 1u) == 0u)
                return result;
        }
        return result;
    }

private:
    static constexpr double pi = 3.1415926535897932384626433832795;

    struct Probe
    {
        void prepare(double frequencyHz, double sampleRate) noexcept
        {
            valid = std::isfinite(frequencyHz) && std::isfinite(sampleRate)
                 && frequencyHz >= 20.0 && frequencyHz <= 0.45 * sampleRate;
            phaseCos = 1.0;
            phaseSin = 0.0;
            real = 0.0;
            imag = 0.0;
            if (!valid)
            {
                stepCos = 1.0;
                stepSin = 0.0;
                return;
            }
            const double angle = 2.0 * pi * frequencyHz / sampleRate;
            stepCos = std::cos(angle);
            stepSin = std::sin(angle);
        }

        void push(float sample) noexcept
        {
            if (!valid)
                return;
            real += static_cast<double>(sample) * phaseCos;
            imag += static_cast<double>(sample) * phaseSin;
            const double nextCos = phaseCos * stepCos - phaseSin * stepSin;
            const double nextSin = phaseSin * stepCos + phaseCos * stepSin;
            phaseCos = nextCos;
            phaseSin = nextSin;
        }

        [[nodiscard]] float rmsEnergy(int samples) const noexcept
        {
            if (!valid || samples <= 0)
                return 0.0f;
            const double scale = 2.0
                / (static_cast<double>(samples) * static_cast<double>(samples));
            const double energy = scale * (real * real + imag * imag);
            return static_cast<float>(std::max(0.0, energy));
        }

        double phaseCos = 1.0;
        double phaseSin = 0.0;
        double stepCos = 1.0;
        double stepSin = 0.0;
        double real = 0.0;
        double imag = 0.0;
        bool valid = false;
    };

    [[nodiscard]] static float clamp01(float value) noexcept
    {
        return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
    }

    [[nodiscard]] static float sanitise(float value) noexcept
    {
        if (!std::isfinite(value) || std::fpclassify(value) == FP_SUBNORMAL)
            return 0.0f;
        return std::clamp(value, -16.0f, 16.0f);
    }

    [[nodiscard]] static float smoothStep(float edge0, float edge1, float value) noexcept
    {
        if (edge1 <= edge0)
            return value >= edge1 ? 1.0f : 0.0f;
        const float x = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

    static void smoothMetric(float& current,
                             float target,
                             float attack,
                             float release) noexcept
    {
        target = clamp01(target);
        const float coefficient = target > current ? attack : release;
        current += coefficient * (target - current);
        current = clamp01(current);
    }

    void smoothEvidence(const Evidence& target, float blockSeconds) noexcept
    {
        const auto coefficient = [blockSeconds](float milliseconds) noexcept
        {
            const float seconds = std::max(0.0005f, milliseconds * 0.001f);
            return std::clamp(1.0f - std::exp(-blockSeconds / seconds),
                              0.001f, 1.0f);
        };
        const float fastAttack = coefficient(10.0f);
        const float mediumAttack = coefficient(24.0f);
        const float release = coefficient(90.0f);
        const float slowRelease = coefficient(180.0f);

        smoothMetric(smoothed_.harmonicity, target.harmonicity,
                     mediumAttack, release);
        smoothMetric(smoothed_.breathiness, target.breathiness,
                     mediumAttack, slowRelease);
        smoothMetric(smoothed_.eventStrength, target.eventStrength,
                     fastAttack, coefficient(35.0f));
        smoothMetric(smoothed_.spectralReliability, target.spectralReliability,
                     mediumAttack, release);
        smoothMetric(smoothed_.formantStability, target.formantStability,
                     mediumAttack, release);
        smoothMetric(smoothed_.secondHarmonicDominance,
                     target.secondHarmonicDominance,
                     mediumAttack, slowRelease);
        smoothMetric(smoothed_.voicedBodyEnergy, target.voicedBodyEnergy,
                     mediumAttack, release);
        smoothMetric(smoothed_.polyphonyRisk, target.polyphonyRisk,
                     mediumAttack, slowRelease);
    }

    void decayToSilence(int samples) noexcept
    {
        const float seconds = static_cast<float>(std::max(1, samples) / sampleRate_);
        const float release = std::clamp(1.0f - std::exp(-seconds / 0.080f),
                                         0.001f, 1.0f);
        smoothed_.harmonicity += release * (0.0f - smoothed_.harmonicity);
        smoothed_.breathiness += release * (0.0f - smoothed_.breathiness);
        smoothed_.eventStrength += release * (0.0f - smoothed_.eventStrength);
        smoothed_.spectralReliability += release * (0.0f - smoothed_.spectralReliability);
        smoothed_.formantStability += release * (0.0f - smoothed_.formantStability);
        smoothed_.secondHarmonicDominance += release * (0.0f - smoothed_.secondHarmonicDominance);
        smoothed_.voicedBodyEnergy += release * (0.0f - smoothed_.voicedBodyEnergy);
        smoothed_.polyphonyRisk += release * (0.0f - smoothed_.polyphonyRisk);
    }

    void publish(const Evidence& evidence) noexcept
    {
        sequence_.fetch_add(1u, std::memory_order_acq_rel);
        harmonicity_.store(evidence.harmonicity, std::memory_order_relaxed);
        breathiness_.store(evidence.breathiness, std::memory_order_relaxed);
        eventStrength_.store(evidence.eventStrength, std::memory_order_relaxed);
        spectralReliability_.store(evidence.spectralReliability, std::memory_order_relaxed);
        formantStability_.store(evidence.formantStability, std::memory_order_relaxed);
        secondHarmonicDominance_.store(evidence.secondHarmonicDominance, std::memory_order_relaxed);
        voicedBodyEnergy_.store(evidence.voicedBodyEnergy, std::memory_order_relaxed);
        polyphonyRisk_.store(evidence.polyphonyRisk, std::memory_order_relaxed);
        sequence_.fetch_add(1u, std::memory_order_release);
    }

    double sampleRate_ = 48000.0;
    int maximumBlockSize_ = 512;
    int maximumChannels_ = 2;
    float lowCoefficient_ = 0.05f;
    float bodyCoefficient_ = 0.20f;
    float fastEnergyCoefficient_ = 0.01f;
    float slowEnergyCoefficient_ = 0.001f;
    float lowState_ = 0.0f;
    float bodyState_ = 0.0f;
    float fastEnergy_ = 0.0f;
    float slowEnergy_ = 0.0f;
    float previousSample_ = 0.0f;
    std::array<float, 3> previousBandRatios_ { 0.34f, 0.46f, 0.20f };
    bool bandRatiosInitialised_ = false;
    Evidence smoothed_;

    std::atomic<std::uint32_t> sequence_ { 0u };
    std::atomic<float> harmonicity_ { 0.0f };
    std::atomic<float> breathiness_ { 0.0f };
    std::atomic<float> eventStrength_ { 0.0f };
    std::atomic<float> spectralReliability_ { 0.0f };
    std::atomic<float> formantStability_ { 0.0f };
    std::atomic<float> secondHarmonicDominance_ { 0.0f };
    std::atomic<float> voicedBodyEnergy_ { 0.0f };
    std::atomic<float> polyphonyRisk_ { 0.0f };
};
