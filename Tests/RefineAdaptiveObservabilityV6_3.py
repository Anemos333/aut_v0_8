from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
h_path = root / 'Source' / 'ModernPitchEngine.h'
cpp = cpp_path.read_text()
h = h_path.read_text()


def one(text, anchor, replacement, label):
    count = text.count(anchor)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(anchor, replacement, 1)


marker = 'ADAPTIVE_OBSERVABILITY_PERIOD_GATE_V6_3'
if marker not in cpp:
    # V6.3 does not create a stable/acquire/transition-dependent detector mode.
    # It adds one physical-property gate to V6.1: a path measurement may bypass
    # consensus/beam only when its measured period is irreducible. If a shorter
    # integer divisor explains essentially the same samples, the measurement is
    # still valid evidence but cannot claim immediate coordinate authority.
    h = one(
        h,
        '''            int pathIndex = -1;\n            int ageInHops = 1000;\n            bool valid = false;\n''',
        '''            int pathIndex = -1;\n            int ageInHops = 1000;\n            // ADAPTIVE_OBSERVABILITY_PERIOD_GATE_V6_3: physical ambiguity of\n            // this path's period estimate. This is independent of tracking\n            // state and never invalidates a real measurement.\n            bool periodCoordinateAmbiguous = false;\n            bool valid = false;\n''',
        'PitchCandidate ambiguity field')

    source_anchor = '''    if (refinedTau <= 0.0)\n        return result;\n\n    const float frequency = static_cast<float>(effectiveSampleRate / refinedTau);\n'''
    source_replacement = '''    if (refinedTau <= 0.0)\n        return result;\n\n    // ADAPTIVE_OBSERVABILITY_PERIOD_GATE_V6_3\n    // A long lag is not an unambiguous coordinate if a shorter integer divisor\n    // reproduces essentially the same source and residual periodicity. This is\n    // a geometry test, not a confidence fallback and not a tracking-state rule.\n    bool periodCoordinateAmbiguousV63 = false;\n    const float selectedSourceCorrelationV63 = sourceLagCorrelation(sourceTau);\n    const float selectedResidualCorrelationV63 = residualLagCorrelation(sourceTau);\n    const float selectedYinV63 = clamp01(\n        1.0f - difference_[static_cast<std::size_t>(sourceTau)]);\n\n    for (int divisor = 2; divisor <= 4 && !periodCoordinateAmbiguousV63; ++divisor)\n    {\n        const int primitiveTau = static_cast<int>(std::lround(\n            static_cast<double>(sourceTau) / static_cast<double>(divisor)));\n        if (primitiveTau < tauMinimum || primitiveTau > tauMaximum)\n            continue;\n\n        // The YIN value is already available, so use it as a cheap first gate\n        // before doing the two correlation passes. This keeps V6.3 bounded.\n        const float primitiveYin = clamp01(\n            1.0f - difference_[static_cast<std::size_t>(primitiveTau)]);\n        if (primitiveYin < selectedYinV63 - 0.08f)\n            continue;\n\n        const float primitiveSourceCorrelation = sourceLagCorrelation(primitiveTau);\n        if (primitiveSourceCorrelation < selectedSourceCorrelationV63 - 0.04f)\n            continue;\n\n        const float primitiveResidualCorrelation = residualLagCorrelation(primitiveTau);\n        if (primitiveResidualCorrelation < selectedResidualCorrelationV63 - 0.08f)\n            continue;\n\n        periodCoordinateAmbiguousV63 = true;\n    }\n\n    const float frequency = static_cast<float>(effectiveSampleRate / refinedTau);\n'''
    cpp = one(cpp, source_anchor, source_replacement, 'period reducibility geometry')

    result_anchor = '''    result.tonalCleanliness = bestTonalCleanliness;\n'''
    result_replacement = '''    result.tonalCleanliness = bestTonalCleanliness;\n    result.periodCoordinateAmbiguous = periodCoordinateAmbiguousV63;\n'''
    cpp = one(cpp, result_anchor, result_replacement, 'publish ambiguity')

    authority_anchor = '''        return c.periodicity >= 0.58f\n            && family >= 0.68f\n'''
    authority_replacement = '''        // ADAPTIVE_OBSERVABILITY_PERIOD_GATE_V6_3\n        // Ambiguous periods remain valid detector evidence, but V6.1 must not\n        // bypass the historical resolver on their behalf.\n        return !c.periodCoordinateAmbiguous\n            && c.periodicity >= 0.58f\n            && family >= 0.68f\n'''
    cpp = one(cpp, authority_anchor, authority_replacement, 'V6.1 authority gate')

    cpp_path.write_text(cpp)
    h_path.write_text(h)

print('ADAPTIVE_OBSERVABILITY_PERIOD_GATE_V6_3 materialized')
