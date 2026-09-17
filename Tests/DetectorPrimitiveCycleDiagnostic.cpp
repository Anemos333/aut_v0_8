#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

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

struct Rng
{
    std::uint32_t s;
    float next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
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
    for (float v : x)
        e += static_cast<double>(v) * static_cast<double>(v);
    return std::sqrt(e / static_cast<double>(std::max<std::size_t>(1, x.size())));
}

void scaleRms(std::vector<float>& x, double target)
{
    const double r = rms(x);
    if (!(r > 0.0))
        return;
    const double gain = target / r;
    for (float& v : x)
        v = static_cast<float>(static_cast<double>(v) * gain);
}

double cents(double measured, double target)
{
    if (!(measured > 0.0) || !(target > 0.0))
        return 1.0e9;
    return 1200.0 * std::log2(measured / target);
}

const char* classify(double measured, double target)
{
    constexpr double tolerance = 45.0;
    if (std::abs(cents(measured, target)) <= tolerance) return "correct";
    if (std::abs(cents(measured, target * 0.5)) <= tolerance) return "half";
    if (std::abs(cents(measured, target / 3.0)) <= tolerance) return "third";
    if (std::abs(cents(measured, target * 0.25)) <= tolerance) return "quarter";
    if (std::abs(cents(measured, target * 2.0)) <= tolerance) return "double";
    return "other";
}

struct Metrics
{
    int n = 0;
    double hz = 0.0;
    double tau = 0.0;
    double selectedSource = 0.0;
    double selectedResidual = 0.0;
    double selectedYin = 0.0;
    double selectedCycle = 0.0;
    std::array<int, 3> divisorN {};
    std::array<double, 3> divisorSource {};
    std::array<double, 3> divisorResidual {};
    std::array<double, 3> divisorYin {};
    std::array<double, 3> divisorCycle {};
};

void add(Metrics& m,
         const ModernPitchEngine::MultiRatePitchTracker::PitchCandidate& c)
{
    if (!c.valid || !(c.frequencyHz > 0.0f) || c.diagnosticSelectedTau <= 0)
        return;
    ++m.n;
    m.hz += c.frequencyHz;
    m.tau += c.diagnosticSelectedTau;
    m.selectedSource += c.diagnosticSelectedSourceCorrelation;
    m.selectedResidual += c.diagnosticSelectedResidualCorrelation;
    m.selectedYin += c.diagnosticSelectedYin;
    m.selectedCycle += c.diagnosticSelectedCycleConsistency;
    for (std::size_t i = 0; i < 3; ++i)
    {
        if (c.diagnosticDivisorSourceCorrelation[i] <= -1.5f)
            continue;
        ++m.divisorN[i];
        m.divisorSource[i] += c.diagnosticDivisorSourceCorrelation[i];
        m.divisorResidual[i] += c.diagnosticDivisorResidualCorrelation[i];
        m.divisorYin[i] += c.diagnosticDivisorYin[i];
        m.divisorCycle[i] += c.diagnosticDivisorCycleConsistency[i];
    }
}

struct Buckets
{
    Metrics correct;
    Metrics half;
    Metrics third;
    Metrics quarter;
    Metrics dbl;
    Metrics other;
};

Metrics& bucket(Buckets& b, const char* name)
{
    const std::string s(name);
    if (s == "correct") return b.correct;
    if (s == "half") return b.half;
    if (s == "third") return b.third;
    if (s == "quarter") return b.quarter;
    if (s == "double") return b.dbl;
    return b.other;
}

