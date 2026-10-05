#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace neumaton::scaleeditor
{
enum class SpacingShape
{
    even = 1,
    gentleOpening,
    naturalOpening,
    steadyOpening,
    softArc,
    tightToWide,
    wideToTight
};

struct Degree
{
    double phase = 0.0;   // Normalised logarithmic position in [0, 1].
    bool locked = false;  // Locked degrees are fixed anchors for redistribution/warp.
    bool included = true; // Excluded degrees remain editable but are omitted on save.
};

class Geometry
{
public:
    static constexpr int minPitchClasses = 3;
    static constexpr int maxPitchClasses = 96;
    static constexpr double minimumPhaseGap = 1.0e-4;

    Geometry()
    {
        setEqualDivision (12, false);
    }

    [[nodiscard]] const std::vector<Degree>& degrees() const noexcept { return degrees_; }
    [[nodiscard]] std::vector<Degree>& degrees() noexcept { return degrees_; }

    [[nodiscard]] int intervalCount() const noexcept
    {
        return static_cast<int> (degrees_.size()) + 1;
    }

    [[nodiscard]] int includedPitchClassCount() const noexcept
    {
        int count = 1; // unison
        for (const auto& degree : degrees_)
            if (degree.included)
                ++count;
        return count;
    }

    void clear() noexcept
    {
        degrees_.clear();
    }

    bool setEqualDivision (int steps, bool preserveLocksWhenCountMatches = true)
    {
        if (steps < minPitchClasses || steps > maxPitchClasses)
            return false;

        const int requestedDegrees = steps - 1;
        if (! preserveLocksWhenCountMatches
            || requestedDegrees != static_cast<int> (degrees_.size()))
        {
            degrees_.clear();
            degrees_.reserve (static_cast<std::size_t> (requestedDegrees));
            for (int i = 1; i < steps; ++i)
                degrees_.push_back ({ static_cast<double> (i) / static_cast<double> (steps), false, true });
            return true;
        }

        redistributeFreeDegrees();
        return true;
    }

    bool addDegree (double phase)
    {
        if (degrees_.size() >= static_cast<std::size_t> (maxPitchClasses - 1)
            || ! std::isfinite (phase))
            return false;

        phase = std::clamp (phase, minimumPhaseGap, 1.0 - minimumPhaseGap);

        const auto insertion = std::lower_bound (
            degrees_.begin(), degrees_.end(), phase,
            [] (const Degree& degree, double value) { return degree.phase < value; });

        const double left = insertion == degrees_.begin() ? 0.0 : std::prev (insertion)->phase;
        const double right = insertion == degrees_.end() ? 1.0 : insertion->phase;
        if (phase - left < minimumPhaseGap || right - phase < minimumPhaseGap)
            return false;

        degrees_.insert (insertion, Degree { phase, false, true });
        return true;
    }

    bool removeDegree (int index)
    {
        if (! validIndex (index) || degrees_[static_cast<std::size_t> (index)].locked)
            return false;

        degrees_.erase (degrees_.begin() + index);
        return true;
    }

    bool setDegreePhase (int index, double phase)
    {
        if (! validIndex (index))
            return false;

        auto& degree = degrees_[static_cast<std::size_t> (index)];
        if (degree.locked || ! std::isfinite (phase))
            return false;

        const double left = index > 0
            ? degrees_[static_cast<std::size_t> (index - 1)].phase
            : 0.0;
        const double right = index + 1 < static_cast<int> (degrees_.size())
            ? degrees_[static_cast<std::size_t> (index + 1)].phase
            : 1.0;

        degree.phase = std::clamp (phase,
                                   left + minimumPhaseGap,
                                   right - minimumPhaseGap);
        return true;
    }

    bool warpAroundDegree (int index, double targetPhase)
    {
        if (! validIndex (index))
            return false;

        const auto original = degrees_;
        if (original[static_cast<std::size_t> (index)].locked
            || ! std::isfinite (targetPhase))
            return false;

        int leftAnchorIndex = -1;
        int rightAnchorIndex = static_cast<int> (original.size());
        for (int i = index - 1; i >= 0; --i)
        {
            if (original[static_cast<std::size_t> (i)].locked)
            {
                leftAnchorIndex = i;
                break;
            }
        }
        for (int i = index + 1; i < static_cast<int> (original.size()); ++i)
        {
            if (original[static_cast<std::size_t> (i)].locked)
            {
                rightAnchorIndex = i;
                break;
            }
        }

        const double leftAnchor = leftAnchorIndex >= 0
            ? original[static_cast<std::size_t> (leftAnchorIndex)].phase
            : 0.0;
        const double rightAnchor = rightAnchorIndex < static_cast<int> (original.size())
            ? original[static_cast<std::size_t> (rightAnchorIndex)].phase
            : 1.0;
        const double originalSelected = original[static_cast<std::size_t> (index)].phase;

        targetPhase = std::clamp (targetPhase,
                                  leftAnchor + minimumPhaseGap,
                                  rightAnchor - minimumPhaseGap);

        const double leftDen = originalSelected - leftAnchor;
        const double rightDen = rightAnchor - originalSelected;
        if (leftDen <= 0.0 || rightDen <= 0.0)
            return false;

        for (int i = leftAnchorIndex + 1; i < rightAnchorIndex; ++i)
        {
            auto& current = degrees_[static_cast<std::size_t> (i)];
            const auto& source = original[static_cast<std::size_t> (i)];
            if (source.locked)
                continue;

            if (i <= index)
            {
                const double t = (source.phase - leftAnchor) / leftDen;
                current.phase = leftAnchor + t * (targetPhase - leftAnchor);
            }
            else
            {
                const double t = (source.phase - originalSelected) / rightDen;
                current.phase = targetPhase + t * (rightAnchor - targetPhase);
            }
        }

        return isStrictlyOrdered();
    }

    bool setLocked (int index, bool locked)
    {
        if (! validIndex (index))
            return false;
        degrees_[static_cast<std::size_t> (index)].locked = locked;
        return true;
    }

    bool setIncluded (int index, bool included)
    {
        if (! validIndex (index))
            return false;
        degrees_[static_cast<std::size_t> (index)].included = included;
        return true;
    }

    void redistributeFreeDegrees()
    {
        applySpacingShape (SpacingShape::even);
    }

    void applySpacingShape (SpacingShape shape)
    {
        if (degrees_.empty())
            return;

        int leftAnchorIndex = -1;
        double leftAnchorPhase = 0.0;

        for (int rightAnchorIndex = 0;
             rightAnchorIndex <= static_cast<int> (degrees_.size());
             ++rightAnchorIndex)
        {
            const bool rightIsBoundary = rightAnchorIndex == static_cast<int> (degrees_.size());
            const bool rightIsLocked = ! rightIsBoundary
                && degrees_[static_cast<std::size_t> (rightAnchorIndex)].locked;
            if (! rightIsBoundary && ! rightIsLocked)
                continue;

            const double rightAnchorPhase = rightIsBoundary
                ? 1.0
                : degrees_[static_cast<std::size_t> (rightAnchorIndex)].phase;

            const int freeCount = rightAnchorIndex - leftAnchorIndex - 1;
            const int intervalCount = freeCount + 1;
            if (freeCount > 0)
            {
                std::vector<double> weights;
                weights.reserve (static_cast<std::size_t> (intervalCount));

                double totalWeight = 0.0;
                for (int interval = 0; interval < intervalCount; ++interval)
                {
                    const double weight = spacingWeight (shape, interval, intervalCount);
                    weights.push_back (weight);
                    totalWeight += weight;
                }

                if (std::isfinite (totalWeight) && totalWeight > 0.0)
                {
                    double cumulative = 0.0;
                    for (int k = 0; k < freeCount; ++k)
                    {
                        cumulative += weights[static_cast<std::size_t> (k)];
                        const double t = cumulative / totalWeight;
                        degrees_[static_cast<std::size_t> (leftAnchorIndex + 1 + k)].phase
                            = leftAnchorPhase + t * (rightAnchorPhase - leftAnchorPhase);
                    }
                }
            }

            leftAnchorIndex = rightAnchorIndex;
            leftAnchorPhase = rightAnchorPhase;
        }
    }

    [[nodiscard]] std::vector<double> toRatios (double equaveRatio) const
    {
        std::vector<double> ratios;
        if (! std::isfinite (equaveRatio) || equaveRatio <= 1.0)
            return ratios;

        const double logEquave = std::log2 (equaveRatio);
        ratios.reserve (static_cast<std::size_t> (includedPitchClassCount()));
        ratios.push_back (1.0);

        for (const auto& degree : degrees_)
        {
            if (! degree.included)
                continue;
            const double ratio = std::exp2 (degree.phase * logEquave);
            if (std::isfinite (ratio) && ratio > 1.0 && ratio < equaveRatio)
                ratios.push_back (ratio);
        }
        return ratios;
    }

    [[nodiscard]] bool isStrictlyOrdered() const noexcept
    {
        double previous = 0.0;
        for (const auto& degree : degrees_)
        {
            if (! std::isfinite (degree.phase)
                || degree.phase <= previous
                || degree.phase >= 1.0)
                return false;
            previous = degree.phase;
        }
        return true;
    }

private:
    [[nodiscard]] static double spacingWeight (SpacingShape shape,
                                               int intervalIndex,
                                               int intervalCount) noexcept
    {
        const double n = static_cast<double> (intervalIndex + 1);
        const double t = intervalCount > 1
            ? static_cast<double> (intervalIndex) / static_cast<double> (intervalCount - 1)
            : 0.0;

        double weight = 1.0;
        switch (shape)
        {
            case SpacingShape::even:
                weight = 1.0;
                break;

            case SpacingShape::gentleOpening:
                weight = std::exp (0.5 * t);
                break;

            case SpacingShape::naturalOpening:
                weight = std::log1p (n);
                break;

            case SpacingShape::steadyOpening:
                weight = n;
                break;

            case SpacingShape::softArc:
            {
                constexpr double pi = 3.1415926535897932384626433832795;
                weight = 0.15 + 0.85 * (0.5 - 0.5 * std::cos (pi * t));
                break;
            }

            case SpacingShape::tightToWide:
                weight = n * n;
                break;

            case SpacingShape::wideToTight:
                weight = 1.0 / (n * n);
                break;
        }

        return std::isfinite (weight) && weight > 0.0 ? weight : 1.0;
    }

    [[nodiscard]] bool validIndex (int index) const noexcept
    {
        return index >= 0 && index < static_cast<int> (degrees_.size());
    }

    std::vector<Degree> degrees_;
};
}
