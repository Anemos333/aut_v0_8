#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <sstream>
#include <complex>
#include <vector>

#include <JuceHeader.h>
#include "Tempo.h"
#include "ScaleDefinitions.h"
#define private public
#include "ModernPitchEngine.h"
#undef private

namespace {
int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}

bool nearlyEqual(double a, double b, double tol = 1.0e-11)
{
    return std::abs(a - b) <= tol;
}

std::vector<double> normalisedLogRatios(const std::vector<double>& ratios)
{
    std::vector<double> logs;
    for (double ratio : ratios)
    {
        if (!std::isfinite(ratio) || ratio <= 0.0)
            continue;
        while (ratio < 1.0) ratio *= 2.0;
        while (ratio >= 2.0) ratio *= 0.5;
        logs.push_back(std::log2(ratio));
    }
    std::sort(logs.begin(), logs.end());
    logs.erase(std::unique(logs.begin(), logs.end(), [](double a, double b)
    {
        return std::abs(a - b) <= 1.0e-8;
    }), logs.end());
    return logs;
}

double referenceTargetLog2(double inputLog2, const std::vector<double>& ratios, double rootHz)
{
    const auto logs = normalisedLogRatios(ratios);
    if (logs.empty() || !std::isfinite(inputLog2) || !std::isfinite(rootHz) || rootHz <= 0.0)
        return inputLog2;

    const double rootLog2 = std::log2(rootHz);
    const double relative = inputLog2 - rootLog2;
    const double baseOctave = std::floor(relative);
    double best = inputLog2;
    double bestDistance = std::numeric_limits<double>::max();

    for (double pos : logs)
    {
        for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset)
        {
            const double candidate = rootLog2 + baseOctave + static_cast<double>(octaveOffset) + pos;
            const double distance = std::abs(candidate - inputLog2);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                best = candidate;
            }
        }
    }
    return best;
}

bool isScaleMember(double targetLog2, const std::vector<double>& ratios, double rootHz, double tol = 2.0e-9)
{
    const auto logs = normalisedLogRatios(ratios);
    const double rootLog2 = std::log2(rootHz);
    const double relative = targetLog2 - rootLog2;
    const double octave = std::floor(relative);
    for (double pos : logs)
        for (int wrap = -1; wrap <= 1; ++wrap)
            if (std::abs((octave + static_cast<double>(wrap) + pos) - relative) <= tol)
                return true;
    return false;
}

void runScaleDefinitionChecks()
{
    const auto& scales = ScaleDefinitions::getAllScales();
    check(!scales.empty(), "built-in scale list must not be empty");
    for (std::size_t i = 0; i < scales.size(); ++i)
    {
        const auto& scale = scales[i];
        check(!scale.name.empty(), "scale " + std::to_string(i) + " has a name");
        check(!scale.category.empty(), "scale " + std::to_string(i) + " has a category");
        check(!scale.ratios.empty(), scale.name + " has ratios");
        if (!scale.ratios.empty())
            check(nearlyEqual(scale.ratios.front(), 1.0, 1.0e-12), scale.name + " starts at unison 1.0");
        for (std::size_t r = 0; r < scale.ratios.size(); ++r)
        {
            const double ratio = scale.ratios[r];
            check(std::isfinite(ratio), scale.name + " ratio finite");
            check(ratio >= 1.0 && ratio < 2.0, scale.name + " ratio in [1, 2)");
            if (r > 0)
                check(scale.ratios[r] > scale.ratios[r - 1] + 1.0e-10,
                      scale.name + " ratios strictly sorted/unique");
        }
    }
    std::cout << "ScaleDefinitions checked: " << scales.size() << " scales\n";
}

void runQuantizerStrictChecks()
{
    const auto& scales = ScaleDefinitions::getAllScales();
    const std::array<double, 4> roots { 261.6256, 293.6648, 440.0, 261.6256 * 1.122462048309373 };
    int cases = 0;

    for (const auto& scale : scales)
    {
        for (double root : roots)
        {
            ModernPitchEngine::ScaleQuantizer quantizer;
            check(quantizer.update(scale.ratios.data(), static_cast<int>(scale.ratios.size()), root),
                  scale.name + " initial quantizer update changed state");
            check(quantizer.hasScale(), scale.name + " quantizer has scale");

            for (int step = 0; step <= 360; ++step)
            {
                const double freq = 25.0 * std::pow(5000.0 / 25.0, static_cast<double>(step) / 360.0);
                const double inputLog2 = std::log2(freq);
                const double expected = referenceTargetLog2(inputLog2, scale.ratios, root);
                quantizer.resetTarget();
                const double actual = quantizer.chooseTargetLog2(inputLog2, 0.0f);
                if (!nearlyEqual(actual, expected, 1.0e-11))
                {
                    check(false, scale.name + " nearest target mismatch at " + std::to_string(freq)
                          + " Hz: expected " + std::to_string(expected)
                          + ", got " + std::to_string(actual));
                    break;
                }
                check(isScaleMember(actual, scale.ratios, root), scale.name + " target is member of scale");
                ++cases;
            }

            // Hysteresis may keep the previous note instead of the mathematically nearest note,
            // but it must still keep a valid member of the active scale, not a chromatic fallback.
            quantizer.resetTarget();
            for (int cents = -600; cents <= 600; cents += 5)
            {
                const double inputLog2 = std::log2(root) + static_cast<double>(cents) / 1200.0;
                const double actual = quantizer.chooseTargetLog2(inputLog2, 12.0f);
                check(isScaleMember(actual, scale.ratios, root), scale.name + " hysteresis target stays on scale");
                ++cases;
            }
        }
    }
    std::cout << "ScaleQuantizer strict target cases: " << cases << "\n";
}

