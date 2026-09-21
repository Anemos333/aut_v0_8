#include <JuceHeader.h>

#define private public
#include "../Source/ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
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

bool near(float measured, double target, double cents = 45.0)
{
    return measured > 0.0f
        && std::abs(1200.0 * std::log2(static_cast<double>(measured) / target)) <= cents;
}

void run(double hz, double snrDb, std::uint32_t seed, bool strongSecond)
{
    constexpr int n = 24000;
    std::vector<float> signal(n), noise(n);
    Rng rng { seed };
    float fast = 0.0f, slow = 0.0f;
    double phase = 0.0;

    for (int i = 0; i < n; ++i)
    {
        phase += 2.0 * pi * hz / sr;
        if (phase >= 2.0 * pi) phase -= 2.0 * pi;

        const double h1 = strongSecond ? 0.35 : 1.0;
        const double h2 = strongSecond ? 1.00 : 0.44;
        signal[static_cast<std::size_t>(i)] = static_cast<float>(
            h1 * std::sin(phase)
          + h2 * std::sin(2.0 * phase + 0.17)
          + 0.23 * std::sin(3.0 * phase + 0.41)
          + 0.12 * std::sin(4.0 * phase + 0.73));

        const float white = rng.next();
        fast = 0.92f * fast + 0.08f * white;
        slow = 0.992f * slow + 0.008f * white;
        noise[static_cast<std::size_t>(i)] =
            0.52f * white + 0.31f * fast + 0.17f * slow;
    }

    scaleRms(signal, std::pow(10.0, -42.0 / 20.0));
    scaleRms(noise, rms(signal) / std::pow(10.0, snrDb / 20.0));
    for (int i = 0; i < n; ++i)
        signal[static_cast<std::size_t>(i)] += noise[static_cast<std::size_t>(i)];

    ModernPitchEngine::MultiRatePitchTracker tracker;
    tracker.prepare(sr);
    tracker.setRange(45.0f, 1600.0f);

    int hops = 0;
    int available = 0;
    int availableCorrect = 0;
    int availableHalf = 0;
    int availableDouble = 0;
    int availableOther = 0;
    int provisional = 0;
    int provisionalCorrect = 0;
    int valid = 0;
    int validCorrect = 0;
    int firstAvailable = -1;
    int firstValid = -1;

    for (int i = 0; i < n; ++i)
    {
        ModernPitchEngine::PitchObservation o;
        if (!tracker.processSample(signal[static_cast<std::size_t>(i)], o))
            continue;

        ++hops;
        const bool measured = o.measurementAvailable
            && std::isfinite(o.correctionFrequencyHz)
            && o.correctionFrequencyHz > 0.0f;
        if (measured)
        {
            if (firstAvailable < 0) firstAvailable = i;
            ++available;
            if (near(o.correctionFrequencyHz, hz))
                ++availableCorrect;
            else if (near(o.correctionFrequencyHz, hz * 0.5))
                ++availableHalf;
            else if (near(o.correctionFrequencyHz, hz * 2.0))
                ++availableDouble;
            else
                ++availableOther;

            if (!o.valid)
            {
                ++provisional;
                if (near(o.correctionFrequencyHz, hz))
                    ++provisionalCorrect;
            }
        }

        if (o.valid)
        {
            if (firstValid < 0) firstValid = i;
            ++valid;
            if (near(o.correctionFrequencyHz, hz))
                ++validCorrect;
        }
    }

    const auto frac = [](int a, int b)
    {
        return b > 0 ? static_cast<double>(a) / static_cast<double>(b) : 0.0;
    };
    const auto ms = [](int sample)
    {
        return sample >= 0 ? 1000.0 * sample / sr : -1.0;
    };

    std::cout << std::fixed << std::setprecision(4)
              << "MEASUREMENT_CONTINUUM"
              << " hz=" << hz
              << " snr=" << snrDb
              << " strong_second=" << (strongSecond ? 1 : 0)
              << " hops=" << hops
              << " available=" << available
              << " available_fraction=" << frac(available, hops)
              << " available_correct_fraction=" << frac(availableCorrect, available)
              << " provisional=" << provisional
              << " provisional_correct_fraction=" << frac(provisionalCorrect, provisional)
              << " valid=" << valid
              << " valid_fraction=" << frac(valid, hops)
              << " valid_correct_fraction=" << frac(validCorrect, valid)
              << " half=" << availableHalf
              << " double=" << availableDouble
              << " other=" << availableOther
              << " first_available_ms=" << ms(firstAvailable)
              << " first_valid_ms=" << ms(firstValid)
              << '\n';
}
}

int main()
{
    for (std::uint32_t seed : { 0x1234567u, 0x9e3779b9u, 0x51f15e5du, 0xc001d00du })
    {
        for (double hz : { 110.0, 220.0, 440.0 })
        {
            run(hz, 6.0, seed, false);
            run(hz, 3.0, seed ^ 0xa5a5a5a5u, false);
        }
        run(110.0, 6.0, seed ^ 0x3c6ef372u, true);
        run(110.0, 3.0, seed ^ 0xbb67ae85u, true);
    }
    return 0;
}
