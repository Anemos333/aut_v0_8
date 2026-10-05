#include "ScaleEditorGeometry.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
bool check (bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

bool near (double a, double b, double tolerance = 1.0e-9)
{
    return std::abs (a - b) <= tolerance;
}
}

int main()
{
    using neumaton::scaleeditor::Geometry;
    using neumaton::scaleeditor::SpacingShape;

    bool ok = true;

    Geometry geometry;
    ok &= check (geometry.intervalCount() == 12,
                 "default_is_12_equal_divisions");
    ok &= check (geometry.degrees().size() == 11,
                 "twelve_divisions_have_eleven_internal_degrees");
    ok &= check (near (geometry.degrees()[5].phase, 0.5),
                 "equal_division_midpoint_is_exact");

    const auto tritave = geometry.toRatios (3.0);
    ok &= check (tritave.size() == 12
                 && near (tritave[1], std::pow (3.0, 1.0 / 12.0), 1.0e-10),
                 "equal_division_scales_to_arbitrary_equave");

    geometry.setLocked (3, true);
    const double lockedPhase = geometry.degrees()[3].phase;
    geometry.setDegreePhase (4, 0.48);
    geometry.redistributeFreeDegrees();
    ok &= check (near (geometry.degrees()[3].phase, lockedPhase),
                 "redistribution_preserves_locked_degree");
    ok &= check (geometry.isStrictlyOrdered(),
                 "redistribution_preserves_order");

    Geometry shaped;
    shaped.setEqualDivision (12, false);
    shaped.setLocked (5, true);
    shaped.setIncluded (2, false);
    const double shapeAnchor = shaped.degrees()[5].phase;
    shaped.applySpacingShape (SpacingShape::gentleOpening);
    const double firstOpeningStep = shaped.degrees()[0].phase;
    const double secondOpeningStep = shaped.degrees()[1].phase - shaped.degrees()[0].phase;
    ok &= check (near (shaped.degrees()[5].phase, shapeAnchor)
                 && shaped.isStrictlyOrdered(),
                 "spacing_shape_preserves_locked_anchor_and_order");
    ok &= check (secondOpeningStep > firstOpeningStep,
                 "gentle_opening_widens_successive_intervals");
    ok &= check (! shaped.degrees()[2].included,
                 "spacing_shape_preserves_inclusion_state");

    Geometry reverseShape;
    reverseShape.setEqualDivision (12, false);
    reverseShape.applySpacingShape (SpacingShape::wideToTight);
    const double firstReverseStep = reverseShape.degrees()[0].phase;
    const double secondReverseStep = reverseShape.degrees()[1].phase - reverseShape.degrees()[0].phase;
    ok &= check (firstReverseStep > secondReverseStep,
                 "wide_to_tight_reverses_interval_weighting");

    Geometry warp;
    warp.setEqualDivision (12, false);
    warp.setLocked (2, true);
    warp.setLocked (8, true);
    const double leftAnchor = warp.degrees()[2].phase;
    const double rightAnchor = warp.degrees()[8].phase;
    ok &= check (warp.warpAroundDegree (5, 0.58),
                 "group_warp_is_accepted");
    ok &= check (near (warp.degrees()[2].phase, leftAnchor)
                 && near (warp.degrees()[8].phase, rightAnchor),
                 "group_warp_preserves_locked_anchors");
    ok &= check (near (warp.degrees()[5].phase, 0.58, 1.0e-9)
                 && warp.isStrictlyOrdered(),
                 "group_warp_moves_selected_degree_and_preserves_order");

    Geometry exclusion;
    exclusion.setEqualDivision (7, false);
    exclusion.setIncluded (2, false);
    const auto ratios = exclusion.toRatios (2.0);
    ok &= check (exclusion.includedPitchClassCount() == 6
                 && ratios.size() == 6,
                 "excluded_degree_is_omitted_from_saved_ratios");

    Geometry topology;
    topology.setEqualDivision (19, false);
    topology.setLocked (4, true);
    topology.setEqualDivision (19, true);
    ok &= check (topology.degrees()[4].locked,
                 "same_count_divide_preserves_locks");
    topology.setEqualDivision (31, true);
    bool anyLocked = false;
    for (const auto& degree : topology.degrees())
        anyLocked = anyLocked || degree.locked;
    ok &= check (! anyLocked && topology.intervalCount() == 31,
                 "count_change_resets_locks_and_rebuilds_topology");

    Geometry limits;
    ok &= check (! limits.setEqualDivision (2, false)
                 && ! limits.setEqualDivision (97, false)
                 && limits.setEqualDivision (96, false),
                 "editor_enforces_3_to_96_pitch_class_range");

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
