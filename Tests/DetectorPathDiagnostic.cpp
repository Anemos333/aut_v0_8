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

bool near(float measured, double target)
{
    return measured > 0.0f
        && std::abs(1200.0 * std::log2(static_cast<double>(measured) / target)) <= 45.0;
}

struct Counts
{
    int valid = 0;
    int correct = 0;
    int half = 0;
    int twice = 0;
    int other = 0;
    double sumHz = 0.0;
    double sumFamily = 0.0;
    double sumClean = 0.0;
    double sumPeriodicity = 0.0;
};

void add(Counts& c, const ModernPitchEngine::MultiRatePitchTracker::PitchCandidate& p, double target)
{
    if (!p.valid || !(p.frequencyHz > 0.0f)) return;
    ++c.valid;
    c.sumHz += p.frequencyHz;
    c.sumFamily += p.harmonicFamily;
    c.sumClean += p.tonalCleanliness;
    c.sumPeriodicity += p.periodicity;
    if (near(p.frequencyHz, target)) ++c.correct;
    else if (near(p.frequencyHz, target * 0.5)) ++c.half;
    else if (near(p.frequencyHz, target * 2.0)) ++c.twice;
    else ++c.other;
}

void addOutput(Counts& c, float hz, double target)
{
    if (!(hz > 0.0f)) return;
    ++c.valid;
    c.sumHz += hz;
    if (near(hz, target)) ++c.correct;
    else if (near(hz, target * 0.5)) ++c.half;
    else if (near(hz, target * 2.0)) ++c.twice;
    else ++c.other;
}

void print(const char* label, const Counts& c)
{
    std::cout << ' ' << label << "_valid=" << c.valid
              << ' ' << label << "_correct=" << c.correct
              << ' ' << label << "_half=" << c.half
              << ' ' << label << "_double=" << c.twice
              << ' ' << label << "_other=" << c.other;
    if (c.valid > 0)
    {
        std::cout << ' ' << label << "_mean_hz=" << c.sumHz / c.valid;
        if (std::string(label) != "out")
            std::cout << ' ' << label << "_family=" << c.sumFamily / c.valid
                      << ' ' << label << "_clean=" << c.sumClean / c.valid
                      << ' ' << label << "_period=" << c.sumPeriodicity / c.valid;
    }
}

void run(double targetHz,
         double snrDb,
         std::uint32_t seed,
         bool legacyProfile)
{
    constexpr int n = 24000;
    std::vector<float> voice(n), noise(n);
    Rng rng { seed };
    float fast = 0.0f, slow = 0.0f;
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * targetHz / sr;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;
        if (legacyProfile)
            voice[i] = static_cast<float>(std::sin(phase)
                + 0.34 * std::sin(2.0 * phase + 0.31)
                + 0.18 * std::sin(3.0 * phase + 0.73));
        else
            voice[i] = static_cast<float>(std::sin(phase)
                + 0.44 * std::sin(2.0 * phase + 0.17)
                + 0.23 * std::sin(3.0 * phase + 0.41)
                + 0.12 * std::sin(4.0 * phase + 0.73));

        const float white = rng.next();
        fast = 0.92f * fast + 0.08f * white;
        slow = 0.992f * slow + 0.008f * white;
        noise[i] = 0.52f * white + 0.31f * fast + 0.17f * slow;
    }
    scaleRms(voice, std::pow(10.0, -42.0 / 20.0));
    scaleRms(noise, rms(voice) / std::pow(10.0, snrDb / 20.0));
    for (int i = 0; i < n; ++i) voice[i] += noise[i];

    auto t = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    t->prepare(sr);
    t->setRange(45.0f, 1600.0f);

    Counts out, full, half, quarter, eighth;
    int firstValid = -1, firstCorrect = -1;
    for (int i = 0; i < n; ++i)
    {
        ModernPitchEngine::PitchObservation o;
        if (!t->processSample(voice[i], o)) continue;

        if (t->fullRateCandidate_.ageInHops == 0)
            add(full, t->fullRateCandidate_.candidate, targetHz);
        if (t->halfRateCandidate_.ageInHops == 0)
            add(half, t->halfRateCandidate_.candidate, targetHz);
        if (t->quarterRateCandidate_.ageInHops == 0)
            add(quarter, t->quarterRateCandidate_.candidate, targetHz);
        if (t->eighthRateCandidate_.ageInHops == 0)
            add(eighth, t->eighthRateCandidate_.candidate, targetHz);

        if (o.valid && o.correctionFrequencyHz > 0.0f)
        {
            if (firstValid < 0) firstValid = i;
            if (firstCorrect < 0 && near(o.correctionFrequencyHz, targetHz)) firstCorrect = i;
            addOutput(out, o.correctionFrequencyHz, targetHz);
        }
    }

    std::cout << std::fixed << std::setprecision(3)
              << "PATH_STRESS hz=" << targetHz
              << " snr=" << snrDb
              << " seed=" << seed
              << " profile=" << (legacyProfile ? "legacy" : "harmonic4")
              << " first_valid_ms=" << (firstValid < 0 ? -1.0 : 1000.0 * firstValid / sr)
              << " first_correct_ms=" << (firstCorrect < 0 ? -1.0 : 1000.0 * firstCorrect / sr);
    print("out", out);
    print("full", full);
    print("half", half);
    print("quarter", quarter);
    print("eighth", eighth);
    std::cout << '\n';
}
}

int main()
{
    const std::array<std::uint32_t, 4> seeds {
        0x1234567u, 0x9e3779b9u, 0x51f15e5du, 0xc001d00du
    };
    for (bool legacy : {false, true})
    {
        for (std::uint32_t seed : seeds)
        {
            run(220.0, 9.0, seed, legacy);
            run(220.0, 6.0, seed, legacy);
            run(220.0, 3.0, seed, legacy);
            run(440.0, 6.0, seed, legacy);
            run(440.0, 3.0, seed, legacy);
        }
    }
    return 0;
}
