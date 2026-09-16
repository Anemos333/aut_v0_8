#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

#include <algorithm>
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

struct Profile
{
    double h1, h2, h3, h4;
};
constexpr Profile harmonic4 { 1.0, 0.44, 0.23, 0.12 };
constexpr Profile legacy { 1.0, 0.34, 0.18, 0.0 };

double rms(const std::vector<float>& x)
{
    double e = 0.0;
    for (float v : x) e += static_cast<double>(v) * v;
    return x.empty() ? 0.0 : std::sqrt(e / static_cast<double>(x.size()));
}

void scaleRms(std::vector<float>& x, double target)
{
    const double r = rms(x);
    if (!(r > 0.0)) return;
    const double g = target / r;
    for (float& v : x) v = static_cast<float>(static_cast<double>(v) * g);
}

std::vector<float> tone(double hz, int n, const Profile& p, double dbfs)
{
    std::vector<float> out(static_cast<std::size_t>(n), 0.0f);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * hz / sr;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
        out[static_cast<std::size_t>(i)] = static_cast<float>(
            p.h1 * std::sin(phase)
            + p.h2 * std::sin(2.0 * phase + 0.17)
            + p.h3 * std::sin(3.0 * phase + 0.41)
            + p.h4 * std::sin(4.0 * phase + 0.73));
    }
    scaleRms(out, std::pow(10.0, dbfs / 20.0));
    return out;
}

std::vector<float> noisy(std::vector<float> voice, double snrDb, std::uint32_t seed)
{
    std::vector<float> noise(voice.size(), 0.0f);
    Rng rng { seed };
    float fast = 0.0f, slow = 0.0f;
    for (std::size_t i = 0; i < noise.size(); ++i)
    {
        const float white = rng.next();
        fast = 0.92f * fast + 0.08f * white;
        slow = 0.992f * slow + 0.008f * white;
        noise[i] = 0.52f * white + 0.31f * fast + 0.17f * slow;
    }
    scaleRms(noise, rms(voice) / std::pow(10.0, snrDb / 20.0));
    for (std::size_t i = 0; i < voice.size(); ++i) voice[i] += noise[i];
    return voice;
}

bool near(float hz, double target, double cents = 45.0)
{
    return hz > 0.0f
        && std::abs(1200.0 * std::log2(static_cast<double>(hz) / target)) <= cents;
}