void runTempoChecks()
{
    using namespace CreativeTempo;
    Controller c;
    c.prepare(48000.0);

    Settings s;
    s.mode = Mode::off;
    HostPosition host;
    c.beginBlock(host, s, 128);
    auto d0 = c.processSample(12.0, 34.0, 7, 0.0f, true, 0, s, 55.0f);
    check(!d0.active, "tempo off inactive");
    check(d0.controllerCents == 12.0 && d0.destinationCents == 34.0 && d0.targetRevision == 7,
          "tempo off is strict pass-through");
    check(std::abs(d0.transitionTimeMs - 55.0f) < 1.0e-6f, "tempo off preserves normal transition time");

    c.reset();
    s.mode = Mode::tempoGlide;
    s.division = Division::note32;
    s.glideFraction = 0.35f;
    s.fallbackBpm = 120.0;
    c.beginBlock(host, s, 128);
    (void)c.processSample(0.0, 0.0, 0, 0.0f, true, 0, s, 35.0f);
    auto dg = c.processSample(0.0, 100.0, 1, 0.0f, true, 1, s, 35.0f);
    check(dg.active, "tempo glide active");
    check(!dg.waitingForGrid, "tempo glide releases immediately without PPQ");
    check(dg.forceTransition, "tempo glide forces transition on new target");
    check(dg.targetRevision == 1, "tempo glide publishes new revision immediately");
    check(std::abs(dg.transitionTimeMs - 21.875f) < 0.01f,
          "tempo glide 1/32 @ 120 BPM, 35% -> 21.875 ms");

    c.reset();
    s.mode = Mode::glideLock;
    s.lockStrength = 1.0f;
    s.smartOnset = false;
    host.hasBpm = true; host.bpm = 120.0;
    host.hasPpq = true; host.ppqAtBlockStart = 0.01;
    host.hasTimeInSamples = true; host.timeInSamples = 0;
    host.isPlaying = true;
    c.beginBlock(host, s, 4000);
    (void)c.processSample(0.0, 0.0, 0, 0.0f, true, 0, s, 35.0f);
    auto dl = c.processSample(0.0, 100.0, 1, 0.0f, true, 1, s, 35.0f);
    check(dl.waitingForGrid, "glide lock holds target until grid");
    check(dl.targetRevision == 0, "glide lock keeps previous revision while waiting");
    int releaseSample = -1;
    for (int i = 2; i < 4000; ++i)
    {
        auto step = c.processSample(0.0, 100.0, 1, 0.0f, true, i, s, 35.0f);
        if (!step.waitingForGrid && step.targetRevision == 1)
        {
            releaseSample = i;
            check(step.forceTransition, "glide lock forces transition at release");
            break;
        }
    }
    check(std::abs(releaseSample - 2760) <= 2,
          "glide lock releases at next 1/32 grid (expected sample ~2760, got " + std::to_string(releaseSample) + ")");

    c.reset();
    s.smartOnset = true;
    host.ppqAtBlockStart = 0.124;
    c.beginBlock(host, s, 256);
    (void)c.processSample(0.0, 0.0, 0, 0.0f, true, 0, s, 35.0f);
    auto ds = c.processSample(0.0, -50.0, 1, 0.80f, true, 1, s, 35.0f);
    check(!ds.waitingForGrid && ds.targetRevision == 1,
          "smart onset near grid releases immediately");

    c.reset();
    s.smartOnset = false;
    s.lockStrength = 0.0f;
    host.ppqAtBlockStart = 0.01;
    c.beginBlock(host, s, 256);
    (void)c.processSample(0.0, 0.0, 0, 0.0f, true, 0, s, 35.0f);
    auto di = c.processSample(0.0, 33.0, 1, 0.0f, true, 1, s, 35.0f);
    check(!di.waitingForGrid && di.targetRevision == 1,
          "glide lock strength 0 releases immediately");

    std::cout << "CreativeTempo controller checks complete\n";
}
}

int main()
{
    runScaleDefinitionChecks();
    runQuantizerStrictChecks();
    runTempoChecks();
    if (failures != 0)
    {
        std::cerr << failures << " failures\n";
        return 1;
    }
    std::cout << "ALL_SCALE_TEMPO_STRICT_TESTS_PASSED\n";
    return 0;
}
