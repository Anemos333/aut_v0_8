from pathlib import Path

cpp_path = Path(__file__).resolve().parents[1] / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()

marker = 'RANGE_CENSORED_DIRECT_OWNERSHIP_V4'
if marker not in cpp:
    # V4 deliberately does not touch analyse(), Acquire, detector thresholds,
    # octave persistence, supervisor logic, Hold, or the renderer. It changes
    # only the meaning of "direct coordinate owner" during cross-path fusion.
    #
    # A low-rate path near the top of its analysis range can measure F while a
    # faster path measures 2F. If 2F is physically outside the low path's direct
    # range, the low observation is valid evidence that the periodic family
    # exists, but it is not symmetric evidence that F rather than 2F owns the
    # physical coordinate. The two independent measurements may each carry
    # ordinary local pitch error, so sibling matching uses the sum of their
    # normal direct-coordinate tolerances rather than a single-path tolerance.

    consensus_anchor = '''    int validCount = 0;\n    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)\n'''
    if cpp.count(consensus_anchor) != 1:
        raise RuntimeError(f'consensus anchor: expected one, found {cpp.count(consensus_anchor)}')

    cpp = cpp.replace(consensus_anchor, '''    // RANGE_CENSORED_DIRECT_OWNERSHIP_V4\n    const auto directMaximumForPath = [](int pathIndex) noexcept\n    {\n        switch (pathIndex)\n        {\n            case 0: return 2600.0f;\n            case 1: return 900.0f;\n            case 2: return 460.0f;\n            case 3: return 230.0f;\n            default: break;\n        }\n        return 0.0f;\n    };\n\n    const auto anotherPathOwnsUpperSibling = [&](int sourceCandidateIndex,\n                                                  float lowerFrequency) noexcept\n    {\n        const float upperFrequency = 2.0f * lowerFrequency;\n        if (upperFrequency > maximumPitchHz_)\n            return false;\n\n        // Each direct coordinate is normally clustered with a 55-cent window.\n        // Comparing two independently measured octave siblings therefore needs\n        // the combined geometric uncertainty, not another single 55-cent gate.\n        constexpr float combinedSiblingToleranceCents = 110.0f;\n\n        for (int otherIndex = 0; otherIndex < candidateCount; ++otherIndex)\n        {\n            if (otherIndex == sourceCandidateIndex)\n                continue;\n\n            const auto& other = candidates[static_cast<std::size_t>(otherIndex)];\n            if (!other.valid || !std::isfinite(other.frequencyHz)\n                || other.frequencyHz <= 0.0f)\n            {\n                continue;\n            }\n\n            // The witness must itself be on a path physically capable of owning\n            // the upper coordinate. A harmonic outside that path's direct range\n            // cannot be used to censor another detector path.\n            if (upperFrequency > directMaximumForPath(other.pathIndex) + 5.0f)\n                continue;\n\n            if (centsDistance(other.frequencyHz, upperFrequency)\n                <= combinedSiblingToleranceCents)\n            {\n                return true;\n            }\n        }\n        return false;\n    };\n\n    int validCount = 0;\n    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)\n''', 1)

    direct_anchor = '''            const bool direct = bestOctaveShift == 0;\n            const float tolerance = direct ? 55.0f : 38.0f;\n'''
    if cpp.count(direct_anchor) != 1:
        raise RuntimeError(f'directness anchor: expected one, found {cpp.count(direct_anchor)}')

    cpp = cpp.replace(direct_anchor, '''            const bool direct = bestOctaveShift == 0;\n            const bool upperSiblingOutsideThisPath = direct\n                && 2.0f * candidate.frequencyHz <= maximumPitchHz_\n                && 2.0f * candidate.frequencyHz\n                    > directMaximumForPath(candidate.pathIndex) + 5.0f;\n            const bool rangeCensoredDirect = upperSiblingOutsideThisPath\n                && anotherPathOwnsUpperSibling(candidateIndex, candidate.frequencyHz);\n            const bool coordinateDirect = direct && !rangeCensoredDirect;\n            const float tolerance = direct ? 55.0f : 38.0f;\n''', 1)

    prior_anchor = '''            const float octavePrior = direct ? 1.0f\n                : (std::abs(bestOctaveShift) == 1 ? 0.52f : 0.25f);\n'''
    if cpp.count(prior_anchor) != 1:
        raise RuntimeError(f'octave prior anchor: expected one, found {cpp.count(prior_anchor)}')

    cpp = cpp.replace(prior_anchor, '''            const float octavePrior = coordinateDirect ? 1.0f\n                : (rangeCensoredDirect ? 0.52f\n                   : (std::abs(bestOctaveShift) == 1 ? 0.52f : 0.25f));\n''', 1)

    support_anchor = '''            if ((!direct && baseScore < minimumOctaveSupport)\n                || evidenceWeight < minimumWeight)\n'''
    if cpp.count(support_anchor) != 1:
        raise RuntimeError(f'support gate anchor: expected one, found {cpp.count(support_anchor)}')

    cpp = cpp.replace(support_anchor, '''            if ((!coordinateDirect && baseScore < minimumOctaveSupport)\n                || evidenceWeight < minimumWeight)\n''', 1)

    coordinate_anchor = '''            if (direct)\n            {\n                directWeightedLogFrequency += static_cast<double>(coordinateWeight)\n                    * safeLog2(static_cast<double>(bestFrequency));\n                directCoordinateWeightSum += coordinateWeight;\n            }\n'''
    if cpp.count(coordinate_anchor) != 1:
        raise RuntimeError(f'direct coordinate anchor: expected one, found {cpp.count(coordinate_anchor)}')

    cpp = cpp.replace(coordinate_anchor, '''            if (coordinateDirect)\n            {\n                directWeightedLogFrequency += static_cast<double>(coordinateWeight)\n                    * safeLog2(static_cast<double>(bestFrequency));\n                directCoordinateWeightSum += coordinateWeight;\n            }\n''', 1)

    count_anchor = '''            ++supportCount;\n            if (direct)\n                ++directSupportCount;\n'''
    if cpp.count(count_anchor) != 1:
        raise RuntimeError(f'direct count anchor: expected one, found {cpp.count(count_anchor)}')

    cpp = cpp.replace(count_anchor, '''            ++supportCount;\n            if (coordinateDirect)\n                ++directSupportCount;\n''', 1)

    cpp_path.write_text(cpp)

print('RANGE_CENSORED_DIRECT_OWNERSHIP_V4 materialized')
