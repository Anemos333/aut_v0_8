from pathlib import Path

cpp_path = Path(__file__).resolve().parents[1] / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()

marker = 'PRIMITIVE_PERIOD_PATH_GEOMETRY_V3'
if marker not in cpp:
    # -------------------------------------------------------------------------
    # 1) Resolve a doubled-period/subharmonic inside the path that measured it.
    #    The shorter period must already have been evaluated by the normal YIN /
    #    vocal-structure machinery and must be nearly as good in every physical
    #    dimension. No threshold is lowered and no new frequency is synthesized.
    score_anchor = '''    float bestScore = -1.0f;\n    float bestSelectionScore = -1.0f;\n    int bestTau = -1;\n    float bestPeriodicity = 0.0f;\n    float bestHarmonicFamily = 0.0f;\n    float bestTonalCleanliness = 0.0f;\n\n    for (std::size_t candidateIndex = 0;\n'''
    if cpp.count(score_anchor) != 1:
        raise RuntimeError(f'candidate score anchor: expected one, found {cpp.count(score_anchor)}')
    cpp = cpp.replace(score_anchor, '''    float bestScore = -1.0f;\n    float bestSelectionScore = -1.0f;\n    int bestTau = -1;\n    float bestPeriodicity = 0.0f;\n    float bestHarmonicFamily = 0.0f;\n    float bestTonalCleanliness = 0.0f;\n\n    // PRIMITIVE_PERIOD_PATH_GEOMETRY_V3\n    std::array<int, 5> evaluatedTaus {};\n    std::array<float, 5> evaluatedScores {};\n    std::array<float, 5> evaluatedSelectionScores {};\n    std::array<float, 5> evaluatedPeriodicities {};\n    std::array<float, 5> evaluatedFamilies {};\n    std::array<float, 5> evaluatedCleanliness {};\n\n    for (std::size_t candidateIndex = 0;\n''', 1)

    selection_anchor = '''        const float selectionScore = score\n            * (directHighThresholdCandidate ? 1.35f : 1.0f);\n\n        if (selectionScore > bestSelectionScore)\n'''
    if cpp.count(selection_anchor) != 1:
        raise RuntimeError(f'selection metric anchor: expected one, found {cpp.count(selection_anchor)}')
    cpp = cpp.replace(selection_anchor, '''        const float selectionScore = score\n            * (directHighThresholdCandidate ? 1.35f : 1.0f);\n\n        evaluatedTaus[candidateIndex] = tau;\n        evaluatedScores[candidateIndex] = score;\n        evaluatedSelectionScores[candidateIndex] = selectionScore;\n        evaluatedPeriodicities[candidateIndex] = periodicity;\n        evaluatedFamilies[candidateIndex] = harmonicFamily;\n        evaluatedCleanliness[candidateIndex] = tonalCleanliness;\n\n        if (selectionScore > bestSelectionScore)\n''', 1)

    primitive_anchor = '''    const float minimumCandidateScore = rescueMode_ ? 0.34f : 0.45f;\n'''
    if cpp.count(primitive_anchor) != 1:
        raise RuntimeError(f'primitive choice anchor: expected one, found {cpp.count(primitive_anchor)}')
    cpp = cpp.replace(primitive_anchor, '''    // PRIMITIVE_PERIOD_WITHIN_PATH_V3\n    // A true period also correlates at two periods, so a noisy detector may pick\n    // the doubled lag (F0/2). Prefer the shorter/primitive lag only when that lag\n    // was independently evaluated in this same path and its periodicity, vocal\n    // family, cleanliness and score are all nearly equivalent to the longer lag.\n    // A true lower fundamental with odd-harmonic structure fails this test because\n    // its half-period correlation collapses or changes sign.\n    if (bestTau > 3)\n    {\n        const float primitiveScoreFloor = rescueMode_ ? 0.34f : 0.45f;\n        int primitiveIndex = -1;\n        float primitiveRank = -1.0f;\n        for (std::size_t index = 0; index < evaluatedTaus.size(); ++index)\n        {\n            const int tau = evaluatedTaus[index];\n            if (tau < 2 || tau >= bestTau\n                || std::abs(2 * tau - bestTau) > 3)\n            {\n                continue;\n            }\n\n            const float score = evaluatedScores[index];\n            const float periodicity = evaluatedPeriodicities[index];\n            const float family = evaluatedFamilies[index];\n            const float cleanliness = evaluatedCleanliness[index];\n            if (score < primitiveScoreFloor\n                || score < 0.80f * bestScore\n                || periodicity < 0.50f\n                || family < 0.45f\n                || cleanliness < 0.50f\n                || periodicity + 0.075f < bestPeriodicity\n                || family + 0.10f < bestHarmonicFamily\n                || cleanliness + 0.10f < bestTonalCleanliness)\n            {\n                continue;\n            }\n\n            if (evaluatedSelectionScores[index] > primitiveRank)\n            {\n                primitiveRank = evaluatedSelectionScores[index];\n                primitiveIndex = static_cast<int>(index);\n            }\n        }\n\n        if (primitiveIndex >= 0)\n        {\n            const auto index = static_cast<std::size_t>(primitiveIndex);\n            bestTau = evaluatedTaus[index];\n            bestScore = evaluatedScores[index];\n            bestSelectionScore = evaluatedSelectionScores[index];\n            bestPeriodicity = evaluatedPeriodicities[index];\n            bestHarmonicFamily = evaluatedFamilies[index];\n            bestTonalCleanliness = evaluatedCleanliness[index];\n        }\n    }\n\n    const float minimumCandidateScore = rescueMode_ ? 0.34f : 0.45f;\n''', 1)

    # -------------------------------------------------------------------------
    # 2) Cross-path range censoring. A path that reports F while its own direct
    #    analysis range physically ends below 2F cannot use that F as proof that
    #    the lower octave owns the coordinate when another path directly measures
    #    2F. It remains family evidence, just not coordinate ownership.
    consensus_anchor = '''    int validCount = 0;\n    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)\n'''
    if cpp.count(consensus_anchor) != 1:
        raise RuntimeError(f'consensus anchor: expected one, found {cpp.count(consensus_anchor)}')
    cpp = cpp.replace(consensus_anchor, '''    // RANGE_CENSORED_PATH_IS_FAMILY_EVIDENCE_V3\n    const auto directMaximumForPath = [](int pathIndex) noexcept\n    {\n        switch (pathIndex)\n        {\n            case 0: return 2600.0f;\n            case 1: return 900.0f;\n            case 2: return 460.0f;\n            case 3: return 230.0f;\n            default: break;\n        }\n        return 0.0f;\n    };\n\n    const auto anotherPathMeasuresUpperSibling = [&](int sourceCandidateIndex,\n                                                      float lowerFrequency) noexcept\n    {\n        const float upperFrequency = 2.0f * lowerFrequency;\n        if (upperFrequency > maximumPitchHz_)\n            return false;\n        for (int otherIndex = 0; otherIndex < candidateCount; ++otherIndex)\n        {\n            if (otherIndex == sourceCandidateIndex)\n                continue;\n            const auto& other = candidates[static_cast<std::size_t>(otherIndex)];\n            if (!other.valid || other.frequencyHz <= 0.0f)\n                continue;\n            if (centsDistance(other.frequencyHz, upperFrequency) <= 55.0f)\n                return true;\n        }\n        return false;\n    };\n\n    int validCount = 0;\n    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)\n''', 1)

    direct_anchor = '''            const bool direct = bestOctaveShift == 0;\n            const float tolerance = direct ? 55.0f : 38.0f;\n'''
    if cpp.count(direct_anchor) != 1:
        raise RuntimeError(f'directness anchor: expected one, found {cpp.count(direct_anchor)}')
    cpp = cpp.replace(direct_anchor, '''            const bool direct = bestOctaveShift == 0;\n            const bool rangeCensoredDirect = direct\n                && 2.0f * candidate.frequencyHz <= maximumPitchHz_\n                && 2.0f * candidate.frequencyHz\n                    > directMaximumForPath(candidate.pathIndex) + 5.0f\n                && anotherPathMeasuresUpperSibling(candidateIndex,\n                                                   candidate.frequencyHz);\n            const bool coordinateDirect = direct && !rangeCensoredDirect;\n            const float tolerance = direct ? 55.0f : 38.0f;\n''', 1)

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

print('PRIMITIVE_PERIOD_PATH_GEOMETRY_V3 materialized')
