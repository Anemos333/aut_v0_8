#include "ModernPitchEngine.h"
#include "ObservationOwnershipPolicy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <thread>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr double twoPi = 2.0 * pi;
constexpr float minimumDetectorRms = 0.0012f;
constexpr float numericalPresenceSample = 1.0e-8f;
constexpr float numericalPresenceRms = 1.0e-8f;

[[nodiscard]] float sanitiseAudioSample(float value) noexcept
{
    if (!std::isfinite(value) || std::fpclassify(value) == FP_SUBNORMAL)
        return 0.0f;
    return std::clamp(value, -32.0f, 32.0f);
}

[[nodiscard]] float smoothStep(float edge0, float edge1, float value) noexcept
{
    if (edge1 <= edge0)
        return value >= edge1 ? 1.0f : 0.0f;
    const float x = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

[[nodiscard]] double finiteOr(double value, double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] float finiteOr(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}
} // namespace

// BiquadLowPass

void ModernPitchEngine::BiquadLowPass::prepare(double sampleRate,
                                                double cutoffHz,
                                                double q) noexcept
{
    const double safeSampleRate = std::max(1.0, sampleRate);
    const double safeCutoff = std::clamp(cutoffHz, 10.0, safeSampleRate * 0.45);
    const double safeQ = std::max(0.05, q);

    const double omega = twoPi * safeCutoff / safeSampleRate;
    const double cosine = std::cos(omega);
    const double sine = std::sin(omega);
    const double alpha = sine / (2.0 * safeQ);
    const double a0 = 1.0 + alpha;

    b0_ = ((1.0 - cosine) * 0.5) / a0;
    b1_ = (1.0 - cosine) / a0;
    b2_ = b0_;
    a1_ = (-2.0 * cosine) / a0;
    a2_ = (1.0 - alpha) / a0;
    reset();
}

void ModernPitchEngine::BiquadLowPass::reset() noexcept
{
    z1_ = 0.0;
    z2_ = 0.0;
}

float ModernPitchEngine::BiquadLowPass::process(float input) noexcept
{
    // BIQUAD_INPUT_SANITIZE_OWNED_UPSTREAM_V1: private anti-alias stages receive
    // the finite stream already sanitized by the public engine boundary.
    const double x = static_cast<double>(input);
    const double output = b0_ * x + z1_;
    z1_ = b1_ * x - a1_ * output + z2_;
    z2_ = b2_ * x - a2_ * output;
    if (!std::isfinite(output) || !std::isfinite(z1_) || !std::isfinite(z2_))
    {
        reset();
        return 0.0f;
    }
    return static_cast<float>(output);
}

//==============================================================================
// MultiRatePitchTracker

// PARKED_LOW_RATE_WORKER_V1
// Only half/eighth analyse() calls move off the audio thread. The ring buffers
// are immutable from submit() until wait() returns because processSample() does
// not advance to the next input sample before this rendezvous. The worker owns
// its AnalysisWorkspace, so detector arithmetic remains re-entrant and no
// scratch buffer is shared with full/quarter analysis.
class ModernPitchEngine::MultiRatePitchTracker::AnalysisWorker final
{
public:
    struct Job
    {
        bool runHalf = false;
        bool runEighth = false;

        int halfWritePosition = 0;
        int halfAvailableSamples = 0;
        double halfSampleRate = 0.0;
        float halfMinimum = 0.0f;
        float halfMaximum = 0.0f;

        int eighthWritePosition = 0;
        int eighthAvailableSamples = 0;
        double eighthSampleRate = 0.0;
        float eighthMinimum = 0.0f;
        float eighthMaximum = 0.0f;
    };

    struct Result
    {
        PitchCandidate half {};
        PitchCandidate eighth {};
        bool halfComputed = false;
        bool eighthComputed = false;
    };

    explicit AnalysisWorker(MultiRatePitchTracker& owner) noexcept
        : owner_(owner)
    {
    }

    ~AnalysisWorker()
    {
        stop();
    }

    bool start() noexcept
    {
        try
        {
            thread_ = std::thread([this] { run(); });
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void stop() noexcept
    {
        stopRequested_.store(true, std::memory_order_release);
        wakeEvent_.signal();
        if (thread_.joinable())
            thread_.join();
    }

    std::uint64_t submit(const Job& job) noexcept
    {
        job_ = job;
        const std::uint64_t ticket =
            requestTicket_.load(std::memory_order_relaxed) + 1u;
        requestTicket_.store(ticket, std::memory_order_release);
        wakeEvent_.signal();
        return ticket;
    }

    Result wait(std::uint64_t ticket) noexcept
    {
        while (completedTicket_.load(std::memory_order_acquire) < ticket)
            std::this_thread::yield();

        return result_;
    }

private:
    void run() noexcept
    {
        std::uint64_t seenTicket = 0;

        while (!stopRequested_.load(std::memory_order_acquire))
        {
            wakeEvent_.wait();

            if (stopRequested_.load(std::memory_order_acquire))
                break;

            const std::uint64_t ticket =
                requestTicket_.load(std::memory_order_acquire);
            if (ticket == seenTicket)
                continue;

            const Job job = job_;
            Result local {};

            if (job.runHalf)
            {
                local.half = owner_.measureCoordinate(owner_.halfRateRing_,
                                            job.halfWritePosition,
                                            job.halfAvailableSamples,
                                            job.halfSampleRate,
                                            job.halfMinimum,
                                            job.halfMaximum,
                                            standardAnalysisSize,
                                            workspace_);
                local.half.pathIndex = 1;
                local.half.ageInHops = 0;
                local.halfComputed = true;
            }

            if (job.runEighth)
            {
                local.eighth = owner_.measureCoordinate(owner_.eighthRateRing_,
                                              job.eighthWritePosition,
                                              job.eighthAvailableSamples,
                                              job.eighthSampleRate,
                                              job.eighthMinimum,
                                              job.eighthMaximum,
                                              maxAnalysisSize,
                                              workspace_);
                local.eighth.pathIndex = 3;
                local.eighth.ageInHops = 0;
                local.eighthComputed = true;
            }

            result_ = local;
            seenTicket = ticket;
            completedTicket_.store(ticket, std::memory_order_release);
        }
    }

    MultiRatePitchTracker& owner_;
    AnalysisWorkspace workspace_ {};
    juce::WaitableEvent wakeEvent_;
    std::thread thread_;
    std::atomic<bool> stopRequested_ { false };
    std::atomic<std::uint64_t> requestTicket_ { 0 };
    std::atomic<std::uint64_t> completedTicket_ { 0 };
    Job job_ {};
    Result result_ {};
};

ModernPitchEngine::MultiRatePitchTracker::MultiRatePitchTracker() noexcept = default;
ModernPitchEngine::MultiRatePitchTracker::~MultiRatePitchTracker() = default;

void ModernPitchEngine::MultiRatePitchTracker::prepare(double sampleRate) noexcept
{
    // Host re-prepare happens off the realtime callback. Tear down an old
    // parked worker before changing any detector geometry it may read.
    analysisWorker_.reset();

    sampleRate_ = std::isfinite(sampleRate)
        ? std::max(8000.0, sampleRate)
        : 48000.0;

    halfRateAntiAlias_.prepare(sampleRate_, std::min(5200.0, sampleRate_ * 0.20));
    quarterRateAntiAlias_.prepare(sampleRate_ * 0.5,
                                  std::min(2600.0, sampleRate_ * 0.10));
    eighthRateAntiAlias_.prepare(sampleRate_ * 0.25,
                                 std::min(1300.0, sampleRate_ * 0.05));

    dcBlockCoefficient_ = static_cast<float>(std::exp(-twoPi * 22.0 / sampleRate_));

    fastEnergyCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.0018 * sampleRate_)));
    slowEnergyCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.035 * sampleRate_)));

    reset();

    // Allocation and thread creation are confined to prepare(). If the worker
    // cannot be created, processSample() keeps the exact serial golden path.
    std::unique_ptr<AnalysisWorker> candidate(
        new (std::nothrow) AnalysisWorker(*this));
    if (candidate != nullptr && candidate->start())
        analysisWorker_ = std::move(candidate);
}

void ModernPitchEngine::MultiRatePitchTracker::reset() noexcept
{
    fullRateRing_.fill(0.0f);
    halfRateRing_.fill(0.0f);
    quarterRateRing_.fill(0.0f);
    eighthRateRing_.fill(0.0f);
    analysisWorkspace_.frame.fill(0.0f);
    analysisWorkspace_.voiceResidualFrame.fill(0.0f);
    analysisWorkspace_.difference.fill(1.0f);

    fullRateWritePosition_ = 0;
    halfRateWritePosition_ = 0;
    quarterRateWritePosition_ = 0;
    eighthRateWritePosition_ = 0;
    fullRateAvailableSamples_ = 0;
    halfRateAvailableSamples_ = 0;
    quarterRateAvailableSamples_ = 0;
    eighthRateAvailableSamples_ = 0;
    halfRateDecimationCounter_ = 0;
    quarterRateDecimationCounter_ = 0;
    eighthRateDecimationCounter_ = 0;
    hopCounter_ = 0;
    analysisHopCounter_ = 0;

    halfRateAntiAlias_.reset();
    quarterRateAntiAlias_.reset();
    eighthRateAntiAlias_.reset();

    previousInput_ = 0.0f;
    previousDcOutput_ = 0.0f;
    fastEnergy_ = 0.0f;
    slowEnergy_ = 0.0f;
    noiseFloorEnergy_ = minimumDetectorRms * minimumDetectorRms;
    onsetEnvelope_ = 0.0f;
    onsetCooldownSamples_ = 0;
    onsetPending_ = false;

    fullRateCandidate_ = {};
    halfRateCandidate_ = {};
    quarterRateCandidate_ = {};
    eighthRateCandidate_ = {};
    decoderBeam_.fill({});

    trackedPitchHz_ = 0.0f;
    reacquisitionAnchorHz_ = 0.0f;
    trackedConfidence_ = 0.0f;
    trackedPeriodicity_ = 0.0f;
    trackedConsensus_ = 0.0f;
    trackedSupportCount_ = 0;
    invalidHopCount_ = 0;
    rescueMode_ = false;
    presenceMode_ = false;
    presenceSinceLastHop_ = false;
    transitionWake_ = false;
    observationContinuityBroken_ = false;
    voiceAuthorityContextValid_ = false;
    voiceAuthorityHarmonicity_ = 0.0f;
    voiceAuthorityBreathiness_ = 0.0f;
    voiceAuthorityBodyEnergy_ = 0.0f;
    voiceAuthoritySpectralReliability_ = 0.0f;
    voiceAuthorityFormantStability_ = 0.0f;
    voiceAuthorityLowerFamilyEvidence_ = 0.0f;

    octaveState_ = 0;
    pendingOctaveDelta_ = 0;
    pendingOctaveCount_ = 0;
    pendingOctaveFrequencyHz_ = 0.0f;
    committedOctaveFrequencyHz_ = 0.0f;
    octaveCommitGuardHops_ = 0;
}

void ModernPitchEngine::MultiRatePitchTracker::setRange(float minimumPitchHz,
                                                         float maximumPitchHz) noexcept
{
    minimumPitchHz_ = std::clamp(minimumPitchHz, 25.0f, 500.0f);
    maximumPitchHz_ = std::clamp(maximumPitchHz,
                                 minimumPitchHz_ + 20.0f,
                                 3000.0f);
}

void ModernPitchEngine::MultiRatePitchTracker::setSensitivity(float sensitivity) noexcept
{
    sensitivity_ = clamp01(sensitivity);
}

void ModernPitchEngine::MultiRatePitchTracker::setVoiceAuthorityContext(
    bool valid,
    float harmonicity,
    float breathiness,
    float bodyEnergy,
    float spectralReliability,
    float /*eventStrength*/,
    float formantStability,
    float lowerFamilyEvidence) noexcept
{
    voiceAuthorityContextValid_ = valid;
    voiceAuthorityHarmonicity_ = clamp01(harmonicity);
    voiceAuthorityBreathiness_ = clamp01(breathiness);
    voiceAuthorityBodyEnergy_ = clamp01(bodyEnergy);
    voiceAuthoritySpectralReliability_ = clamp01(spectralReliability);
    voiceAuthorityFormantStability_ = clamp01(formantStability);
    voiceAuthorityLowerFamilyEvidence_ = clamp01(lowerFamilyEvidence);
}

void ModernPitchEngine::MultiRatePitchTracker::setReacquisitionAnchor(
    float frequencyHz) noexcept
{
    // OBSERVATION_MEMORY_SEPARATION_V1
    // This value is only a short physical-continuity prior. It is not musical
    // memory. After a real input discontinuity the scale target may remain owned
    // downstream, but that old target may not repopulate detector history.
    if (observationContinuityBroken_)
    {
        reacquisitionAnchorHz_ = 0.0f;
        return;
    }
    reacquisitionAnchorHz_ = std::isfinite(frequencyHz) && frequencyHz > 0.0f
        ? std::clamp(frequencyHz, 20.0f, 4000.0f)
        : 0.0f;
}

void ModernPitchEngine::MultiRatePitchTracker::clearObservationMemory(
    bool clearAnalysisBuffers) noexcept
{
    trackedPitchHz_ = 0.0f;
    reacquisitionAnchorHz_ = 0.0f;
    trackedConfidence_ = 0.0f;
    trackedPeriodicity_ = 0.0f;
    trackedConsensus_ = 0.0f;
    trackedSupportCount_ = 0;
    invalidHopCount_ = 0;
    decoderBeam_.fill({});
    octaveState_ = 0;
    pendingOctaveDelta_ = 0;
    pendingOctaveCount_ = 0;
    pendingOctaveFrequencyHz_ = 0.0f;
    committedOctaveFrequencyHz_ = 0.0f;
    octaveCommitGuardHops_ = 0;
    observationContinuityBroken_ = true;

    if (!clearAnalysisBuffers)
        return;

    // A physical gap means samples on the two sides are not one analysis frame.
    // Drop only detector buffers; the single wet renderer/OLA is untouched.
    fullRateRing_.fill(0.0f);
    halfRateRing_.fill(0.0f);
    quarterRateRing_.fill(0.0f);
    eighthRateRing_.fill(0.0f);
    fullRateWritePosition_ = 0;
    halfRateWritePosition_ = 0;
    quarterRateWritePosition_ = 0;
    eighthRateWritePosition_ = 0;
    fullRateAvailableSamples_ = 0;
    halfRateAvailableSamples_ = 0;
    quarterRateAvailableSamples_ = 0;
    eighthRateAvailableSamples_ = 0;
    halfRateDecimationCounter_ = 0;
    quarterRateDecimationCounter_ = 0;
    eighthRateDecimationCounter_ = 0;
    analysisHopCounter_ = 0;
    fullRateCandidate_ = {};
    halfRateCandidate_ = {};
    quarterRateCandidate_ = {};
    eighthRateCandidate_ = {};
    halfRateAntiAlias_.reset();
    quarterRateAntiAlias_.reset();
    eighthRateAntiAlias_.reset();
    previousInput_ = 0.0f;
    previousDcOutput_ = 0.0f;
    onsetEnvelope_ = 0.0f;
    onsetCooldownSamples_ = 0;
    onsetPending_ = false;
    presenceSinceLastHop_ = false;
}

void ModernPitchEngine::MultiRatePitchTracker::push(
    std::array<float, ringSize>& ring,
    int& writePosition,
    int& availableSamples,
    float sample) noexcept
{
    ring[static_cast<std::size_t>(writePosition)] = sample;
    writePosition = (writePosition + 1) & ringMask;
    availableSamples = std::min(availableSamples + 1, ringSize);
}

ModernPitchEngine::MultiRatePitchTracker::PitchCandidate
ModernPitchEngine::MultiRatePitchTracker::measureCoordinate(
    const std::array<float, ringSize>& ring,
    int writePosition,
    int availableSamples,
    double effectiveSampleRate,
    float minimumFrequency,
    float maximumFrequency,
    int analysisLength,
    AnalysisWorkspace& workspace) noexcept
{
    auto& frame_ = workspace.frame;
    auto& voiceResidualFrame_ = workspace.voiceResidualFrame;
    auto& difference_ = workspace.difference;

    PitchCandidate result;
    analysisLength = std::clamp(analysisLength, 64, maxAnalysisSize);

    if (availableSamples < analysisLength || effectiveSampleRate <= 0.0
        || minimumFrequency >= maximumFrequency)
    {
        return result;
    }

    const int startPosition = (writePosition - analysisLength + ringSize) & ringMask;

    double mean = 0.0;
    for (int index = 0; index < analysisLength; ++index)
    {
        const float sample = ring[static_cast<std::size_t>((startPosition + index) & ringMask)];
        frame_[static_cast<std::size_t>(index)] = sample;
        mean += static_cast<double>(sample);
    }
    mean /= static_cast<double>(analysisLength);

    double squaredSum = 0.0;
    for (int index = 0; index < analysisLength; ++index)
    {
        float& sample = frame_[static_cast<std::size_t>(index)];
        sample -= static_cast<float>(mean);
        squaredSum += static_cast<double>(sample) * static_cast<double>(sample);
    }

    const float rms = static_cast<float>(std::sqrt(
        squaredSum / static_cast<double>(analysisLength)));
    if (rms < minimumDetectorRms)
        return result;

    // Reuse the existing first-order inverse-filtered residual. This is the
    // measurement substrate; it is not an authority/confidence layer.
    double predictorNumerator = 0.0;
    double predictorDenominator = 0.0;
    for (int index = 1; index < analysisLength; ++index)
    {
        const double current = frame_[static_cast<std::size_t>(index)];
        const double previous = frame_[static_cast<std::size_t>(index - 1)];
        predictorNumerator += current * previous;
        predictorDenominator += previous * previous;
    }
    const float predictor = static_cast<float>(std::clamp(
        predictorNumerator / std::max(1.0e-20, predictorDenominator),
        -0.92, 0.92));
    voiceResidualFrame_[0] = frame_[0];
    for (int index = 1; index < analysisLength; ++index)
    {
        voiceResidualFrame_[static_cast<std::size_t>(index)] =
            frame_[static_cast<std::size_t>(index)]
            - predictor * frame_[static_cast<std::size_t>(index - 1)];
    }

    const float noiseRms = std::sqrt(std::max(1.0e-12f, noiseFloorEnergy_));
    const float snrRatio = rms / std::max(0.5f * minimumDetectorRms, noiseRms);
    const float snrSupport = smoothStep(1.10f, 3.50f, snrRatio);

    const int tauMinimum = std::clamp(
        static_cast<int>(std::floor(effectiveSampleRate
                                    / static_cast<double>(maximumFrequency))),
        2,
        analysisLength - 16);

    const int tauMaximum = std::clamp(
        static_cast<int>(std::ceil(effectiveSampleRate
                                   / static_cast<double>(minimumFrequency))),
        tauMinimum + 1,
        analysisLength - 16);

    difference_.fill(1.0f);
    difference_[0] = 1.0f;

    for (int tau = 1; tau <= tauMaximum; ++tau)
    {
        const int overlap = analysisLength - tau;
        float sum0 = 0.0f;
        float sum1 = 0.0f;
        float sum2 = 0.0f;
        float sum3 = 0.0f;

        int index = 0;
        const int vectorEnd = overlap & ~3;
        for (; index < vectorEnd; index += 4)
        {
            const float delta0 = voiceResidualFrame_[static_cast<std::size_t>(index)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
            const float delta1 = voiceResidualFrame_[static_cast<std::size_t>(index + 1)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau + 1)];
            const float delta2 = voiceResidualFrame_[static_cast<std::size_t>(index + 2)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau + 2)];
            const float delta3 = voiceResidualFrame_[static_cast<std::size_t>(index + 3)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau + 3)];
            sum0 += delta0 * delta0;
            sum1 += delta1 * delta1;
            sum2 += delta2 * delta2;
            sum3 += delta3 * delta3;
        }

        float differenceSum = (sum0 + sum1) + (sum2 + sum3);
        for (; index < overlap; ++index)
        {
            const float delta = voiceResidualFrame_[static_cast<std::size_t>(index)]
                              - voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
            differenceSum += delta * delta;
        }

        difference_[static_cast<std::size_t>(tau)] = differenceSum
            / static_cast<float>(std::max(1, overlap));
    }

    double cumulativeSum = 0.0;
    for (int tau = 1; tau <= tauMaximum; ++tau)
    {
        cumulativeSum += static_cast<double>(difference_[static_cast<std::size_t>(tau)]);
        difference_[static_cast<std::size_t>(tau)] = cumulativeSum > 1.0e-20
            ? static_cast<float>(static_cast<double>(difference_[static_cast<std::size_t>(tau)])
                                 * static_cast<double>(tau) / cumulativeSum)
            : 1.0f;
    }

    const float yinThreshold = 0.12f + 0.16f * sensitivity_;
    int thresholdTau = -1;
    int globalTau = tauMinimum;
    float globalValue = difference_[static_cast<std::size_t>(tauMinimum)];

    for (int tau = tauMinimum; tau <= tauMaximum; ++tau)
    {
        const float value = difference_[static_cast<std::size_t>(tau)];
        if (value < globalValue)
        {
            globalValue = value;
            globalTau = tau;
        }

        if (thresholdTau < 0 && value < yinThreshold)
        {
            int localTau = tau;
            while (localTau + 1 <= tauMaximum
                   && difference_[static_cast<std::size_t>(localTau + 1)]
                        < difference_[static_cast<std::size_t>(localTau)])
            {
                ++localTau;
            }
            thresholdTau = localTau;
        }
    }

    std::array<int, 5> candidateTaus {
        thresholdTau >= 0 ? thresholdTau : globalTau,
        globalTau,
        std::max(tauMinimum, globalTau / 2),
        std::min(tauMaximum, globalTau * 2),
        std::min(tauMaximum, (globalTau * 3) / 2)
    };
    constexpr std::array<float, 5> candidatePriors {
        1.00f, 0.98f, 0.88f, 0.70f, 0.78f
    };

    const auto residualLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return 0.0f;
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
            const double b = voiceResidualFrame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        return denominator > 0.0
            ? clamp01(static_cast<float>(correlation / denominator)) : 0.0f;
    };

    const auto sourceLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return -1.0f;
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = frame_[static_cast<std::size_t>(index)];
            const double b = frame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        return denominator > 0.0
            ? static_cast<float>(correlation / denominator) : -1.0f;
    };

    struct CoordinateEvidence
    {
        int tau = -1;
        float periodicity = 0.0f;
        float yinConfidence = 0.0f;
        float cycleFamily = 0.0f;
        float periodSupport = 0.0f;
    };
    std::array<CoordinateEvidence, 5> evidenceCache {};
    std::size_t evidenceCount = 0;

    float bestScore = -1.0f;
    int bestTau = -1;
    float bestPeriodicity = 0.0f;

    for (std::size_t candidateIndex = 0;
         candidateIndex < candidateTaus.size();
         ++candidateIndex)
    {
        const int tau = std::clamp(candidateTaus[candidateIndex],
                                   tauMinimum,
                                   tauMaximum);

        CoordinateEvidence evidence;
        bool cached = false;
        for (std::size_t cachedIndex = 0; cachedIndex < evidenceCount; ++cachedIndex)
        {
            if (evidenceCache[cachedIndex].tau == tau)
            {
                evidence = evidenceCache[cachedIndex];
                cached = true;
                break;
            }
        }

        if (!cached)
        {
            evidence.tau = tau;
            evidence.periodicity = residualLagCorrelation(tau);
            evidence.yinConfidence = clamp01(
                1.0f - difference_[static_cast<std::size_t>(tau)]);

            float cycleFamilySum = evidence.periodicity;
            float cycleFamilyWeight = 1.0f;
            if (2 * tau < analysisLength - 8)
            {
                cycleFamilySum += 0.70f * residualLagCorrelation(2 * tau);
                cycleFamilyWeight += 0.70f;
            }
            if (3 * tau < analysisLength - 8)
            {
                cycleFamilySum += 0.45f * residualLagCorrelation(3 * tau);
                cycleFamilyWeight += 0.45f;
            }
            evidence.cycleFamily = clamp01(cycleFamilySum / cycleFamilyWeight);

            const float periodsInWindow = static_cast<float>(analysisLength)
                                        / static_cast<float>(std::max(1, tau));
            evidence.periodSupport = std::clamp(
                periodsInWindow / 2.2f, 0.55f, 1.0f);

            evidenceCache[evidenceCount++] = evidence;
        }

        // Existing score terms only: the certification-only harmonic-contrast
        // term is deliberately absent. Nothing below may veto a finite coordinate.
        const float score = (0.44f * evidence.yinConfidence
                           + 0.22f * evidence.periodicity
                           + 0.17f * evidence.cycleFamily)
                          * evidence.periodSupport
                          * candidatePriors[candidateIndex]
                          * (0.82f + 0.18f * snrSupport);

        if (score > bestScore)
        {
            bestScore = score;
            bestTau = tau;
            bestPeriodicity = evidence.periodicity;
        }
    }

    if (bestTau < 2)
        return result;

    int sourceTau = bestTau;
    float sourcePeak = sourceLagCorrelation(bestTau);
    for (int offset = -2; offset <= 2; ++offset)
    {
        const int candidateTau = bestTau + offset;
        if (candidateTau < tauMinimum || candidateTau > tauMaximum)
            continue;
        const float candidatePeak = sourceLagCorrelation(candidateTau);
        if (candidatePeak > sourcePeak)
        {
            sourcePeak = candidatePeak;
            sourceTau = candidateTau;
        }
    }

    // Reuse PRIMITIVE_DIVISOR_GEOMETRY_V6_6 unchanged: octave/family correction
    // is geometry, not permission to expose the resulting coordinate.
    const int selectedSourceTau = sourceTau;
    const float selectedSourceCorrelation = sourcePeak;
    const float selectedResidualCorrelation = residualLagCorrelation(sourceTau);
    const float selectedYin = clamp01(
        1.0f - difference_[static_cast<std::size_t>(sourceTau)]);

    int primitiveTau = sourceTau;
    float primitiveSourceCorrelation = selectedSourceCorrelation;
    float primitiveResidualCorrelation = selectedResidualCorrelation;
    float primitiveYin = selectedYin;

    constexpr std::array<int, 3> primitiveDivisors { 4, 3, 2 };
    for (const int divisor : primitiveDivisors)
    {
        const int candidateTau = static_cast<int>(std::lround(
            static_cast<double>(selectedSourceTau)
            / static_cast<double>(divisor)));
        if (candidateTau < tauMinimum
            || candidateTau > tauMaximum
            || candidateTau >= selectedSourceTau - 2)
        {
            continue;
        }

        const float candidateSourceCorrelation = sourceLagCorrelation(candidateTau);
        if (candidateSourceCorrelation < 0.55f
            || candidateSourceCorrelation < selectedSourceCorrelation - 0.12f)
        {
            continue;
        }

        const float candidateResidualCorrelation = residualLagCorrelation(candidateTau);
        const float candidateYin = clamp01(
            1.0f - difference_[static_cast<std::size_t>(candidateTau)]);
        if (candidateResidualCorrelation < 0.30f
            || candidateYin < 0.30f
            || candidateResidualCorrelation < selectedResidualCorrelation - 0.12f
            || candidateYin < selectedYin - 0.12f)
        {
            continue;
        }

        primitiveTau = candidateTau;
        primitiveSourceCorrelation = candidateSourceCorrelation;
        primitiveResidualCorrelation = candidateResidualCorrelation;
        primitiveYin = candidateYin;
        break;
    }

    if (primitiveTau != sourceTau)
    {
        sourceTau = primitiveTau;
        sourcePeak = primitiveSourceCorrelation;
        bestPeriodicity = clamp01(primitiveResidualCorrelation);
        (void) primitiveYin;
    }

    double refinedTau = static_cast<double>(sourceTau);
    if (sourceTau > tauMinimum && sourceTau < tauMaximum)
    {
        const double left = sourceLagCorrelation(sourceTau - 1);
        const double centre = sourceLagCorrelation(sourceTau);
        const double right = sourceLagCorrelation(sourceTau + 1);
        const double denominator = left - 2.0 * centre + right;
        if (std::abs(denominator) > 1.0e-12)
        {
            const double fractional = std::clamp(
                0.5 * (left - right) / denominator, -0.75, 0.75);
            refinedTau += fractional;
        }
    }

    if (refinedTau <= 0.0)
        return result;

    const float frequency = static_cast<float>(effectiveSampleRate / refinedTau);
    if (!std::isfinite(frequency)
        || frequency < minimumFrequency * 0.82f
        || frequency > maximumFrequency * 1.18f)
    {
        return result;
    }

    result.frequencyHz = frequency;
    result.confidence = clamp01(bestScore);
    result.periodicity = bestPeriodicity;
    result.harmonicFamily = -1.0f;
    result.aperiodicity = -1.0f;
    result.tonalCleanliness = -1.0f;
    result.valid = true;
    return result;
}

