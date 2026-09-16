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
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return static_cast<float>(s & 0xffffu) / 32767.5f - 1.0f;
    }
};

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

double cents(float measured, double target)
{
    if (!(measured > 0.0f) || !(target > 0.0)) return 1.0e9;
    return 1200.0 * std::log2(static_cast<double>(measured) / target);
}

const char* pitchClass(float hz, double target)
{
    if (!(hz > 0.0f)) return "none";
    if (std::abs(cents(hz, target)) <= 45.0) return "F";
    if (std::abs(cents(hz, target * 0.5)) <= 45.0) return "F/2";
    if (std::abs(cents(hz, target * 2.0)) <= 45.0) return "2F";
    if (std::abs(cents(hz, target * 0.25)) <= 45.0) return "F/4";
    return "other";
}

void printCandidate(const char* name,
                    const ModernPitchEngine::MultiRatePitchTracker& t,
                    const ModernPitchEngine::MultiRatePitchTracker::CandidateSlot& slot,
                    double target)
{
    const auto& c = slot.candidate;
    const float base = c.valid ? t.candidateBaseScore(c) : 0.0f;
    const float authority = c.valid
        ? t.pathPitchAuthority(c.pathIndex, c.frequencyHz) : 0.0f;
    std::cout << ' ' << name << '=' << (c.valid ? c.frequencyHz : 0.0f)
              << ':' << pitchClass(c.frequencyHz, target)
              << ",age" << slot.ageInHops
              << ",b" << base
              << ",a" << authority
              << ",p" << c.periodicity
              << ",f" << c.harmonicFamily
              << ",c" << c.tonalCleanliness;
}

void printHypothesis(const char* name,
                     const std::array<ModernPitchEngine::MultiRatePitchTracker::ConsensusHypothesis,
                                      ModernPitchEngine::MultiRatePitchTracker::maxConsensusHypotheses>& h,
                     int count,
                     double target)
{
    int best = -1;
    double bestDistance = 1.0e9;
    const double wanted = std::string(name) == "half" ? target * 0.5
                        : (std::string(name) == "double" ? target * 2.0 : target);
    for (int i = 0; i < count; ++i)
    {
        if (!h[static_cast<std::size_t>(i)].valid) continue;
        const double d = std::abs(cents(h[static_cast<std::size_t>(i)].frequencyHz, wanted));
        if (d < bestDistance)
        {
            bestDistance = d;
            best = i;
        }
    }
    if (best < 0 || bestDistance > 100.0)
    {
        std::cout << " h_" << name << "=none";
        return;
    }
    const auto& x = h[static_cast<std::size_t>(best)];
    std::cout << " h_" << name << '=' << x.frequencyHz
              << ",ev" << x.evidenceScore
              << ",co" << x.consensus
              << ",dir" << x.directSupportCount
              << ",sup" << x.supportCount
              << ",fresh0x" << std::hex << static_cast<int>(x.freshSupportMask)
              << std::dec
              << ",fam" << x.harmonicFamily
              << ",clean" << x.tonalCleanliness;
}

void run(double targetHz, double snrDb, std::uint32_t seed, const char* label)
{
    constexpr int n = 24000;
    std::vector<float> signal(n), noise(n);
    Rng rng { seed };
    float fast = 0.0f, slow = 0.0f;
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * targetHz / sr;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
        signal[static_cast<std::size_t>(i)] = static_cast<float>(
              std::sin(phase)
            + 0.44 * std::sin(2.0 * phase + 0.17)
            + 0.23 * std::sin(3.0 * phase + 0.41)
            + 0.12 * std::sin(4.0 * phase + 0.73));

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

    int printed = 0;
    std::string previousClass;
    std::cout << "OWNERSHIP_CASE label=" << label
              << " hz=" << targetHz << " snr=" << snrDb
              << " seed=" << seed << '\n';

    for (int i = 0; i < n; ++i)
    {
        ModernPitchEngine::PitchObservation o;
        if (!t->processSample(signal[static_cast<std::size_t>(i)], o))
            continue;

        const float outHz = o.valid ? o.correctionFrequencyHz : 0.0f;
        const std::string cls = pitchClass(outHz, targetHz);
        const bool changed = cls != previousClass;
        const bool wrong = o.valid && cls != "F";
        const bool early = printed < 12;
        const bool periodicSample = o.valid && (t->analysisHopCounter_ % 16 == 0);
        if (!(early || changed || wrong || periodicSample))
        {
            previousClass = cls;
            continue;
        }
        previousClass = cls;
        if (++printed > 80) break;

        std::array<ModernPitchEngine::MultiRatePitchTracker::PitchCandidate,
                   ModernPitchEngine::MultiRatePitchTracker::detectorPathCount> candidates {};
        const int candidateCount = t->collectFreshCandidates(candidates);
        std::array<ModernPitchEngine::MultiRatePitchTracker::ConsensusHypothesis,
                   ModernPitchEngine::MultiRatePitchTracker::maxConsensusHypotheses> hypotheses {};
        const int hypothesisCount = t->buildConsensusHypotheses(candidates,
                                                                 candidateCount,
                                                                 hypotheses);

        float beamHz = 0.0f;
        float beamScore = -1000.0f;
        if (t->decoderBeam_[0].valid)
        {
            beamHz = static_cast<float>(std::exp2(t->decoderBeam_[0].logFrequency));
            beamScore = t->decoderBeam_[0].score;
        }

        std::cout << std::fixed << std::setprecision(3)
                  << "OWN hop=" << t->analysisHopCounter_
                  << " sample=" << i
                  << " out=" << outHz << ':' << cls
                  << " obs_valid=" << (o.valid ? 1 : 0)
                  << " tracked=" << t->trackedPitchHz_ << ':' << pitchClass(t->trackedPitchHz_, targetHz)
                  << " beam=" << beamHz << ':' << pitchClass(beamHz, targetHz)
                  << ",bs" << beamScore
                  << " pend=" << t->pendingOctaveDelta_
                  << ':' << t->pendingOctaveCount_
                  << ':' << t->pendingOctaveFrequencyHz_;

        printCandidate("full", *t, t->fullRateCandidate_, targetHz);
        printCandidate("half", *t, t->halfRateCandidate_, targetHz);
        printCandidate("quarter", *t, t->quarterRateCandidate_, targetHz);
        printCandidate("eighth", *t, t->eighthRateCandidate_, targetHz);
        printHypothesis("half", hypotheses, hypothesisCount, targetHz);
        printHypothesis("fund", hypotheses, hypothesisCount, targetHz);
        printHypothesis("double", hypotheses, hypothesisCount, targetHz);
        std::cout << '\n';
    }
}
}

int main()
{
    run(220.0, 6.0, 0x01234567u, "bad220_seed1");
    run(440.0, 6.0, 0x01234567u, "mixed440_seed1");
    run(110.0, 6.0, 0x51f15e5du, "bad110_seed3");
    return 0;
}
