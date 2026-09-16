from pathlib import Path

cpp_path = Path(__file__).resolve().parents[1] / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()

marker = 'PRIMITIVE_PERIOD_PATH_CADENCE_V5'
if marker not in cpp:
    # V5 stays entirely in detector measurement geometry. It does not touch the
    # renderer, wet authority, scale supervisor, Hold, Amount, or musical target.
    # It addresses three measured failure mechanisms independently:
    #   A) a path selecting a doubled lag (F/2) although the primitive lag is
    #      already an almost equally complete physical repetition;
    #   B) octave-confirmation memory being erased between natural updates of a
    #      slow detector path;
    #   C) a band-limited low-rate alias winning raw fallback over a strong
    #      upper sibling measured by a path capable of owning that coordinate.

    # ------------------------------------------------------------------
    # A) Within-path primitive period: after the normal candidate has survived
    # all existing structural gates, compare its source/refidual repetition to
    # the half lag.  This is deliberately relative geometry, not a weaker
    # validity threshold.  A true fundamental with a strong second harmonic is
    # protected because odd components reverse at T/2 and reduce correlation.
    primitive_anchor = '''    int sourceTau = bestTau;\n    float sourcePeak = sourceLagCorrelation(bestTau);\n    for (int offset = -2; offset <= 2; ++offset)\n    {\n        const int candidateTau = bestTau + offset;\n        if (candidateTau < tauMinimum || candidateTau > tauMaximum)\n            continue;\n        const float candidatePeak = sourceLagCorrelation(candidateTau);\n        if (candidatePeak > sourcePeak)\n        {\n            sourcePeak = candidatePeak;\n            sourceTau = candidateTau;\n        }\n    }\n\n    double refinedTau = static_cast<double>(sourceTau);\n'''
    if cpp.count(primitive_anchor) != 1:
        raise RuntimeError(f'primitive source anchor: expected one, found {cpp.count(primitive_anchor)}')

    primitive_replacement = '''    int sourceTau = bestTau;\n    float sourcePeak = sourceLagCorrelation(bestTau);\n    for (int offset = -2; offset <= 2; ++offset)\n    {\n        const int candidateTau = bestTau + offset;\n        if (candidateTau < tauMinimum || candidateTau > tauMaximum)\n            continue;\n        const float candidatePeak = sourceLagCorrelation(candidateTau);\n        if (candidatePeak > sourcePeak)\n        {\n            sourcePeak = candidatePeak;\n            sourceTau = candidateTau;\n        }\n    }\n\n    // PRIMITIVE_PERIOD_PATH_CADENCE_V5\n    // A multiple of the true period can correlate extremely well in noise simply\n    // because it contains two complete real cycles.  Prefer the shorter period\n    // only when T/2 independently reconstructs essentially the same waveform in\n    // both the source and inverse-filtered residual and keeps the same YIN/comb\n    // geometry.  No validity floor is relaxed and no unmeasured coordinate is\n    // created.\n    if (sourceTau >= 2 * tauMinimum)\n    {\n        const int primitiveCentre = static_cast<int>(std::lround(\n            0.5 * static_cast<double>(sourceTau)));\n        int primitiveTau = primitiveCentre;\n        float primitiveSourcePeak = -1.0f;\n        for (int offset = -2; offset <= 2; ++offset)\n        {\n            const int candidateTau = primitiveCentre + offset;\n            if (candidateTau < tauMinimum || candidateTau > tauMaximum)\n                continue;\n            const float candidatePeak = sourceLagCorrelation(candidateTau);\n            if (candidatePeak > primitiveSourcePeak)\n            {\n                primitiveSourcePeak = candidatePeak;\n                primitiveTau = candidateTau;\n            }\n        }\n\n        const float selectedResidual = residualLagCorrelation(sourceTau);\n        const float primitiveResidual = residualLagCorrelation(primitiveTau);\n        const float selectedYin = clamp01(\n            1.0f - difference_[static_cast<std::size_t>(sourceTau)]);\n        const float primitiveYin = clamp01(\n            1.0f - difference_[static_cast<std::size_t>(primitiveTau)]);\n        const float selectedComb = residualHarmonicContrast(sourceTau);\n        const float primitiveComb = residualHarmonicContrast(primitiveTau);\n\n        const bool samePrimitiveWaveform = primitiveSourcePeak >= 0.58f\n            && primitiveSourcePeak >= sourcePeak - 0.085f\n            && primitiveResidual >= 0.58f\n            && primitiveResidual >= selectedResidual - 0.085f\n            && primitiveYin >= selectedYin - 0.10f\n            && primitiveComb >= selectedComb - 0.12f;\n        if (samePrimitiveWaveform)\n        {\n            sourceTau = primitiveTau;\n            sourcePeak = primitiveSourcePeak;\n        }\n    }\n\n    double refinedTau = static_cast<double>(sourceTau);\n'''
    cpp = cpp.replace(primitive_anchor, primitive_replacement, 1)

    # ------------------------------------------------------------------
    # C) Cross-path provenance. The same helper is used in consensus and raw
    # fallback. A lower candidate is shadowed only when its octave lies outside
    # that path's configured direct range AND another physically capable path
    # currently measures that upper sibling with strong source structure.
    consensus_anchor = '''    int validCount = 0;\n    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)\n'''
    if cpp.count(consensus_anchor) != 1:
        raise RuntimeError(f'consensus anchor: expected one, found {cpp.count(consensus_anchor)}')

    consensus_helper = '''    const auto directMaximumForPathV5 = [](int pathIndex) noexcept\n    {\n        switch (pathIndex)\n        {\n            case 0: return 2600.0f;\n            case 1: return 900.0f;\n            case 2: return 460.0f;\n            case 3: return 230.0f;\n            default: break;\n        }\n        return 0.0f;\n    };\n\n    const auto rangeLimitedLowerAliasV5 = [&](int sourceCandidateIndex,\n                                               float lowerFrequency) noexcept\n    {\n        if (!(lowerFrequency > 0.0f))\n            return false;\n        const auto& source = candidates[static_cast<std::size_t>(sourceCandidateIndex)];\n        const float upperFrequency = 2.0f * lowerFrequency;\n        if (upperFrequency > maximumPitchHz_\n            || upperFrequency <= directMaximumForPathV5(source.pathIndex) + 5.0f)\n        {\n            return false;\n        }\n\n        for (int otherIndex = 0; otherIndex < candidateCount; ++otherIndex)\n        {\n            if (otherIndex == sourceCandidateIndex)\n                continue;\n            const auto& other = candidates[static_cast<std::size_t>(otherIndex)];\n            if (!other.valid || !std::isfinite(other.frequencyHz)\n                || other.frequencyHz <= 0.0f\n                || upperFrequency > directMaximumForPathV5(other.pathIndex) + 5.0f)\n            {\n                continue;\n            }\n            const float family = other.harmonicFamily >= 0.0f\n                ? clamp01(other.harmonicFamily) : 0.0f;\n            const float cleanliness = other.tonalCleanliness >= 0.0f\n                ? clamp01(other.tonalCleanliness) : 0.0f;\n            if (other.periodicity >= 0.52f\n                && family >= 0.58f\n                && cleanliness >= 0.62f\n                && centsDistance(other.frequencyHz, upperFrequency) <= 70.0f)\n            {\n                return true;\n            }\n        }\n        return false;\n    };\n\n    int validCount = 0;\n    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)\n'''
    cpp = cpp.replace(consensus_anchor, consensus_helper, 1)

    direct_anchor = '''            const bool direct = bestOctaveShift == 0;\n            const float tolerance = direct ? 55.0f : 38.0f;\n'''
    if cpp.count(direct_anchor) != 1:
        raise RuntimeError(f'directness anchor: expected one, found {cpp.count(direct_anchor)}')
    cpp = cpp.replace(direct_anchor, '''            const bool direct = bestOctaveShift == 0;\n            const bool rangeLimitedAlias = direct\n                && rangeLimitedLowerAliasV5(candidateIndex, candidate.frequencyHz);\n            const bool coordinateDirect = direct && !rangeLimitedAlias;\n            const float tolerance = direct ? 55.0f : 38.0f;\n''', 1)

    prior_anchor = '''            const float octavePrior = direct ? 1.0f\n                : (std::abs(bestOctaveShift) == 1 ? 0.52f : 0.25f);\n'''
    if cpp.count(prior_anchor) != 1:
        raise RuntimeError(f'octave prior anchor: expected one, found {cpp.count(prior_anchor)}')
    cpp = cpp.replace(prior_anchor, '''            const float octavePrior = coordinateDirect ? 1.0f\n                : (rangeLimitedAlias ? 0.52f\n                   : (std::abs(bestOctaveShift) == 1 ? 0.52f : 0.25f));\n''', 1)

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

    decode_anchor = '''    const int candidateCount = collectFreshCandidates(candidates);\n    if (candidateCount <= 0)\n        return {};\n\n    // DETECTOR_VETO_NOT_PERMISSION_V1\n'''
    if cpp.count(decode_anchor) != 1:
        raise RuntimeError(f'decode helper anchor: expected one, found {cpp.count(decode_anchor)}')

    decode_helper = '''    const int candidateCount = collectFreshCandidates(candidates);\n    if (candidateCount <= 0)\n        return {};\n\n    const auto directMaximumForPathDecodeV5 = [](int pathIndex) noexcept\n    {\n        switch (pathIndex)\n        {\n            case 0: return 2600.0f;\n            case 1: return 900.0f;\n            case 2: return 460.0f;\n            case 3: return 230.0f;\n            default: break;\n        }\n        return 0.0f;\n    };\n    const auto rawCandidateShadowedByUpperV5 = [&](int sourceIndex) noexcept\n    {\n        const auto& source = candidates[static_cast<std::size_t>(sourceIndex)];\n        if (!source.valid || !(source.frequencyHz > 0.0f))\n            return false;\n        const float upperFrequency = 2.0f * source.frequencyHz;\n        if (upperFrequency > maximumPitchHz_\n            || upperFrequency <= directMaximumForPathDecodeV5(source.pathIndex) + 5.0f)\n        {\n            return false;\n        }\n        for (int otherIndex = 0; otherIndex < candidateCount; ++otherIndex)\n        {\n            if (otherIndex == sourceIndex)\n                continue;\n            const auto& other = candidates[static_cast<std::size_t>(otherIndex)];\n            if (!other.valid || !std::isfinite(other.frequencyHz)\n                || other.frequencyHz <= 0.0f\n                || upperFrequency > directMaximumForPathDecodeV5(other.pathIndex) + 5.0f)\n            {\n                continue;\n            }\n            const float family = other.harmonicFamily >= 0.0f\n                ? clamp01(other.harmonicFamily) : 0.0f;\n            const float cleanliness = other.tonalCleanliness >= 0.0f\n                ? clamp01(other.tonalCleanliness) : 0.0f;\n            if (other.periodicity >= 0.52f\n                && family >= 0.58f\n                && cleanliness >= 0.62f\n                && centsDistance(other.frequencyHz, upperFrequency) <= 70.0f)\n            {\n                return true;\n            }\n        }\n        return false;\n    };\n\n    // DETECTOR_VETO_NOT_PERMISSION_V1\n'''
    cpp = cpp.replace(decode_anchor, decode_helper, 1)

    raw_loop_anchor = '''            const auto& candidate = candidates[static_cast<std::size_t>(index)];\n            if (!candidate.valid || candidate.ageInHops != 0\n'''
    if cpp.count(raw_loop_anchor) != 1:
        raise RuntimeError(f'raw fallback anchor: expected one, found {cpp.count(raw_loop_anchor)}')
    cpp = cpp.replace(raw_loop_anchor, '''            const auto& candidate = candidates[static_cast<std::size_t>(index)];\n            if (rawCandidateShadowedByUpperV5(index))\n                continue;\n            if (!candidate.valid || candidate.ageInHops != 0\n''', 1)

    # ------------------------------------------------------------------
    # B) Path-cadence octave persistence. Lack of a new detector decision is not
    # contradictory evidence. Keep the pending geometric challenger across empty
    # hops; the existing invalidHopCount watchdog still clears all pending state
    # after 12 genuinely invalid hops. Likewise, a stale same-register decision
    # may hold the tracked F0 but cannot erase fresh octave evidence.
    invalid_anchor = '''    if (!decision.valid)\n    {\n        pendingOctaveDelta_ = 0;\n        pendingOctaveCount_ = 0;\n        pendingOctaveFrequencyHz_ = 0.0f;\n        return false;\n    }\n'''
    if cpp.count(invalid_anchor) != 1:
        raise RuntimeError(f'pending invalid anchor: expected one, found {cpp.count(invalid_anchor)}')
    cpp = cpp.replace(invalid_anchor, '''    if (!decision.valid)\n    {\n        // PATH_CADENCE_OCTAVE_EVIDENCE_V1: absence of a newly scheduled path\n        // measurement is not evidence against a pending octave. processSample()\n        // still clears this state after its existing finite invalid-hop watchdog.\n        return false;\n    }\n''', 1)

    normal_non_octave_anchor = '''    if (!isOctaveLikeTransition(trackedPitchHz_,\n                                decision.candidate.frequencyHz,\n                                octaveDelta,\n                                residualCents))\n    {\n        pendingOctaveDelta_ = 0;\n        pendingOctaveCount_ = 0;\n        pendingOctaveFrequencyHz_ = 0.0f;\n        return true;\n    }\n'''
    if cpp.count(normal_non_octave_anchor) != 1:
        raise RuntimeError(f'normal non-octave anchor: expected one, found {cpp.count(normal_non_octave_anchor)}')
    cpp = cpp.replace(normal_non_octave_anchor, '''    if (!isOctaveLikeTransition(trackedPitchHz_,\n                                decision.candidate.frequencyHz,\n                                octaveDelta,\n                                residualCents))\n    {\n        // Only a genuinely refreshed same-register/non-octave measurement\n        // contradicts the pending challenger. A reused low-rate observation has\n        // no right to erase evidence simply because its path has not run again.\n        if (decision.freshSupportMask != 0)\n        {\n            pendingOctaveDelta_ = 0;\n            pendingOctaveCount_ = 0;\n            pendingOctaveFrequencyHz_ = 0.0f;\n        }\n        return true;\n    }\n''', 1)

    cpp_path.write_text(cpp)

print('PRIMITIVE_PERIOD_PATH_CADENCE_V5 materialized')