ModernPitchEngine::MultiRatePitchTracker::PitchCandidate
ModernPitchEngine::MultiRatePitchTracker::analyse(
    const std::array<float, ringSize>& ring,
    int writePosition,
    int availableSamples,
    double effectiveSampleRate,
    float minimumFrequency,
    float maximumFrequency,
    int analysisLength,
    AnalysisWorkspace& workspace) noexcept
{
    // ANALYSIS_WORKSPACE_REENTRANCY_V1: preserve the golden detector body and
    // its exact operation ordering. These local aliases deliberately keep the
    // original identifiers used by every arithmetic expression below.
    auto& frame_ = workspace.frame;
    auto& voiceResidualFrame_ = workspace.voiceResidualFrame;
    auto& difference_ = workspace.difference;
    auto& residualHannWindow_ = workspace.residualHannWindow;

    PitchCandidate result;
    analysisLength = std::clamp(analysisLength, 64, maxAnalysisSize);

    if (availableSamples < analysisLength || effectiveSampleRate <= 0.0
        || minimumFrequency >= maximumFrequency)
    {
        return result;
    }

    const int startPosition = (writePosition - analysisLength + ringSize) & ringMask;

    double mean = 0.0;
    for (int index = 0; index < analysisLength; ++index)
    {
        const float sample = ring[static_cast<std::size_t>((startPosition + index) & ringMask)];
        frame_[static_cast<std::size_t>(index)] = sample;
        mean += static_cast<double>(sample);
    }
    mean /= static_cast<double>(analysisLength);

    double squaredSum = 0.0;
    for (int index = 0; index < analysisLength; ++index)
    {
        float& sample = frame_[static_cast<std::size_t>(index)];
        sample -= static_cast<float>(mean);
        squaredSum += static_cast<double>(sample) * static_cast<double>(sample);
    }

    const float rms = static_cast<float>(std::sqrt(
        squaredSum / static_cast<double>(analysisLength)));
    // DETECTOR_IS_OBSERVER_V1: audio presence does not lower pitch-analysis
    // standards. Aperiodic material may correctly yield no F0 while the
    // downstream scale target remains fully authoritative.
    if (rms < minimumDetectorRms)
        return result;

    // VOICE_AWARE_F0_FRONTEND_V1
    // Estimate a first-order vocal-tract predictor from this analysis frame and
    // run period estimation on the inverse-filtered residual. This is analysis
    // only: no sample from voiceResidualFrame_ can reach the audio renderer.
    double predictorNumerator = 0.0;
    double predictorDenominator = 0.0;
    for (int index = 1; index < analysisLength; ++index)
    {
        const double current = frame_[static_cast<std::size_t>(index)];
        const double previous = frame_[static_cast<std::size_t>(index - 1)];
        predictorNumerator += current * previous;
        predictorDenominator += previous * previous;
    }
    const float predictor = static_cast<float>(std::clamp(
        predictorNumerator / std::max(1.0e-20, predictorDenominator),
        -0.92, 0.92));
    voiceResidualFrame_[0] = frame_[0];
    for (int index = 1; index < analysisLength; ++index)
    {
        voiceResidualFrame_[static_cast<std::size_t>(index)] =
            frame_[static_cast<std::size_t>(index)]
            - predictor * frame_[static_cast<std::size_t>(index - 1)];
    }

    const float noiseRms = std::sqrt(std::max(1.0e-12f, noiseFloorEnergy_));
    const float snrRatio = rms / std::max(0.5f * minimumDetectorRms, noiseRms);
    const float snrSupport = smoothStep(1.10f, 3.50f, snrRatio);

    const int tauMinimum = std::clamp(
        static_cast<int>(std::floor(effectiveSampleRate
                                    / static_cast<double>(maximumFrequency))),
        2,
        analysisLength - 16);

    const int tauMaximum = std::clamp(
        static_cast<int>(std::ceil(effectiveSampleRate
                                   / static_cast<double>(minimumFrequency))),
        tauMinimum + 1,
        analysisLength - 16);

    difference_.fill(1.0f);
    difference_[0] = 1.0f;

    for (int tau = 1; tau <= tauMaximum; ++tau)
    {
        const int overlap = analysisLength - tau;
        float sum0 = 0.0f;
        float sum1 = 0.0f;
        float sum2 = 0.0f;
        float sum3 = 0.0f;

        int index = 0;
        const int vectorEnd = overlap & ~3;
        for (; index < vectorEnd; index += 4)
        {
            const float delta0 = voiceResidualFrame_[static_cast<std::size_t>(index)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
            const float delta1 = voiceResidualFrame_[static_cast<std::size_t>(index + 1)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau + 1)];
            const float delta2 = voiceResidualFrame_[static_cast<std::size_t>(index + 2)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau + 2)];
            const float delta3 = voiceResidualFrame_[static_cast<std::size_t>(index + 3)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau + 3)];
            sum0 += delta0 * delta0;
            sum1 += delta1 * delta1;
            sum2 += delta2 * delta2;
            sum3 += delta3 * delta3;
        }

        float differenceSum = (sum0 + sum1) + (sum2 + sum3);
        for (; index < overlap; ++index)
        {
            const float delta = voiceResidualFrame_[static_cast<std::size_t>(index)]
                              - voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
            differenceSum += delta * delta;
        }

        difference_[static_cast<std::size_t>(tau)] = differenceSum
            / static_cast<float>(std::max(1, overlap));
    }

    double cumulativeSum = 0.0;
    for (int tau = 1; tau <= tauMaximum; ++tau)
    {
        cumulativeSum += static_cast<double>(difference_[static_cast<std::size_t>(tau)]);
        difference_[static_cast<std::size_t>(tau)] = cumulativeSum > 1.0e-20
            ? static_cast<float>(static_cast<double>(difference_[static_cast<std::size_t>(tau)])
                                 * static_cast<double>(tau) / cumulativeSum)
            : 1.0f;
    }

    const float yinThreshold = 0.12f + 0.16f * sensitivity_;
    const float fallbackThreshold = 0.26f + 0.20f * sensitivity_
        + (rescueMode_ ? 0.08f : 0.0f);

    int thresholdTau = -1;
    int globalTau = tauMinimum;
    float globalValue = difference_[static_cast<std::size_t>(tauMinimum)];

    for (int tau = tauMinimum; tau <= tauMaximum; ++tau)
    {
        const float value = difference_[static_cast<std::size_t>(tau)];
        if (value < globalValue)
        {
            globalValue = value;
            globalTau = tau;
        }

        if (thresholdTau < 0 && value < yinThreshold)
        {
            int localTau = tau;
            while (localTau + 1 <= tauMaximum
                   && difference_[static_cast<std::size_t>(localTau + 1)]
                        < difference_[static_cast<std::size_t>(localTau)])
            {
                ++localTau;
            }
            thresholdTau = localTau;
        }
    }

    // MEASUREMENT_CONTINUUM_V1: do not collapse a weak period estimate into
    // the same state as "no measurement".  A poor YIN shape stays provisional:
    // frequency/confidence/periodicity are retained, while valid remains false
    // unless the normal structural threshold is met.  The supervisor may use
    // that grey-zone measurement only when independent voice-body evidence says
    // it belongs to a sung body rather than a consonant/noise event.
    const bool structurallyTrusted = thresholdTau >= 0
        || globalValue <= fallbackThreshold;

    // Alternative periods are deliberately retained because a weak fundamental
    // can be recovered from its harmonics.  They are not equally trusted:
    // doubled periods (subharmonics) receive the strongest prior penalty and
    // must subsequently survive the cross-rate consensus and temporal decoder.
    std::array<int, 5> candidateTaus {
        thresholdTau >= 0 ? thresholdTau : globalTau,
        globalTau,
        std::max(tauMinimum, globalTau / 2),
        std::min(tauMaximum, globalTau * 2),
        std::min(tauMaximum, (globalTau * 3) / 2)
    };
    constexpr std::array<float, 5> candidatePriors {
        1.00f, 0.98f, 0.88f, 0.70f, 0.78f
    };

    const auto residualLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return 0.0f;
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
            const double b = voiceResidualFrame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        return denominator > 0.0
            ? clamp01(static_cast<float>(correlation / denominator)) : 0.0f;
    };

    // RESIDUAL_HARMONIC_CONTRAST_V1
    // A real voiced source produces narrow coherent lines at F0 multiples after
    // inverse filtering. Broadband breath/background produces comparable energy
    // between those lines. The half-harmonic subtraction also suppresses the
    // common 2F0 alias without requiring detector history to own the register.
    //
    // RESIDUAL_LINE_EXACT_MEMOIZATION_V1
    // Cache only an exactly identical IEEE-754 argument bit pattern within this
    // analyse() call. The first request executes the golden loop unchanged.
    std::array<std::uint64_t, 96> residualLineCacheKeys {};
    std::array<float, 96> residualLineCacheValues {};
    std::size_t residualLineCacheCount = 0;

    // RESIDUAL_HANN_LAZY_MEMOIZATION_V1
    // False at the beginning of every analyse() call. The first unique spectral
    // line computes each Hann coefficient at the exact point where the golden
    // loop computed it and stores the resulting double. No sample/window-energy
    // accumulation is moved or precomputed.
    bool residualHannReady = false;

    // RESIDUAL_ENERGY_EXACT_REUSE_V1
    // signalEnergy and windowEnergy are independent of cyclesPerSample.
    // The first unique spectral line still computes them at the exact golden
    // points and in the exact golden order. Later unique lines reuse only the
    // already-rounded double results from that first line.
    bool residualEnergyReady = false;
    double residualSignalEnergy = 0.0;
    double residualWindowEnergy = 0.0;

    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
    {
        if (!std::isfinite(cyclesPerSample)
            || cyclesPerSample <= 0.0 || cyclesPerSample >= 0.48)
        {
            return 0.0f;
        }

        std::uint64_t cacheKey = 0;
        static_assert(sizeof(cacheKey) == sizeof(cyclesPerSample));
        std::memcpy(&cacheKey, &cyclesPerSample, sizeof(cacheKey));
        for (std::size_t cacheIndex = 0;
             cacheIndex < residualLineCacheCount;
             ++cacheIndex)
        {
            if (residualLineCacheKeys[cacheIndex] == cacheKey)
                return residualLineCacheValues[cacheIndex];
        }

        double real = 0.0;
        double imag = 0.0;
        double signalEnergy = residualEnergyReady ? residualSignalEnergy : 0.0;
        double windowEnergy = residualEnergyReady ? residualWindowEnergy : 0.0;
        const double denominatorN = static_cast<double>(std::max(1, analysisLength - 1));
        for (int index = 0; index < analysisLength; ++index)
        {
            double window = 0.0;
            if (!residualHannReady)
            {
                window = 0.5 - 0.5 * std::cos(
                    twoPi * static_cast<double>(index) / denominatorN);
                residualHannWindow_[static_cast<std::size_t>(index)] = window;
            }
            else
            {
                window = residualHannWindow_[static_cast<std::size_t>(index)];
            }

            const double sample = static_cast<double>(
                voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
            const double phase = twoPi * cyclesPerSample * static_cast<double>(index);
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
            if (!residualEnergyReady)
            {
                signalEnergy += sample * sample;
                windowEnergy += window * window;
            }
        }
        if (!residualEnergyReady)
        {
            residualSignalEnergy = signalEnergy;
            residualWindowEnergy = windowEnergy;
            residualEnergyReady = true;
        }
        residualHannReady = true;
        const double normaliser = std::max(1.0e-20, signalEnergy * windowEnergy);
        const float value = clamp01(static_cast<float>(std::sqrt(
            2.0 * (real * real + imag * imag) / normaliser)));

        if (residualLineCacheCount < residualLineCacheKeys.size())
        {
            residualLineCacheKeys[residualLineCacheCount] = cacheKey;
            residualLineCacheValues[residualLineCacheCount] = value;
            ++residualLineCacheCount;
        }
        return value;
    };

    const auto residualHarmonicContrast = [&](int tau) noexcept
    {
        if (tau <= 1)
            return 0.0f;
        const double fundamentalCycles = 1.0 / static_cast<double>(tau);
        float harmonicScore = residualLineCoherence(fundamentalCycles);
        float interHarmonicScore = 0.0f;
        float harmonicWeight = 1.0f;
        float interWeight = 0.0f;
        for (int harmonic = 2; harmonic <= 6; ++harmonic)
        {
            const double harmonicCycles = fundamentalCycles
                                        * static_cast<double>(harmonic);
            if (harmonicCycles >= 0.45)
                break;
            const float weight = 1.0f / std::sqrt(static_cast<float>(harmonic));
            harmonicScore += weight * residualLineCoherence(harmonicCycles);
            harmonicWeight += weight;

            const double interCycles = fundamentalCycles
                                     * (static_cast<double>(harmonic) - 0.5);
            if (interCycles < 0.45)
            {
                interHarmonicScore += weight * residualLineCoherence(interCycles);
                interWeight += weight;
            }
        }
        const float harmonicMean = harmonicScore / std::max(1.0e-6f, harmonicWeight);
        const float interMean = interWeight > 1.0e-6f
            ? interHarmonicScore / interWeight : 0.0f;
        // Require harmonic lines to emerge above the local inter-harmonic floor,
        // not merely above absolute amplitude. This remains useful at low SNR.
        return smoothStep(0.025f, 0.30f,
                          harmonicMean - 0.78f * interMean);
    };

    float bestScore = -1.0f;
    float bestSelectionScore = -1.0f;
    int bestTau = -1;
    float bestPeriodicity = 0.0f;
    float bestHarmonicFamily = 0.0f;
    float bestTonalCleanliness = 0.0f;

    // TAU_EVIDENCE_MEMOIZATION_V1
    // Candidate priors and selection order remain independent, but two slots
    // resolving to the same clamped tau must not recompute identical physical
    // evidence. Every cached field is already a float in the golden path, so
    // reuse preserves the exact inputs to the unchanged score expression.
    struct CandidateTauEvidence
    {
        int tau = -1;
        float periodicity = 0.0f;
        float yinConfidence = 0.0f;
        float cycleFamily = 0.0f;
        float harmonicContrast = 0.0f;
        float harmonicFamily = 0.0f;
        float tonalCleanliness = 0.0f;
        float periodSupport = 0.0f;
    };
    std::array<CandidateTauEvidence, 5> tauEvidenceCache {};
    std::size_t tauEvidenceCount = 0;

    for (std::size_t candidateIndex = 0;
         candidateIndex < candidateTaus.size();
         ++candidateIndex)
    {
        int tau = std::clamp(candidateTaus[candidateIndex],
                             tauMinimum,
                             tauMaximum);

        const CandidateTauEvidence* cachedEvidence = nullptr;
        for (std::size_t cachedIndex = 0;
             cachedIndex < tauEvidenceCount;
             ++cachedIndex)
        {
            if (tauEvidenceCache[cachedIndex].tau == tau)
            {
                cachedEvidence = &tauEvidenceCache[cachedIndex];
                break;
            }
        }

        CandidateTauEvidence evidence;
        if (cachedEvidence != nullptr)
        {
            evidence = *cachedEvidence;
        }
        else
        {
            evidence.tau = tau;

            double correlation = 0.0;
            double energyA = 0.0;
            double energyB = 0.0;
            const int overlap = analysisLength - tau;

            for (int index = 0; index < overlap; ++index)
            {
                const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
                const double b = voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
                correlation += a * b;
                energyA += a * a;
                energyB += b * b;
            }

            const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
            const float normalisedCorrelation = denominator > 0.0
                ? static_cast<float>(correlation / denominator)
                : 0.0f;
            // Zero correlation is zero periodic evidence. The old affine mapping
            // made uncorrelated noise start at 0.5 periodicity.
            evidence.periodicity = clamp01(normalisedCorrelation);
            evidence.yinConfidence = clamp01(
                1.0f - difference_[static_cast<std::size_t>(tau)]);

            float cycleFamilySum = evidence.periodicity;
            float cycleFamilyWeight = 1.0f;
            if (2 * tau < analysisLength - 8)
            {
                cycleFamilySum += 0.70f * residualLagCorrelation(2 * tau);
                cycleFamilyWeight += 0.70f;
            }
            if (3 * tau < analysisLength - 8)
            {
                cycleFamilySum += 0.45f * residualLagCorrelation(3 * tau);
                cycleFamilyWeight += 0.45f;
            }
            evidence.cycleFamily = clamp01(cycleFamilySum / cycleFamilyWeight);
            evidence.harmonicContrast = residualHarmonicContrast(tau);
            // Both time-domain repetition and a residual harmonic comb must agree.
            // A weak value in either dimension cannot be hidden by the other one.
            evidence.harmonicFamily = std::sqrt(std::max(
                0.0f, evidence.cycleFamily * evidence.harmonicContrast));
            const float repeatedTonalStructure = std::sqrt(std::max(
                0.0f, evidence.periodicity * evidence.cycleFamily));

            // SUBFRAME_GLOTTAL_STABILITY_V1
            const auto subframeLagCorrelation = [&](int start, int length, int lag) noexcept
            {
                if (lag <= 0 || length <= lag + 8 || start < 0
                    || start + length > analysisLength)
                {
                    return 0.0f;
                }
                double corr = 0.0;
                double energyA = 0.0;
                double energyB = 0.0;
                const int stop = start + length - lag;
                for (int index = start; index < stop; ++index)
                {
                    const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
                    const double b = voiceResidualFrame_[static_cast<std::size_t>(index + lag)];
                    corr += a * b;
                    energyA += a * a;
                    energyB += b * b;
                }
                const double denominator = std::sqrt(std::max(1.0e-20,
                                                              energyA * energyB));
                return denominator > 0.0
                    ? clamp01(static_cast<float>(corr / denominator)) : 0.0f;
            };

            const int halfLength = analysisLength / 2;
            const float firstHalfPeriodicity = subframeLagCorrelation(0,
                                                                      halfLength,
                                                                      tau);
            const float secondHalfPeriodicity = subframeLagCorrelation(
                analysisLength - halfLength, halfLength, tau);
            const float subframeStability = std::sqrt(std::max(
                0.0f, firstHalfPeriodicity * secondHalfPeriodicity));
            const float stableRepeatedStructure = std::sqrt(std::max(
                0.0f, repeatedTonalStructure * subframeStability));
            evidence.tonalCleanliness = clamp01(std::sqrt(std::max(
                0.0f, evidence.harmonicContrast * stableRepeatedStructure))
                * (0.90f + 0.10f * snrSupport));

            // Prefer candidates containing at least two periods, but do not reject
            // low notes whose fundamental is mainly inferred from their harmonics.
            const float periodsInWindow = static_cast<float>(analysisLength)
                                        / static_cast<float>(std::max(1, tau));
            evidence.periodSupport = std::clamp(
                periodsInWindow / 2.2f, 0.55f, 1.0f);

            tauEvidenceCache[tauEvidenceCount++] = evidence;
        }

        const float periodicity = evidence.periodicity;
        const float yinConfidence = evidence.yinConfidence;
        const float cycleFamily = evidence.cycleFamily;
        const float harmonicContrast = evidence.harmonicContrast;
        const float harmonicFamily = evidence.harmonicFamily;
        const float tonalCleanliness = evidence.tonalCleanliness;
        const float periodSupport = evidence.periodSupport;

        const float score = (0.44f * yinConfidence
                           + 0.22f * periodicity
                           + 0.17f * cycleFamily
                           + 0.17f * harmonicContrast)
                          * periodSupport
                          * candidatePriors[candidateIndex]
                          * (0.82f + 0.18f * snrSupport);

        // DIRECT_HIGH_YIN_FIRST_MINIMUM_V1: selection-only preference.
        // Never inflate the published confidence/evidence score.
        const bool directHighThresholdCandidate = thresholdTau >= 0
            && candidateIndex == 0
            && effectiveSampleRate >= sampleRate_ * 0.75
            && effectiveSampleRate / static_cast<double>(std::max(1, tau)) > 900.0
            && harmonicFamily >= 0.60f
            && tonalCleanliness >= 0.68f;
        const float selectionScore = score
            * (directHighThresholdCandidate ? 1.35f : 1.0f);

        if (selectionScore > bestSelectionScore)
        {
            bestSelectionScore = selectionScore;
            bestScore = score;
            bestTau = tau;
            bestPeriodicity = periodicity;
            bestHarmonicFamily = harmonicFamily;
            bestTonalCleanliness = tonalCleanliness;
        }
    }

    const float minimumCandidateScore = rescueMode_ ? 0.34f : 0.45f;
    const float provisionalFamilyFloor = rescueMode_ ? 0.16f : 0.19f;
    const float provisionalScoreFloor = rescueMode_ ? 0.20f : 0.24f;
    if (bestTau < 2
        || bestHarmonicFamily < provisionalFamilyFloor
        || bestScore < provisionalScoreFloor)
    {
        return result;
    }

    // CLEAN_RESIDUAL_DISCOVERS_SOURCE_GEOMETRY_REFINES_V1
    const auto sourceLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return -1.0f;
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = frame_[static_cast<std::size_t>(index)];
            const double b = frame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        return denominator > 0.0
            ? static_cast<float>(correlation / denominator) : -1.0f;
    };

    int sourceTau = bestTau;
    float sourcePeak = sourceLagCorrelation(bestTau);
    for (int offset = -2; offset <= 2; ++offset)
    {
        const int candidateTau = bestTau + offset;
        if (candidateTau < tauMinimum || candidateTau > tauMaximum)
            continue;
        const float candidatePeak = sourceLagCorrelation(candidateTau);
        if (candidatePeak > sourcePeak)
        {
            sourcePeak = candidatePeak;
            sourceTau = candidateTau;
        }
    }

    // PRIMITIVE_DIVISOR_GEOMETRY_V6_6
    // The existing candidate set can represent global/2 but not global/3 or
    // global/4. Consequently a path that falls into 3P or 4P literally has no
    // route back to P before this point. Rather than adding more full candidate
    // evaluations (and CPU), test only simple divisors of the already selected
    // basin, using cheap staged current-sample geometry.
    const int selectedSourceTauV66 = sourceTau;
    const float selectedSourceCorrelationV66 = sourcePeak;
    const float selectedResidualCorrelationV66 = residualLagCorrelation(sourceTau);
    const float selectedYinV66 = clamp01(
        1.0f - difference_[static_cast<std::size_t>(sourceTau)]);

    int primitiveTauV66 = sourceTau;
    float primitiveSourceCorrelationV66 = selectedSourceCorrelationV66;
    float primitiveResidualCorrelationV66 = selectedResidualCorrelationV66;
    float primitiveYinV66 = selectedYinV66;

    // Highest divisor first means the shortest demonstrated primitive period
    // wins when several multiples are equally explanatory (e.g. 4P -> 2P -> P).
    constexpr std::array<int, 3> primitiveDivisorsV66 { 4, 3, 2 };
    for (const int divisor : primitiveDivisorsV66)
    {
        const int candidateTau = static_cast<int>(std::lround(
            static_cast<double>(selectedSourceTauV66)
            / static_cast<double>(divisor)));
        if (candidateTau < tauMinimum
            || candidateTau > tauMaximum
            || candidateTau >= selectedSourceTauV66 - 2)
        {
            continue;
        }

        // Stage 1 is intentionally the strongest and cheapest separator. It was
        // clean by >0.13 correlation even against the hostile strong-second
        // control. Most true primitive periods therefore stop here with one
        // extra source-correlation pass and no further work.
        const float candidateSourceCorrelation = sourceLagCorrelation(candidateTau);
        if (candidateSourceCorrelation < 0.55f
            || candidateSourceCorrelation < selectedSourceCorrelationV66 - 0.12f)
        {
            continue;
        }

        // Stage 2 falsifies accidental source-waveform resemblance. These are
        // current residual/YIN measurements already available to analyse(); no
        // new hypothesis layer or temporal persistence is introduced.
        const float candidateResidualCorrelation = residualLagCorrelation(candidateTau);
        const float candidateYin = clamp01(
            1.0f - difference_[static_cast<std::size_t>(candidateTau)]);
        if (candidateResidualCorrelation < 0.30f
            || candidateYin < 0.30f
            || candidateResidualCorrelation < selectedResidualCorrelationV66 - 0.12f
            || candidateYin < selectedYinV66 - 0.12f)
        {
            continue;
        }

        primitiveTauV66 = candidateTau;
        primitiveSourceCorrelationV66 = candidateSourceCorrelation;
        primitiveResidualCorrelationV66 = candidateResidualCorrelation;
        primitiveYinV66 = candidateYin;
        break;
    }

    if (primitiveTauV66 != sourceTau)
    {
        sourceTau = primitiveTauV66;
        sourcePeak = primitiveSourceCorrelationV66;

        // The promoted coordinate is the same demonstrated periodic family, but
        // publish the primitive path's direct periodic evidence rather than the
        // old multiple's value. Harmonic-family/cleanliness remain the qualified
        // family evidence that allowed this path measurement to exist at all.
        bestPeriodicity = clamp01(primitiveResidualCorrelationV66);
        (void) primitiveYinV66;
    }

    double refinedTau = static_cast<double>(sourceTau);
    if (sourceTau > tauMinimum && sourceTau < tauMaximum)
    {
        const double left = sourceLagCorrelation(sourceTau - 1);
        const double centre = sourceLagCorrelation(sourceTau);
        const double right = sourceLagCorrelation(sourceTau + 1);
        const double denominator = left - 2.0 * centre + right;
        if (std::abs(denominator) > 1.0e-12)
        {
            // Parabolic peak interpolation. Clamp the fractional correction so
            // source refinement cannot escape the already-qualified lag basin.
            const double fractional = std::clamp(
                0.5 * (left - right) / denominator, -0.75, 0.75);
            refinedTau += fractional;
        }
    }

    if (refinedTau <= 0.0)
        return result;

    const float frequency = static_cast<float>(effectiveSampleRate / refinedTau);
    if (!std::isfinite(frequency)
        || frequency < minimumFrequency * 0.82f
        || frequency > maximumFrequency * 1.18f)
    {
        return result;
    }

    result.frequencyHz = frequency;
    result.confidence = clamp01(bestScore);
    result.periodicity = bestPeriodicity;
    result.harmonicFamily = bestHarmonicFamily;
    result.aperiodicity = 1.0f - bestHarmonicFamily;
    result.tonalCleanliness = bestTonalCleanliness;
    const float trustedFamilyFloor = rescueMode_ ? 0.27f : 0.32f;
    // LOW_RATE_RESONANCE_VETO_V1: effectiveSampleRate identifies the analysis
    // rate inside analyse(). Low-rate paths may locate periods with little
    // remaining spectral evidence, so they need a cleaner source before they
    // independently assert "vocal F0". This is not a detector-wide threshold.
    const double analysisRateRatio = effectiveSampleRate / std::max(1.0, sampleRate_);
    const float trustedCleanlinessFloor = analysisRateRatio <= 0.14
        ? (rescueMode_ ? 0.40f : 0.46f)
        : (analysisRateRatio <= 0.30
            ? (rescueMode_ ? 0.36f : 0.40f)
            : (rescueMode_ ? 0.22f : 0.26f));
    result.valid = structurallyTrusted
        && bestScore >= minimumCandidateScore
        && bestHarmonicFamily >= trustedFamilyFloor
        && bestTonalCleanliness >= trustedCleanlinessFloor;
    return result;
}