void printMetric(double target,
                 const char* profile,
                 std::uint32_t seed,
                 const char* path,
                 const char* className,
                 const Metrics& m)
{
    if (m.n <= 0)
        return;
    const double inv = 1.0 / static_cast<double>(m.n);
    const double selSource = m.selectedSource * inv;
    const double selResidual = m.selectedResidual * inv;
    const double selYin = m.selectedYin * inv;
    const double selCycle = m.selectedCycle * inv;

    std::cout << std::fixed << std::setprecision(4)
              << "PRIMITIVE_GEOMETRY target=" << target
              << " profile=" << profile
              << " seed=" << seed
              << " path=" << path
              << " class=" << className
              << " n=" << m.n
              << " mean_hz=" << m.hz * inv
              << " mean_tau=" << m.tau * inv
              << " sel_source=" << selSource
              << " sel_residual=" << selResidual
              << " sel_yin=" << selYin
              << " sel_cycle=" << selCycle;

    for (std::size_t i = 0; i < 3; ++i)
    {
        const int divisor = static_cast<int>(i) + 2;
        if (m.divisorN[i] <= 0)
        {
            std::cout << " d" << divisor << "_n=0";
            continue;
        }
        const double dinv = 1.0 / static_cast<double>(m.divisorN[i]);
        const double source = m.divisorSource[i] * dinv;
        const double residual = m.divisorResidual[i] * dinv;
        const double yin = m.divisorYin[i] * dinv;
        const double cycle = m.divisorCycle[i] * dinv;
        std::cout << " d" << divisor << "_n=" << m.divisorN[i]
                  << " d" << divisor << "_source=" << source
                  << " d" << divisor << "_source_delta=" << source - selSource
                  << " d" << divisor << "_residual=" << residual
                  << " d" << divisor << "_residual_delta=" << residual - selResidual
                  << " d" << divisor << "_yin=" << yin
                  << " d" << divisor << "_yin_delta=" << yin - selYin
                  << " d" << divisor << "_cycle=" << cycle
                  << " d" << divisor << "_cycle_delta=" << cycle - selCycle;
    }
    std::cout << '\n';
}

void printBuckets(double target,
                  const char* profile,
                  std::uint32_t seed,
                  const char* path,
                  const Buckets& b)
{
    printMetric(target, profile, seed, path, "correct", b.correct);
    printMetric(target, profile, seed, path, "half", b.half);
    printMetric(target, profile, seed, path, "third", b.third);
    printMetric(target, profile, seed, path, "quarter", b.quarter);
    printMetric(target, profile, seed, path, "double", b.dbl);
    printMetric(target, profile, seed, path, "other", b.other);
}

void run(double targetHz,
         double snrDb,
         std::uint32_t seed,
         const VoiceProfile& profile)
{
    constexpr int n = 24000;
    std::vector<float> signal(n), noise(n);
    Rng rng { seed };
    float fast = 0.0f;
    float slow = 0.0f;
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * targetHz / sr;
        if (phase >= 2.0 * pi)
            phase -= 2.0 * pi;
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

    auto tracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    tracker->prepare(sr);
    tracker->setRange(45.0f, 1600.0f);

    Buckets quarter, eighth;
    for (int i = 0; i < n; ++i)
    {
        ModernPitchEngine::PitchObservation observation;
        if (!tracker->processSample(signal[static_cast<std::size_t>(i)], observation))
            continue;

        if (tracker->quarterRateCandidate_.ageInHops == 0)
        {
            const auto& c = tracker->quarterRateCandidate_.candidate;
            if (c.valid)
                add(bucket(quarter, classify(c.frequencyHz, targetHz)), c);
        }
        if (tracker->eighthRateCandidate_.ageInHops == 0)
        {
            const auto& c = tracker->eighthRateCandidate_.candidate;
            if (c.valid)
                add(bucket(eighth, classify(c.frequencyHz, targetHz)), c);
        }
    }

    printBuckets(targetHz, profile.name, seed, "quarter", quarter);
    printBuckets(targetHz, profile.name, seed, "eighth", eighth);
}
}

int main()
{
    const std::array<std::uint32_t, 4> seeds {
        0x01234567u, 0x9e3779b9u, 0x51f15e5du, 0xc001d00du
    };

    for (std::uint32_t seed : seeds)
    {
        run(110.0, 6.0, seed, harmonic4);
        run(110.0, 6.0, seed, legacy);
        run(110.0, 6.0, seed, strongSecond);
        run(220.0, 6.0, seed, harmonic4);
        run(220.0, 6.0, seed, legacy);
        run(440.0, 6.0, seed, harmonic4);
        run(440.0, 6.0, seed, legacy);
    }
    return 0;
}
