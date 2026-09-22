#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

class F0DetectorV1 final
{
public:
    enum class State
    {
        acquire,
        stable,
        transition
    };

    struct Result
    {
        State state = State::acquire;
        float stableHz = 0.0f;
        bool stableUpdated = false;
    };

    void prepare(double sampleRate, float minimumHz = 55.0f, float maximumHz = 1600.0f) noexcept
    {
        sampleRate_ = std::isfinite(sampleRate) ? std::max(8000.0, sampleRate) : 48000.0;
        minimumHz_ = std::max(20.0f, minimumHz);
        maximumHz_ = std::max(minimumHz_ + 1.0f, maximumHz);
        reset();
    }

    void reset() noexcept
    {
        ring_.fill(0.0f);
        writePosition_ = 0;
        availableSamples_ = 0;
        hopCounter_ = 0;
        stableHz_ = 0.0f;
        pendingHz_ = 0.0f;
        pendingCount_ = 0;
        state_ = State::acquire;
    }

    [[nodiscard]] Result processSample(float inputSample, bool measure) noexcept
    {
        if (!std::isfinite(inputSample) || std::fpclassify(inputSample) == FP_SUBNORMAL)
            inputSample = 0.0f;

        ring_[static_cast<std::size_t>(writePosition_)] = inputSample;
        writePosition_ = (writePosition_ + 1) & ringMask;
        availableSamples_ = std::min(availableSamples_ + 1, ringSize);

        Result result { state_, stableHz_, false };

        if (++hopCounter_ < detectorHop)
            return result;
        hopCounter_ = 0;

        if (!measure)
        {
            pendingCount_ = 0;
            pendingHz_ = 0.0f;
            state_ = stableHz_ > 0.0f ? State::transition : State::acquire;
            return { state_, stableHz_, false };
        }

        const Estimate estimateResult = estimate();
        if (!estimateResult.valid)
            return rejectCurrent();

        const Validation validated = validate(estimateResult);
        if (!validated.valid)
            return rejectCurrent();

        const float refinedHz = refine(validated);
        if (!(refinedHz >= minimumHz_ && refinedHz <= maximumHz_) || !std::isfinite(refinedHz))
            return rejectCurrent();

        if (pendingCount_ == 0 || centsDistance(refinedHz, pendingHz_) > confirmationCents)
        {
            pendingHz_ = refinedHz;
            pendingCount_ = 1;
            state_ = stableHz_ > 0.0f ? State::transition : State::acquire;
            return { state_, stableHz_, false };
        }

        pendingHz_ = 0.5f * (pendingHz_ + refinedHz);
        ++pendingCount_;

        if (pendingCount_ < confirmationsRequired)
        {
            state_ = stableHz_ > 0.0f ? State::transition : State::acquire;
            return { state_, stableHz_, false };
        }

        stableHz_ = pendingHz_;
        pendingCount_ = 0;
        pendingHz_ = 0.0f;
        state_ = State::stable;
        return { state_, stableHz_, true };
    }

    [[nodiscard]] float stableHz() const noexcept { return stableHz_; }
    [[nodiscard]] State state() const noexcept { return state_; }

private:
    struct Estimate
    {
        int lag = 0;
        float score = 0.0f;
        float cmndf = 1.0f;
        bool valid = false;
    };

    struct Validation
    {
        int lag = 0;
        float correlation = 0.0f;
        bool valid = false;
    };

    static constexpr int ringSize = 1024;
    static constexpr int ringMask = ringSize - 1;
    static constexpr int maximumAnalysisSamples = 448; // 9.33 ms at 48 kHz
    static constexpr int minimumAnalysisSamples = 160;
    static constexpr int detectorHop = 16;
    static constexpr int confirmationsRequired = 2;
    static constexpr float confirmationCents = 18.0f;
    static_assert((ringSize & (ringSize - 1)) == 0, "ring size must be a power of two");

    [[nodiscard]] float sampleFromAnalysis(int analysisLength, int index) const noexcept
    {
        const int start = (writePosition_ - analysisLength + ringSize) & ringMask;
        return ring_[static_cast<std::size_t>((start + index) & ringMask)];
    }

    [[nodiscard]] float correlation(int analysisLength, int lag) const noexcept
    {
        if (lag < 2 || lag >= analysisLength - 8)
            return -1.0f;

        const int overlap = analysisLength - lag;
        double cross = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        for (int i = 0; i < overlap; ++i)
        {
            const double a = sampleFromAnalysis(analysisLength, i);
            const double b = sampleFromAnalysis(analysisLength, i + lag);
            cross += a * b;
            energyA += a * a;
            energyB += b * b;
        }

        const double denominator = std::sqrt(std::max(1.0e-24, energyA * energyB));
        return denominator > 0.0
            ? static_cast<float>(cross / denominator)
            : -1.0f;
    }