float ModernPitchEngine::MultiRatePitchTracker::centsDistance(
    float frequencyA,
    float frequencyB) noexcept
{
    if (frequencyA <= 0.0f || frequencyB <= 0.0f)
        return 100000.0f;

    return std::abs(1200.0f * std::log2(frequencyA / frequencyB));
}

float ModernPitchEngine::MultiRatePitchTracker::candidateBaseScore(
    const PitchCandidate& candidate) const noexcept
{
    if (!candidate.valid || candidate.frequencyHz <= 0.0f)
        return 0.0f;

    const float ageWeight = std::exp(-0.22f
        * static_cast<float>(std::max(0, candidate.ageInHops)));
    return clamp01((0.70f * candidate.confidence
                  + 0.30f * candidate.periodicity) * ageWeight);
}

float ModernPitchEngine::MultiRatePitchTracker::voiceBodyAuthorityV67() const noexcept
{
    if (!voiceAuthorityContextValid_)
        return 0.0f;

    return clamp01(0.30f * voiceAuthorityBodyEnergy_
                 + 0.24f * voiceAuthorityHarmonicity_
                 + 0.22f * voiceAuthoritySpectralReliability_
                 + 0.14f * voiceAuthorityFormantStability_
                 + 0.10f * (1.0f - voiceAuthorityBreathiness_));
}

bool ModernPitchEngine::MultiRatePitchTracker::voiceAllowsLowerFamilyV67() const noexcept
{
    if (!voiceAuthorityContextValid_)
        return false;

    // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7
    // This is permission to inspect below, not permission to choose below. The
    // actual lower coordinate must still be measured by one or more F0 paths.
    return voiceBodyAuthorityV67() >= 0.40f
        && voiceAuthoritySpectralReliability_ >= 0.24f
        && voiceAuthorityBreathiness_ <= 0.76f
        && voiceAuthorityLowerFamilyEvidence_ >= 0.18f;
}

float ModernPitchEngine::MultiRatePitchTracker::pathPitchAuthority(
    int pathIndex,
    float frequencyHz) const noexcept
{
    const auto bandWeight = [](float frequency,
                               float lowerSoft,
                               float lowerFull,
                               float upperFull,
                               float upperSoft) noexcept
    {
        const float lower = smoothStep(lowerSoft, lowerFull, frequency);
        const float upper = 1.0f - smoothStep(upperFull, upperSoft, frequency);
        // CONTINUOUS_F0_BAND_AUTHORITY_V1: outside its physical band a
        // path may corroborate an octave family, but it has zero coordinate
        // authority. The historical non-zero floor let out-of-band aliases
        // steer a measurement once confidence vetoes were removed.
        return std::clamp(lower * upper, 0.0f, 1.0f);
    };

    switch (pathIndex)
    {
        case 0: return bandWeight(frequencyHz, 135.0f, 185.0f, 1250.0f, 2400.0f);
        case 1: return bandWeight(frequencyHz,  62.0f,  84.0f,  720.0f, 1020.0f);
        // QUARTER_PATH_UPPER_COORDINATE_ROLLOFF_V1
        case 2: return bandWeight(frequencyHz,  28.0f,  42.0f,  390.0f,  450.0f);
        case 3: return bandWeight(frequencyHz,  20.0f,  30.0f,  170.0f,  250.0f);
        default: break;
    }
    return 0.0f;
}

float ModernPitchEngine::MultiRatePitchTracker::pathCoordinateAuthority(
    int pathIndex,
    float frequencyHz) const noexcept
{
    // CONTINUOUS_F0_NATIVE_COORDINATE_V1
    // Reuse the detector's existing direct maxima as non-overlapping ownership
    // bands. A slower path outside its native band still contributes family
    // evidence through pathPitchAuthority(), but cannot steer F0.
    bool native = false;
    switch (pathIndex)
    {
        case 0: native = frequencyHz > 900.0f; break;
        case 1: native = frequencyHz > 460.0f && frequencyHz <= 900.0f; break;
        case 2: native = frequencyHz > 230.0f && frequencyHz <= 460.0f; break;
        case 3: native = frequencyHz <= 230.0f; break;
        default: break;
    }
    return native ? pathPitchAuthority(pathIndex, frequencyHz) : 0.0f;
}

float ModernPitchEngine::MultiRatePitchTracker::pathCleanlinessAuthority(
    int pathIndex,
    float frequencyHz) const noexcept
{
    const auto bandWeight = [](float frequency,
                               float lowerSoft,
                               float lowerFull,
                               float upperFull,
                               float upperSoft) noexcept
    {
        const float lower = smoothStep(lowerSoft, lowerFull, frequency);
        const float upper = 1.0f - smoothStep(upperFull, upperSoft, frequency);
        return std::clamp(lower * upper, 0.0f, 1.0f);
    };

    // The lower the analysis rate, the less high-frequency evidence remains to
    // distinguish glottal periodicity from breath/hiss. Low-rate paths therefore
    // remain valuable frequency estimators but progressively weaker cleanliness
    // witnesses. This is deliberate, not a quality ranking of their F0 estimate.
    switch (pathIndex)
    {
        case 0: return 1.00f * bandWeight(frequencyHz, 130.0f, 175.0f, 1450.0f, 2700.0f);
        case 1: return 0.95f * bandWeight(frequencyHz,  58.0f,  80.0f,  760.0f, 1080.0f);
        case 2: return 0.78f * bandWeight(frequencyHz,  26.0f,  40.0f,  390.0f,  560.0f);
        case 3: return 0.40f * bandWeight(frequencyHz,  20.0f,  30.0f,  175.0f,  255.0f);
        default: break;
    }
    return 0.0f;
}

int ModernPitchEngine::MultiRatePitchTracker::collectFreshCandidates(
    std::array<PitchCandidate, detectorPathCount>& candidates) const noexcept
{
    int count = 0;

    const auto append = [&candidates, &count](const CandidateSlot& slot,
                                              int pathIndex,
                                              int maximumAge)
    {
        if (!slot.candidate.valid || slot.ageInHops > maximumAge
            || count >= detectorPathCount)
        {
            return;
        }

        PitchCandidate candidate = slot.candidate;
        candidate.pathIndex = pathIndex;
        candidate.ageInHops = slot.ageInHops;
        candidates[static_cast<std::size_t>(count++)] = candidate;
    };

    append(fullRateCandidate_,    0, 2);
    append(halfRateCandidate_,    1, 3);
    append(quarterRateCandidate_, 2, 5);
    append(eighthRateCandidate_,  3, 9);
    return count;
}

int ModernPitchEngine::MultiRatePitchTracker::buildConsensusHypotheses(
    const std::array<PitchCandidate, detectorPathCount>& candidates,
    int candidateCount,
    std::array<ConsensusHypothesis, maxConsensusHypotheses>& hypotheses) const noexcept
{
    int seedCount = 0;

    // Every detector contributes octave-explicit seeds.  A detector can only
    // contribute once to a resulting cluster, so generated octave variants do
    // not create fake consensus by themselves.
    for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
    {
        const auto& candidate = candidates[static_cast<std::size_t>(candidateIndex)];
        if (!candidate.valid)
            continue;

        for (int octaveShift = -2; octaveShift <= 2; ++octaveShift)
        {
            const float frequency = std::ldexp(candidate.frequencyHz, octaveShift);
            if (frequency < minimumPitchHz_ || frequency > maximumPitchHz_)
                continue;

            bool duplicate = false;
            for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)
            {
                if (centsDistance(hypotheses[static_cast<std::size_t>(seedIndex)].frequencyHz,
                                  frequency) < 28.0f)
                {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate && seedCount < maxConsensusHypotheses)
            {
                auto& seed = hypotheses[static_cast<std::size_t>(seedCount++)];
                seed = {};
                seed.frequencyHz = frequency;
                seed.valid = true;
            }
        }
    }

    int validCount = 0;
    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)
    {
        const float seedFrequency = hypotheses[static_cast<std::size_t>(seedIndex)].frequencyHz;
        double weightedLogFrequency = 0.0;
        float coordinateWeightSum = 0.0f;
        // DIRECT_COORDINATE_OWNS_F0_V1: octave-transposed support verifies a
        // family but cannot steer a coordinate that is measured directly.
        double directWeightedLogFrequency = 0.0;
        float directCoordinateWeightSum = 0.0f;
        float evidenceWeightSum = 0.0f;
        float confidenceSum = 0.0f;
        float periodicitySum = 0.0f;
        float harmonicFamilySum = 0.0f;
        float cleanlinessSum = 0.0f;
        float cleanlinessWeightSum = 0.0f;
        int supportCount = 0;
        int cleanSupportCount = 0;
        int directSupportCount = 0;
        std::uint8_t supportMask = 0;
        std::uint8_t freshSupportMask = 0;

        for (int candidateIndex = 0; candidateIndex < candidateCount; ++candidateIndex)
        {
            const auto& candidate = candidates[static_cast<std::size_t>(candidateIndex)];
            int bestOctaveShift = 0;
            float bestFrequency = candidate.frequencyHz;
            float bestDistance = centsDistance(bestFrequency, seedFrequency);

            for (int octaveShift = -2; octaveShift <= 2; ++octaveShift)
            {
                const float shiftedFrequency = std::ldexp(candidate.frequencyHz, octaveShift);
                if (shiftedFrequency < minimumPitchHz_ || shiftedFrequency > maximumPitchHz_)
                    continue;

                const float distance = centsDistance(shiftedFrequency, seedFrequency);
                if (distance < bestDistance)
                {
                    bestDistance = distance;
                    bestFrequency = shiftedFrequency;
                    bestOctaveShift = octaveShift;
                }
            }

            const bool direct = bestOctaveShift == 0;
            const float tolerance = direct ? 55.0f : 38.0f;
            if (bestDistance > tolerance)
                continue;

            const float octavePrior = direct ? 1.0f
                : (std::abs(bestOctaveShift) == 1 ? 0.52f : 0.25f);
            const float pitchAuthority = pathPitchAuthority(candidate.pathIndex,
                                                            candidate.frequencyHz);
            const float coordinateAuthority = pathCoordinateAuthority(
                candidate.pathIndex, candidate.frequencyHz);
            const float cleanAuthority = pathCleanlinessAuthority(candidate.pathIndex,
                                                                  candidate.frequencyHz);
            const float baseScore = candidateBaseScore(candidate);
            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            const float evidenceWeight = baseScore * pitchAuthority * octavePrior;
            // STALE_PATH_VERIFIES_NOT_STEERS_V1: family evidence may be stale
            // and octave-transposed; only a direct measurement inside its native
            // band receives coordinate authority.
            const float steeringFreshness = std::exp(-0.62f
                * static_cast<float>(std::max(0, candidate.ageInHops)));
            const float coordinateWeight = evidenceWeight * steeringFreshness
                * (0.72f + 0.28f * candidateCleanliness);
            const float nativeCoordinateWeight = baseScore * coordinateAuthority
                * steeringFreshness * (0.72f + 0.28f * candidateCleanliness);
            const float cleanlinessFreshness = std::exp(-0.42f
                * static_cast<float>(std::max(0, candidate.ageInHops)));
            const float cleanlinessWeight = baseScore * cleanAuthority
                * cleanlinessFreshness;

            // Octave-transposed support is useful as harmonic evidence, but it
            // must be genuinely strong; otherwise it is ignored rather than
            // being allowed to manufacture a low subharmonic.
            const float minimumOctaveSupport = rescueMode_ ? 0.48f : 0.60f;
            const float minimumWeight = rescueMode_ ? 0.07f : 0.10f;
            if ((!direct && baseScore < minimumOctaveSupport)
                || evidenceWeight < minimumWeight)
            {
                continue;
            }

            weightedLogFrequency += static_cast<double>(coordinateWeight)
                                  * safeLog2(static_cast<double>(bestFrequency));
            coordinateWeightSum += coordinateWeight;
            if (direct && nativeCoordinateWeight > 0.0f)
            {
                directWeightedLogFrequency += static_cast<double>(nativeCoordinateWeight)
                    * safeLog2(static_cast<double>(bestFrequency));
                directCoordinateWeightSum += nativeCoordinateWeight;
            }
            evidenceWeightSum += evidenceWeight;
            confidenceSum += evidenceWeight * candidate.confidence;
            periodicitySum += evidenceWeight * candidate.periodicity;
            const float candidateFamily = candidate.harmonicFamily >= 0.0f
                ? clamp01(candidate.harmonicFamily) : 1.0f;
            harmonicFamilySum += evidenceWeight * candidateFamily;
            cleanlinessSum += cleanlinessWeight * candidateCleanliness;
            cleanlinessWeightSum += cleanlinessWeight;
            if (cleanlinessWeight >= 0.08f && candidateCleanliness >= 0.24f)
                ++cleanSupportCount;
            ++supportCount;
            if (direct && nativeCoordinateWeight > 0.0f)
                ++directSupportCount;

            const auto bit = static_cast<std::uint8_t>(1u << candidate.pathIndex);
            supportMask = static_cast<std::uint8_t>(supportMask | bit);
            if (candidate.ageInHops == 0)
                freshSupportMask = static_cast<std::uint8_t>(freshSupportMask | bit);
        }

        if (supportCount <= 0 || coordinateWeightSum <= 1.0e-6f
            || evidenceWeightSum <= 1.0e-6f)
        {
            continue;
        }

        ConsensusHypothesis hypothesis;
        const bool hasDirectCoordinate = directCoordinateWeightSum > 1.0e-6f;
        const double coordinateLogFrequency = hasDirectCoordinate
            ? directWeightedLogFrequency / static_cast<double>(directCoordinateWeightSum)
            : weightedLogFrequency / static_cast<double>(coordinateWeightSum);
        hypothesis.frequencyHz = static_cast<float>(std::exp2(coordinateLogFrequency));
        hypothesis.confidence = clamp01(confidenceSum / evidenceWeightSum);
        hypothesis.periodicity = clamp01(periodicitySum / evidenceWeightSum);
        hypothesis.harmonicFamily = clamp01(harmonicFamilySum / evidenceWeightSum);
        hypothesis.tonalCleanliness = cleanlinessWeightSum > 1.0e-6f
            ? clamp01(cleanlinessSum / cleanlinessWeightSum) : 0.0f;
        hypothesis.supportCount = supportCount;
        hypothesis.cleanSupportCount = cleanSupportCount;
        hypothesis.directSupportCount = directSupportCount;
        hypothesis.supportMask = supportMask;
        hypothesis.freshSupportMask = freshSupportMask;

        const float pathConsensus = static_cast<float>(supportCount - 1)
                                  / static_cast<float>(detectorPathCount - 1);
        const float directConsensus = static_cast<float>(directSupportCount)
                                    / static_cast<float>(detectorPathCount);
        hypothesis.consensus = clamp01(0.12f
                                     + 0.58f * pathConsensus
                                     + 0.30f * directConsensus);

        const float meanEvidence = clamp01(evidenceWeightSum
            / static_cast<float>(std::max(1, supportCount)));
        const float directPenalty = directSupportCount == 0 ? 0.16f : 0.0f;
        hypothesis.evidenceScore = meanEvidence
                                 * (0.58f
                                  + 0.22f * hypothesis.consensus
                                  + 0.20f * hypothesis.tonalCleanliness)
                                 + 0.045f * static_cast<float>(directSupportCount)
                                 - directPenalty;
        const float minimumHypothesisEvidence = rescueMode_ ? 0.15f : 0.20f;
        const float cleanFloor = rescueMode_ ? 0.22f : 0.26f;
        const bool cleanEnough = hypothesis.tonalCleanliness >= cleanFloor
            || (hypothesis.cleanSupportCount >= 2
                && hypothesis.harmonicFamily >= 0.42f
                && hypothesis.tonalCleanliness >= 0.18f);
        // GLOTTAL_EVIDENCE_FUSION_V2: a resonant pole can look periodic on one
        // decimated path.  Two direct paths constitute independent geometric
        // evidence; otherwise demand much stronger cleanliness from the lone
        // path.  This is detector evidence fusion, not a global confidence gate.
        const float solitaryHypothesisFloor = rescueMode_ ? 0.64f : 0.68f;
        const bool sourceStructureCredible = hypothesis.directSupportCount >= 2
            || hypothesis.tonalCleanliness >= solitaryHypothesisFloor;
        hypothesis.valid = hypothesis.evidenceScore > minimumHypothesisEvidence
            && cleanEnough
            && sourceStructureCredible;

        if (!hypothesis.valid)
            continue;

        // Merge clusters that converged after weighted refinement.
        int mergeIndex = -1;
        for (int existing = 0; existing < validCount; ++existing)
        {
            if (centsDistance(hypotheses[static_cast<std::size_t>(existing)].frequencyHz,
                              hypothesis.frequencyHz) < 24.0f)
            {
                mergeIndex = existing;
                break;
            }
        }

        if (mergeIndex >= 0)
        {
            if (hypothesis.evidenceScore
                > hypotheses[static_cast<std::size_t>(mergeIndex)].evidenceScore)
            {
                hypotheses[static_cast<std::size_t>(mergeIndex)] = hypothesis;
            }
        }
        else if (validCount < maxConsensusHypotheses)
        {
            hypotheses[static_cast<std::size_t>(validCount++)] = hypothesis;
        }
    }

    std::sort(hypotheses.begin(),
              hypotheses.begin() + validCount,
              [](const ConsensusHypothesis& left,
                 const ConsensusHypothesis& right)
              {
                  return left.evidenceScore > right.evidenceScore;
              });
    return validCount;
}

bool ModernPitchEngine::MultiRatePitchTracker::isOctaveLikeTransition(
    float fromFrequency,
    float toFrequency,
    int& octaveDelta,
    float& residualCents) noexcept
{
    octaveDelta = 0;
    residualCents = 100000.0f;
    if (fromFrequency <= 0.0f || toFrequency <= 0.0f)
        return false;

    const float octaveDistance = std::log2(toFrequency / fromFrequency);
    octaveDelta = static_cast<int>(std::lround(octaveDistance));
    residualCents = std::abs(1200.0f
        * (octaveDistance - static_cast<float>(octaveDelta)));
    return octaveDelta != 0 && std::abs(octaveDelta) <= 2
        && residualCents <= 85.0f;
}

void ModernPitchEngine::MultiRatePitchTracker::updateDecoderBeam(
    const std::array<ConsensusHypothesis, maxConsensusHypotheses>& hypotheses,
    int hypothesisCount,
    bool onsetPending) noexcept
{
    std::array<DecoderState, maxConsensusHypotheses + decoderBeamWidth> proposals {};
    int proposalCount = 0;

    for (int hypothesisIndex = 0;
         hypothesisIndex < hypothesisCount && proposalCount < maxConsensusHypotheses;
         ++hypothesisIndex)
    {
        const auto& hypothesis = hypotheses[static_cast<std::size_t>(hypothesisIndex)];
        if (!hypothesis.valid)
            continue;

        DecoderState proposal;
        proposal.valid = true;
        proposal.logFrequency = safeLog2(hypothesis.frequencyHz);
        proposal.score = hypothesis.evidenceScore + 0.26f * hypothesis.consensus;
        proposal.octaveIndex = octaveState_;

        float bestTransitionScore = -1000.0f;
        int bestOctaveIndex = octaveState_;
        bool foundPrevious = false;

        // DETECTOR_IS_OBSERVER_V1: detector history is analysis evidence only.
        // Musical rigidity never changes this decoder; Scale Lock authority lives
        // downstream in the supervisor/quantizer.
        {
            for (const auto& previous : decoderBeam_)
            {
                if (!previous.valid)
                    continue;

                foundPrevious = true;
                const float deltaCents = static_cast<float>(1200.0
                    * (proposal.logFrequency - previous.logFrequency));
                const float absoluteCents = std::abs(deltaCents);
                const bool strongCurrentFamily = hypothesis.harmonicFamily >= 0.58f
                    && hypothesis.periodicity >= 0.52f
                    && hypothesis.tonalCleanliness >= 0.34f;
                const float continuityBonus = (strongCurrentFamily ? 0.10f : 0.30f)
                    * std::exp(-absoluteCents / 85.0f);
                const float transitionPenalty = onsetPending
                    ? 0.10f * std::min(1.0f, absoluteCents / 1800.0f)
                    : (strongCurrentFamily ? 0.10f : 0.19f)
                        * std::min(2.0f, absoluteCents / 650.0f);

                int octaveDelta = 0;
                float residualCents = 0.0f;
                const bool octaveLike = isOctaveLikeTransition(
                    static_cast<float>(std::exp2(previous.logFrequency)),
                    hypothesis.frequencyHz,
                    octaveDelta,
                    residualCents);
                const float octavePenalty = octaveLike
                    ? 0.24f * static_cast<float>(std::abs(octaveDelta))
                        * (1.0f - 0.70f * hypothesis.consensus)
                    : 0.0f;

                // OBSERVATION_MEMORY_IS_FALSIFIABLE_V1: a strong current
                // vocal family demotes history to a prior; it never owns F0.
                const float historyWeight = onsetPending ? 0.24f
                    : (strongCurrentFamily ? 0.28f : 0.72f);
                const float transitionScore = historyWeight * previous.score
                                            + proposal.score
                                            + continuityBonus
                                            - transitionPenalty
                                            - octavePenalty;
                if (transitionScore > bestTransitionScore)
                {
                    bestTransitionScore = transitionScore;
                    bestOctaveIndex = previous.octaveIndex
                        + (octaveLike ? octaveDelta : 0);
                }
            }
        }

        if (foundPrevious)
            proposal.score = bestTransitionScore;
        proposal.octaveIndex = bestOctaveIndex;
        proposals[static_cast<std::size_t>(proposalCount++)] = proposal;
    }

    // A short detector hold prevents one weak observation from becoming a new
    // measured F0. It never weakens correction: downstream scale ownership
    // continues while the detector is uncertain.
    {
        for (const auto& previous : decoderBeam_)
        {
            if (!previous.valid || proposalCount >= static_cast<int>(proposals.size()))
                continue;

            DecoderState held = previous;
            held.score = previous.score * (onsetPending ? 0.22f : 0.76f)
                       - (onsetPending ? 0.10f : 0.055f);
            ++held.ageInHops;
            if (held.ageInHops <= 4)
                proposals[static_cast<std::size_t>(proposalCount++)] = held;
        }
    }

    std::sort(proposals.begin(),
              proposals.begin() + proposalCount,
              [](const DecoderState& left, const DecoderState& right)
              {
                  return left.score > right.score;
              });

    decoderBeam_.fill({});
    int accepted = 0;
    for (int proposalIndex = 0;
         proposalIndex < proposalCount && accepted < decoderBeamWidth;
         ++proposalIndex)
    {
        const auto& proposal = proposals[static_cast<std::size_t>(proposalIndex)];
        if (!proposal.valid)
            continue;

        bool duplicate = false;
        for (int existing = 0; existing < accepted; ++existing)
        {
            const float distance = static_cast<float>(1200.0
                * std::abs(proposal.logFrequency
                         - decoderBeam_[static_cast<std::size_t>(existing)].logFrequency));
            if (distance < 24.0f)
            {
                duplicate = true;
                break;
            }
        }

        if (!duplicate)
            decoderBeam_[static_cast<std::size_t>(accepted++)] = proposal;
    }
}

