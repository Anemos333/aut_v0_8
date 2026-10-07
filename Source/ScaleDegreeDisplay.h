#pragma once

#include "PitchCore.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace neumaton::ui
{
// Message-thread presentation only. The measured target remains authoritative;
// this helper never chooses a target or changes the audio trajectory.
struct ScaleDegreeDisplay
{
    int degree = 0; // One-based; zero means no target in the current scale.
    int count = 0;
    bool held = false;
};

inline ScaleDegreeDisplay describeScaleTarget(double targetHz,
                                               double referenceHz,
                                               double equaveRatio,
                                               const std::vector<double>& ratios,
                                               bool held)
{
    ScaleDegreeDisplay result;
    result.held = held;
    if (!std::isfinite(referenceHz) || referenceHz <= 0.0
        || !std::isfinite(equaveRatio) || equaveRatio <= 1.0)
        return result;

    const double logEquave = std::log(equaveRatio);
    std::vector<double> positions;
    const auto limit = std::min(ratios.size(),
        static_cast<std::size_t>(pitch::ScaleQuantizer::maxDegrees));
    positions.reserve(limit);
    for (std::size_t i = 0; i < limit; ++i)
    {
        const double ratio = ratios[i];
        if (!std::isfinite(ratio) || ratio <= 0.0)
            continue;
        double position = std::log(ratio) / logEquave;
        position -= std::floor(position);
        positions.push_back(position);
    }
    std::sort(positions.begin(), positions.end());
    positions.erase(std::unique(positions.begin(), positions.end(),
        [] (double a, double b) { return std::abs(a - b) <= 1.0e-9; }),
        positions.end());
    result.count = static_cast<int>(positions.size());
    if (positions.empty() || !std::isfinite(targetHz) || targetHz <= 0.0)
        return result;

    const double relative = std::log(targetHz / referenceHz) / logEquave;
    double nearest = std::numeric_limits<double>::max();
    int degree = 0;
    for (std::size_t i = 0; i < positions.size(); ++i)
    {
        const double delta = relative - positions[i];
        const double distance = std::abs(delta - std::round(delta));
        if (distance < nearest)
        {
            nearest = distance;
            degree = static_cast<int>(i) + 1;
        }
    }
    // Float metering loses a little precision. A held target from a previous
    // scale/centre/tuning must not be relabelled as a new scale's degree.
    if (nearest * 1200.0 * std::log2(equaveRatio) <= 0.1)
        result.degree = degree;
    return result;
}
} // namespace neumaton::ui
