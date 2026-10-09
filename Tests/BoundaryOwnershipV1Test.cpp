#include "PitchCore.h"
#include "PitchCorrectionTrajectory.h"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
int failures = 0;

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

double hzAt(double referenceHz, double cents)
{
    return referenceHz * std::exp2(cents / 1200.0);
}

bool nearTarget(double actualHz, double expectedHz, double epsilonCents = 0.1)
{
    return actualHz > 0.0 && expectedHz > 0.0
        && std::abs(1200.0 * std::log2(actualHz / expectedHz)) < epsilonCents;
}

neumaton::pitch::PitchResult stable(double hz, bool fresh = true)
{
    neumaton::pitch::PitchResult result;
    result.state = neumaton::pitch::TrackingState::stable;
    result.hasStable = true;
    result.stableHz = static_cast<float>(hz);
    result.newMeasurement = fresh;
    return result;
}

void testScale(const std::vector<double>& ratios,
               double referenceHz,
               double equave,
               double lowerCents,
               double upperCents)
{
    neumaton::pitch::ScaleQuantizer q;
    require(q.setScale(ratios.data(), static_cast<int>(ratios.size()),
                       referenceHz, equave), "scale must be accepted");
    const double midpoint = (lowerCents + upperCents) * 0.5;
    const double lowerHz = hzAt(referenceHz, lowerCents);
    const double upperHz = hzAt(referenceHz, upperCents);

    for (double cents : { midpoint - 0.4, midpoint, midpoint + 0.4 })
    {
        const auto t = q.quantize(hzAt(referenceHz, cents), 1.0f, 0.0f, 0.5f);
        require(t.valid && t.ambiguousBoundary,
                "a true nearest-neighbour midpoint is marked ambiguous");
        require(nearTarget(t.targetHz, upperHz),
                "both sides and exact tie resolve to the upper legal degree");
    }
    require(nearTarget(q.quantize(hzAt(referenceHz, lowerCents + 5.0),
                                  1.0f, 0.0f).targetHz, lowerHz),
            "ordinary lower interior remains nearest lower degree");
    require(nearTarget(q.quantize(hzAt(referenceHz, upperCents - 5.0),
                                  1.0f, 0.0f).targetHz, upperHz),
            "ordinary upper interior remains nearest upper degree");

    neumaton::render::PitchCorrectionTrajectory trajectory;
    trajectory.prepare(48000.0);
    const auto advance = [&](double cents, bool fresh = true)
    {
        static_cast<void>(trajectory.process(
            stable(hzAt(referenceHz, cents), fresh), q,
            1.0f, 0.0f, 0.0f, 0.5f));
        return trajectory.targetPitchHz();
    };
    require(nearTarget(advance(midpoint), upperHz),
            "trajectory arms the upper ambiguous owner");
    for (int n = 0; n < 24; ++n)
    {
        // Several-cent midpoint excursions: deliberately cross the quantizer's
        // instantaneous upper-preference and lower-nearest partitions.
        const double jitter = n % 2 == 0
            ? -std::min(0.12 * (upperCents - lowerCents), 20.0)
            : std::min(0.08 * (upperCents - lowerCents), 12.0);
        require(nearTarget(advance(midpoint + jitter), upperHz),
                "boundary jitter never alternates audible target");
    }
    const double exitDelta = 0.40 * (upperCents - lowerCents);
    for (int n = 0; n < 16; ++n)
    {
        require(nearTarget(advance(midpoint - exitDelta, false), upperHz),
                "sample repetition cannot count as independent confirmation");
    }
    for (int n = 0; n < 5; ++n)
        require(nearTarget(advance(midpoint - exitDelta, true), upperHz),
                "isolated lower challenges cannot steal ambiguous ownership");
    require(nearTarget(advance(midpoint - exitDelta, true), lowerHz),
            "six consecutive fresh, deep lower observations release ownership");
    require(nearTarget(advance(lowerCents, false), lowerHz),
            "once a real lower note commits it remains stable");
    trajectory.reset();
    require(nearTarget(advance(lowerCents), lowerHz),
            "reset drops boundary memory and returns normal nearest-note selection");
}
}

int main()
{
    const double c = 261.6255653005986;
    std::vector<double> twelve, twentyFour, thirtyOne;
    for (int i = 0; i < 12; ++i) twelve.push_back(std::exp2(i / 12.0));
    for (int i = 0; i < 24; ++i) twentyFour.push_back(std::exp2(i / 24.0));
    for (int i = 0; i < 31; ++i) thirtyOne.push_back(std::exp2(i / 31.0));
    testScale(twelve, c, 2.0, 0.0, 100.0);
    testScale(twentyFour, c, 2.0, 0.0, 50.0);
    testScale(thirtyOne, c, 2.0, 0.0, 1200.0 / 31.0);
    testScale({1.0, std::exp2(73.0 / 1200.0),
               std::exp2(230.0 / 1200.0), std::exp2(701.0 / 1200.0)},
              c, 2.0, 0.0, 73.0);
    // Octave seam: the upper degree is the next octave's tonic.
    testScale(twelve, c, 2.0, 1100.0, 1200.0);
    // Non-octave equave: nearest-note geometry must remain scale-native.
    std::vector<double> bohlenPierce;
    for (int i = 0; i < 13; ++i)
        bohlenPierce.push_back(std::pow(3.0, static_cast<double>(i) / 13.0));
    testScale(bohlenPierce, c, 3.0, 0.0,
              1200.0 * std::log2(3.0) / 13.0);

    std::cout << "Boundary ownership regressions: "
              << (failures == 0 ? "PASS" : "FAIL") << '\n';
    return failures == 0 ? 0 : 1;
}