ModernPitchEngine::MultiRatePitchTracker::DecoderDecision
ModernPitchEngine::MultiRatePitchTracker::decodeCandidate(bool onsetPending) noexcept
{
    std::array<PitchCandidate, detectorPathCount> candidates {};
    const int candidateCount = collectFreshCandidates(candidates);
    if (candidateCount <= 0)
        return {};

    // AUTHORITATIVE_DIRECT_FAST_PATH_V6
    // Most detector hops are not ambiguous and should not be sent through a
    // voting bureaucracy. A structurally strong fresh measurement can own the
    // physical coordinate immediately unless another comparably strong current
    // path genuinely contradicts it. Paths remain asymmetric: if a low-rate
    // path cannot physically observe the upper octave while another capable
    // path actually measures it, the low F/2 result remains family evidence but
    // is not a competing coordinate.
    const auto directMaximumForPathV6 = [](int pathIndex) noexcept
    {
        switch (pathIndex)
        {
            case 0: return 2600.0f;
            case 1: return 900.0f;
            case 2: return 460.0f;
            case 3: return 230.0f;
            default: return 0.0f;
        }
    };

    const auto directStructureScoreV6 = [this](const PitchCandidate& c) noexcept
    {
        if (!c.valid || !std::isfinite(c.frequencyHz) || c.frequencyHz <= 0.0f)
            return -1.0f;
        const float family = c.harmonicFamily >= 0.0f
            ? clamp01(c.harmonicFamily) : 0.0f;
        const float clean = c.tonalCleanliness >= 0.0f
            ? clamp01(c.tonalCleanliness) : 0.0f;
        return 0.34f * clamp01(c.confidence)
             + 0.24f * clamp01(c.periodicity)
             + 0.21f * family
             + 0.21f * clean;
    };

    const auto structurallyAuthoritativeV6 = [&](const PitchCandidate& c) noexcept
    {
        if (!c.valid || !std::isfinite(c.frequencyHz) || c.frequencyHz <= 0.0f)
            return false;
        const float family = c.harmonicFamily >= 0.0f
            ? clamp01(c.harmonicFamily) : 0.0f;
        const float clean = c.tonalCleanliness >= 0.0f
            ? clamp01(c.tonalCleanliness) : 0.0f;
        return c.periodicity >= 0.58f
            && family >= 0.68f
            && clean >= 0.68f
            && directStructureScoreV6(c) >= 0.70f;
    };

    const auto lowerAliasShadowedV6 = [&](int sourceIndex) noexcept
    {
        const auto& source = candidates[static_cast<std::size_t>(sourceIndex)];
        if (!structurallyAuthoritativeV6(source))
            return false;
        const float upperHz = 2.0f * source.frequencyHz;
        if (upperHz > maximumPitchHz_
            || upperHz <= directMaximumForPathV6(source.pathIndex) + 5.0f)
        {
            return false;
        }

        for (int otherIndex = 0; otherIndex < candidateCount; ++otherIndex)
        {
            if (otherIndex == sourceIndex)
                continue;
            const auto& other = candidates[static_cast<std::size_t>(otherIndex)];
            if (!structurallyAuthoritativeV6(other)
                || upperHz > directMaximumForPathV6(other.pathIndex) + 5.0f)
            {
                continue;
            }
            // A recent slower-path measurement may shadow an alias between its
            // scheduled updates; collectFreshCandidates() already bounds age.
            if (centsDistance(other.frequencyHz, upperHz) <= 85.0f)
                return true;
        }
        return false;
    };

    // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7
    // A lower-rate octave member remains family evidence by default. The vocal
    // body can OPEN the lower-family question, but cannot answer it.
    const bool lowerFamilyPermissionV67 = voiceAllowsLowerFamilyV67();
    const auto isOneOctaveBelowV67 = [this](float lowerHz, float upperHz) noexcept
    {
        return lowerHz > 0.0f && upperHz > 0.0f
            && centsDistance(2.0f * lowerHz, upperHz) <= 85.0f;
    };
    const auto challengesCommittedUpperV67 = [&](const PitchCandidate& c) noexcept
    {
        const float reference = trackedPitchHz_ > 0.0f
            ? trackedPitchHz_ : committedOctaveFrequencyHz_;
        return reference > 0.0f
            && isOneOctaveBelowV67(c.frequencyHz, reference);
    };

    int authoritativeIndexV6 = -1;
    float authoritativeScoreV6 = -1.0f;
    for (int index = 0; index < candidateCount; ++index)
    {
        const auto& candidate = candidates[static_cast<std::size_t>(index)];
        if (candidate.ageInHops != 0
            || !structurallyAuthoritativeV6(candidate)
            || (lowerAliasShadowedV6(index) && !lowerFamilyPermissionV67)
            || (challengesCommittedUpperV67(candidate)
                && voiceAuthorityContextValid_
                && !lowerFamilyPermissionV67))
        {
            continue;
        }
        const float score = directStructureScoreV6(candidate);
        if (score > authoritativeScoreV6)
        {
            authoritativeScoreV6 = score;
            authoritativeIndexV6 = index;
        }
    }

    if (authoritativeIndexV6 >= 0)
    {
        const auto& best = candidates[static_cast<std::size_t>(authoritativeIndexV6)];
        bool genuinelyAmbiguous = false;
        for (int index = 0; index < candidateCount; ++index)
        {
            if (index == authoritativeIndexV6)
                continue;
            const auto& other = candidates[static_cast<std::size_t>(index)];
            if (other.ageInHops != 0
                || !structurallyAuthoritativeV6(other)
                || (lowerAliasShadowedV6(index) && !lowerFamilyPermissionV67)
                || centsDistance(other.frequencyHz, best.frequencyHz) <= 70.0f)
            {
                continue;
            }
            // Only a comparably strong current physical measurement earns the
            // right to invoke the slower ambiguity resolver. A weak path cannot
            // veto an evident coordinate merely because it disagrees.
            const bool octaveFamilyPair = isOneOctaveBelowV67(
                    other.frequencyHz, best.frequencyHz)
                || isOneOctaveBelowV67(best.frequencyHz, other.frequencyHz);
            // If the vocal body has physically justified looking below, an
            // octave-related pair is a real ambiguity regardless of a small
            // score advantage. We consult the old resolver; we do NOT select
            // the lower member here.
            if ((octaveFamilyPair && lowerFamilyPermissionV67)
                || directStructureScoreV6(other) >= authoritativeScoreV6 - 0.10f)
            {
                genuinelyAmbiguous = true;
                break;
            }
        }

        if (!genuinelyAmbiguous)
        {
            // AUTHORITATIVE_RESCUE_CORROBORATION_V6_1
            // Count only native strong measurements of this same coordinate.
            // Retained slower-path observations may corroborate between their
            // scheduled updates, but octave-transposed family support does not.
            int nativeSupport = 0;
            std::uint8_t nativeFreshMask = 0;
            for (int index = 0; index < candidateCount; ++index)
            {
                const auto& other = candidates[static_cast<std::size_t>(index)];
                if (!structurallyAuthoritativeV6(other)
                    || (lowerAliasShadowedV6(index) && !lowerFamilyPermissionV67)
                    || centsDistance(other.frequencyHz, best.frequencyHz) > 70.0f)
                {
                    continue;
                }
                ++nativeSupport;
                if (other.ageInHops == 0)
                {
                    nativeFreshMask = static_cast<std::uint8_t>(
                        nativeFreshMask | static_cast<std::uint8_t>(1u << other.pathIndex));
                }
            }

            DecoderDecision directDecision;
            directDecision.candidate = best;
            directDecision.candidate.valid = true;
            directDecision.consensus = nativeSupport > 1
                ? std::min(1.0f, 0.25f * static_cast<float>(nativeSupport - 1))
                : 0.0f;
            directDecision.supportCount = std::max(1, nativeSupport);
            directDecision.directSupportCount = std::max(1, nativeSupport);
            directDecision.freshSupportMask = nativeFreshMask != 0
                ? nativeFreshMask
                : static_cast<std::uint8_t>(1u << best.pathIndex);
            directDecision.decoderOctaveIndex = octaveState_;
            // A single lower-family witness may nominate a downward octave, but
            // body permission is only permission to inspect it. Require either
            // two native direct paths or the historical octave confirmer before
            // the lower coordinate may bypass continuity.
            const bool downwardFamilyChallengeV67 =
                challengesCommittedUpperV67(best);
            // VOICE_BODY_LIVE_PERMISSION_V6_7_1
            // One structurally strong path plus an independent live vocal-body
            // F/2 signature is already two different physical observations. Do
            // not demand a second F0 path as a bureaucratic permission layer.
            // If an upper path is still structurally strong, the ambiguity loop
            // above has already routed the pair to the resolver instead.
            directDecision.authoritativeDirect = !downwardFamilyChallengeV67
                || nativeSupport >= 2
                || lowerFamilyPermissionV67;
            directDecision.valid = true;
            return directDecision;
        }
    }

    // DETECTOR_VETO_NOT_PERMISSION_V1: a fresh finite detector result is a
    // measurement, even when confidence/consensus are poor. Confidence may
    // rank competing measurements, but it may not suppress the only real F0.
    // Upstream analyse() still rejects genuinely aperiodic/invalid material,
    // so this never invents a frequency when no detector path measured one.
    const auto makeFreshRawDecision = [&]() noexcept
    {
        DecoderDecision rawDecision;
        int bestIndex = -1;
        float bestScore = -1.0f;
        for (int index = 0; index < candidateCount; ++index)
        {
            const auto& candidate = candidates[static_cast<std::size_t>(index)];
            if (!candidate.valid || candidate.ageInHops != 0
                || !std::isfinite(candidate.frequencyHz)
                || candidate.frequencyHz < minimumPitchHz_
                || candidate.frequencyHz > maximumPitchHz_)
            {
                continue;
            }
            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            // SINGLE_PATH_RESONANCE_IS_NOT_VOICE_V1: raw fallback has no
            // independent path corroboration, so a real analyzer candidate must
            // be substantially cleaner than the ordinary multi-path floor.
            // SOLITARY_VOICE_STRUCTURE_V3: isolated full-band formant peaks
            // measured in regression top out below ~0.65 cleanliness. A real
            // lone F0 must show source structure beyond that measured region.
            const float solitaryCleanFloor = candidate.pathIndex == 0 ? 0.68f
                : (candidate.pathIndex == 1 ? 0.66f
                   : (candidate.pathIndex == 2 ? 0.62f : 0.70f));
            if (candidate.tonalCleanliness >= 0.0f
                && candidateCleanliness < solitaryCleanFloor)
            {
                continue;
            }
            const float score = candidateBaseScore(candidate)
                * pathCoordinateAuthority(candidate.pathIndex, candidate.frequencyHz)
                * (0.70f + 0.30f * candidateCleanliness);
            if (score > bestScore)
            {
                bestScore = score;
                bestIndex = index;
            }
        }
        if (bestIndex < 0)
            return rawDecision;

        rawDecision.candidate = candidates[static_cast<std::size_t>(bestIndex)];
        rawDecision.candidate.valid = true;
        rawDecision.consensus = 0.0f;
        rawDecision.supportCount = 1;
        rawDecision.directSupportCount = 1;
        rawDecision.freshSupportMask = static_cast<std::uint8_t>(
            1u << rawDecision.candidate.pathIndex);
        rawDecision.decoderOctaveIndex = octaveState_;
        rawDecision.valid = true;
        return rawDecision;
    };

    // Consensus remains useful for ranking/falsification, never as permission
    // to expose a measured F0 to the musical supervisor.
    std::array<ConsensusHypothesis, maxConsensusHypotheses> hypotheses {};
    const int hypothesisCount = buildConsensusHypotheses(candidates,
                                                         candidateCount,
                                                         hypotheses);
    if (hypothesisCount <= 0)
        return makeFreshRawDecision();

    // TRANSITION_WAKES_DETECTOR_NOT_OUTPUT_V1
    // A persistent transition/acquire state may falsify detector memory, never
    // manufacture F0. Require multiple direct paths plus strong current source
    // cleanliness before clearing a contradictory old register hypothesis.
    if (transitionWake_)
    {
        const float oldReferenceHz = trackedPitchHz_ > 0.0f
            ? trackedPitchHz_ : reacquisitionAnchorHz_;
        int wakeHypothesis = -1;
        float wakeScore = -1000.0f;
        if (oldReferenceHz > 0.0f)
        {
            for (int index = 0; index < hypothesisCount; ++index)
            {
                const auto& current = hypotheses[static_cast<std::size_t>(index)];
                if (!current.valid
                    || current.freshSupportMask == 0
                    || current.directSupportCount < 2
                    || current.cleanSupportCount < 2
                    || current.tonalCleanliness < 0.62f
                    || current.harmonicFamily < 0.58f
                    || current.periodicity < 0.52f
                    || centsDistance(oldReferenceHz, current.frequencyHz) < 95.0f)
                {
                    continue;
                }
                const float currentScore = current.evidenceScore
                    + 0.18f * current.consensus
                    + 0.18f * current.tonalCleanliness;
                if (currentScore > wakeScore)
                {
                    wakeScore = currentScore;
                    wakeHypothesis = index;
                }
            }
        }
        if (wakeHypothesis >= 0)
            clearObservationMemory(false);
    }

    updateDecoderBeam(hypotheses, hypothesisCount, onsetPending);
    // Decoder history is diagnostic/ranking evidence. It cannot erase the
    // current measured coordinate merely because the transition is unusual.

    // BEAM_RANKS_CURRENT_MEASUREMENT_V2: current evidence is still required,
    // but temporal continuity now ranks competing current hypotheses instead of
    // being computed and then ignored. This is especially important when a
    // harmonic one octave away briefly has the strongest instantaneous score.
    int matchedHypothesis = -1;
    float bestCurrentScore = -1000.0f;
    const bool beamValid = decoderBeam_[0].valid;
    const float beamFrequencyHz = beamValid
        ? static_cast<float>(std::exp2(decoderBeam_[0].logFrequency)) : 0.0f;
    for (int index = 0; index < hypothesisCount; ++index)
    {
        const auto& current = hypotheses[static_cast<std::size_t>(index)];
        if (!current.valid || current.freshSupportMask == 0
            || current.directSupportCount < 1)
        {
            continue;
        }

        float score = current.evidenceScore + 0.20f * current.consensus;
        if (beamValid && beamFrequencyHz > 0.0f)
        {
            const bool strongCurrentFamily = current.harmonicFamily >= 0.58f
                && current.periodicity >= 0.52f
                && current.tonalCleanliness >= 0.34f;
            const float distance = centsDistance(beamFrequencyHz,
                                                 current.frequencyHz);
            score += (strongCurrentFamily ? 0.12f : 0.46f)
                * std::exp(-distance / 95.0f);

            int octaveDelta = 0;
            float octaveResidual = 0.0f;
            if (isOctaveLikeTransition(beamFrequencyHz, current.frequencyHz,
                                       octaveDelta, octaveResidual))
            {
                const float singleFamilyPenalty = strongCurrentFamily
                    ? (current.directSupportCount >= 2 ? 0.06f : 0.12f)
                    : (current.directSupportCount >= 2 ? 0.20f : 0.46f);
                score -= singleFamilyPenalty;
            }
        }

        if (score > bestCurrentScore)
        {
            bestCurrentScore = score;
            matchedHypothesis = index;
        }
    }
    if (matchedHypothesis < 0)
        return makeFreshRawDecision();

    const auto& hypothesis = hypotheses[static_cast<std::size_t>(matchedHypothesis)];
    DecoderDecision decision;
    decision.candidate.frequencyHz = hypothesis.frequencyHz;
    decision.candidate.confidence = clamp01(hypothesis.confidence
        * (0.76f + 0.24f * hypothesis.consensus));
    decision.candidate.periodicity = hypothesis.periodicity;
    decision.candidate.harmonicFamily = hypothesis.harmonicFamily;
    decision.candidate.aperiodicity = 1.0f - hypothesis.harmonicFamily;
    decision.candidate.tonalCleanliness = hypothesis.tonalCleanliness;
    decision.candidate.valid = true;
    decision.consensus = hypothesis.consensus;
    decision.supportCount = hypothesis.supportCount;
    decision.directSupportCount = hypothesis.directSupportCount;
    decision.freshSupportMask = hypothesis.freshSupportMask;
    decision.decoderOctaveIndex = decoderBeam_[0].octaveIndex;

    // DIRECT_HIGH_PATH_OWNS_RATIONAL_ALIAS_V2
    constexpr float halfRateDirectMaximumHz = 900.0f;
    const auto& freshFull = fullRateCandidate_.candidate;
    const bool freshQualifiedHighFull = fullRateCandidate_.ageInHops == 0
        && freshFull.valid
        && std::isfinite(freshFull.frequencyHz)
        && freshFull.frequencyHz > halfRateDirectMaximumHz
        && freshFull.harmonicFamily >= 0.68f
        && freshFull.tonalCleanliness >= 0.68f;
    if (freshQualifiedHighFull
        && decision.candidate.frequencyHz > 0.0f)
    {
        int aliasDivisor = 0;
        for (int divisor = 2; divisor <= 3; ++divisor)
        {
            const float expanded = static_cast<float>(divisor)
                * decision.candidate.frequencyHz;
            if (centsDistance(expanded, freshFull.frequencyHz) <= 55.0f)
            {
                aliasDivisor = divisor;
                break;
            }
        }

        if (aliasDivisor != 0)
        {
            // Lower-rate rational aliases verify periodic family membership but
            // cannot own a coordinate outside their direct measurement band.
            // Single-path consensus semantics make the authority explicit.
            decision.candidate = freshFull;
            decision.candidate.valid = true;
            decision.consensus = 0.0f;
            decision.supportCount = 1;
            decision.directSupportCount = 1;
            decision.freshSupportMask = static_cast<std::uint8_t>(1u);
            decision.decoderOctaveIndex = octaveState_;
        }
    }

    // DETECTOR_VETO_NOT_PERMISSION_V1: once a current finite measurement has
    // survived the detector's falsification stages, low confidence/consensus
    // cannot make it invalid. Octave/subharmonic ambiguity is handled below by
    // confirmOctaveTransition() as an explicit, bounded veto.
    decision.valid = true;
    return decision;
}

bool ModernPitchEngine::MultiRatePitchTracker::confirmOctaveTransition(
    DecoderDecision& decision,
    bool onsetPending) noexcept
{
    if (!decision.valid)
    {
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        return false;
    }

    // DETECTOR_IS_OBSERVER_V1: audio presence cannot commit a register.
    // Octave-like observations always use the same evidence/continuity guards.

    // If current F0 expired while a musical note body is still latched,
    // reacquisition is NOT an initial register acquisition.  The persistent
    // anchor owns the register and a subharmonic may not restart it.
    if (trackedPitchHz_ <= 0.0f && rescueMode_ && reacquisitionAnchorHz_ > 0.0f)
    {
        // AUTHORITATIVE_RESCUE_CORROBORATION_V6_1
        // Rescue memory is useful only while current physics is ambiguous. Two
        // or more native strong paths agreeing on the same live coordinate are
        // sufficient to falsify a stale anchor immediately. A solitary octave
        // alias still falls through to the historical bounded negative veto.
        if (decision.authoritativeDirect
            && (decision.directSupportCount >= 2
                || voiceAllowsLowerFamilyV67()))
        {
            // VOICE_BODY_LIVE_PERMISSION_V6_7_1: stale detector memory cannot
            // demand a second F0 path after current vocal-body physics has
            // independently corroborated the fresh lower coordinate.
            int directRescueDelta = 0;
            float directRescueResidual = 0.0f;
            if (isOctaveLikeTransition(reacquisitionAnchorHz_,
                                       decision.candidate.frequencyHz,
                                       directRescueDelta,
                                       directRescueResidual))
            {
                octaveState_ = std::clamp(octaveState_ + directRescueDelta, -4, 4);
            }
            committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
            octaveCommitGuardHops_ = 4;
            pendingOctaveDelta_ = 0;
            pendingOctaveCount_ = 0;
            pendingOctaveFrequencyHz_ = 0.0f;
            decision.decoderOctaveIndex = octaveState_;
            return true;
        }

        int rescueOctaveDelta = 0;
        float rescueResidualCents = 0.0f;
        const bool octaveLike = isOctaveLikeTransition(
            reacquisitionAnchorHz_, decision.candidate.frequencyHz,
            rescueOctaveDelta, rescueResidualCents);

        if (!octaveLike)
        {
            // A non-octave live measurement is not guilty merely because it is
            // far from the stale anchor. An explicit transient onset may veto
            // this one observation; the next measured non-onset F0 passes.
            if (onsetPending)
            {
                decision.valid = false;
                return false;
            }
            pendingOctaveDelta_ = 0;
            pendingOctaveCount_ = 0;
            pendingOctaveFrequencyHz_ = 0.0f;
            committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
            octaveCommitGuardHops_ = 6;
            decision.decoderOctaveIndex = octaveState_;
            return true;
        }

        const bool samePending = pendingOctaveDelta_ == rescueOctaveDelta
            && pendingOctaveFrequencyHz_ > 0.0f
            && centsDistance(pendingOctaveFrequencyHz_,
                             decision.candidate.frequencyHz) < 42.0f;
        if (!samePending)
        {
            pendingOctaveDelta_ = rescueOctaveDelta;
            pendingOctaveCount_ = 0;
            pendingOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        }
        if (decision.freshSupportMask != 0)
            ++pendingOctaveCount_;

        // OCTAVE_AMBIGUITY_V2: octave/subharmonic aliases are common on real
        // vocals.  Keep this a finite negative veto, but require enough fresh
        // geometric persistence that one consonant/harmonic burst cannot own a
        // register. A real onset remains fast; multi-path direct support is the
        // only reason to shorten the non-onset count.
        // OCTAVE_AMBIGUITY_V3: a sung octave is allowed, but a consonant or
        // strong harmonic may remain octave-like for several milliseconds.
        // Onset cannot shortcut this guard. Independent direct paths shorten
        // the fixed window; confidence never lengthens or shortens it.
        const bool multiPathDirect = decision.directSupportCount >= 2
            && decision.consensus >= 0.24f;
        const bool strongCurrentFamily = decision.candidate.harmonicFamily >= 0.58f
            && decision.candidate.periodicity >= 0.52f
            && decision.candidate.tonalCleanliness >= 0.34f;
        const int requiredObservations = strongCurrentFamily
            ? (multiPathDirect ? 4 : 8)
            : (multiPathDirect
                ? (rescueOctaveDelta < 0 ? 12 : 10)
                : (rescueOctaveDelta < 0 ? 28 : 24));
        const bool rescueLowerPermittedV67 = rescueOctaveDelta >= 0
            || !voiceAuthorityContextValid_
            || voiceAllowsLowerFamilyV67()
            || multiPathDirect;
        if (pendingOctaveCount_ < requiredObservations
            || !rescueLowerPermittedV67)
        {
            decision.valid = false;
            return false;
        }

        octaveState_ = std::clamp(octaveState_ + rescueOctaveDelta, -4, 4);
        committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        octaveCommitGuardHops_ = 12;
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        decision.decoderOctaveIndex = octaveState_;
        return true;
    }

    // Initial register acquisition is an evidence decision, independent of
    // whether samples are non-zero. Presence never upgrades weak F0 evidence.
    // Initial register acquisition without explicit audio presence remains
    // deliberately temporal for synthetic/offline detector-only use.
    if (trackedPitchHz_ <= 0.0f)
    {
        // FIRST_MEASUREMENT_OWNS_V1: first ownership requires a current real
        // detector measurement, not a confidence vote. No fresh measurement
        // still means no F0 and therefore no invented target.
        if (decision.freshSupportMask == 0 || decision.directSupportCount < 1)
        {
            decision.valid = false;
            return false;
        }
        committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        octaveCommitGuardHops_ = 6;
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        decision.decoderOctaveIndex = octaveState_;
        return true;
    }

    // AUTHORITATIVE_DIRECT_FAST_PATH_V6
    // At this point initial acquisition and rescue-anchor semantics have already
    // been handled above. A decisive current physical measurement must not be
    // reinterpreted as a long-lived continuity preference. This is the explicit
    // anti-stall rule: detector memory cannot turn the old F0 into a constant
    // transposition while the current source has demonstrated a new coordinate.
    if (decision.authoritativeDirect)
    {
        int directOctaveDelta = 0;
        float directOctaveResidual = 0.0f;
        if (isOctaveLikeTransition(trackedPitchHz_,
                                   decision.candidate.frequencyHz,
                                   directOctaveDelta,
                                   directOctaveResidual))
        {
            octaveState_ = std::clamp(octaveState_ + directOctaveDelta, -4, 4);
        }
        committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        octaveCommitGuardHops_ = 4;
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        decision.decoderOctaveIndex = octaveState_;
        return true;
    }

    if (octaveCommitGuardHops_ > 0)
    {
        --octaveCommitGuardHops_;
        if (committedOctaveFrequencyHz_ > 0.0f
            && centsDistance(committedOctaveFrequencyHz_,
                             decision.candidate.frequencyHz) < 95.0f)
        {
            decision.decoderOctaveIndex = octaveState_;
            return true;
        }
    }

    int octaveDelta = 0;
    float residualCents = 0.0f;
    if (!isOctaveLikeTransition(trackedPitchHz_,
                                decision.candidate.frequencyHz,
                                octaveDelta,
                                residualCents))
    {
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        return true;
    }

    const bool samePending = pendingOctaveDelta_ == octaveDelta
        && pendingOctaveFrequencyHz_ > 0.0f
        && centsDistance(pendingOctaveFrequencyHz_,
                         decision.candidate.frequencyHz) < 42.0f;

    if (!samePending)
    {
        pendingOctaveDelta_ = octaveDelta;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = decision.candidate.frequencyHz;
    }

    // Count only genuinely refreshed evidence.  Reusing an old low-rate
    // candidate over several full-rate hops must not confirm a subharmonic.
    if (decision.freshSupportMask != 0)
        ++pendingOctaveCount_;

    // OCTAVE_AMBIGUITY_V2: normal tracking uses the same finite veto.
    // Eight single-family fresh hops are only ~5.3 ms at the 32-sample hop:
    // fast enough for sung note changes, long enough to reject most one-frame
    // register hallucinations. Downward aliases receive two extra hops.
    const bool multiPathDirect = decision.directSupportCount >= 2
        && decision.consensus >= 0.24f;

    // DIRECT_HIGH_FAMILY_FAST_CONFIRM_V1
    // The full path is the only direct authority above 900 Hz. Half and quarter
    // paths may still verify the same source as exact 1/2 and 1/3 aliases. When
    // all three are simultaneously voice-clean, do not demand the generic
    // octave-jump persistence from a register that was itself the 1/2 alias.
    const auto& directHighFull = fullRateCandidate_.candidate;
    const auto& directHighHalf = halfRateCandidate_.candidate;
    const auto& directHighQuarter = quarterRateCandidate_.candidate;
    const bool directHighFullValid = fullRateCandidate_.ageInHops == 0
        && directHighFull.valid
        && std::isfinite(directHighFull.frequencyHz)
        && directHighFull.frequencyHz > 900.0f
        && directHighFull.harmonicFamily >= 0.68f
        && directHighFull.tonalCleanliness >= 0.68f;
    const bool directHighHalfFamily = halfRateCandidate_.ageInHops <= 3
        && directHighHalf.valid
        && directHighHalf.frequencyHz > 0.0f
        && directHighHalf.harmonicFamily >= 0.68f
        && directHighHalf.tonalCleanliness >= 0.68f
        && centsDistance(2.0f * directHighHalf.frequencyHz,
                         directHighFull.frequencyHz) <= 55.0f;
    const bool directHighQuarterFamily = quarterRateCandidate_.ageInHops <= 5
        && directHighQuarter.valid
        && directHighQuarter.frequencyHz > 0.0f
        && directHighQuarter.harmonicFamily >= 0.68f
        && directHighQuarter.tonalCleanliness >= 0.68f
        && centsDistance(3.0f * directHighQuarter.frequencyHz,
                         directHighFull.frequencyHz) <= 55.0f;
    const bool directHighFamilyFastConfirm = octaveDelta == 1
        && directHighFullValid
        && directHighHalfFamily
        && directHighQuarterFamily
        && centsDistance(decision.candidate.frequencyHz,
                         directHighFull.frequencyHz) <= 55.0f
        && centsDistance(2.0f * trackedPitchHz_,
                         directHighFull.frequencyHz) <= 85.0f;

    const int requiredObservations = directHighFamilyFastConfirm
        ? 3
        : (multiPathDirect
            ? (octaveDelta < 0 ? 12 : 10)
            : (octaveDelta < 0 ? 28 : 24));

    // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7
    // Negative evidence is extremely narrow: only a DOWNWARD octave family
    // conflict, only with valid coherent body context, and only when one path
    // is trying to win by persistence alone. This is not global prudence. A
    // real lower period measured by two native paths remains immediately legal.
    const bool lowerFamilyPhysicallyPermittedV67 = octaveDelta >= 0
        || !voiceAuthorityContextValid_
        || voiceAllowsLowerFamilyV67()
        || multiPathDirect;
    if (!lowerFamilyPhysicallyPermittedV67)
    {
        pendingOctaveCount_ = std::min(pendingOctaveCount_,
                                      std::max(0, requiredObservations - 1));
        decision.candidate.frequencyHz = trackedPitchHz_;
        decision.candidate.confidence = trackedConfidence_ * 0.97f;
        decision.candidate.periodicity = trackedPeriodicity_;
        decision.consensus = trackedConsensus_;
        decision.supportCount = trackedSupportCount_;
        decision.decoderOctaveIndex = octaveState_;
        decision.valid = trackedPitchHz_ > 0.0f;
        return false;
    }

    if (pendingOctaveCount_ < requiredObservations)
    {
        // Hold the committed register only while an explicitly octave-like
        // challenger is being falsification-checked. Confidence cannot extend
        // this veto beyond the fixed observation count.
        decision.candidate.frequencyHz = trackedPitchHz_;
        decision.candidate.confidence = trackedConfidence_ * 0.97f;
        decision.candidate.periodicity = trackedPeriodicity_;
        decision.consensus = trackedConsensus_;
        decision.supportCount = trackedSupportCount_;
        decision.decoderOctaveIndex = octaveState_;
        decision.valid = trackedPitchHz_ > 0.0f;
        return false;
    }

    octaveState_ = std::clamp(octaveState_ + octaveDelta, -4, 4);
    committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
    octaveCommitGuardHops_ = 12;
    pendingOctaveDelta_ = 0;
    pendingOctaveCount_ = 0;
    pendingOctaveFrequencyHz_ = 0.0f;
    decision.decoderOctaveIndex = octaveState_;
    return true;
}

