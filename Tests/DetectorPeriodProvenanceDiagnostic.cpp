#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

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
    for (float v : x) e += static_cast<double>(v) * static_cast<double>(v);
    return std::sqrt(e / static_cast<double>(std::max<std::size_t>(1, x.size())));
}

void scaleRms(std::vector<float>& x, double target)
{
    const double r = rms(x);
    if (!(r > 0.0)) return;
    const double g = target / r;
    for (float& v : x) v = static_cast<float>(static_cast<double>(v) * g);
}

bool near(float measured, double target, double cents = 45.0)
{
    return measured > 0.0f
        && std::abs(1200.0 * std::log2(static_cast<double>(measured) / target)) <= cents;
}

double rateForPath(int path)
{
    switch (path)
    {
        case 0: return sr;
        case 1: return sr * 0.5;
        case 2: return sr * 0.25;
        case 3: return sr * 0.125;
        default: return 0.0;
    }
}

const char* pathName(int path)
{
    switch (path)
    {
        case 0: return "full";
        case 1: return "half";
        case 2: return "quarter";
        case 3: return "eighth";
        default: return "unknown";
    }
}

void reportCandidate(double target,
                     const VoiceProfile& profile,
                     std::uint32_t seed,
                     int sample,
                     int path,
                     const ModernPitchEngine::MultiRatePitchTracker::PitchCandidate& c)
{
    if (!c.valid || !(c.frequencyHz > 0.0f))
        return;

    const bool correct = near(c.frequencyHz, target);
    const bool half = near(c.frequencyHz, 0.5 * target);
    const bool twice = near(c.frequencyHz, 2.0 * target);
    if (correct)
        return; // provenance output is intentionally sparse: only wrong coordinates.

    const double rate = rateForPath(path);
    const auto tauHz = [rate](int tau)
    {
        return tau > 0 ? rate / static_cast<double>(tau) : -1.0;
    };

    std::cout << std::fixed << std::setprecision(4)
              << "PERIOD_PROVENANCE"
              << " target=" << target
              << " profile=" << profile.name
              << " seed=" << seed
              << " sample=" << sample
              << " path=" << pathName(path)
              << " out_hz=" << c.frequencyHz
              << " class=" << (half ? "half" : (twice ? "double" : "other"))
              << " threshold_tau=" << c.diagnosticThresholdTau
              << " threshold_hz=" << tauHz(c.diagnosticThresholdTau)
              << " global_tau=" << c.diagnosticGlobalTau
              << " global_hz=" << tauHz(c.diagnosticGlobalTau)
              << " best_tau=" << c.diagnosticBestTau
              << " best_hz=" << tauHz(c.diagnosticBestTau)
              << " source_tau=" << c.diagnosticSourceTau
              << " source_hz=" << tauHz(c.diagnosticSourceTau)
              << " threshold_yin=" << c.diagnosticThresholdYin
              << " threshold_period=" << c.diagnosticThresholdPeriodicity
              << " threshold_family=" << c.diagnosticThresholdFamily
              << " threshold_clean=" << c.diagnosticThresholdCleanliness
              << " threshold_score=" << c.diagnosticThresholdScore
              << " sel_src_corr=" << c.diagnosticSelectedSourceCorrelation
              << " prim_src_corr=" << c.diagnosticPrimitiveSourceCorrelation
              << " sel_res_corr=" << c.diagnosticSelectedResidualCorrelation
              << " prim_res_corr=" << c.diagnosticPrimitiveResidualCorrelation
              << " sel_yin=" << c.diagnosticSelectedYin
              << " prim_yin=" << c.diagnosticPrimitiveYin
              << '\n';
}

void run(double target,
         double snrDb,
         std::uint32_t seed,
         const VoiceProfile& profile,
         int focusPath)
{
    constexpr int n = 24000;
    std::vector<float> signal(n), noise(n);
    Rng rng { seed };
    float fast = 0.0f;
    float slow = 0.0f;
    double phase = 0.0;

    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * target / sr;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
        signal[static_cast<std::size_t>(i)] = static_cast<float>(
            profile.fundamental * std::sin(phase)
            + profile.second * std::sin(2.0 * phase + 0.17)
            + profile.third * std::sin(3.0 * phase + 0.41)
            + profile.fourth * std::sin(4.0 * phase + 0.73));

        const float white = rng.next();
        fast = 0.92f * fast + 0.08f * white;
        slow = 0.992f * slow + 0.008f * white;
        noise[static_cast<std::size_t>(i)] = 0.52f * white + 0.31f * fast + 0.17f * slow;
    }

    scaleRms(signal, std::pow(10.0, -42.0 / 20.0));
    scaleRms(noise, rms(signal) / std::pow(10.0, snrDb / 20.0));
    for (int i = 0; i < n; ++i)
        signal[static_cast<std::size_t>(i)] += noise[static_cast<std::size_t>(i)];

    auto t = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    t->prepare(sr);
    t->setRange(45.0f, 1600.0f);

    int wrong = 0;
    int valid = 0;
    for (int i = 0; i < n; ++i)
    {
        ModernPitchEngine::PitchObservation o;
        if (!t->processSample(signal[static_cast<std::size_t>(i)], o))
            continue;

        const ModernPitchEngine::MultiRatePitchTracker::CandidateSlot* slot = nullptr;
        if (focusPath == 0) slot = &t->fullRateCandidate_;
        if (focusPath == 1) slot = &t->halfRateCandidate_;
        if (focusPath == 2) slot = &t->quarterRateCandidate_;
        if (focusPath == 3) slot = &t->eighthRateCandidate_;
        if (slot == nullptr || slot->ageInHops != 0 || !slot->candidate.valid)
            continue;

        ++valid;
        if (!near(slot->candidate.frequencyHz, target))
        {
            ++wrong;
            // Print the first 14 wrong fresh measurements per case. This is
            // enough to expose the mechanism without flooding CI logs.
            if (wrong <= 14)
                reportCandidate(target, profile, seed, i, focusPath, slot->candidate);
        }
    }

    std::cout << "PERIOD_CASE target=" << target
              << " profile=" << profile.name
              << " seed=" << seed
              << " path=" << pathName(focusPath)
              << " valid=" << valid
              << " wrong=" << wrong
              << '\n';
}
}

int main()
{
    // Worst measured low-note case: eighth path itself alternates 110 <-> 55.
    run(110.0, 6.0, 0xc001d00du, harmonic4, 3);
    run(110.0, 6.0, 0xc001d00du, legacy, 3);

    // Negative control: same path/range but strong second harmonic remains mostly
    // correct at 110. This guards against blindly preferring the shorter lag.
    run(110.0, 6.0, 0x01234567u, strongSecond, 3);

    // Mid/high failures: quarter can observe the true coordinate directly yet
    // intermittently reports F/2.
    run(220.0, 6.0, 0x01234567u, harmonic4, 2);
    run(440.0, 6.0, 0xc001d00du, legacy, 2);
    return 0;
}
