from pathlib import Path

cpp_path = Path(__file__).resolve().parents[1] / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()

marker = 'QUALIFIED_FIRST_MINIMUM_OWNS_PATH_V6_2'
if marker not in cpp:
    state_anchor = '''    float bestHarmonicFamily = 0.0f;\n    float bestTonalCleanliness = 0.0f;\n\n    for (std::size_t candidateIndex = 0;\n'''
    state_replacement = '''    float bestHarmonicFamily = 0.0f;\n    float bestTonalCleanliness = 0.0f;\n    bool qualifiedFirstMinimumOwnsPath = false;\n\n    for (std::size_t candidateIndex = 0;\n'''
    if cpp.count(state_anchor) != 1:
        raise RuntimeError(f'state anchor: expected one, found {cpp.count(state_anchor)}')
    cpp = cpp.replace(state_anchor, state_replacement, 1)

    selection_anchor = '''        // DIRECT_HIGH_YIN_FIRST_MINIMUM_V1: selection-only preference.\n        // Never inflate the published confidence/evidence score.\n        const bool directHighThresholdCandidate = thresholdTau >= 0\n            && candidateIndex == 0\n            && effectiveSampleRate >= sampleRate_ * 0.75\n            && effectiveSampleRate / static_cast<double>(std::max(1, tau)) > 900.0\n            && harmonicFamily >= 0.60f\n            && tonalCleanliness >= 0.68f;\n        const float selectionScore = score\n            * (directHighThresholdCandidate ? 1.35f : 1.0f);\n\n        if (selectionScore > bestSelectionScore)\n        {\n            bestSelectionScore = selectionScore;\n            bestScore = score;\n            bestTau = tau;\n            bestPeriodicity = periodicity;\n            bestHarmonicFamily = harmonicFamily;\n            bestTonalCleanliness = tonalCleanliness;\n        }\n'''
    selection_replacement = '''        // QUALIFIED_FIRST_MINIMUM_OWNS_PATH_V6_2\n        // YIN's first threshold minimum is already the shortest demonstrated\n        // physical period. If that minimum also has strong residual repetition,\n        // harmonic-family structure and cleanliness, do not send the path back\n        // through a second harmonic-family election that can replace F with F/2.\n        // This changes selection only: validity thresholds and published evidence\n        // are untouched, and it adds no analysis work.\n        const bool qualifiedFirstMinimum = thresholdTau >= 0\n            && candidateIndex == 0\n            && yinConfidence >= 0.60f\n            && periodicity >= 0.52f\n            && harmonicFamily >= 0.62f\n            && tonalCleanliness >= 0.62f\n            && score >= (rescueMode_ ? 0.34f : 0.45f);\n\n        // Preserve the already-validated special handling above 900 Hz when the\n        // general first-minimum qualification does not apply.\n        const bool directHighThresholdCandidate = thresholdTau >= 0\n            && candidateIndex == 0\n            && effectiveSampleRate >= sampleRate_ * 0.75\n            && effectiveSampleRate / static_cast<double>(std::max(1, tau)) > 900.0\n            && harmonicFamily >= 0.60f\n            && tonalCleanliness >= 0.68f;\n        const float selectionScore = score\n            * (directHighThresholdCandidate ? 1.35f : 1.0f);\n\n        if (qualifiedFirstMinimum)\n        {\n            qualifiedFirstMinimumOwnsPath = true;\n            bestSelectionScore = selectionScore;\n            bestScore = score;\n            bestTau = tau;\n            bestPeriodicity = periodicity;\n            bestHarmonicFamily = harmonicFamily;\n            bestTonalCleanliness = tonalCleanliness;\n        }\n        else if (!qualifiedFirstMinimumOwnsPath\n                 && selectionScore > bestSelectionScore)\n        {\n            bestSelectionScore = selectionScore;\n            bestScore = score;\n            bestTau = tau;\n            bestPeriodicity = periodicity;\n            bestHarmonicFamily = harmonicFamily;\n            bestTonalCleanliness = tonalCleanliness;\n        }\n'''
    if cpp.count(selection_anchor) != 1:
        raise RuntimeError(f'selection anchor: expected one, found {cpp.count(selection_anchor)}')
    cpp = cpp.replace(selection_anchor, selection_replacement, 1)
    cpp_path.write_text(cpp)

print('QUALIFIED_FIRST_MINIMUM_OWNS_PATH_V6_2 materialized')