const char* classify(float hz, double target)
{
    if (near(hz, target)) return "F";
    if (near(hz, target * 0.5)) return "F2";
    if (near(hz, target / 3.0)) return "F3";
    if (near(hz, target * 2.0)) return "2F";
    return "OTHER";
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

const ModernPitchEngine::MultiRatePitchTracker::CandidateSlot*
slotFor(const ModernPitchEngine::MultiRatePitchTracker& t, int path)
{
    if (path == 0) return &t.fullRateCandidate_;
    if (path == 1) return &t.halfRateCandidate_;
    if (path == 2) return &t.quarterRateCandidate_;
    if (path == 3) return &t.eighthRateCandidate_;
    return nullptr;
}

void reportFresh(const char* caseName,
                 double target,
                 int sample,
                 int path,
                 const ModernPitchEngine::MultiRatePitchTracker::PitchCandidate& c,
                 float previousHz,
                 const char* previousClass,
                 int& printBudget)
{
    const char* cls = classify(c.frequencyHz, target);
    const double jumpCents = previousHz > 0.0f
        ? 1200.0 * std::log2(static_cast<double>(c.frequencyHz) / previousHz)
        : 0.0;
    const bool changedClass = previousClass != nullptr && std::string(cls) != previousClass;
    const bool largeJump = previousHz > 0.0f && std::abs(jumpCents) > 180.0;
    const bool wrong = std::string(cls) != "F";
    if (!(wrong || changedClass || largeJump) || printBudget <= 0)
        return;

    --printBudget;
    const auto delta = [](float primitive, float selected) { return primitive - selected; };
    std::cout << std::fixed << std::setprecision(4)
              << "BASIN_EVENT case=" << caseName
              << " target=" << target
              << " sample=" << sample
              << " path=" << pathName(path)
              << " hz=" << c.frequencyHz
              << " class=" << cls
              << " jump_cents=" << jumpCents
              << " threshold_tau=" << c.diagnosticThresholdTau
              << " global_tau=" << c.diagnosticGlobalTau
              << " source_tau=" << c.diagnosticSourceTau
              << " d2_tau=" << c.diagnosticDiv2Tau
              << " d3_tau=" << c.diagnosticDiv3Tau
              << " d4_tau=" << c.diagnosticDiv4Tau
              << " d2_src_delta=" << delta(c.diagnosticDiv2SourceCorrelation, c.diagnosticSelectedSourceCorrelation)
              << " d3_src_delta=" << delta(c.diagnosticDiv3SourceCorrelation, c.diagnosticSelectedSourceCorrelation)
              << " d4_src_delta=" << delta(c.diagnosticDiv4SourceCorrelation, c.diagnosticSelectedSourceCorrelation)
              << " d2_res_delta=" << delta(c.diagnosticDiv2ResidualCorrelation, c.diagnosticSelectedResidualCorrelation)
              << " d3_res_delta=" << delta(c.diagnosticDiv3ResidualCorrelation, c.diagnosticSelectedResidualCorrelation)
              << " d4_res_delta=" << delta(c.diagnosticDiv4ResidualCorrelation, c.diagnosticSelectedResidualCorrelation)
              << " d2_yin_delta=" << delta(c.diagnosticDiv2Yin, c.diagnosticSelectedYin)
              << " d3_yin_delta=" << delta(c.diagnosticDiv3Yin, c.diagnosticSelectedYin)
              << " d4_yin_delta=" << delta(c.diagnosticDiv4Yin, c.diagnosticSelectedYin)
              << '\n';
}

void runSteady(const char* name,
               double target,
               std::uint32_t seed,
               const Profile& profile,
               int path)
{
    constexpr int n = 24000;
    auto signal = noisy(tone(target, n, profile, -42.0), 6.0, seed);
    auto t = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    t->prepare(sr);
    t->setRange(45.0f, 1600.0f);

    int valid = 0, correct = 0, half = 0, third = 0, other = 0, basinSwitches = 0;
    float previousHz = 0.0f;
    std::string previousClass;
    int budget = 32;

    for (int i = 0; i < n; ++i)
    {
        ModernPitchEngine::PitchObservation o;
        if (!t->processSample(signal[static_cast<std::size_t>(i)], o)) continue;
        const auto* slot = slotFor(*t, path);
        if (slot == nullptr || slot->ageInHops != 0 || !slot->candidate.valid) continue;
        const auto& c = slot->candidate;
        ++valid;
        const std::string cls = classify(c.frequencyHz, target);
        if (cls == "F") ++correct;
        else if (cls == "F2") ++half;
        else if (cls == "F3") ++third;
        else ++other;
        if (!previousClass.empty() && cls != previousClass) ++basinSwitches;
        reportFresh(name, target, i, path, c, previousHz,
                    previousClass.empty() ? nullptr : previousClass.c_str(), budget);
        previousHz = c.frequencyHz;
        previousClass = cls;
    }

    std::cout << "BASIN_CASE case=" << name
              << " path=" << pathName(path)
              << " valid=" << valid
              << " correct=" << correct
              << " half=" << half
              << " third=" << third
              << " other=" << other
              << " switches=" << basinSwitches
              << '\n';
}

void runMotion(const char* name, std::uint32_t seed, int path)
{
    constexpr int segment = 12000;
    auto a = tone(196.0, segment, harmonic4, -36.0);
    auto b = tone(261.6255653, segment, harmonic4, -36.0);
    a.insert(a.end(), b.begin(), b.end());
    auto signal = noisy(std::move(a), 6.0, seed);

    auto t = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    t->prepare(sr);
    t->setRange(45.0f, 1600.0f);

    int valid = 0, correct = 0, half = 0, third = 0, other = 0, switches = 0;
    float previousHz = 0.0f;
    std::string previousClass;
    int budget = 40;
    for (int i = 0; i < static_cast<int>(signal.size()); ++i)
    {
        ModernPitchEngine::PitchObservation o;
        if (!t->processSample(signal[static_cast<std::size_t>(i)], o) || i < segment) continue;
        const auto* slot = slotFor(*t, path);
        if (slot == nullptr || slot->ageInHops != 0 || !slot->candidate.valid) continue;
        const auto& c = slot->candidate;
        ++valid;
        const std::string cls = classify(c.frequencyHz, 261.6255653);
        if (cls == "F") ++correct;
        else if (cls == "F2") ++half;
        else if (cls == "F3") ++third;
        else ++other;
        if (!previousClass.empty() && cls != previousClass) ++switches;
        reportFresh(name, 261.6255653, i - segment, path, c, previousHz,
                    previousClass.empty() ? nullptr : previousClass.c_str(), budget);
        previousHz = c.frequencyHz;
        previousClass = cls;
    }
    std::cout << "BASIN_MOTION case=" << name
              << " path=" << pathName(path)
              << " valid=" << valid
              << " correct=" << correct
              << " half=" << half
              << " third=" << third
              << " other=" << other
              << " switches=" << switches
              << '\n';
}
}

int main()
{
    // Two worst 440/legacy seeds from V6.3.
    runSteady("legacy440_seed3", 440.0, 0x51f15e5du, legacy, 2);
    runSteady("legacy440_seed4", 440.0, 0xc001d00du, legacy, 2);

    // Control proving that raw low-rate ambiguity can coexist with a correct
    // output-level register decision.
    runSteady("legacy110_seed4", 110.0, 0xc001d00du, legacy, 3);

    // Exact noisy transitions that remain weak after V6.1/V6.3. Quarter is the
    // lowest path that can directly own 261.6 Hz; eighth is printed as family
    // context rather than direct-coordinate authority.
    for (std::uint32_t base : {0x01234567u, 0x9e3779b9u, 0x51f15e5du, 0xc001d00du})
    {
        const std::uint32_t seed = base ^ 0x3c6ef372u;
        runMotion("motion196_to_261_quarter", seed, 2);
        runMotion("motion196_to_261_eighth", seed, 3);
    }
    return 0;
}