bool ModernPitchEngine::MultiRatePitchTracker::processSample(
    float inputSample,
    PitchObservation& observation) noexcept
{
    observation = {};
    // TRACKER_INPUT_SANITIZE_OWNED_UPSTREAM_V1: public engine boundary owns it.
    if (std::abs(inputSample) > numericalPresenceSample)
        presenceSinceLastHop_ = true;

    const float dcBlocked = inputSample - previousInput_
                          + dcBlockCoefficient_ * previousDcOutput_;
    previousInput_ = inputSample;
    previousDcOutput_ = dcBlocked;

    const float energy = dcBlocked * dcBlocked;
    const float floorTarget = std::max(1.0e-12f, energy);
    const float floorCoefficient = floorTarget < noiseFloorEnergy_
        ? 0.020f : 0.000003f;
    noiseFloorEnergy_ += floorCoefficient * (floorTarget - noiseFloorEnergy_);
    noiseFloorEnergy_ = std::max(1.0e-12f, noiseFloorEnergy_);
    fastEnergy_ += fastEnergyCoefficient_ * (energy - fastEnergy_);
    slowEnergy_ += slowEnergyCoefficient_ * (energy - slowEnergy_);

    if (onsetCooldownSamples_ > 0)
        --onsetCooldownSamples_;

    const float energyRatio = fastEnergy_ / std::max(1.0e-9f, slowEnergy_);
    const float energeticEnough = fastEnergy_ > minimumDetectorRms * minimumDetectorRms * 3.0f;
    const float onsetStrength = clamp01((energyRatio - 1.8f) / 3.2f);

    onsetEnvelope_ = std::max(onsetStrength, onsetEnvelope_ * 0.985f);

    if (energeticEnough && energyRatio > 3.1f && onsetCooldownSamples_ == 0)
    {
        onsetPending_ = true;
        onsetCooldownSamples_ = std::max(1,
            static_cast<int>(std::lround(sampleRate_ * 0.010)));
    }

    push(fullRateRing_, fullRateWritePosition_, fullRateAvailableSamples_, dcBlocked);

    const float halfFiltered = halfRateAntiAlias_.process(dcBlocked);
    if (++halfRateDecimationCounter_ >= 2)
    {
        halfRateDecimationCounter_ = 0;
        push(halfRateRing_, halfRateWritePosition_, halfRateAvailableSamples_, halfFiltered);

        const float quarterFiltered = quarterRateAntiAlias_.process(halfFiltered);
        if (++quarterRateDecimationCounter_ >= 2)
        {
            quarterRateDecimationCounter_ = 0;
            push(quarterRateRing_, quarterRateWritePosition_,
                 quarterRateAvailableSamples_, quarterFiltered);

            const float eighthFiltered = eighthRateAntiAlias_.process(quarterFiltered);
            if (++eighthRateDecimationCounter_ >= 2)
            {
                eighthRateDecimationCounter_ = 0;
                push(eighthRateRing_, eighthRateWritePosition_,
                     eighthRateAvailableSamples_, eighthFiltered);
            }
        }
    }

    if (++hopCounter_ < detectorHop)
        return false;

    hopCounter_ = 0;
    ++analysisHopCounter_;
    presenceMode_ = presenceSinceLastHop_;
    presenceSinceLastHop_ = false;

    // ZERO_INPUT_CLEARS_OBSERVER_NOT_MUSIC_V1
    if (!presenceMode_)
    {
        clearObservationMemory(true);
        observation.audioPresent = false;
        observation.measurementAvailable = false;
        observation.valid = false;
        observation.onset = false;
        observation.onsetStrength = 0.0f;
        return true;
    }

    ++fullRateCandidate_.ageInHops;
    ++halfRateCandidate_.ageInHops;
    ++quarterRateCandidate_.ageInHops;
    ++eighthRateCandidate_.ageInHops;

    // PARKED_LOW_RATE_WORKER_V1
    // Launch half/eighth from this exact hop, compute full/quarter on the audio
    // thread, then rendezvous before touching CandidateSlot/decoder state. The
    // four results are committed in the original 0 -> 1 -> 2 -> 3 order.
    if (analysisWorker_ != nullptr)
    {
        const bool halfDue = (analysisHopCounter_ & 1) == 0;
        const bool quarterDue = (analysisHopCounter_ & 3) == 0;
        const bool eighthDue = (analysisHopCounter_ & 7) == 0;

        const float fullMinimum = std::max(160.0f, minimumPitchHz_);
        const float fullMaximum = std::min(maximumPitchHz_, 2600.0f);
        const float halfMinimum = std::max(78.0f, minimumPitchHz_);
        const float halfMaximum = std::min(maximumPitchHz_, 900.0f);
        const float quarterMinimum = std::max(35.0f, minimumPitchHz_);
        const float quarterMaximum = std::min(maximumPitchHz_, 460.0f);
        const float eighthMinimum = std::max(25.0f, minimumPitchHz_);
        const float eighthMaximum = std::min(maximumPitchHz_, 230.0f);

        AnalysisWorker::Job workerJob;
        workerJob.runHalf = halfDue && halfMinimum < halfMaximum;
        workerJob.runEighth = eighthDue && eighthMinimum < eighthMaximum;
        workerJob.halfWritePosition = halfRateWritePosition_;
        workerJob.halfAvailableSamples = halfRateAvailableSamples_;
        workerJob.halfSampleRate = sampleRate_ * 0.5;
        workerJob.halfMinimum = halfMinimum;
        workerJob.halfMaximum = halfMaximum;
        workerJob.eighthWritePosition = eighthRateWritePosition_;
        workerJob.eighthAvailableSamples = eighthRateAvailableSamples_;
        workerJob.eighthSampleRate = sampleRate_ * 0.125;
        workerJob.eighthMinimum = eighthMinimum;
        workerJob.eighthMaximum = eighthMaximum;

        const bool workerSubmitted = workerJob.runHalf || workerJob.runEighth;
        std::uint64_t workerTicket = 0;
        if (workerSubmitted)
            workerTicket = analysisWorker_->submit(workerJob);

        PitchCandidate fullResult {};
        PitchCandidate quarterResult {};
        bool fullComputed = false;
        bool quarterComputed = false;

        if (fullMinimum < fullMaximum)
        {
            fullResult = measureCoordinate(fullRateRing_,
                                 fullRateWritePosition_,
                                 fullRateAvailableSamples_,
                                 sampleRate_,
                                 fullMinimum,
                                 fullMaximum,
                                 standardAnalysisSize,
                                 analysisWorkspace_);
            fullResult.pathIndex = 0;
            fullResult.ageInHops = 0;
            fullComputed = true;
        }

        if (quarterDue && quarterMinimum < quarterMaximum)
        {
            quarterResult = measureCoordinate(quarterRateRing_,
                                    quarterRateWritePosition_,
                                    quarterRateAvailableSamples_,
                                    sampleRate_ * 0.25,
                                    quarterMinimum,
                                    quarterMaximum,
                                    384,
                                    analysisWorkspace_);
            quarterResult.pathIndex = 2;
            quarterResult.ageInHops = 0;
            quarterComputed = true;
        }

        AnalysisWorker::Result workerResult;
        if (workerSubmitted)
            workerResult = analysisWorker_->wait(workerTicket);

        // Do not expose partially-computed paths to provisional measurement,
        // consensus, octave confirmation, quantizer or renderer supervision.
        if (fullComputed)
        {
            fullRateCandidate_.candidate = fullResult;
            fullRateCandidate_.ageInHops = 0;
        }

        if (workerResult.halfComputed)
        {
            halfRateCandidate_.candidate = workerResult.half;
            halfRateCandidate_.ageInHops = 0;
        }

        if (quarterComputed)
        {
            quarterRateCandidate_.candidate = quarterResult;
            quarterRateCandidate_.ageInHops = 0;
        }

        if (workerResult.eighthComputed)
        {
            eighthRateCandidate_.candidate = workerResult.eighth;
            eighthRateCandidate_.ageInHops = 0;
        }
    }
    else
    {
        const float fullMinimum = std::max(160.0f, minimumPitchHz_);
        const float fullMaximum = std::min(maximumPitchHz_, 2600.0f);
        if (fullMinimum < fullMaximum)
        {
            fullRateCandidate_.candidate = measureCoordinate(fullRateRing_,
                                                   fullRateWritePosition_,
                                                   fullRateAvailableSamples_,
                                                   sampleRate_,
                                                   fullMinimum,
                                                   fullMaximum,
                                                   standardAnalysisSize,
                                                   analysisWorkspace_);
            fullRateCandidate_.candidate.pathIndex = 0;
            fullRateCandidate_.candidate.ageInHops = 0;
            fullRateCandidate_.ageInHops = 0;
        }
    
        if ((analysisHopCounter_ & 1) == 0)
        {
            const float halfMinimum = std::max(78.0f, minimumPitchHz_);
            const float halfMaximum = std::min(maximumPitchHz_, 900.0f);
            if (halfMinimum < halfMaximum)
            {
                halfRateCandidate_.candidate = measureCoordinate(halfRateRing_,
                                                       halfRateWritePosition_,
                                                       halfRateAvailableSamples_,
                                                       sampleRate_ * 0.5,
                                                       halfMinimum,
                                                       halfMaximum,
                                                       standardAnalysisSize,
                                                       analysisWorkspace_);
                halfRateCandidate_.candidate.pathIndex = 1;
                halfRateCandidate_.candidate.ageInHops = 0;
                halfRateCandidate_.ageInHops = 0;
            }
        }
    
        if ((analysisHopCounter_ & 3) == 0)
        {
            const float quarterMinimum = std::max(35.0f, minimumPitchHz_);
            const float quarterMaximum = std::min(maximumPitchHz_, 460.0f);
            if (quarterMinimum < quarterMaximum)
            {
                quarterRateCandidate_.candidate = measureCoordinate(quarterRateRing_,
                                                          quarterRateWritePosition_,
                                                          quarterRateAvailableSamples_,
                                                          sampleRate_ * 0.25,
                                                          quarterMinimum,
                                                          quarterMaximum,
                                                          384,
                                                          analysisWorkspace_);
                quarterRateCandidate_.candidate.pathIndex = 2;
                quarterRateCandidate_.candidate.ageInHops = 0;
                quarterRateCandidate_.ageInHops = 0;
            }
        }
    
        if ((analysisHopCounter_ & 7) == 0)
        {
            const float eighthMinimum = std::max(25.0f, minimumPitchHz_);
            const float eighthMaximum = std::min(maximumPitchHz_, 230.0f);
            if (eighthMinimum < eighthMaximum)
            {
                eighthRateCandidate_.candidate = measureCoordinate(eighthRateRing_,
                                                         eighthRateWritePosition_,
                                                         eighthRateAvailableSamples_,
                                                         sampleRate_ * 0.125,
                                                         eighthMinimum,
                                                         eighthMaximum,
                                                         maxAnalysisSize,
                                                         analysisWorkspace_);
                eighthRateCandidate_.candidate.pathIndex = 3;
                eighthRateCandidate_.candidate.ageInHops = 0;
                eighthRateCandidate_.ageInHops = 0;
            }
        }
    
        }

    const auto chooseProvisionalMeasurement = [this]() noexcept
    {
        PitchCandidate best {};
        float bestScore = -1.0f;
        const auto consider = [this, &best, &bestScore](const CandidateSlot& slot,
                                                        int maximumAge) noexcept
        {
            const auto& candidate = slot.candidate;
            // PATH_REJECTED_CANDIDATE_IS_NOT_PROVISIONAL_V1
            if (!candidate.valid)
                return;
            if (slot.ageInHops > maximumAge
                || !std::isfinite(candidate.frequencyHz)
                || candidate.frequencyHz <= 0.0f
                || (candidate.harmonicFamily >= 0.0f
                    && candidate.harmonicFamily < 0.20f))
            {
                return;
            }
            const float ageWeight = std::exp(-0.55f
                * static_cast<float>(std::max(0, slot.ageInHops)));
            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            // PROVISIONAL_PATH_CLEANLINESS_V1: low-rate geometry alone cannot
            // publish a vocal coordinate when source cleanliness is weak.
            const float provisionalPathFloor = candidate.pathIndex == 0 ? 0.68f
                : (candidate.pathIndex == 1 ? 0.66f
                   : (candidate.pathIndex == 2 ? 0.62f : 0.70f));
            if (candidate.tonalCleanliness >= 0.0f
                && candidateCleanliness < provisionalPathFloor)
            {
                return;
            }
            const float score = ageWeight
                * pathCoordinateAuthority(candidate.pathIndex, candidate.frequencyHz)
                * (0.46f * clamp01(candidate.confidence)
                 + 0.28f * clamp01(candidate.periodicity)
                 + 0.26f * candidateCleanliness);
            if (score > bestScore)
            {
                bestScore = score;
                best = candidate;
            }
        };
        consider(fullRateCandidate_, 2);
        consider(halfRateCandidate_, 3);
        consider(quarterRateCandidate_, 5);
        consider(eighthRateCandidate_, 9);
        return best;
    };

    const PitchCandidate provisionalMeasurement = chooseProvisionalMeasurement();
    std::array<PitchCandidate, detectorPathCount> rawCandidates {};
    const int rawDetectorSupport = collectFreshCandidates(rawCandidates);
    DecoderDecision decision = decodeCandidate(onsetPending_);
    const int previousOctaveState = octaveState_;
    const bool decoderDecisionAccepted = confirmOctaveTransition(decision,
                                                                  onsetPending_);
    const bool committedOctaveChange = octaveState_ != previousOctaveState;

    if (decision.valid && decision.candidate.valid)
    {
        observationContinuityBroken_ = false;
        const bool firstLock = trackedPitchHz_ <= 0.0f;
        const float selectedLog = std::log2(decision.candidate.frequencyHz);
        const float trackedLog = firstLock ? selectedLog : std::log2(trackedPitchHz_);

        // An onset may move faster than a stable note, but it no longer bypasses
        // the decoder.  Confirmed octave changes remain intentionally smoother
        // to avoid a low-frequency burst when the decision is first committed.
        float smoothing = 0.32f;
        if (firstLock)
            smoothing = 1.0f;
        else if (committedOctaveChange)
            smoothing = 0.82f;
        else if (onsetPending_)
            smoothing = decoderDecisionAccepted ? 0.58f : 0.36f;

        trackedPitchHz_ = std::exp2(trackedLog
            + smoothing * (selectedLog - trackedLog));
        trackedConfidence_ += 0.38f
            * (decision.candidate.confidence - trackedConfidence_);
        trackedPeriodicity_ += 0.38f
            * (decision.candidate.periodicity - trackedPeriodicity_);
        trackedConsensus_ += 0.35f
            * (decision.consensus - trackedConsensus_);
        trackedSupportCount_ = decision.supportCount;
        invalidHopCount_ = 0;

        const float rmsGate = smoothStep(minimumDetectorRms,
                                         minimumDetectorRms * 4.0f,
                                         std::sqrt(std::max(0.0f, slowEnergy_)));
        const float confidenceGate = smoothStep(0.42f, 0.88f, trackedConfidence_);
        const float periodicityGate = smoothStep(0.48f, 0.90f, trackedPeriodicity_);
        const float consensusGate = smoothStep(0.10f, 0.78f, trackedConsensus_);

        observation.frequencyHz = trackedPitchHz_;

        // DETECTOR_IS_OBSERVER_V1: exact correction may use the latest F0 only
        // after that F0 has survived the detector's normal evidence/register
        // guards. Audio presence by itself has no pitch authority.
        observation.correctionFrequencyHz =
            std::isfinite(decision.candidate.frequencyHz)
            && decision.candidate.frequencyHz > 0.0f
            ? decision.candidate.frequencyHz
            : trackedPitchHz_;
        observation.confidence = trackedConfidence_;
        observation.periodicity = trackedPeriodicity_;
        observation.consensus = trackedConsensus_;
        observation.detectorSupport = std::max(trackedSupportCount_, rawDetectorSupport);
        observation.octaveState = octaveState_;
        observation.pendingOctaveObservations = pendingOctaveCount_;
        const float detectorVoicing = clamp01(rmsGate
            * (0.48f * confidenceGate
             + 0.30f * periodicityGate
             + 0.22f * consensusGate));
        observation.audioPresent = presenceMode_;
        observation.voicing = detectorVoicing;
        observation.measurementAvailable = true;
        observation.valid = true; // this branch contains a confirmed F0
    }
    else
    {
        ++invalidHopCount_;
        trackedConfidence_ *= 0.90f;
        trackedPeriodicity_ *= 0.90f;
        trackedConsensus_ *= 0.88f;

        if (invalidHopCount_ > 12)
        {
            trackedPitchHz_ = 0.0f;
            trackedConfidence_ = 0.0f;
            trackedPeriodicity_ = 0.0f;
            trackedConsensus_ = 0.0f;
            trackedSupportCount_ = 0;
            decoderBeam_.fill({});
            pendingOctaveDelta_ = 0;
            pendingOctaveCount_ = 0;
            pendingOctaveFrequencyHz_ = 0.0f;
        }

        const bool provisionalAvailable =
            std::isfinite(provisionalMeasurement.frequencyHz)
            && provisionalMeasurement.frequencyHz > 0.0f;
        observation.frequencyHz = trackedPitchHz_;
        observation.correctionFrequencyHz = provisionalAvailable
            ? provisionalMeasurement.frequencyHz : trackedPitchHz_;
        observation.measurementAvailable = provisionalAvailable;
        observation.confidence = provisionalAvailable
            ? provisionalMeasurement.confidence : trackedConfidence_;
        observation.periodicity = provisionalAvailable
            ? provisionalMeasurement.periodicity : trackedPeriodicity_;
        observation.consensus = trackedConsensus_;
        observation.detectorSupport = rawDetectorSupport;
        observation.octaveState = octaveState_;
        observation.pendingOctaveObservations = pendingOctaveCount_;
        observation.audioPresent = presenceMode_;
        observation.voicing = 0.0f;
        observation.valid = false;
    }

    observation.onset = onsetPending_;
    observation.onsetStrength = onsetPending_ ? std::max(0.65f, onsetEnvelope_)
                                             : onsetEnvelope_;
    onsetPending_ = false;
    return true;
}

//==============================================================================

//==============================================================================
// ScaleQuantizer

std::uint64_t ModernPitchEngine::ScaleQuantizer::hashScale(
    const double* ratios,
    int count,
    double root) noexcept
{
    constexpr std::uint64_t offset = 1469598103934665603ull;
    constexpr std::uint64_t prime = 1099511628211ull;
    std::uint64_t hash = offset;
    const auto mix = [&hash, prime](std::uint64_t value) noexcept
    {
        hash ^= value;
        hash *= prime;
    };

    mix(static_cast<std::uint64_t>(std::max(0, count)));
    std::uint64_t bits = 0;
    std::memcpy(&bits, &root, sizeof(bits));
    mix(bits);
    for (int i = 0; ratios != nullptr && i < count; ++i)
    {
        bits = 0;
        std::memcpy(&bits, ratios + i, sizeof(bits));
        mix(bits);
    }
    return hash;
}

void ModernPitchEngine::ScaleQuantizer::reset() noexcept
{
    logRatios_.fill(0.0);
    ratioCount_ = 1;
    rootLog2_ = ModernPitchEngine::safeLog2(440.0);
    hash_ = 0;
    generation_ = 0;
    minStepCents_ = 1200.0f;
}

bool ModernPitchEngine::ScaleQuantizer::setScale(
    const double* ratios,
    int ratioCount,
    double rootFrequency,
    std::uint64_t generation) noexcept
{
    if (generation != 0 && generation == generation_)
        return false;

    const double safeRoot = std::isfinite(rootFrequency) && rootFrequency > 0.0
        ? rootFrequency : 440.0;
    const int safeCount = std::clamp(ratioCount, 0, maxScaleRatios);
    const auto nextHash = hashScale(ratios, safeCount, safeRoot);

    // SCALE_GENERATION_OWNS_GEOMETRY_V1: a versioned immutable snapshot
    // bypasses all per-block scale inspection. Unversioned callers retain the
    // previous hash-based contract for tests and compatibility.
    generation_ = generation;
    if (nextHash == hash_)
        return false;

    hash_ = nextHash;
    rootLog2_ = ModernPitchEngine::safeLog2(safeRoot);
    ratioCount_ = 0;
    logRatios_[static_cast<std::size_t>(ratioCount_++)] = 0.0;

    for (int i = 0; ratios != nullptr && i < safeCount
         && ratioCount_ < maxScaleRatios; ++i)
    {
        const double ratio = ratios[i];
        if (!std::isfinite(ratio) || ratio <= 0.0)
            continue;
        double folded = std::log2(ratio);
        folded -= std::floor(folded);
        if (folded >= 1.0 - 1.0e-10)
            folded = 0.0;

        bool duplicate = false;
        for (int j = 0; j < ratioCount_; ++j)
        {
            if (std::abs(logRatios_[static_cast<std::size_t>(j)] - folded) < 1.0e-8)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            logRatios_[static_cast<std::size_t>(ratioCount_++)] = folded;
    }

    std::sort(logRatios_.begin(), logRatios_.begin() + ratioCount_);

    minStepCents_ = 1200.0f;
    for (int i = 0; i < ratioCount_; ++i)
    {
        const int next = (i + 1) % ratioCount_;
        double step = next > i
            ? (logRatios_[static_cast<std::size_t>(next)]
               - logRatios_[static_cast<std::size_t>(i)]) * 1200.0
            : (1.0 + logRatios_[0]
               - logRatios_[static_cast<std::size_t>(i)]) * 1200.0;
        step = std::max(0.1, step);
        minStepCents_ = std::min(minStepCents_, static_cast<float>(step));
    }

    return true;
}

double ModernPitchEngine::ScaleQuantizer::nearestTargetLog2(
    double inputLog2) const noexcept
{
    if (!std::isfinite(inputLog2) || ratioCount_ <= 0)
        return inputLog2;

    const double relative = inputLog2 - rootLog2_;
    const double octave = std::floor(relative);
    double nearest = inputLog2;
    double nearestDistance = std::numeric_limits<double>::infinity();

    for (int i = 0; i < ratioCount_; ++i)
    {
        const double degree = logRatios_[static_cast<std::size_t>(i)];
        for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset)
        {
            const double candidate = rootLog2_ + octave
                + static_cast<double>(octaveOffset) + degree;
            const double distance = std::abs(candidate - inputLog2);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = candidate;
            }
        }
    }
    return nearest;
}


double ModernPitchEngine::ScaleQuantizer::adjacentTargetLog2(
    double currentTargetLog2,
    int direction) const noexcept
{
    if (!std::isfinite(currentTargetLog2) || direction == 0 || ratioCount_ <= 0)
        return currentTargetLog2;

    const int sign = direction > 0 ? 1 : -1;
    const double relative = currentTargetLog2 - rootLog2_;
    const double octave = std::floor(relative);
    double best = currentTargetLog2;
    double bestDistance = std::numeric_limits<double>::infinity();

    for (int octaveOffset = -2; octaveOffset <= 2; ++octaveOffset)
    {
        for (int degreeIndex = 0; degreeIndex < ratioCount_; ++degreeIndex)
        {
            const double candidate = rootLog2_ + octave
                + static_cast<double>(octaveOffset)
                + logRatios_[static_cast<std::size_t>(degreeIndex)];
            const double signedDistance = static_cast<double>(sign)
                * (candidate - currentTargetLog2);
            if (signedDistance > 1.0e-8 && signedDistance < bestDistance)
            {
                bestDistance = signedDistance;
                best = candidate;
            }
        }
    }
    return best;
}