    [[nodiscard]] Estimate estimate() const noexcept
    {
        const int analysisLength = std::min(availableSamples_, maximumAnalysisSamples);
        if (analysisLength < minimumAnalysisSamples)
            return {};

        double frameEnergy = 0.0;
        for (int i = 0; i < analysisLength; ++i)
        {
            const double x = sampleFromAnalysis(analysisLength, i);
            frameEnergy += x * x;
        }
        const double rms = std::sqrt(frameEnergy / static_cast<double>(analysisLength));
        if (rms < 1.0e-6)
            return {};

        const int minimumLag = std::max(
            2, static_cast<int>(std::floor(sampleRate_ / maximumHz_)));
        const int maximumLag = std::min(
            analysisLength - 12,
            static_cast<int>(std::ceil(sampleRate_ / minimumHz_)));
        if (minimumLag >= maximumLag)
            return {};

        std::array<float, maximumAnalysisSamples> difference {};
        double cumulative = 0.0;
        int firstQualifiedLag = 0;
        float firstQualifiedValue = 1.0f;
        int bestLag = minimumLag;
        float bestValue = std::numeric_limits<float>::max();

        for (int lag = 1; lag <= maximumLag; ++lag)
        {
            const int overlap = analysisLength - lag;
            double d = 0.0;
            for (int i = 0; i < overlap; ++i)
            {
                const double delta = static_cast<double>(sampleFromAnalysis(analysisLength, i))
                                   - static_cast<double>(sampleFromAnalysis(analysisLength, i + lag));
                d += delta * delta;
            }
            d /= static_cast<double>(std::max(1, overlap));
            difference[static_cast<std::size_t>(lag)] = static_cast<float>(d);
            cumulative += d;

            if (lag < minimumLag || cumulative <= 1.0e-24)
                continue;

            const float cmndf = static_cast<float>(
                d * static_cast<double>(lag) / cumulative);

            if (cmndf < bestValue)
            {
                bestValue = cmndf;
                bestLag = lag;
            }

            if (firstQualifiedLag == 0 && cmndf < 0.32f)
            {
                firstQualifiedLag = lag;
                firstQualifiedValue = cmndf;
            }
        }

        const int lag = firstQualifiedLag > 0 ? firstQualifiedLag : bestLag;
        const float cmndf = firstQualifiedLag > 0 ? firstQualifiedValue : bestValue;
        const float corr = correlation(analysisLength, lag);

        if (!(corr > 0.42f) || !(cmndf < 0.55f))
            return {};

        return { lag, corr, cmndf, true };
    }

    [[nodiscard]] Validation validate(const Estimate& candidate) const noexcept
    {
        const int analysisLength = std::min(availableSamples_, maximumAnalysisSamples);
        int lag = candidate.lag;
        float corr = correlation(analysisLength, lag);

        // Family validation is deliberately separate from estimation. If a
        // doubled period is at least as coherent, the shorter-lag proposal was
        // a harmonic-family candidate rather than validated F0.
        for (int step = 0; step < 2; ++step)
        {
            const int doubled = lag * 2;
            if (doubled >= analysisLength - 12)
                break;

            const float doubledCorr = correlation(analysisLength, doubled);
            const float halfFrequency = static_cast<float>(sampleRate_ / doubled);
            if (halfFrequency >= minimumHz_
                && doubledCorr > 0.60f
                && doubledCorr >= corr - 0.015f)
            {
                lag = doubled;
                corr = doubledCorr;
                continue;
            }
            break;
        }

        const float hz = static_cast<float>(sampleRate_ / lag);
        if (!(hz >= minimumHz_ && hz <= maximumHz_))
            return {};
        if (!(corr > 0.58f))
            return {};

        // A true periodic family should not be supported only by an isolated
        // accidental peak. Half-period coherence is allowed (strong second),
        // but the selected full-period coordinate must remain authoritative.
        const int halfLag = lag / 2;
        if (halfLag >= 2)
        {
            const float halfCorr = correlation(analysisLength, halfLag);
            if (halfCorr > corr + 0.18f)
                return {};
        }

        return { lag, corr, true };
    }

    [[nodiscard]] float refine(const Validation& validated) const noexcept
    {
        const int analysisLength = std::min(availableSamples_, maximumAnalysisSamples);
        const int lag = validated.lag;
        if (lag <= 2 || lag >= analysisLength - 13)
            return static_cast<float>(sampleRate_ / lag);

        const double left = correlation(analysisLength, lag - 1);
        const double centre = correlation(analysisLength, lag);
        const double right = correlation(analysisLength, lag + 1);
        const double denominator = left - 2.0 * centre + right;

        double refinedLag = static_cast<double>(lag);
        if (std::abs(denominator) > 1.0e-9)
            refinedLag += std::clamp(0.5 * (left - right) / denominator, -0.75, 0.75);

        return static_cast<float>(sampleRate_ / refinedLag);
    }

    [[nodiscard]] Result rejectCurrent() noexcept
    {
        pendingCount_ = 0;
        pendingHz_ = 0.0f;
        state_ = stableHz_ > 0.0f ? State::transition : State::acquire;
        return { state_, stableHz_, false };
    }

    [[nodiscard]] static float centsDistance(float a, float b) noexcept
    {
        if (!(a > 0.0f) || !(b > 0.0f))
            return std::numeric_limits<float>::infinity();
        return std::abs(1200.0f * std::log2(a / b));
    }

    std::array<float, ringSize> ring_ {};
    double sampleRate_ = 48000.0;
    float minimumHz_ = 55.0f;
    float maximumHz_ = 1600.0f;
    int writePosition_ = 0;
    int availableSamples_ = 0;
    int hopCounter_ = 0;

    float stableHz_ = 0.0f;
    float pendingHz_ = 0.0f;
    int pendingCount_ = 0;
    State state_ = State::acquire;
};
