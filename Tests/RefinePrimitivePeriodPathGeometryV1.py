from pathlib import Path

p = Path(__file__).resolve().parents[1] / 'Source' / 'ModernPitchEngine.cpp'
cpp = p.read_text()


def replace_once(anchor, replacement, label):
    global cpp
    count = cpp.count(anchor)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    cpp = cpp.replace(anchor, replacement, 1)


if 'PRIMITIVE_PERIOD_PATH_CADENCE_V5' not in cpp:
    # A) Primitive-period geometry inside one detector path.
    anchor = '''    int sourceTau = bestTau;\n    float sourcePeak = sourceLagCorrelation(bestTau);\n    for (int offset = -2; offset <= 2; ++offset)\n    {\n        const int candidateTau = bestTau + offset;\n        if (candidateTau < tauMinimum || candidateTau > tauMaximum)\n            continue;\n        const float candidatePeak = sourceLagCorrelation(candidateTau);\n        if (candidatePeak > sourcePeak)\n        {\n            sourcePeak = candidatePeak;\n            sourceTau = candidateTau;\n        }\n    }\n\n    double refinedTau = static_cast<double>(sourceTau);\n'''
    replacement = '''    int sourceTau = bestTau;\n    float sourcePeak = sourceLagCorrelation(bestTau);\n    for (int offset = -2; offset <= 2; ++offset)\n    {\n        const int candidateTau = bestTau + offset;\n        if (candidateTau < tauMinimum || candidateTau > tauMaximum)\n            continue;\n        const float candidatePeak = sourceLagCorrelation(candidateTau);\n        if (candidatePeak > sourcePeak)\n        {\n            sourcePeak = candidatePeak;\n            sourceTau = candidateTau;\n        }\n    }\n\n    // PRIMITIVE_PERIOD_PATH_CADENCE_V5\n    // A doubled lag may correlate well merely because it contains two true\n    // cycles. T/2 owns the coordinate only when source, residual, YIN and comb\n    // geometry all say it is essentially the same complete repetition.\n    if (sourceTau >= 2 * tauMinimum)\n    {\n        const int centre = static_cast<int>(std::lround(\n            0.5 * static_cast<double>(sourceTau)));\n        int primitiveTau = centre;\n        float primitiveSourcePeak = -1.0f;\n        for (int offset = -2; offset <= 2; ++offset)\n        {\n            const int tau = centre + offset;\n            if (tau < tauMinimum || tau > tauMaximum)\n                continue;\n            const float peak = sourceLagCorrelation(tau);\n            if (peak > primitiveSourcePeak)\n            {\n                primitiveSourcePeak = peak;\n                primitiveTau = tau;\n            }\n        }\n\n        const float selectedResidual = residualLagCorrelation(sourceTau);\n        const float primitiveResidual = residualLagCorrelation(primitiveTau);\n        const float selectedYin = clamp01(\n            1.0f - difference_[static_cast<std::size_t>(sourceTau)]);\n        const float primitiveYin = clamp01(\n            1.0f - difference_[static_cast<std::size_t>(primitiveTau)]);\n        const float selectedComb = residualHarmonicContrast(sourceTau);\n        const float primitiveComb = residualHarmonicContrast(primitiveTau);\n\n        const bool samePrimitiveWaveform = primitiveSourcePeak >= 0.58f\n            && primitiveSourcePeak >= sourcePeak - 0.085f\n            && primitiveResidual >= 0.58f\n            && primitiveResidual >= selectedResidual - 0.085f\n            && primitiveYin >= selectedYin - 0.10f\n            && primitiveComb >= selectedComb - 0.12f;\n        if (samePrimitiveWaveform)\n        {\n            sourceTau = primitiveTau;\n            sourcePeak = primitiveSourcePeak;\n        }\n    }\n\n    double refinedTau = static_cast<double>(sourceTau);\n'''
    replace_once(anchor, replacement, 'primitive geometry')

    # C1) Consensus provenance: a path whose direct range ends below 2F may
    # verify the family at F but cannot own F when another capable path directly
    # measures the strong upper sibling 2F.
    anchor = '''    int validCount = 0;\n    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)\n'''
    replacement = '''    const auto directMaximumForPathV5 = [](int pathIndex) noexcept\n    {\n        switch (pathIndex)\n        {\n            case 0: return 2600.0f;\n            case 1: return 900.0f;\n            case 2: return 460.0f;\n            case 3: return 230.0f;\n            default: return 0.0f;\n        }\n    };\n    const auto rangeLimitedLowerAliasV5 = [&](int sourceIndex,\n                                               float lowerHz) noexcept\n    {\n        if (!(lowerHz > 0.0f))\n            return false;\n        const auto& source = candidates[static_cast<std::size_t>(sourceIndex)];\n        const float upperHz = 2.0f * lowerHz;\n        if (upperHz > maximumPitchHz_\n            || upperHz <= directMaximumForPathV5(source.pathIndex) + 5.0f)\n            return false;\n\n        for (int i = 0; i < candidateCount; ++i)\n        {\n            if (i == sourceIndex)\n                continue;\n            const auto& other = candidates[static_cast<std::size_t>(i)];\n            if (!other.valid || !std::isfinite(other.frequencyHz)\n                || other.frequencyHz <= 0.0f\n                || upperHz > directMaximumForPathV5(other.pathIndex) + 5.0f)\n                continue;\n            const float family = other.harmonicFamily >= 0.0f\n                ? clamp01(other.harmonicFamily) : 0.0f;\n            const float clean = other.tonalCleanliness >= 0.0f\n                ? clamp01(other.tonalCleanliness) : 0.0f;\n            if (other.periodicity >= 0.52f\n                && family >= 0.58f\n                && clean >= 0.62f\n                && centsDistance(other.frequencyHz, upperHz) <= 70.0f)\n                return true;\n        }\n        return false;\n    };\n\n    int validCount = 0;\n    for (int seedIndex = 0; seedIndex < seedCount; ++seedIndex)\n'''
    replace_once(anchor, replacement, 'consensus provenance helper')

    anchor = '''            const bool direct = bestOctaveShift == 0;\n            const float tolerance = direct ? 55.0f : 38.0f;\n'''
    replacement = '''            const bool direct = bestOctaveShift == 0;\n            const bool rangeLimitedAlias = direct\n                && rangeLimitedLowerAliasV5(candidateIndex, candidate.frequencyHz);\n            const bool coordinateDirect = direct && !rangeLimitedAlias;\n            const float tolerance = direct ? 55.0f : 38.0f;\n'''
    replace_once(anchor, replacement, 'coordinate directness')

    anchor = '''            const float octavePrior = direct ? 1.0f\n                : (std::abs(bestOctaveShift) == 1 ? 0.52f : 0.25f);\n'''
    replacement = '''            const float octavePrior = coordinateDirect ? 1.0f\n                : (rangeLimitedAlias ? 0.52f\n                   : (std::abs(bestOctaveShift) == 1 ? 0.52f : 0.25f));\n'''
    replace_once(anchor, replacement, 'coordinate prior')

    replace_once(
        '''            if ((!direct && baseScore < minimumOctaveSupport)\n                || evidenceWeight < minimumWeight)\n''',
        '''            if ((!coordinateDirect && baseScore < minimumOctaveSupport)\n                || evidenceWeight < minimumWeight)\n''',
        'coordinate support gate')

    replace_once(
        '''            if (direct)\n            {\n                directWeightedLogFrequency += static_cast<double>(coordinateWeight)\n                    * safeLog2(static_cast<double>(bestFrequency));\n                directCoordinateWeightSum += coordinateWeight;\n            }\n''',
        '''            if (coordinateDirect)\n            {\n                directWeightedLogFrequency += static_cast<double>(coordinateWeight)\n                    * safeLog2(static_cast<double>(bestFrequency));\n                directCoordinateWeightSum += coordinateWeight;\n            }\n''',
        'direct coordinate accumulation')

    replace_once(
        '''            ++supportCount;\n            if (direct)\n                ++directSupportCount;\n''',
        '''            ++supportCount;\n            if (coordinateDirect)\n                ++directSupportCount;\n''',
        'direct support count')

    # C2) Raw fallback must use the same provenance rule; V4 omitted this path.
    anchor = '''    const int candidateCount = collectFreshCandidates(candidates);\n    if (candidateCount <= 0)\n        return {};\n\n'''
    replacement = '''    const int candidateCount = collectFreshCandidates(candidates);\n    if (candidateCount <= 0)\n        return {};\n\n    const auto directMaximumForPathDecodeV5 = [](int pathIndex) noexcept\n    {\n        switch (pathIndex)\n        {\n            case 0: return 2600.0f;\n            case 1: return 900.0f;\n            case 2: return 460.0f;\n            case 3: return 230.0f;\n            default: return 0.0f;\n        }\n    };\n    const auto rawCandidateShadowedByUpperV5 = [&](int sourceIndex) noexcept\n    {\n        const auto& source = candidates[static_cast<std::size_t>(sourceIndex)];\n        if (!source.valid || !(source.frequencyHz > 0.0f))\n            return false;\n        const float upperHz = 2.0f * source.frequencyHz;\n        if (upperHz > maximumPitchHz_\n            || upperHz <= directMaximumForPathDecodeV5(source.pathIndex) + 5.0f)\n            return false;\n\n        for (int i = 0; i < candidateCount; ++i)\n        {\n            if (i == sourceIndex)\n                continue;\n            const auto& other = candidates[static_cast<std::size_t>(i)];\n            if (!other.valid || !std::isfinite(other.frequencyHz)\n                || other.frequencyHz <= 0.0f\n                || upperHz > directMaximumForPathDecodeV5(other.pathIndex) + 5.0f)\n                continue;\n            const float family = other.harmonicFamily >= 0.0f\n                ? clamp01(other.harmonicFamily) : 0.0f;\n            const float clean = other.tonalCleanliness >= 0.0f\n                ? clamp01(other.tonalCleanliness) : 0.0f;\n            if (other.periodicity >= 0.52f\n                && family >= 0.58f\n                && clean >= 0.62f\n                && centsDistance(other.frequencyHz, upperHz) <= 70.0f)\n                return true;\n        }\n        return false;\n    };\n\n'''
    replace_once(anchor, replacement, 'raw fallback helper')

    replace_once(
        '''            const auto& candidate = candidates[static_cast<std::size_t>(index)];\n            if (!candidate.valid || candidate.ageInHops != 0\n''',
        '''            const auto& candidate = candidates[static_cast<std::size_t>(index)];\n            if (rawCandidateShadowedByUpperV5(index))\n                continue;\n            if (!candidate.valid || candidate.ageInHops != 0\n''',
        'raw fallback candidate')

    # B) Cadence-aware confirmation: empty/stale hops do not contradict fresh
    # octave evidence. The existing >12 invalid-hop watchdog remains the bound.
    replace_once(
        '''    if (!decision.valid)\n    {\n        pendingOctaveDelta_ = 0;\n        pendingOctaveCount_ = 0;\n        pendingOctaveFrequencyHz_ = 0.0f;\n        return false;\n    }\n''',
        '''    if (!decision.valid)\n    {\n        // PATH_CADENCE_OCTAVE_EVIDENCE_V1\n        // No newly scheduled measurement is absence, not contradiction.\n        return false;\n    }\n''',
        'invalid decision cadence')

    replace_once(
        '''    if (!isOctaveLikeTransition(trackedPitchHz_,\n                                decision.candidate.frequencyHz,\n                                octaveDelta,\n                                residualCents))\n    {\n        pendingOctaveDelta_ = 0;\n        pendingOctaveCount_ = 0;\n        pendingOctaveFrequencyHz_ = 0.0f;\n        return true;\n    }\n''',
        '''    if (!isOctaveLikeTransition(trackedPitchHz_,\n                                decision.candidate.frequencyHz,\n                                octaveDelta,\n                                residualCents))\n    {\n        if (decision.freshSupportMask != 0)\n        {\n            pendingOctaveDelta_ = 0;\n            pendingOctaveCount_ = 0;\n            pendingOctaveFrequencyHz_ = 0.0f;\n        }\n        return true;\n    }\n''',
        'same-register cadence')

    p.write_text(cpp)

print('PRIMITIVE_PERIOD_PATH_CADENCE_V5 materialized')