//==============================================================================
// ModernPitchEngine control and processing
//==============================================================================
// ModernPitchEngine control and processing

float ModernPitchEngine::clamp01(float value) noexcept
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

double ModernPitchEngine::safeLog2(double value) noexcept
{
    return std::log2(std::max(value, 1.0e-12));
}


int ModernPitchEngine::latencyForMode(LatencyMode mode) noexcept
{
    // SINGLE_WET_PURITY_V6
    // The 128-sample spectral lattice is not a valid production transport for
    // this renderer: the measured +100-cent target/source power ratio is only
    // about 2.07 (roughly 3 dB), which is an audibly strong source-frequency
    // component.  256 samples is the smallest currently proven single-wet
    // lattice (>2000:1 on the same regression), so Experimental must report
    // and use that honest latency until a genuinely low-latency transport can
    // satisfy the same spectral-purity contract.
    switch (mode)
    {
        case LatencyMode::ultraLive: return 256;
        case LatencyMode::live:      return 256;
        case LatencyMode::quality:   return 512;
    }
    return 256;
}

void ModernPitchEngine::prepare(double sampleRate,
                                int maximumExpectedSamplesPerBlock,
                                int numberOfChannels,
                                LatencyMode latencyMode)
{
    static_cast<void>(maximumExpectedSamplesPerBlock);
    sampleRate_ = std::max(8000.0, finiteOr(sampleRate, 48000.0));
    channelCount_ = std::clamp(numberOfChannels, 1, maxSupportedChannels);
    latencyMode_ = latencyMode;
    latencySamples_ = latencyForMode(latencyMode_);

    linkedTracker_.prepare(sampleRate_);
    tempoController_.prepare(sampleRate_);
    for (int channel = 0; channel < maxSupportedChannels; ++channel)
        wetRenderers_[static_cast<std::size_t>(channel)].prepare(sampleRate_, latencySamples_);
    reset();
}

void ModernPitchEngine::reset() noexcept
{
    linkedTracker_.reset();
    linkedQuantizer_.reset();
    tempoController_.reset();
    linkedCorrection_ = {};
    for (int channel = 0; channel < maxSupportedChannels; ++channel)
        wetRenderers_[static_cast<std::size_t>(channel)].reset();
    latestObservation_ = {};
    audibleCorrectionCents_ = 0.0;
    sustainedSamples_ = 0;

    meterSequence_.store(0u, std::memory_order_relaxed);
    meterPitchHz_.store(0.0f, std::memory_order_relaxed);
    meterTargetHz_.store(0.0f, std::memory_order_relaxed);
    meterConfidence_.store(0.0f, std::memory_order_relaxed);
    meterVoicing_.store(0.0f, std::memory_order_relaxed);
    meterPeriodicity_.store(0.0f, std::memory_order_relaxed);
    meterConsensus_.store(0.0f, std::memory_order_relaxed);
    meterCorrectionCents_.store(0.0f, std::memory_order_relaxed);
    meterCorrectionVelocity_.store(0.0f, std::memory_order_relaxed);
    meterTargetJumpCents_.store(0.0f, std::memory_order_relaxed);
    meterSustainedSeconds_.store(0.0f, std::memory_order_relaxed);
    meterDetectorSupport_.store(0, std::memory_order_relaxed);
    meterOctaveState_.store(0, std::memory_order_relaxed);
    meterPendingOctave_.store(0, std::memory_order_relaxed);
    meterTrackingState_.store(static_cast<int>(TrackingState::unvoiced),
                              std::memory_order_relaxed);
    meterTempoBpm_.store(120.0f, std::memory_order_relaxed);
    meterTempoGridPhase_.store(0.0f, std::memory_order_relaxed);
    meterTempoGlideTimeMs_.store(0.0f, std::memory_order_relaxed);
    meterTempoActive_.store(false, std::memory_order_relaxed);
    meterTempoWaiting_.store(false, std::memory_order_relaxed);
    meterTempoHostSync_.store(false, std::memory_order_relaxed);
    meterTempoMode_.store(static_cast<int>(CreativeTempo::Mode::off),
                          std::memory_order_relaxed);
}

float ModernPitchEngine::adaptiveHysteresis(
    const Parameters& parameters,
    const ScaleQuantizer& quantizer,
    const PitchObservation& observation) const noexcept
{
    // HOLD_IS_LITERAL_USER_CENTS_V2: the GUI value already has the complete
    // musical meaning. Scale density, mode, confidence and sensor output may
    // not silently remap it. Dense scales are allowed to have a wide Hold only
    // when the user explicitly asks for one; a qualified new note can still
    // override that radius in the supervisor below.
    (void) quantizer;
    (void) observation;
    return std::clamp(finiteOr(parameters.lockHysteresis, 24.0f), 0.0f, 80.0f);
}

double ModernPitchEngine::responseTimeMs(
    const Parameters& parameters,
    bool targetChanged,
    double targetJumpCents) const noexcept
{
    const double requested = std::clamp(
        static_cast<double>(finiteOr(parameters.retuneTimeMs, 50.0f)),
        0.0, 500.0);
    if (requested <= 0.00001)
        return 0.0; // Response=0 is literal and independent from target authority.
    double response = std::max(0.35, requested);

    if (parameters.scaleLock)
    {
        const double norm = std::pow(requested / 500.0, 1.35);
        double modeMaximumMs = 3.0;
        switch (latencyMode_)
        {
            // MICROTONAL_HARD_LOCK_V3: Scale Lock owns its documented fast
            // trajectory.  The normal transition controller must not stretch
            // a dense-scale note change into tens of milliseconds.
            case LatencyMode::quality:
                response = 3.0 + 2.0 * norm;
                modeMaximumMs = 5.0;
                break;
            case LatencyMode::live:
                response = 1.5 + 1.5 * norm;
                modeMaximumMs = 3.0;
                break;
            case LatencyMode::ultraLive:
                response = 0.35 + 1.15 * norm;
                modeMaximumMs = 1.5;
                break;
        }
        const double humanTiming = 0.40
            * static_cast<double>(clamp01(parameters.humanize));
        response = std::min(modeMaximumMs, response + humanTiming);
    }

    if (targetChanged && std::abs(targetJumpCents) > 0.1)
    {
        const double transitionMs = std::clamp(
            static_cast<double>(finiteOr(parameters.transitionTimeMs, 35.0f)),
            0.0, 2000.0);
        if (parameters.tempo.mode != CreativeTempo::Mode::off)
        {
            // Creative Tempo controls the same correction trajectory.
            response = std::max(response, transitionMs);
        }
        else if (!parameters.scaleLock)
        {
            // Main used a pre-rolled second synthesis layer for note changes.
            // Keep only its useful bounded transition timing outside Scale
            // Lock. Scale Lock already has its own <=5/3/1.5 ms trajectory.
            const double jumpWeight = std::clamp(
                std::abs(targetJumpCents) / 600.0, 0.0, 1.0);
            const double trajectoryMs = std::clamp(
                transitionMs * (0.22 + 0.38 * jumpWeight), 0.35, 32.0);
            response = std::max(response, trajectoryMs);
        }
    }
    return std::clamp(response, 0.35, 500.0);
}

bool ModernPitchEngine::advanceConservativeF0Rescue(
    CorrectionState& state,
    ScaleQuantizer& quantizer,
    const PitchObservation& observation,
    const Parameters& parameters,
    bool bodyLikeFrame) noexcept
{
    constexpr int minimumHistory = 6;
    const int hopSamples = MultiRatePitchTracker::hopSize();
    const int maximumPredictionHops = std::max(2, static_cast<int>(std::ceil(
        0.024 * sampleRate_ / static_cast<double>(hopSamples))));

    const bool forbiddenState = state.trackingState == TrackingState::attack
        || state.trackingState == TrackingState::transition
        || state.trackingState == TrackingState::unvoiced;
    const bool continuationOfQualifiedDropout = state.rescueQualificationHops > 0
        || state.rescuePredictionActive;
    const bool eligible = observation.audioPresent
        && state.targetValid
        && state.noteBodyLatched
        && bodyLikeFrame
        && !observation.onset
        && !forbiddenState
        && state.recentRealPitchCount >= minimumHistory
        && (state.trackingState == TrackingState::stable
            || continuationOfQualifiedDropout);

    if (!eligible)
    {
        state.rescueQualificationHops = 0;
        state.rescuePredictionActive = false;
        state.rescuePredictionHops = 0;
        state.rescueDirection = 0;
        state.rescueTargetShifted = false;
        return false;
    }

    if (!state.rescuePredictionActive)
    {
        ++state.rescueQualificationHops;
        if (state.rescueQualificationHops < 2)
            return false;

        const int count = state.recentRealPitchCount;
        const double first = state.recentRealPitchLog2[0];
        const double last = state.recentRealPitchLog2[static_cast<std::size_t>(count - 1)];
        double minimum = first;
        double maximum = first;
        int positiveSteps = 0;
        int negativeSteps = 0;
        int signChanges = 0;
        int previousSign = 0;
        double lastStepCents = 0.0;

        for (int i = 1; i < count; ++i)
        {
            const double value = state.recentRealPitchLog2[static_cast<std::size_t>(i)];
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            const double stepCents = (value
                - state.recentRealPitchLog2[static_cast<std::size_t>(i - 1)]) * 1200.0;
            lastStepCents = stepCents;
            const int sign = stepCents > 0.6 ? 1 : (stepCents < -0.6 ? -1 : 0);
            if (sign > 0)
                ++positiveSteps;
            else if (sign < 0)
                ++negativeSteps;
            if (sign != 0)
            {
                if (previousSign != 0 && sign != previousSign)
                    ++signChanges;
                previousSign = sign;
            }
        }

        const double degreeCents = std::min(100.0,
            std::max(0.1, static_cast<double>(quantizer.minimumStepCents())));
        const double netCents = (last - first) * 1200.0;
        const double rangeCents = (maximum - minimum) * 1200.0;
        const int activeSteps = positiveSteps + negativeSteps;
        const int direction = netCents > 0.0 ? 1 : (netCents < 0.0 ? -1 : 0);
        const int consistentSteps = direction > 0 ? positiveSteps : negativeSteps;
        const double signConsistency = activeSteps > 0
            ? static_cast<double>(consistentSteps) / static_cast<double>(activeSteps)
            : 0.0;
        const double fromCentreCents = state.pitchCentreValid
            ? (last - state.pitchCentreLog2) * 1200.0 : 0.0;
        const bool nearBoundary = direction > 0
            ? fromCentreCents >= 0.32 * degreeCents
            : direction < 0 && fromCentreCents <= -0.32 * degreeCents;
        const bool strongDirectionalHistory = direction != 0
            && std::abs(netCents) >= 0.30 * degreeCents
            && signConsistency >= 0.80
            && signChanges <= 1
            && (nearBoundary || std::abs(netCents) >= 0.60 * degreeCents)
            && rangeCents >= 0.34 * degreeCents;

        state.rescueDirection = strongDirectionalHistory ? direction : 0;
        const double averageSlope = netCents
            / static_cast<double>(std::max(1, count - 1));
        const double slopeLimit = state.rescueDirection == 0
            ? std::min(4.0, 0.08 * degreeCents)
            : std::min(8.0, 0.12 * degreeCents);
        const double requestedSlope = state.rescueDirection == 0
            ? lastStepCents : averageSlope;
        state.rescueSlopeCentsPerHop = std::clamp(
            requestedSlope, -slopeLimit, slopeLimit);
        if (state.rescueDirection > 0)
            state.rescueSlopeCentsPerHop = std::max(0.0, state.rescueSlopeCentsPerHop);
        else if (state.rescueDirection < 0)
            state.rescueSlopeCentsPerHop = std::min(0.0, state.rescueSlopeCentsPerHop);

        state.rescueBaseSourceLog2 = last;
        state.rescueSourceLog2 = last;
        state.rescueBaseTargetLog2 = state.targetLog2;
        state.rescueBaseDesiredCents = state.desiredCents;
        state.rescuePredictionHops = 0;
        state.rescueTargetShifted = false;
        state.rescuePredictionActive = true;

        // NO_PREDICTED_NOTE_IDENTITY_V1: a detector hole may extrapolate the
        // source coordinate for transport continuity, but it may never create a
        // new musical degree. Only a subsequent real F0 may change targetLog2.
        state.rescueTargetShifted = false;
    }

    if (++state.rescuePredictionHops > maximumPredictionHops)
    {
        state.rescueQualificationHops = 0;
        state.rescuePredictionActive = false;
        state.rescuePredictionHops = 0;
        state.rescueDirection = 0;
        state.rescueTargetShifted = false;
        state.recentRealPitchCount = 0;
        return false;
    }

    const double degreeCents = std::min(100.0,
        std::max(0.1, static_cast<double>(quantizer.minimumStepCents())));
    const double decay = state.rescueDirection == 0
        ? std::pow(0.55, static_cast<double>(state.rescuePredictionHops - 1))
        : std::pow(0.82, static_cast<double>(state.rescuePredictionHops - 1));
    const double stepCents = state.rescueSlopeCentsPerHop * decay;
    const double proposedLog2 = state.rescueSourceLog2 + stepCents / 1200.0;
    const double proposedDeltaCents = (proposedLog2 - state.rescueBaseSourceLog2) * 1200.0;
    const double maximumDeltaCents = (state.rescueDirection == 0 ? 0.20 : 0.75)
        * degreeCents;
    const double boundedDeltaCents = std::clamp(
        proposedDeltaCents, -maximumDeltaCents, maximumDeltaCents);
    state.rescueSourceLog2 = state.rescueBaseSourceLog2
        + boundedDeltaCents / 1200.0;

    const double targetDeltaCents = (state.targetLog2
        - state.rescueBaseTargetLog2) * 1200.0;
    const double sourceDeltaCents = (state.rescueSourceLog2
        - state.rescueBaseSourceLog2) * 1200.0;
    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    // NO_UNITY_ESCAPE_V1: a detector hole continues the scale-owned transport;
    // Amount cannot pull rescue motion back toward the source coordinate.
    state.desiredCents = std::clamp(
        state.rescueBaseDesiredCents + targetDeltaCents - sourceDeltaCents,
        -maximumCents, maximumCents);
    return true;
}

