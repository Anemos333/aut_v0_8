#include "ScaleDefinitions.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>

namespace
{
struct RawScaleDefinition
{
    const char* stableId;
    const char* name;
    const char* category;
    ScaleRuntimeClass runtimeClass;
    ScalePrecision precision;
    TonalCenterRole centerRole;
    ReferenceTuningPolicy referencePolicy;
    double equaveRatio;
    double defaultReferenceHz;
    const char* centsCsv;
};

constexpr RawScaleDefinition rawScales[] =
{
#include "ScaleDatabasePart1.inc"
#include "ScaleDatabasePart2.inc"
#include "ScaleDatabasePart3.inc"
#include "ScaleDatabasePart4.inc"
};

static_assert (std::size (rawScales) == 120,
               "Neumaton V1 factory scale corpus must contain exactly 120 entries");

[[nodiscard]] std::vector<double> ratiosFromCents (const char* csv,
                                                   double equaveRatio)
{
    if (csv == nullptr || ! std::isfinite (equaveRatio) || equaveRatio <= 1.0)
        return { 1.0 };

    const double logEquave = std::log2 (equaveRatio);
    const double periodCents = 1200.0 * logEquave;

    std::vector<double> ratios;
    ratios.reserve (96);
    ratios.push_back (1.0);

    const char* cursor = csv;
    while (*cursor != '\0' && ratios.size() < 96)
    {
        char* end = nullptr;
        const double cents = std::strtod (cursor, &end);
        if (end == cursor)
        {
            ++cursor;
            continue;
        }

        if (std::isfinite (cents))
        {
            double phaseCents = std::fmod (cents, periodCents);
            if (phaseCents < 0.0)
                phaseCents += periodCents;

            if (phaseCents < periodCents - 1.0e-7)
            {
                const double ratio = std::exp2 (phaseCents / 1200.0);
                if (std::isfinite (ratio) && ratio >= 1.0
                    && ratio < equaveRatio)
                    ratios.push_back (ratio);
            }
        }

        cursor = end;
        while (*cursor == ',' || *cursor == ' ' || *cursor == '\t')
            ++cursor;
    }

    std::sort (ratios.begin(), ratios.end());
    const auto last = std::unique (ratios.begin(), ratios.end(),
        [] (double a, double b)
        {
            return std::abs (a - b) < 1.0e-10;
        });
    ratios.erase (last, ratios.end());

    if (ratios.empty() || std::abs (ratios.front() - 1.0) > 1.0e-10)
        ratios.insert (ratios.begin(), 1.0);

    return ratios;
}
} // namespace

std::vector<ScaleInfo> ScaleDefinitions::scales_;

std::vector<ScaleInfo> ScaleDefinitions::buildScales()
{
    std::vector<ScaleInfo> result;
    result.reserve (std::size (rawScales));

    for (const auto& raw : rawScales)
    {
        ScaleInfo scale;
        scale.stableId = raw.stableId;
        scale.packId = std::string (factoryPackId);
        scale.name = raw.name;
        scale.category = raw.category;
        scale.runtimeClass = raw.runtimeClass;
        scale.precision = raw.precision;
        scale.centerRole = raw.centerRole;
        scale.referencePolicy = raw.referencePolicy;
        scale.equaveRatio = raw.equaveRatio;
        scale.defaultReferenceHz = raw.defaultReferenceHz;
        scale.ratios = ratiosFromCents (raw.centsCsv, raw.equaveRatio);
        result.push_back (std::move (scale));
    }

    return result;
}

const std::vector<ScaleInfo>& ScaleDefinitions::getAllScales()
{
    if (scales_.empty())
        scales_ = buildScales();
    return scales_;
}

int ScaleDefinitions::getScaleCount()
{
    return static_cast<int> (getAllScales().size());
}

const ScaleInfo& ScaleDefinitions::getScale (int index)
{
    const auto& scales = getAllScales();
    if (scales.empty())
    {
        static const ScaleInfo fallback {
            "scale_fallback", std::string (factoryPackId), "Chromatic", "Fallback",
            {}, {}, {}, ScaleRuntimeClass::fixedScale, ScalePrecision::theoreticalExact,
            TonalCenterRole::tonicOrKeyCenter, ReferenceTuningPolicy::userA4,
            2.0, 440.0, { 1.0 }
        };
        return fallback;
    }

    const auto safe = static_cast<std::size_t> (
        std::clamp (index, 0, static_cast<int> (scales.size()) - 1));
    return scales[safe];
}

int ScaleDefinitions::findScaleIndexByStableId (std::string_view stableId) noexcept
{
    const auto& scales = getAllScales();
    for (std::size_t i = 0; i < scales.size(); ++i)
        if (std::string_view (scales[i].stableId) == stableId)
            return static_cast<int> (i);
    return -1;
}

const ScaleInfo* ScaleDefinitions::findScaleByStableId (std::string_view stableId) noexcept
{
    const int index = findScaleIndexByStableId (stableId);
    return index >= 0 ? &getScale (index) : nullptr;
}
