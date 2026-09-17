from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()


def one(text, anchor, replacement, label):
    count = text.count(anchor)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(anchor, replacement, 1)


marker = 'PENDING_FAMILY_IS_EVIDENCE_NOT_COORDINATE_V6_5_1'
if marker not in cpp:
    # V6.5's first sibling-shadow condition was too specific: it required the
    # capable path's pending wrong member to equal the range-limited path's lower
    # member. On 440 legacy the quarter path can collapse to 220, 146.7 or 110
    # while eighth remains at 220. Any of those simple-family ambiguities means
    # quarter has not physically surrendered its recently demonstrated 440 yet.
    shadow_anchor = '''            if (centsDistance(pathFamilyCoordinateHz_[pathSlot], upperHz) <= 70.0f\n                && centsDistance(pathFamilyPendingHz_[pathSlot], source.frequencyHz) <= 85.0f)\n            {\n                return true;\n            }\n'''
    shadow_replacement = '''            // PENDING_FAMILY_IS_EVIDENCE_NOT_COORDINATE_V6_5_1\n            // The exact pending member is irrelevant. If a path capable of the\n            // upper coordinate recently owned it and is now resolving ANY simple\n            // family member, a range-limited lower sibling cannot take register\n            // ownership during that bounded window.\n            if (centsDistance(pathFamilyCoordinateHz_[pathSlot], upperHz) <= 70.0f)\n                return true;\n'''
    cpp = one(cpp, shadow_anchor, shadow_replacement,
              'general pending-family sibling shadow')

    anchor = '''    int authoritativeIndexV6 = -1;\n'''
    replacement = '''    // PENDING_FAMILY_IS_EVIDENCE_NOT_COORDINATE_V6_5_1\n    // V6.5 originally withheld only V6.1's fast-path privilege. That was not\n    // sufficient: the same transient family member could still re-enter through\n    // buildConsensusHypotheses()/beam/raw fallback and become the published F0.\n    // During the tiny path-local resolution window it remains recorded in the\n    // pending-family state above, but it is not an eligible physical coordinate.\n    // The previously owned coordinate is NOT republished as fresh; if no other\n    // current coordinate exists, this hop is detector-uncertain and downstream\n    // scale ownership simply continues its single-wet correction.\n    for (int index = 0; index < candidateCount; ++index)\n    {\n        auto& candidate = candidates[static_cast<std::size_t>(index)];\n        if (!candidate.valid\n            || candidate.pathIndex < 0\n            || candidate.pathIndex >= detectorPathCount)\n        {\n            continue;\n        }\n\n        const std::size_t pathSlot = static_cast<std::size_t>(candidate.pathIndex);\n        const bool ownPathFamilyPending =\n            pathFamilyPendingCount_[pathSlot] > 0\n            && pathFamilyCoordinateAgeHops_[pathSlot] <= pathFamilyMaximumAgeHopsV65\n            && pathFamilyPendingHz_[pathSlot] > 0.0f\n            && centsDistance(candidate.frequencyHz, pathFamilyPendingHz_[pathSlot]) <= 70.0f;\n\n        // A range-limited sibling that reports the lower family member while a\n        // capable path is resolving the upper member is likewise family evidence\n        // rather than a competing coordinate. lowerAliasShadowedV6() contains\n        // both the current-witness rule and V6.5's bounded pending-path rule.\n        const bool familyWitnessOnly = lowerAliasShadowedV6(index);\n\n        if (ownPathFamilyPending || familyWitnessOnly)\n            candidate.valid = false;\n    }\n\n    int authoritativeIndexV6 = -1;\n'''
    cpp = one(cpp, anchor, replacement, 'coordinate eligibility filter')
    cpp_path.write_text(cpp)

print('PENDING_FAMILY_IS_EVIDENCE_NOT_COORDINATE_V6_5_1 materialized')