void ModernPitchEngine::updateCorrectionState(
    CorrectionState& state,
    ScaleQuantizer& quantizer,
    const PitchObservation& observation,
    const Parameters& parameters) noexcept
{
    // TARGET_REVISION_DIAGNOSTICS_REMOVED_V1: ownership state is not mirrored
    // into a second diagnostic ledger.
    // RENDERER_STABLE_HOP_AUTHORITY_V1
    // Normal hops may replace the renderer command. Existing supervisor vetoes
    // below can revoke only audible authority while preserving analysis,
    // quantizer and controller history.
    state.rendererAcceptCurrentHop = true;

    const int hopSamples = MultiRatePitchTracker::hopSize();
    const double hopSeconds = static_cast<double>(hopSamples) / sampleRate_;
    const float humanize = clamp01(parameters.humanize);
    const bool richEvidence = parameters.voiceEvidenceValid;
    const bool trustedPitch = observation.valid
        && std::isfinite(observation.frequencyHz)
        && observation.frequencyHz > 0.0f;
    const bool provisionalMeasurement = !trustedPitch
        && observation.measurementAvailable
        && std::isfinite(observation.correctionFrequencyHz)
        && observation.correctionFrequencyHz > 0.0f;
    const bool provisionalVoicePitch = provisionalMeasurement
        && richEvidence
        && parameters.voiceBodyEnergy >= 0.30f
        && parameters.voiceHarmonicity >= 0.24f
        && parameters.voiceSpectralReliability >= 0.34f
        && parameters.voiceBreathiness <= 0.72f
        && parameters.voiceEventStrength <= 0.74f;
    const bool validPitch = trustedPitch || provisionalVoicePitch;
    if (validPitch)
    {
        state.pitchStaleSamples = 0;
        state.rescueQualificationHops = 0;
        state.rescuePredictionActive = false;
        state.rescuePredictionHops = 0;
        state.rescueDirection = 0;
        state.rescueTargetShifted = false;
    }
    else if (state.noteBodyLatched)
        state.pitchStaleSamples = std::min(std::numeric_limits<int>::max() - hopSamples,
                                           state.pitchStaleSamples + hopSamples);

    const float trackerBody = validPitch
        ? clamp01(0.34f * observation.voicing
                + 0.28f * observation.periodicity
                + 0.23f * observation.confidence
                + 0.15f * observation.consensus)
        : 0.0f;
    const float analysedBody = richEvidence
        ? clamp01(0.44f * parameters.voiceBodyEnergy
                + 0.24f * parameters.voiceHarmonicity
                + 0.18f * parameters.voiceSpectralReliability
                + 0.14f * (1.0f - parameters.voiceBreathiness))
        : trackerBody;
    const float bodyScore = richEvidence
        ? std::max(0.72f * analysedBody, trackerBody)
        : trackerBody;

    // Entering a note requires stronger evidence than staying in one. This
    // hysteresis is about note identity only: it never scales Amount or the
    // correction destination.
    const float enterBodyThreshold = 0.46f - 0.06f * humanize;
    const float holdBodyThreshold = 0.34f - 0.05f * humanize;
    const float bodyThreshold = state.noteBodyLatched
        ? holdBodyThreshold : enterBodyThreshold;
    const bool bodyPresent = observation.audioPresent
        || (bodyScore >= bodyThreshold
            && (!richEvidence || parameters.voiceBreathiness < 0.76f
                || parameters.voiceHarmonicity > 0.48f));

    const float breathScore = richEvidence
        ? clamp01(0.58f * parameters.voiceBreathiness
                + 0.22f * (1.0f - parameters.voiceBodyEnergy)
                + 0.12f * (1.0f - parameters.voiceHarmonicity)
                + 0.08f * (1.0f - parameters.voiceSpectralReliability))
        : 0.0f;
    const bool confirmedBreathFrame = richEvidence
        && !observation.audioPresent
        && breathScore > 0.62f
        && parameters.voiceBreathiness > 0.56f
        && parameters.voiceBodyEnergy < 0.48f
        && parameters.voiceEventStrength < 0.82f;
    const bool confirmedAbsenceFrame = richEvidence
        && !observation.audioPresent
        && parameters.voiceBodyEnergy < 0.20f
        && parameters.voiceHarmonicity < 0.22f
        && parameters.voiceSpectralReliability < 0.28f
        && parameters.voiceEventStrength < 0.72f;

    const bool rescueBodyFrame = richEvidence
        && observation.audioPresent
        && parameters.voiceBodyEnergy >= 0.34f
        && parameters.voiceHarmonicity >= 0.32f
        && parameters.voiceSpectralReliability >= 0.44f
        && parameters.voiceBreathiness <= 0.34f
        && parameters.voiceEventStrength <= 0.30f;

    // SCALE_OWNS_VOICE_V2: detector state describes evidence only. Audible
    // material never receives permission to return to dry/source pitch merely
    // because it is breathy, aperiodic or phonetic.
    const bool explicitPhoneticFrame = richEvidence
        && (parameters.voiceEventStrength >= 0.82f
            || (parameters.voiceBreathiness >= 0.76f
                && parameters.voiceHarmonicity <= 0.48f));

    const float bodyAttack = std::clamp(static_cast<float>(
        1.0 - std::exp(-hopSeconds / 0.018)), 0.001f, 1.0f);
    const float bodyRelease = std::clamp(static_cast<float>(
        1.0 - std::exp(-hopSeconds / 0.070)), 0.001f, 1.0f);
    const float bodyAlpha = bodyScore >= state.noteBodyConfidence
        ? bodyAttack : bodyRelease;
    state.noteBodyConfidence += bodyAlpha
        * (bodyScore - state.noteBodyConfidence);
    state.noteBodyConfidence = clamp01(state.noteBodyConfidence);

    const auto setState = [&state](TrackingState next) noexcept
    {
        if (state.trackingState != next)
        {
            if (state.trackingState == TrackingState::transition
                && next == TrackingState::stable)
            {
                state.velocityCentsPerSecond = 0.0;
            }
            state.trackingState = next;
            state.stateAgeSamples = 0;
        }
    };

    bool bodyCounterAdvanced = false;
    if (state.noteBodyLatched)
    {
        if (bodyPresent)
        {
            state.stableBodyObservations = std::min(32,
                state.stableBodyObservations + 1);
            bodyCounterAdvanced = true;
            state.breathEvidenceSamples = std::max(0,
                state.breathEvidenceSamples - 2 * hopSamples);
            state.uncertainSamples = std::max(0,
                state.uncertainSamples - 2 * hopSamples);
        }
        else if (confirmedBreathFrame)
        {
            state.breathEvidenceSamples += hopSamples;
            state.uncertainSamples = std::max(0,
                state.uncertainSamples - hopSamples);
        }
        else if (confirmedAbsenceFrame || !validPitch)
        {
            state.uncertainSamples += hopSamples;
            state.breathEvidenceSamples = std::max(0,
                state.breathEvidenceSamples - hopSamples);
        }
        else
        {
            // A valid but weak/ambiguous F0 is not enough to keep the latch
            // forever. Accumulate absence slowly while giving the body sensors
            // time to recover from consonants and vibrato minima.
            state.uncertainSamples += std::max(1, hopSamples / 2);
            state.breathEvidenceSamples = std::max(0,
                state.breathEvidenceSamples - hopSamples);
        }
    }

    const int breathConfirmSamples = static_cast<int>(std::lround(
        sampleRate_ * (0.040 + 0.020 * static_cast<double>(humanize))));
    const int ambiguousReleaseSamples = static_cast<int>(std::lround(
        sampleRate_ * (0.160 + 0.080 * static_cast<double>(humanize))));
    const bool confirmedBreath = state.noteBodyLatched
        && state.breathEvidenceSamples >= breathConfirmSamples;
    const bool confirmedAbsence = state.noteBodyLatched
        && state.uncertainSamples >= ambiguousReleaseSamples;

    // VOICE_LABELS_CANNOT_FREEZE_CORRECTION_V1: breath/phonetic/absence labels
    // are identity vetoes only. If a finite real F0 already exists, they cannot
    // freeze the owned transport/correction coordinate and thereby make a
    // moving voice drift away from its still-owned scale target.
    bool identityOnlyVeto = false;

    // BREATH_IS_TARGETED_NOT_DRY_V1: voice labels may veto note identity,
    // never correction authority. If a real F0 exists it continues through the
    // ordinary transport outlier wall and is corrected toward the already-owned
    // scale degree. If no F0 exists, the previous correction is held exactly.

    // Breath/absence is positive evidence and therefore wins even if a noisy
    // frame happens to yield a formally valid F0. This prevents breaths from
    // keeping the pitch engine latched through a spurious detector result.
    // TARGET_AUTHORITY_TAIL_HOLD_V1: once a musical target exists, breath or
    // temporary absence may stop supplying a trustworthy F0, but it is not
    // permission to move the audio back toward the source pitch. The current
    // frame evidence already exists here, so do not wait for temporal breath
    // confirmation while a spurious periodicity is free to retarget the note.
    if (state.targetValid
        && (confirmedBreath
            || confirmedAbsence
            || (richEvidence
                && (confirmedBreathFrame || confirmedAbsenceFrame))))
    {
        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: a breath/absence interval
        // breaks musical-change persistence but never changes audible state.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;
        state.stableBodyObservations = 0;

        if (!validPitch)
        {
            // No measured source coordinate exists. Keep the current target,
            // transport and non-zero correction exactly; never manufacture F0.
            if (confirmedAbsence
                || confirmedAbsenceFrame
                || state.breathEvidenceSamples > static_cast<int>(0.12 * sampleRate_))
            {
                state.pitchCentreValid = false;
            }
            return;
        }

        // VALID_F0_TRANSPORT_SURVIVES_LABEL_V1: the classifier can say
        // "uncertain/breath/phonetic", but a real coordinate must still keep
        // the already-owned correction aligned with the moving source. It gets
        // no authority to nominate a different musical degree.
        identityOnlyVeto = true;
    }

    if (!validPitch)
    {
        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: detector holes cannot bridge
        // two unrelated challenger fragments into a target revision.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        ++state.invalidObservations;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;

        // SCALE_OWNS_TRANSPORT_V1: an F0 hole never invents an audible
        // source coordinate. Detector search/reacquisition may continue, but
        // target, transportPeriodHz and desiredCents remain exactly held until
        // a real non-vetoed measurement returns.
        state.rescueQualificationHops = 0;
        state.rescuePredictionActive = false;
        state.rescuePredictionHops = 0;
        state.rescueDirection = 0;
        state.rescueTargetShifted = false;

        // SOUND_EQUALS_CORRECTION_V1: audio presence owns the voice, but
        // Stable is forbidden until a real target exists. Acquire is now only
        // detector-search telemetry: an already acquired target/correction is
        // preserved exactly while F0 is temporarily missing.
        if (observation.audioPresent)
        {
            state.noteBodyLatched = true;
            state.noteBodyConfidence = 1.0f;
            state.stableBodyObservations = std::max(4, state.stableBodyObservations);
            state.breathEvidenceSamples = 0;
            state.uncertainSamples = 0;
            if (state.targetValid)
            {
                if (state.trackingState != TrackingState::transition)
                    setState(TrackingState::stable);
            }
            else
                setState(TrackingState::acquire);
            return;
        }

        // Missing F0 is not missing voice. A latched note keeps the exact
        // destination while body evidence survives. Acquire/attack may also
        // settle to stable from body evidence alone after a prior valid lock.
        if (state.noteBodyLatched)
        {
            if (state.targetValid)
            {
                if (state.trackingState != TrackingState::transition)
                    setState(TrackingState::stable);
                return;
            }
            const int reacquireSamples = static_cast<int>(std::lround(0.070 * sampleRate_));
            if (state.pitchStaleSamples >= reacquireSamples)
                setState(TrackingState::acquire);
            return;
        }

        // TARGET_AUTHORITY_TAIL_HOLD_V1: a previously selected scale degree
        // remains the destination through pitchless material. No hidden release
        // is allowed to undo the correction and reveal the source note.
        if (state.targetValid)
            setState(TrackingState::acquire);
        else
            setState(TrackingState::unvoiced);
        return;
    }

    state.invalidObservations = 0;

    // PHONETIC_VETO_HOLDS_TRANSPORT_V1: a formally valid detector period on
    // a consonant/breathy event is still not a musical source coordinate.
    // Positive phonetic evidence may veto this observation, but it cannot
    // attenuate, release or otherwise modify an already-owned correction.
    if (explicitPhoneticFrame && state.targetValid)
    {
        // A consonant/event label may veto note identity, but it cannot freeze
        // source transport when the detector has already supplied a real F0.
        // Far/outlier coordinates remain protected by the existing transport
        // innovation and octave safety walls below.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.stableBodyObservations = 0;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;
        if (!validPitch)
            return;
        identityOnlyVeto = true;
    }

    // A strong breath/absence before any body latch must not become a note just
    // because the pitch tracker found a periodic accident in the noise.
    // PHONETIC_STATE_HAS_NO_CORRECTION_AUTHORITY_V1: breath/absence
    // evidence may classify the detector state, but it never writes a
    // source/unity correction. A valid coordinate continues into the same
    // quantizer; an invalid coordinate keeps the already-owned target.

    if (bodyPresent || trackerBody > 0.58f)
    {
        if (!state.noteBodyLatched)
        {
            state.noteBodyLatched = true;
            state.stableBodyObservations = 1;
            state.noteBodyConfidence = std::max(state.noteBodyConfidence,
                                                bodyScore);
        }
        else if (!bodyCounterAdvanced)
        {
            state.stableBodyObservations = std::min(32,
                state.stableBodyObservations + 1);
        }
    }
    else if (!bodyCounterAdvanced)
    {
        state.stableBodyObservations = std::max(0,
            state.stableBodyObservations - 1);
    }

    // A tracker onset inside an already-latched note is usually consonant or
    // energy modulation, not a new note identity. Legato note changes are
    // represented by target identity and transition below.
    const bool musicalOnset = observation.onset
        && (!state.noteBodyLatched
            || state.trackingState == TrackingState::unvoiced);
    if (musicalOnset)
    {
        setState(TrackingState::attack);
        state.stableObservations = 0;
        state.stableBodyObservations = bodyPresent ? 1 : 0;
    }
    else if (state.trackingState == TrackingState::unvoiced)
    {
        setState(TrackingState::acquire);
        state.stableObservations = 0;
    }

    const float correctionFrequencyHz =
        std::isfinite(observation.correctionFrequencyHz)
        && observation.correctionFrequencyHz > 0.0f
        ? observation.correctionFrequencyHz
        : observation.frequencyHz;
    // LOCAL_TRAJECTORY_V1: target nomination uses the fastest accepted physical
    // coordinate. It still cannot command the renderer directly: challenger
    // persistence, octave veto and the transport innovation gate remain between
    // this measurement and audible correction.
    const double observedLog2 = safeLog2(correctionFrequencyHz);
    const double correctionObservedLog2 = observedLog2;
    // Hold belongs to musical identity, not detector confidence or transport.
    // Compute the literal user radius once and use it only for identity logic.
    const float holdRadiusCents = adaptiveHysteresis(
        parameters, quantizer, observation);

    // LOCAL_DEGREE_GEOMETRY_V1: every within-note authority threshold is
    // measured against the actual adjacent scale degree in the direction of
    // motion. A 70-cent motion therefore has a completely different meaning
    // in 75-EDO than inside a 200-cent diatonic gap. The minimum global step is
    // only a numerical fallback, never the musical geometry when a target exists.
    const auto localDegreeStepCents = [&quantizer](double targetLog2,
                                                   double probeLog2) noexcept
    {
        const int direction = probeLog2 >= targetLog2 ? 1 : -1;
        const double adjacent = quantizer.adjacentTargetLog2(targetLog2, direction);
        const double stepCents = std::abs(adjacent - targetLog2) * 1200.0;
        if (std::isfinite(stepCents) && stepCents >= 0.1)
            return stepCents;
        return std::max(0.1, static_cast<double>(quantizer.minimumStepCents()));
    };

    // TERMINAL_TAIL_KEEPS_OWNED_DEGREE_V1: a weakening terminal trajectory
    // may describe where the physical source is moving, but it is not positive
    // evidence for a new musical note. Keep transport live so the falling/rising
    // source can still be corrected to the already-owned scale degree; remove
    // only note-identity authority. This is symmetric for falling and rising
    // tails and is measured in the actual adjacent-degree geometry.
    bool terminalTailIdentityVeto = false;
    bool terminalTailStableCompensation = false;
    if (state.targetValid
        && state.noteBodyLatched
        && richEvidence
        && !observation.onset)
    {
        const double localTailStep = localDegreeStepCents(
            state.targetLog2, observedLog2);
        const double signedTailDistanceCents =
            (observedLog2 - state.targetLog2) * 1200.0;
        const double priorSourceLog2 = state.transportPeriodHz > 0.0
            && std::isfinite(state.transportPeriodHz)
            ? safeLog2(state.transportPeriodHz)
            : (state.pitchCentreValid ? state.pitchCentreLog2 : state.targetLog2);
        const double signedPriorDistanceCents =
            (priorSourceLog2 - state.targetLog2) * 1200.0;
        const bool sameTailSide = std::abs(signedPriorDistanceCents)
                < 0.12 * localTailStep
            || signedTailDistanceCents * signedPriorDistanceCents > 0.0;

        int degradationVotes = 0;
        degradationVotes += parameters.voiceBodyEnergy < 0.52f ? 1 : 0;
        degradationVotes += parameters.voiceHarmonicity < 0.48f ? 1 : 0;
        degradationVotes += parameters.voiceSpectralReliability < 0.50f ? 1 : 0;
        degradationVotes += parameters.voiceBreathiness > 0.42f ? 1 : 0;
        const bool terminalStructure = parameters.voiceEventStrength < 0.55f
            && degradationVotes >= 2;

        // TERMINAL_TAIL_STABLE_TRANSPORT_REBASE_V1
        // A weakening same-note tail may keep reporting valid F0 while its
        // physical source coordinate falls or rises quickly. Stable still owns
        // the already selected scale degree, so Response must not turn that
        // transport motion into a temporary audible pitch escape. This flag
        // grants no target identity authority and does not freeze transport.
        terminalTailStableCompensation = terminalStructure
            && sameTailSide
            && state.trackingState == TrackingState::stable;

        const bool outsideStableCore =
            std::abs(signedTailDistanceCents) >= 0.32 * localTailStep;

        if (terminalStructure && sameTailSide && outsideStableCore)
        {
            terminalTailIdentityVeto = true;
            identityOnlyVeto = true;
            // A tail cannot accumulate hidden note-change credit while weak.
            // Once strong structured evidence returns, normal target nomination
            // resumes immediately; no invented F0 and no dry release are used.
            state.latentTargetValid = false;
            state.latentTargetLog2 = 0.0;
            state.latentTargetHops = 0;
            state.identityChallengerDirection = 0;
            state.identityChallengerEvidence = 0.0;
        }
    }

    bool detectorScaleCommit = false;
    bool provisionalOctaveIdentityVeto = false;
    if (state.targetValid && !identityOnlyVeto)
    {
        // LATENT_SCALE_CANDIDATE_V2: detector uncertainty is represented in a
        // separate analysis state. The currently owned scale degree is never
        // revised merely because an instantaneous F0 crosses a Voronoi boundary.
        const double nominatedTarget = quantizer.nearestTargetLog2(observedLog2);
        const double targetDeltaCents =
            (nominatedTarget - state.targetLog2) * 1200.0;
        const bool nominatesOwnedTarget = std::abs(targetDeltaCents) < 0.5;

        if (nominatesOwnedTarget)
        {
            state.latentTargetValid = false;
            state.latentTargetLog2 = 0.0;
            state.latentTargetHops = 0;
        }
        else
        {
            const double scaleStep = localDegreeStepCents(
                state.targetLog2, observedLog2);
            const double observedDistanceFromOwnedTarget =
                std::abs(observedLog2 - state.targetLog2) * 1200.0;
            const double deepExitRatio = scaleStep <= 50.0 ? 0.52 : 0.72;

            const double absoluteJump = std::abs(targetDeltaCents);
            const int nearestOctave = static_cast<int>(std::lround(
                absoluteJump / 1200.0));
            const bool octaveLikeTarget = nearestOctave >= 1
                && nearestOctave <= 2
                && std::abs(absoluteJump
                    - 1200.0 * static_cast<double>(nearestOctave)) <= 35.0;

            // AMBIGUOUS_BOUNDARY_IS_LOCAL_MOTION_V2: on a wide scale a sung
            // vibrato may live beyond the half-cell boundary for many hops.
            // Until it penetrates the challenger cell deeply enough, it is not
            // even a note-change candidate. Let the pre-existing continuity and
            // local-transport tracker see it so zero Vibrato can remove the
            // physical modulation, but do not grant this latent gate any target
            // authority. Octave-like observations are always treated as deep
            // ambiguity because their error cost is catastrophic.
            // HOLD_DOES_NOT_CREATE_NOTE_IDENTITY_V2: crossing the Hold radius
            // is not, by itself, evidence of a new musical note. Preserve the
            // existing scale-relative deep geometry so vibrato and continuous
            // trajectories cannot chatter between degrees. A wider Hold may
            // suppress this fast/deep promotion, while the independent
            // same-side persistence path below remains free to qualify a real
            // sustained/legato note and then bypass Hold.
            const bool outsideUserHold = observedDistanceFromOwnedTarget
                > static_cast<double>(holdRadiusCents) + 1.0e-6;
            const bool deepCandidate = octaveLikeTarget
                || (outsideUserHold
                    && observedDistanceFromOwnedTarget >= deepExitRatio * scaleStep);

            if (!deepCandidate)
            {
                state.latentTargetValid = false;
                state.latentTargetLog2 = 0.0;
                state.latentTargetHops = 0;
                // Deliberately continue: target identity remains governed by the
                // old bounded continuity logic, which already rejects ±70-cent
                // long vibrato, while transport may still track local motion.
            }
            else
            {
                const bool sameLatent = state.latentTargetValid
                    && std::abs(nominatedTarget - state.latentTargetLog2)
                        * 1200.0 < 0.5;
                if (sameLatent)
                {
                    state.latentTargetHops = std::min(64,
                        state.latentTargetHops + 1);
                }
                else
                {
                    state.latentTargetValid = true;
                    state.latentTargetLog2 = nominatedTarget;
                    state.latentTargetHops = 1;
                }

                int requiredHops = 0;
                if (octaveLikeTarget)
                {
                    // A provisional single detector family may describe an
                    // octave candidate forever but can never own the register.
                    // Trusted/corroborated octave evidence remains possible and
                    // bounded, just deliberately slower than ordinary notes.
                    if (!trustedPitch && observation.detectorSupport < 2)
                    {
                        requiredHops = 1000000;
                        // PROVISIONAL_OCTAVE_CANNOT_BYPASS_IDENTITY_VETO_V1:
                        // the latent gate owns this negative decision. Later
                        // centre/boundary logic may not promote the same
                        // uncorroborated octave by accumulating time alone.
                        provisionalOctaveIdentityVeto = true;
                    }
                    else if (trustedPitch && observation.detectorSupport >= 2)
                        requiredHops = 8;
                    else if (trustedPitch)
                        requiredHops = 14;
                    else
                        requiredHops = 18;
                }
                else
                {
                    // Precision is concentrated at the boundary, not paid for
                    // by normal note speed. A real coordinate deep in the next
                    // cell changes quickly once it repeats geometrically.
                    requiredHops = trustedPitch
                        ? (observation.detectorSupport >= 2 ? 2 : 3)
                        : 6;
                }

                if (state.latentTargetHops >= requiredHops)
                {
                    detectorScaleCommit = true;
                    state.latentTargetValid = false;
                    state.latentTargetLog2 = 0.0;
                    state.latentTargetHops = 0;
                }
                // LATENT_IDENTITY_NEVER_FREEZES_WET_V1: while identity is still
                // pending, continue through centre/transport/correction. The
                // challenger has zero target authority, but it may not create a
                // dry-like/stale correction discontinuity on a long note.
            }
        }
    }

    if (rescueBodyFrame && !observation.onset)
    {
        if (state.recentRealPitchCount
            < static_cast<int>(state.recentRealPitchLog2.size()))
        {
            state.recentRealPitchLog2[static_cast<std::size_t>(
                state.recentRealPitchCount++)] = correctionObservedLog2;
        }
        else
        {
            for (std::size_t i = 1; i < state.recentRealPitchLog2.size(); ++i)
                state.recentRealPitchLog2[i - 1] = state.recentRealPitchLog2[i];
            state.recentRealPitchLog2.back() = correctionObservedLog2;
        }
    }

    bool liveIdentityBreak = false;
    bool forceTargetSwitch = false;
    if (identityOnlyVeto || provisionalOctaveIdentityVeto)
    {
        // IDENTITY_VETO_TRANSPORT_CONTINUES_V1: hold musical identity and its
        // continuity centre, but continue below into local transport/correction.
        // This is the exact opposite of the old early-return freeze.
        liveIdentityBreak = false;
        forceTargetSwitch = false;
    }
    else if (detectorScaleCommit)
    {
        // The destination degree has already passed the detector-domain
        // persistence test. Rebase the analysis centre and present exactly one
        // challenger to the user-owned quantizer/Hold logic.
        state.pitchCentreLog2 = observedLog2;
        state.pitchCentreValid = true;
        state.stableObservations = 0;
        liveIdentityBreak = true;
        forceTargetSwitch = true;
    }
    else if (!state.pitchCentreValid || musicalOnset)
    {
        state.pitchCentreLog2 = observedLog2;
        state.pitchCentreValid = true;
        state.stableObservations = 0;
    }
    else
    {
        const double distanceCents = std::abs(observedLog2 - state.pitchCentreLog2) * 1200.0;
        const double scaleStep = localDegreeStepCents(
            state.targetLog2, observedLog2);
        const double maximumWithinNoteTolerance = std::clamp(
            0.42 * scaleStep, 0.5, 60.0);
        const double withinNoteTolerance = std::min(
            22.0 + 38.0 * static_cast<double>(humanize),
            maximumWithinNoteTolerance);
        double baseAlpha = distanceCents > 95.0 ? 0.30 : 0.07;
        const double observedDistanceFromCurrentTarget = state.targetValid
            ? std::abs(observedLog2 - state.targetLog2) * 1200.0
            : 0.0;
        const double currentIdentityRadius = 0.48 * scaleStep;
        const bool insideCurrentMusicalIdentity = !state.targetValid
            || observedDistanceFromCurrentTarget < currentIdentityRadius;

        if (state.noteBodyLatched
            && insideCurrentMusicalIdentity
            && distanceCents <= withinNoteTolerance)
        {
            baseAlpha = 0.018 + 0.035 * static_cast<double>(1.0f - humanize);
        }

        // SCALE_OWNS_IDENTITY_V1: continuity is geometric and bounded per hop.
        // No confidence/consensus gate exists, but neither can one arbitrary
        // detector coordinate jump the centre across a scale cell.
        constexpr double continuityRate = 0.90;
        const double requestedCentreStepCents = baseAlpha * continuityRate
            * (observedLog2 - state.pitchCentreLog2) * 1200.0;
        const double maximumCentreStepCents = std::clamp(
            0.12 * scaleStep, 2.0, 12.0);
        const double boundedCentreStepCents = std::clamp(
            requestedCentreStepCents,
            -maximumCentreStepCents,
             maximumCentreStepCents);
        state.pitchCentreLog2 += boundedCentreStepCents / 1200.0;
        ++state.stableObservations;

        // SCALE_OWNS_IDENTITY_V2: crossing half a cell is only a nomination,
        // never an immediate target change. Wide vibrato can cross that line on
        // every cycle. Accumulate only same-side excess beyond the half-cell;
        // returning inside or changing direction cancels the nomination.
        if (state.targetValid)
        {
            const double signedObservedCents =
                (observedLog2 - state.targetLog2) * 1200.0;
            const double normalizedDistance = signedObservedCents / scaleStep;
            const int challengerDirection = normalizedDistance > 0.0 ? 1
                : normalizedDistance < 0.0 ? -1 : 0;
            const double boundaryExcess = std::max(
                0.0, std::abs(normalizedDistance) - 0.50);

            if (boundaryExcess <= 0.0 || challengerDirection == 0)
            {
                state.identityChallengerDirection = 0;
                state.identityChallengerEvidence = 0.0;
            }
            else
            {
                if (state.identityChallengerDirection != challengerDirection)
                {
                    state.identityChallengerDirection = challengerDirection;
                    state.identityChallengerEvidence = 0.0;
                }

                const double densityGain = std::clamp(100.0 / scaleStep, 1.0, 4.0);
                state.identityChallengerEvidence += std::min(
                    1.0, boundaryExcess * densityGain);
            }

            const double absoluteObservedDistance = std::abs(signedObservedCents);
            const int nearestOctaveMultiple = static_cast<int>(std::lround(
                absoluteObservedDistance / 1200.0));
            const bool octaveAmbiguous = nearestOctaveMultiple >= 1
                && nearestOctaveMultiple <= 2
                && std::abs(absoluteObservedDistance
                    - 1200.0 * static_cast<double>(nearestOctaveMultiple)) <= 95.0;

            const double centreDistanceFromTarget =
                std::abs(state.pitchCentreLog2 - state.targetLog2) * 1200.0;
            const double deepExitRatio = scaleStep <= 50.0 ? 0.52 : 0.72;
            const bool deepCentreExit = !octaveAmbiguous
                && centreDistanceFromTarget >= deepExitRatio * scaleStep;
            const double persistentEvidenceRequired = octaveAmbiguous ? 12.0 : 6.0;
            const bool persistentBoundaryExit =
                state.identityChallengerEvidence >= persistentEvidenceRequired;

            liveIdentityBreak = deepCentreExit || persistentBoundaryExit;
            forceTargetSwitch = liveIdentityBreak;
        }
    }

    // QUANTIZER_GEOMETRY_ONLY_V2: ownership has already decided whether a
    // target change is allowed. The quantizer only nominates the nearest scale
    // degree; it has no confidence, Hold, hard-lock or pending-note semantics.
    const double targetSelectionLog2 = forceTargetSwitch
        ? observedLog2 : state.pitchCentreLog2;
    double newTarget = state.targetLog2;
    if (!state.targetValid || musicalOnset || forceTargetSwitch)
    {
        newTarget = quantizer.nearestTargetLog2(targetSelectionLog2);
        newTarget += std::round(state.pitchCentreLog2 - newTarget);
    }

    const bool firstOwnedTarget = !state.targetValid;
    const bool targetChanged = firstOwnedTarget
        || std::abs(newTarget - state.targetLog2) * 1200.0 > 0.1;
    const double targetJump = state.targetValid
        ? (newTarget - state.targetLog2) * 1200.0 : 0.0;
    const double identityThreshold = std::clamp(
        0.18 * static_cast<double>(quantizer.minimumStepCents()), 0.5, 30.0);
    const bool targetIdentityChanged = state.targetValid
        && std::abs(targetJump) >= identityThreshold;
    if (targetIdentityChanged || musicalOnset || liveIdentityBreak)
    {
        if (targetIdentityChanged)
        {
            // Rebase continuity state after the quantizer has accepted a real
            // note change. This prevents the old note centre from masquerading
            // as vibrato/softness around the new exact scale destination.
            state.pitchCentreLog2 = observedLog2;
            state.pitchCentreValid = true;
            state.stableObservations = 0;
            state.identityChallengerDirection = 0;
            state.identityChallengerEvidence = 0.0;
        }
        state.recentRealPitchCount = rescueBodyFrame ? 1 : 0;
        if (rescueBodyFrame)
            state.recentRealPitchLog2[0] = correctionObservedLog2;
    }
    if (targetChanged)
    {
        ++state.revision;
        state.lastTargetJumpCents = targetJump;
        if (targetIdentityChanged)
        {
            // TRANSITION_DESTINATION_FROZEN_V2: every committed note boundary
            // starts one clean monotonic trajectory. A later detector candidate
            // may be analysed, but cannot continuously rewrite this destination.
            state.trackingState = TrackingState::transition;
            state.stateAgeSamples = 0;
            state.velocityCentsPerSecond = 0.0;
            state.stableObservations = 0;
            state.stableBodyObservations = bodyPresent ? 1 : 0;
        }
    }
    state.targetLog2 = newTarget;
    state.targetValid = true;

    // Preserve the source coordinate that owned the renderer before this hop.
    // It is used only after target identity has already been resolved. Capturing
    // it here never changes detector/quantizer evidence and never reaches the
    // renderer directly.
    double preTransportSourceLog2 = correctionObservedLog2;
    if (state.transportPeriodHz > 0.0
        && std::isfinite(state.transportPeriodHz))
    {
        preTransportSourceLog2 = safeLog2(state.transportPeriodHz);
    }

    // LOCAL_TRAJECTORY_V1: the audible source coordinate is a persistent
    // local trajectory, not raw F0 and not a slow detector average. Continuous
    // within-note motion (including real vibrato that must be removed at zero
    // Vibrato) is followed rapidly when the innovation is physically small.
    // Large jumps have exactly zero transport authority until scale identity has
    // committed; a committed note change then rebases to the accepted live F0
    // in one supervisor event so target and source jump coherently.
    const bool freezeCommittedTransition =
        state.trackingState == TrackingState::transition && !targetIdentityChanged;
    const bool transportBodyPresent = bodyPresent || (identityOnlyVeto && validPitch);
    if (state.noteBodyLatched && transportBodyPresent && !freezeCommittedTransition)
    {
        const double observedSourceLog2 = correctionObservedLog2;
        if (!(state.transportPeriodHz > 0.0)
            || !std::isfinite(state.transportPeriodHz)
            || firstOwnedTarget)
        {
            state.transportPeriodHz = std::exp2(observedSourceLog2);
            state.transportVelocityCentsPerHop = 0.0;
            state.transportChallengerHops = 0;
            state.transportChallengerLog2 = 0.0;
        }
        else if (targetIdentityChanged)
        {
            // The challenger has already passed persistence + quantizer Hold.
            // Rebase source and target together; do not spend transition time
            // dragging an obsolete old-note source coordinate toward the new F0.
            state.transportPeriodHz = std::exp2(observedSourceLog2);
            state.transportVelocityCentsPerHop = 0.0;
            state.transportChallengerHops = 0;
            state.transportChallengerLog2 = 0.0;
        }
        else
        {
            const double currentSourceLog2 = safeLog2(state.transportPeriodHz);
            const double predictedSourceLog2 = currentSourceLog2
                + state.transportVelocityCentsPerHop / 1200.0;
            const double innovationCents =
                (observedSourceLog2 - predictedSourceLog2) * 1200.0;
            const double localScaleStep = localDegreeStepCents(
                state.targetLog2, observedSourceLog2);
            // DEGREE_RELATIVE_TRANSPORT_GATE_V1: no fixed 8-cent floor. Dense
            // scales must see a correspondingly small local-motion gate, while
            // wide diatonic gaps may follow ordinary sung vibrato continuously.
            const double innovationGateCents = std::max(
                0.5, 0.28 * localScaleStep);

            if (std::abs(innovationCents) <= innovationGateCents)
            {
                state.transportChallengerHops = 0;
                state.transportChallengerLog2 = 0.0;
                constexpr double innovationFollow = 0.88;
                double nextSourceLog2 = predictedSourceLog2
                    + innovationFollow * innovationCents / 1200.0;
                double stepCents =
                    (nextSourceLog2 - currentSourceLog2) * 1200.0;
                const double maximumContinuousStep = std::clamp(
                    0.16 * localScaleStep, 5.0, 16.0);
                stepCents = std::clamp(stepCents,
                                       -maximumContinuousStep,
                                        maximumContinuousStep);
                nextSourceLog2 = currentSourceLog2 + stepCents / 1200.0;
                state.transportPeriodHz = std::exp2(nextSourceLog2);
                state.transportVelocityCentsPerHop = std::clamp(
                    0.52 * state.transportVelocityCentsPerHop
                        + 0.48 * stepCents,
                    -maximumContinuousStep, maximumContinuousStep);
            }
            else
            {
                const double absoluteInnovation = std::abs(innovationCents);
                const int nearestOctave = static_cast<int>(std::lround(
                    absoluteInnovation / 1200.0));
                const bool octaveLikeInnovation = nearestOctave >= 1
                    && nearestOctave <= 2
                    && std::abs(absoluteInnovation
                        - 1200.0 * static_cast<double>(nearestOctave)) <= 95.0;

                if (octaveLikeInnovation)
                {
                    state.transportChallengerHops = 0;
                    state.transportChallengerLog2 = 0.0;
                    state.transportVelocityCentsPerHop *= 0.35;
                }
                else
                {
                    const bool sameChallenger = state.transportChallengerHops > 0
                        && std::abs(observedSourceLog2
                            - state.transportChallengerLog2) * 1200.0 <= 24.0;
                    if (sameChallenger)
                    {
                        state.transportChallengerLog2 = 0.70
                            * state.transportChallengerLog2
                            + 0.30 * observedSourceLog2;
                        state.transportChallengerHops = std::min(
                            12, state.transportChallengerHops + 1);
                    }
                    else
                    {
                        state.transportChallengerLog2 = observedSourceLog2;
                        state.transportChallengerHops = 1;
                    }

                    // UNCOMMITTED_LARGE_INNOVATION_ZERO_TRANSPORT_AUTHORITY_V1:
                    // persistence may inform analysis, but it cannot move the
                    // audible source coordinate. A real new note is handled by
                    // the target-identity commit above, which rebases target and
                    // transport atomically. This removes the old 3-hop catch-up
                    // path that could turn a repeated detector error into a
                    // 100-300 cent audible excursion on a sustained vowel.
                    state.transportVelocityCentsPerHop *= 0.35;
                }
            }
        }
    }

    // RENDERER_STABLE_HOP_AUTHORITY_V1
    // Keep the measurement and controller state, but deny this hop direct
    // renderer authority when the existing supervisor has already classified
    // it as an uncommitted tail/outlier. A committed note change is explicitly
    // allowed so Response can travel normally toward the new stable endpoint.
    const bool uncommittedLargeInnovation =
        state.trackingState == TrackingState::stable
        && state.transportChallengerHops > 0
        && !targetIdentityChanged;

    const auto ownershipDecision = ObservationOwnershipPolicy::evaluate({
        terminalTailIdentityVeto,
        provisionalOctaveIdentityVeto,
        uncommittedLargeInnovation
    });
    state.rendererAcceptCurrentHop =
        ownershipDecision.acceptCurrentObservation;

    const double audibleSourceLog2 = state.transportPeriodHz > 0.0
        && std::isfinite(state.transportPeriodHz)
        ? safeLog2(state.transportPeriodHz)
        : correctionObservedLog2;

    const double vibratoComponent = observedLog2 - state.pitchCentreLog2;
    const float stable = clamp01(0.45f * observation.confidence
                               + 0.35f * observation.consensus
                               + 0.20f * std::min(1.0f,
                                   static_cast<float>(state.stableObservations) / 5.0f));
    const float periodic = clamp01(observation.periodicity);
    // TARGET_LOCAL_SOFTNESS_GEOMETRY_V1: explicit Amount/Humanize/
    // Vibrato softness is bounded inside the actual local target cell. Response
    // is intentionally absent here: it controls convergence time only and can
    // never weaken the destination.
    const double minimumStep = localDegreeStepCents(
        state.targetLog2, audibleSourceLog2);
    const double halfStep = 0.5 * minimumStep;
    const double centreError = std::abs((state.targetLog2 - state.pitchCentreLog2) * 1200.0);
    const float boundarySafety = 1.0f - smoothStep(
        static_cast<float>(0.58 * halfStep),
        static_cast<float>(0.92 * halfStep),
        static_cast<float>(centreError));

    // SINGLE_VISIBLE_VIBRATO_AUTHORITY_V1: the visible Vibrato Preserve
    // control is authoritative in every mode. No hidden non-lock preserve path.
    const float requestedVibrato = clamp01(parameters.vibratoPreserve);
    const float preserve = requestedVibrato * stable * periodic * boundarySafety;

    // SCALE_CELL_OWNS_SOFTNESS_V1: every setting uses the same target-owned
    // law. Softer controls enlarge a bounded cage around the selected degree;
    // they never multiply correction toward unity and never create a dry zone.
    const double amount = static_cast<double>(clamp01(parameters.amount));

    // HARD_TUNE_STABLE_MICROMOTION_COMPENSATION_V1
    // Extreme correction settings mean the user is explicitly asking the
    // already-owned scale degree to remain rigid. Keep detector/transport fully
    // live, but remove Response lag from small within-note source motion by
    // preserving the controller's pre-existing desired-current error. The
    // authority fades continuously to zero before ordinary Humanize/Vibrato
    // settings, so this is not a second correction law or a hidden dry zone.
    const float hardAmountAuthority = smoothStep(
        0.90f, 0.99f, static_cast<float>(amount));
    const float lowHumanizeAuthority = 1.0f - smoothStep(
        0.04f, 0.16f, humanize);
    const float lowVibratoAuthority = 1.0f - smoothStep(
        0.03f, 0.18f, requestedVibrato);
    const float stableMicroMotionAuthority = clamp01(
        hardAmountAuthority
        * lowHumanizeAuthority
        * lowVibratoAuthority
        * boundarySafety);

    const double softness = std::clamp(
        0.72 * (1.0 - amount) + 0.20 * static_cast<double>(humanize),
        0.0, 0.88);
    // SCALE_LOCK_NEVER_OWNS_DEPTH_V1: Scale Lock may alter target retention
    // and trajectory timing, never correction depth. Amount/Humanize/Vibrato
    // soften one common target-owned cage in every mode. The common budget is
    // the former normal-mode budget so visible Humanize keeps its full range;
    // at Amount=1, Humanize=0, Vibrato=0 the residual is still exactly zero.
    constexpr double cageFraction = 0.34;
    constexpr double cageLimit = 42.0;
    const double residualBudgetCents = std::clamp(
        minimumStep * cageFraction, 0.25, cageLimit);

    // SOURCE_COORDINATE_REBASE_V1: the dry is only the coordinate from which
    // transport is measured. The latest accepted live F0 is used in every mode;
    // no softer branch is allowed to fall back to a dry-owned reference.
    const double sourceOffsetCents =
        (audibleSourceLog2 - state.targetLog2) * 1200.0;
    const double targetOwnedSourceResidual = residualBudgetCents > 1.0e-9
        ? residualBudgetCents
            * std::tanh(sourceOffsetCents / residualBudgetCents)
            * softness
        : 0.0;
    const double requestedVibratoCents =
        static_cast<double>(preserve) * vibratoComponent * 1200.0;
    const double vibratoBudgetCents = residualBudgetCents
        * static_cast<double>(requestedVibrato);
    const double targetOwnedOffsetCents = std::clamp(
        targetOwnedSourceResidual
            + std::clamp(requestedVibratoCents,
                         -vibratoBudgetCents,
                         vibratoBudgetCents),
        -residualBudgetCents,
        residualBudgetCents);
    const double correctedLog2 = state.targetLog2
        + targetOwnedOffsetCents / 1200.0;

    double errorCents = (correctedLog2 - audibleSourceLog2) * 1200.0;
    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    errorCents = std::clamp(errorCents, -maximumCents, maximumCents);

    float stableTransportCompensation = 0.0f;
    if (terminalTailStableCompensation)
    {
        // Preserve the existing terminal-tail behaviour exactly.
        stableTransportCompensation = 1.0f;
    }
    else if (state.trackingState == TrackingState::stable
             && state.noteBodyLatched
             && validPitch
             && !musicalOnset
             && !targetIdentityChanged
             && stableMicroMotionAuthority > 0.0f)
    {
        const double transportDeltaCents =
            (audibleSourceLog2 - preTransportSourceLog2) * 1200.0;
        const double microMotionLimitCents = std::clamp(
            0.18 * minimumStep, 4.0, 18.0);
        if (std::isfinite(transportDeltaCents)
            && std::abs(transportDeltaCents) <= microMotionLimitCents)
        {
            // RESPONSE_AND_COMPENSATION_COMPETE_V1
            // Stable hard-tune micro-motion may pre-compensate most of the
            // source motion, but never 100% of it. Response always retains a
            // real share of the trajectory, so a monotonic within-cell move
            // still differentiates fast and slow settings instead of being
            // silently promoted to an instantaneous correction.
            constexpr float maximumImmediateCompensation = 0.85f;
            stableTransportCompensation = maximumImmediateCompensation
                * stableMicroMotionAuthority;
        }
    }

    if (stableTransportCompensation > 0.0f
        && state.trackingState == TrackingState::stable
        && !targetIdentityChanged)
    {
        // Re-evaluate only the same target-owned law at the previous transport
        // coordinate. The difference to the new desired value is therefore the
        // correction delta caused by source transport, not a target/Response
        // change. At hard-tune authority=1 this keeps the existing Response
        // error unchanged while cancelling stable vibrato immediately.
        const double priorSourceOffsetCents =
            (preTransportSourceLog2 - state.targetLog2) * 1200.0;
        const double priorTargetOwnedSourceResidual =
            residualBudgetCents > 1.0e-9
            ? residualBudgetCents
                * std::tanh(priorSourceOffsetCents / residualBudgetCents)
                * softness
            : 0.0;
        const double currentVibratoOffsetCents = std::clamp(
            requestedVibratoCents,
            -vibratoBudgetCents,
            vibratoBudgetCents);
        const double priorTargetOwnedOffsetCents = std::clamp(
            priorTargetOwnedSourceResidual + currentVibratoOffsetCents,
            -residualBudgetCents,
            residualBudgetCents);
        const double priorCorrectedLog2 = state.targetLog2
            + priorTargetOwnedOffsetCents / 1200.0;
        const double priorDesiredAtCurrentControls = std::clamp(
            (priorCorrectedLog2 - preTransportSourceLog2) * 1200.0,
            -maximumCents,
            maximumCents);
        const double transportCorrectionDelta =
            errorCents - priorDesiredAtCurrentControls;

        if (std::isfinite(transportCorrectionDelta))
        {
            state.currentCents = std::clamp(
                state.currentCents
                    + static_cast<double>(stableTransportCompensation)
                        * transportCorrectionDelta,
                -maximumCents,
                maximumCents);
        }
    }

    state.desiredCents = errorCents;
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);
    if (firstOwnedTarget)
    {
        // NO_UNITY_ESCAPE_V1: first acquisition starts already owned by scale;
        // Response may shape later movement, never reveal an initial dry ramp.
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
    }

    if (!musicalOnset)
    {
        const int minimumStableSamples = static_cast<int>(std::lround(
            0.012 * sampleRate_));
        if (state.trackingState == TrackingState::transition)
        {
            if (!targetIdentityChanged && bodyPresent
                && state.stableBodyObservations >= 4
                && state.stateAgeSamples >= minimumStableSamples
                && std::abs(state.desiredCents - state.currentCents) < 0.5)
            {
                setState(TrackingState::stable);
            }
        }
        else if (state.trackingState == TrackingState::attack
                 || state.trackingState == TrackingState::acquire)
        {
            if (state.noteBodyLatched && bodyPresent
                && state.stableBodyObservations >= 4
                && state.stateAgeSamples >= minimumStableSamples)
            {
                setState(TrackingState::stable);
            }
        }
    }

    // Quantizer pending-state no longer exists; preserve established meter semantics.
    meterPendingOctave_.store(0, std::memory_order_relaxed);
    meterOctaveState_.store(observation.octaveState, std::memory_order_relaxed);
}

