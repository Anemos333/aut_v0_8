#include "PitchCore.h"
#include "ScaleDefinitions.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>

namespace
{
bool check(bool condition, const char* name)
{
    std::cout << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}
}

int main()
{
    bool ok = true;
    const auto& scales = ScaleDefinitions::getAllScales();

    int visibleCount = 0;
    std::set<std::string> visibleIds;

    for (const auto& scale : scales)
    {
        if (!scale.visibleInMenu)
            continue;

        ++visibleCount;
        visibleIds.insert(scale.stableId);

        ok &= check(!scale.stableId.empty(), "scale_has_stable_id");
        ok &= check(std::isfinite(scale.equaveRatio) && scale.equaveRatio > 1.0,
                    "scale_has_valid_equave");
        ok &= check(!scale.ratios.empty() && scale.ratios.size() <= 96,
                    "scale_degree_count_supported");
        ok &= check(near(scale.ratios.front(), 1.0, 1.0e-10),
                    "scale_contains_unison");

        bool rangeOk = true;
        double previous = 0.0;
        for (double ratio : scale.ratios)
        {
            rangeOk = rangeOk
                && std::isfinite(ratio)
                && ratio >= 1.0
                && ratio < scale.equaveRatio
                && ratio + 1.0e-12 >= previous;
            previous = ratio;
        }
        ok &= check(rangeOk, "scale_ratios_sorted_inside_equave");
    }

    ok &= check(visibleCount == ScaleDefinitions::factoryVisibleScaleCount,
                "factory_has_exactly_120_visible_scales");
    ok &= check(static_cast<int>(visibleIds.size()) == visibleCount,
                "visible_scale_ids_are_unique");

    const int defaultIndex = ScaleDefinitions::findScaleIndexByStableId(
        ScaleDefinitions::defaultScaleStableId);
    ok &= check(defaultIndex == ScaleDefinitions::defaultFactoryScaleIndex,
                "chromatic_12edo_is_stable_default");

    const auto* edo12 = ScaleDefinitions::findScaleByStableId("scale_0033");
    ok &= check(edo12 != nullptr && edo12->ratios.size() == 12
                && near(edo12->equaveRatio, 2.0, 1.0e-12),
                "12edo_materialized_correctly");

    const auto* edo72 = ScaleDefinitions::findScaleByStableId("scale_0044");
    ok &= check(edo72 != nullptr && edo72->ratios.size() == 72
                && near(edo72->equaveRatio, 2.0, 1.0e-12),
                "72edo_fits_runtime_limit");

    const auto* bp = ScaleDefinitions::findScaleByStableId("scale_0045");
    ok &= check(bp != nullptr && bp->ratios.size() == 13
                && near(bp->equaveRatio, 3.0, 1.0e-12),
                "bohlen_pierce_keeps_3_to_1_equave");

    const auto* legacyPyth = ScaleDefinitions::findScaleByStableId(
        "legacy.scale.pythagorean-12");
    ok &= check(legacyPyth != nullptr && !legacyPyth->visibleInMenu,
                "legacy_exact_migration_scale_is_hidden");

    if (bp != nullptr)
    {
        neumaton::pitch::ScaleQuantizer quantizer;
        constexpr double reference = 440.0;
        const bool accepted = quantizer.setScale(
            bp->ratios.data(), static_cast<int>(bp->ratios.size()),
            reference, bp->equaveRatio);
        ok &= check(accepted && near(quantizer.equaveRatio(), 3.0, 1.0e-12),
                    "quantizer_accepts_3_to_1_equave");

        const double degree1 = reference * std::pow(3.0, 1.0 / 13.0);
        const auto target = quantizer.quantize(degree1, 1.0f, 0.0f);
        ok &= check(target.valid
                    && std::abs(1200.0 * std::log2(target.targetHz / degree1)) < 0.01,
                    "bohlen_pierce_degree_quantizes_without_octave_folding");
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