double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept
{
    if (!state.targetValid)
        return 0.0;

    if (state.stateAgeSamples < std::numeric_limits<int>::max())
        ++state.stateAgeSamples;

    // AUTHORITY_CONTROLS_EXPLICIT_V1: a literal zero Response contains no hidden
    // 0.35 ms controller floor. The same single wet renderer receives the new
    // ratio immediately; there is no dry transition or secondary synthesis path.
    if (state.responseMs <= 0.00001)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
        if (state.trackingState == TrackingState::transition)
        {
            state.trackingState = TrackingState::stable;
            state.stateAgeSamples = 0;
        }
        return state.currentCents;
    }

    // Transition describes a note boundary, never convergence of a second-order
    // controller. A singing note must not remain in transition for seconds just
    // because vibrato keeps the destination moving.
    const int maximumTransitionSamples = static_cast<int>(std::lround(0.120 * sampleRate_));
    if (state.trackingState == TrackingState::transition
        && state.noteBodyLatched
        && state.stateAgeSamples >= maximumTransitionSamples)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
        state.trackingState = TrackingState::stable;
        state.stateAgeSamples = 0;
    }

    const double dt = 1.0 / sampleRate_;
    const double responseSeconds = std::max(0.00035, state.responseMs * 0.001);

    if (state.trackingState == TrackingState::transition)
    {
        // TRANSITION_IS_TRANSPORT_V1: one monotonic trajectory owns voiced,
        // aperiodic and transient material alike. Detector holes only stop new
        // observations; they never interrupt this glide or expose unity/dry.
        const double delta = state.desiredCents - state.currentCents;
        const double alpha = std::clamp(
            1.0 - std::exp(-4.6 * dt / responseSeconds), 0.0, 1.0);
        double step = alpha * delta;
        if (std::abs(step) > std::abs(delta))
            step = delta;
        state.currentCents += step;
        state.velocityCentsPerSecond = step / dt;
        if (std::abs(state.desiredCents - state.currentCents) < 0.001)
        {
            state.currentCents = state.desiredCents;
            state.velocityCentsPerSecond = 0.0;
            state.trackingState = TrackingState::stable;
            state.stateAgeSamples = 0;
        }
        return state.currentCents;
    }

    const double omega = std::min(0.22 / dt, 4.6 / responseSeconds);
    double acceleration = omega * omega
        * (state.desiredCents - state.currentCents)
        - 2.0 * omega * state.velocityCentsPerSecond;
    const double maximumVelocity = std::max(3600.0,
        10.0 * std::max(120.0, std::abs(state.desiredCents)) / responseSeconds);
    const double maximumAcceleration = maximumVelocity
        / std::max(0.0005, responseSeconds * 0.30);
    acceleration = std::clamp(acceleration,
                              -maximumAcceleration,
                              maximumAcceleration);
    state.velocityCentsPerSecond += acceleration * dt;
    state.velocityCentsPerSecond = std::clamp(state.velocityCentsPerSecond,
                                              -maximumVelocity,
                                              maximumVelocity);
    state.currentCents += state.velocityCentsPerSecond * dt;
    if (std::abs(state.desiredCents - state.currentCents) < 0.001
        && std::abs(state.velocityCentsPerSecond) < 0.02)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
    }
    return state.currentCents;
}

double ModernPitchEngine::selectRendererCorrection(
    CorrectionState& state,
    double controllerCents) noexcept
{
    const double safeController = std::isfinite(controllerCents)
        ? controllerCents : 0.0;

    // No previous stable audible command exists at startup, so seed from the
    // controller once. Thereafter a rejected hop cannot overwrite that command.
    if (!state.rendererCommandValid)
    {
        state.rendererCommandCents = safeController;
        state.rendererCommandValid = true;
        return state.rendererCommandCents;
    }

    if (state.rendererAcceptCurrentHop)
        state.rendererCommandCents = safeController;

    return state.rendererCommandCents;
}

void ModernPitchEngine::process(
    juce::AudioBuffer<float>& buffer,
    const double* scaleRatios,
    int numberOfScaleRatios,
    double rootFrequency,
    const Parameters& parameters)
{
    process(buffer, scaleRatios, numberOfScaleRatios, rootFrequency,
            parameters, CreativeTempo::HostPosition {});
}

void ModernPitchEngine::process(
    juce::AudioBuffer<float>& buffer,
    const double* scaleRatios,
    int numberOfScaleRatios,
    double rootFrequency,
    const Parameters& parameters,
    const CreativeTempo::HostPosition& hostTempoPosition,
    std::uint64_t scaleGeneration)
{
    Parameters safe = parameters;
    safe.amount = clamp01(finiteOr(safe.amount, 1.0f));
    safe.retuneTimeMs = std::clamp(finiteOr(safe.retuneTimeMs, 50.0f), 0.0f, 500.0f);
    safe.transitionTimeMs = std::clamp(finiteOr(safe.transitionTimeMs, 35.0f), 0.0f, 2000.0f);
    safe.humanize = clamp01(finiteOr(safe.humanize, 0.20f));
    safe.formantPreservation = clamp01(finiteOr(safe.formantPreservation, 0.90f));
    safe.detectorSensitivity = clamp01(finiteOr(safe.detectorSensitivity, 0.70f));
    safe.voiceHarmonicity = clamp01(finiteOr(safe.voiceHarmonicity, 0.0f));
    safe.voiceBreathiness = clamp01(finiteOr(safe.voiceBreathiness, 0.0f));
    safe.voiceBodyEnergy = clamp01(finiteOr(safe.voiceBodyEnergy, 0.0f));
    safe.voiceSpectralReliability = clamp01(finiteOr(safe.voiceSpectralReliability, 0.0f));
    safe.voiceEventStrength = clamp01(finiteOr(safe.voiceEventStrength, 0.0f));
    safe.voiceFormantStability = clamp01(finiteOr(safe.voiceFormantStability, 0.0f));
    safe.voiceLowerFamilyEvidence = clamp01(finiteOr(safe.voiceLowerFamilyEvidence, 0.0f));
    safe.lockHysteresis = std::clamp(finiteOr(safe.lockHysteresis, 24.0f), 0.0f, 80.0f);
    safe.vibratoPreserve = clamp01(finiteOr(safe.vibratoPreserve, 0.0f));
    safe.minimumPitchHz = std::clamp(finiteOr(safe.minimumPitchHz, 45.0f), 25.0f, 500.0f);
    safe.maximumPitchHz = std::clamp(finiteOr(safe.maximumPitchHz, 1600.0f),
                                     safe.minimumPitchHz + 20.0f, 3000.0f);

    const int channels = std::min({buffer.getNumChannels(), channelCount_, maxSupportedChannels});
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0)
        return;

    // LINKED_ONLY_PRODUCT_PATH_V1: one detector/ownership/trajectory family
    // drives every channel. Rendering remains channel-local.
    linkedQuantizer_.setScale(scaleRatios, numberOfScaleRatios,
                              rootFrequency, scaleGeneration);
    linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);
    linkedTracker_.setSensitivity(safe.detectorSensitivity);
    linkedTracker_.setVoiceAuthorityContext(
        safe.voiceEvidenceValid,
        safe.voiceHarmonicity,
        safe.voiceBreathiness,
        safe.voiceBodyEnergy,
        safe.voiceSpectralReliability,
        safe.voiceEventStrength,
        safe.voiceFormantStability,
        safe.voiceLowerFamilyEvidence);

    tempoController_.beginBlock(hostTempoPosition, safe.tempo, samples);
    if (safe.tempo.mode != CreativeTempo::Mode::off)
        safe.transitionTimeMs = tempoController_.getGlideTimeMs();
    std::array<float*, maxSupportedChannels> data {};
    for (int channel = 0; channel < channels; ++channel)
    {
        data[static_cast<std::size_t>(channel)] = buffer.getWritePointer(channel);
        for (int sample = 0; sample < samples; ++sample)
            data[static_cast<std::size_t>(channel)][sample]
                = sanitiseAudioSample(data[static_cast<std::size_t>(channel)][sample]);
    }

    // SOUND_EQUALS_CORRECTION_V1: linked pitch analysis must not average L+R.
    // Anti-phase or side-heavy vocals can cancel in that sum and make a clearly
    // audible signal look pitchless. Choose one coherent, highest-energy input
    // channel for this block; this changes analysis authority only, never audio.
    int linkedAnalysisChannel = 0;
    if (channels > 1)
    {
        double bestEnergy = -1.0;
        for (int channel = 0; channel < channels; ++channel)
        {
            double energy = 0.0;
            const float* channelData = data[static_cast<std::size_t>(channel)];
            for (int sample = 0; sample < samples; ++sample)
            {
                const double value = static_cast<double>(channelData[sample]);
                energy += value * value;
            }
            if (energy > bestEnergy)
            {
                bestEnergy = energy;
                linkedAnalysisChannel = channel;
            }
        }
    }

    for (int sample = 0; sample < samples; ++sample)
    {
        const float analysis = data[static_cast<std::size_t>(linkedAnalysisChannel)][sample];
        PitchObservation observation;
        if (linkedCorrection_.noteBodyLatched && linkedCorrection_.transportPeriodHz > 0.0)
            linkedTracker_.setReacquisitionAnchor(
                static_cast<float>(linkedCorrection_.transportPeriodHz));
        else
            linkedTracker_.clearReacquisitionAnchor();
        const bool rescueSearch = linkedCorrection_.noteBodyLatched
            && linkedCorrection_.pitchStaleSamples >= static_cast<int>(0.060 * sampleRate_);
        const bool detectorWake = linkedCorrection_.trackingState == TrackingState::transition
            || (linkedCorrection_.trackingState == TrackingState::acquire
                && linkedCorrection_.stateAgeSamples >= static_cast<int>(0.060 * sampleRate_));
        linkedTracker_.setTransitionWake(detectorWake);
        linkedTracker_.setRange(rescueSearch ? std::min(safe.minimumPitchHz, 28.0f) : safe.minimumPitchHz,
                                safe.maximumPitchHz);
        linkedTracker_.setSensitivity(rescueSearch ? std::max(safe.detectorSensitivity, 0.98f)
                                                   : safe.detectorSensitivity);
        linkedTracker_.setRescueMode(rescueSearch); // PITCH_RESCUE_V1
        if (linkedTracker_.processSample(analysis, observation))
        {
            latestObservation_ = observation;
            updateCorrectionState(linkedCorrection_, linkedQuantizer_, observation, safe);
        }

        const double controllerCents = advanceCorrection(linkedCorrection_);
        const auto decision = tempoController_.processSample(
            controllerCents,
            linkedCorrection_.desiredCents,
            linkedCorrection_.revision,
            latestObservation_.onsetStrength,
            linkedCorrection_.targetValid,
            sample,
            safe.tempo,
            static_cast<float>(linkedCorrection_.responseMs));
        if (decision.waitingForGrid)
        {
            linkedCorrection_.currentCents = decision.controllerCents;
            linkedCorrection_.velocityCentsPerSecond = 0.0;
        }
        audibleCorrectionCents_ = selectRendererCorrection(
            linkedCorrection_, decision.controllerCents);
        for (int channel = 0; channel < channels; ++channel)
        {
            const float rendered =
                wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                    data[static_cast<std::size_t>(channel)][sample], audibleCorrectionCents_,
                    safe.formantPreservation);
            // NO_AUDIO_DROPOUT_ON_UNCERTAINTY_V1
            data[static_cast<std::size_t>(channel)][sample] = rendered;
        }
        if (linkedCorrection_.noteBodyLatched
            && linkedCorrection_.trackingState != TrackingState::unvoiced)
        {
            ++sustainedSamples_;
        }
        else
        {
            sustainedSamples_ = 0;
        }
    }

    const auto tempoMeter = tempoController_.getMetering();
    publishMetering(latestObservation_, linkedCorrection_,
                    audibleCorrectionCents_, tempoMeter);
}

void ModernPitchEngine::process(
    juce::AudioBuffer<float>& buffer,
    const std::vector<double>& scaleRatios,
    double rootFrequency,
    const Parameters& parameters)
{
    process(buffer,
            scaleRatios.empty() ? nullptr : scaleRatios.data(),
            static_cast<int>(scaleRatios.size()),
            rootFrequency,
            parameters);
}

void ModernPitchEngine::process(
    float* monoData,
    int numberOfSamples,
    const std::vector<double>& scaleRatios,
    double rootFrequency,
    const Parameters& parameters)
{
    if (monoData == nullptr || numberOfSamples <= 0)
        return;
    float* channels[] { monoData };
    juce::AudioBuffer<float> view(channels, 1, numberOfSamples);
    process(view, scaleRatios, rootFrequency, parameters);
}

void ModernPitchEngine::processBypassed(juce::AudioBuffer<float>& buffer)
{
    const int channels = std::min({buffer.getNumChannels(), channelCount_, maxSupportedChannels});
    const int samples = buffer.getNumSamples();
    for (int channel = 0; channel < channels; ++channel)
    {
        float* data = buffer.getWritePointer(channel);
        auto& renderer = wetRenderers_[static_cast<std::size_t>(channel)];
        for (int sample = 0; sample < samples; ++sample)
            data[sample] = renderer.processBypassedSample(data[sample]);
    }
}

void ModernPitchEngine::publishMetering(
    const PitchObservation& observation,
    const CorrectionState& state,
    double audibleCents,
    const CreativeTempo::Metering& tempoMeter) noexcept
{
    meterSequence_.fetch_add(1u, std::memory_order_acq_rel);
    meterPitchHz_.store(observation.frequencyHz, std::memory_order_relaxed);
    meterTargetHz_.store(state.targetValid
        ? static_cast<float>(std::exp2(state.targetLog2)) : 0.0f,
        std::memory_order_relaxed);
    meterConfidence_.store(observation.confidence, std::memory_order_relaxed);
    meterVoicing_.store(observation.voicing, std::memory_order_relaxed);
    meterPeriodicity_.store(observation.periodicity, std::memory_order_relaxed);
    meterConsensus_.store(observation.consensus, std::memory_order_relaxed);
    meterCorrectionCents_.store(static_cast<float>(audibleCents),
                                std::memory_order_relaxed);
    meterCorrectionVelocity_.store(static_cast<float>(state.velocityCentsPerSecond),
                                   std::memory_order_relaxed);
    meterTargetJumpCents_.store(static_cast<float>(state.lastTargetJumpCents),
                                std::memory_order_relaxed);
    meterSustainedSeconds_.store(static_cast<float>(
        std::min(12.0, static_cast<double>(sustainedSamples_) / sampleRate_)),
        std::memory_order_relaxed);
    meterDetectorSupport_.store(observation.detectorSupport, std::memory_order_relaxed);
    meterOctaveState_.store(observation.octaveState, std::memory_order_relaxed);
    meterTrackingState_.store(static_cast<int>(state.trackingState),
                              std::memory_order_relaxed);
    meterTempoBpm_.store(tempoMeter.bpm, std::memory_order_relaxed);
    meterTempoGridPhase_.store(tempoMeter.gridPhase, std::memory_order_relaxed);
    meterTempoGlideTimeMs_.store(tempoMeter.glideTimeMs, std::memory_order_relaxed);
    meterTempoActive_.store(tempoMeter.active, std::memory_order_relaxed);
    meterTempoWaiting_.store(tempoMeter.waitingForGrid, std::memory_order_relaxed);
    meterTempoHostSync_.store(tempoMeter.hostSyncValid, std::memory_order_relaxed);
    meterTempoMode_.store(static_cast<int>(tempoMeter.mode), std::memory_order_relaxed);
    meterSequence_.fetch_add(1u, std::memory_order_release);
}

ModernPitchEngine::Metering ModernPitchEngine::getMetering() const noexcept
{
    Metering result;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto before = meterSequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0u)
            continue;
        result.detectedPitchHz = meterPitchHz_.load(std::memory_order_relaxed);
        result.targetPitchHz = meterTargetHz_.load(std::memory_order_relaxed);
        result.confidence = meterConfidence_.load(std::memory_order_relaxed);
        result.voicing = meterVoicing_.load(std::memory_order_relaxed);
        result.harmonicity = meterPeriodicity_.load(std::memory_order_relaxed);
        result.breathiness = 1.0f - result.harmonicity;
        result.noisePath = result.breathiness;
        result.spectralReliability = clamp01(0.55f * result.confidence
                                           + 0.45f * result.harmonicity);
        result.maskStability = result.harmonicity;
        result.sustainedNoteSeconds = meterSustainedSeconds_.load(std::memory_order_relaxed);
        result.consensus = meterConsensus_.load(std::memory_order_relaxed);
        result.correctionCents = meterCorrectionCents_.load(std::memory_order_relaxed);
        result.wetMix = 1.0f;
        result.outputSourceCorrespondence = 100.0f * result.spectralReliability;
        result.outputTargetCoherence = 100.0f * result.confidence;
        result.outputPhysicalHarmonicFit = 100.0f * result.harmonicity;
        result.outputTemporalStability = 100.0f * result.maskStability;
        result.outputTargetJumpCents = meterTargetJumpCents_.load(std::memory_order_relaxed);
        result.outputCorrectionVelocityCentsPerSecond
            = meterCorrectionVelocity_.load(std::memory_order_relaxed);
        result.detectorSupport = meterDetectorSupport_.load(std::memory_order_relaxed);
        result.octaveState = meterOctaveState_.load(std::memory_order_relaxed);
        result.pendingOctaveObservations = meterPendingOctave_.load(std::memory_order_relaxed);
        result.state = static_cast<TrackingState>(
            meterTrackingState_.load(std::memory_order_relaxed));
        result.tempoBpm = meterTempoBpm_.load(std::memory_order_relaxed);
        result.tempoGridPhase = meterTempoGridPhase_.load(std::memory_order_relaxed);
        result.tempoGlideTimeMs = meterTempoGlideTimeMs_.load(std::memory_order_relaxed);
        result.tempoActive = meterTempoActive_.load(std::memory_order_relaxed);
        result.tempoWaitingForGrid = meterTempoWaiting_.load(std::memory_order_relaxed);
        result.tempoHostSyncValid = meterTempoHostSync_.load(std::memory_order_relaxed);
        result.tempoMode = static_cast<CreativeTempo::Mode>(
            meterTempoMode_.load(std::memory_order_relaxed));
        const auto after = meterSequence_.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u)
            return result;
    }
    return result;
}
