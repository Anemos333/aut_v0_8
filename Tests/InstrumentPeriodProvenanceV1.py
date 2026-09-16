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


marker = 'PERIOD_PROVENANCE_DIAGNOSTIC_V1'
if marker not in cpp:
    h = one(h,
'''            int pathIndex = -1;
            int ageInHops = 1000;
            bool valid = false;
''',
'''            int pathIndex = -1;
            int ageInHops = 1000;

            // PERIOD_PROVENANCE_DIAGNOSTIC_V1: CI-only fields materialized by
            // Tests/InstrumentPeriodProvenanceV1.py. They expose measurement
            // provenance without changing detector decisions.
            int diagnosticThresholdTau = -1;
            int diagnosticGlobalTau = -1;
            int diagnosticBestTau = -1;
            int diagnosticSourceTau = -1;
            float diagnosticThresholdYin = -1.0f;
            float diagnosticThresholdPeriodicity = -1.0f;
            float diagnosticThresholdFamily = -1.0f;
            float diagnosticThresholdCleanliness = -1.0f;
            float diagnosticThresholdScore = -1.0f;
            float diagnosticSelectedSourceCorrelation = -2.0f;
            float diagnosticPrimitiveSourceCorrelation = -2.0f;
            float diagnosticSelectedResidualCorrelation = -2.0f;
            float diagnosticPrimitiveResidualCorrelation = -2.0f;
            float diagnosticSelectedYin = -1.0f;
            float diagnosticPrimitiveYin = -1.0f;
            bool valid = false;
''', 'PitchCandidate provenance fields')

    cpp = one(cpp,
'''    float bestHarmonicFamily = 0.0f;
    float bestTonalCleanliness = 0.0f;

    for (std::size_t candidateIndex = 0;
''',
'''    float bestHarmonicFamily = 0.0f;
    float bestTonalCleanliness = 0.0f;

    // PERIOD_PROVENANCE_DIAGNOSTIC_V1
    float diagnosticThresholdYin = -1.0f;
    float diagnosticThresholdPeriodicity = -1.0f;
    float diagnosticThresholdFamily = -1.0f;
    float diagnosticThresholdCleanliness = -1.0f;
    float diagnosticThresholdScore = -1.0f;

    for (std::size_t candidateIndex = 0;
''', 'diagnostic locals')

    score_anchor = '''        const float score = (0.44f * yinConfidence
                           + 0.22f * periodicity
                           + 0.17f * cycleFamily
                           + 0.17f * harmonicContrast)
                          * periodSupport
                          * candidatePriors[candidateIndex]
                          * (0.82f + 0.18f * snrSupport);

'''
    score_replacement = score_anchor + '''        if (candidateIndex == 0 && thresholdTau >= 0)
        {
            diagnosticThresholdYin = yinConfidence;
            diagnosticThresholdPeriodicity = periodicity;
            diagnosticThresholdFamily = harmonicFamily;
            diagnosticThresholdCleanliness = tonalCleanliness;
            diagnosticThresholdScore = score;
        }

'''
    cpp = one(cpp, score_anchor, score_replacement, 'threshold metrics')

    result_anchor = '''    result.frequencyHz = frequency;
    result.confidence = clamp01(bestScore);
    result.periodicity = bestPeriodicity;
    result.harmonicFamily = bestHarmonicFamily;
    result.aperiodicity = 1.0f - bestHarmonicFamily;
    result.tonalCleanliness = bestTonalCleanliness;
'''
    result_replacement = '''    result.frequencyHz = frequency;
    result.confidence = clamp01(bestScore);
    result.periodicity = bestPeriodicity;
    result.harmonicFamily = bestHarmonicFamily;
    result.aperiodicity = 1.0f - bestHarmonicFamily;
    result.tonalCleanliness = bestTonalCleanliness;

    // PERIOD_PROVENANCE_DIAGNOSTIC_V1
    result.diagnosticThresholdTau = thresholdTau;
    result.diagnosticGlobalTau = globalTau;
    result.diagnosticBestTau = bestTau;
    result.diagnosticSourceTau = sourceTau;
    result.diagnosticThresholdYin = diagnosticThresholdYin;
    result.diagnosticThresholdPeriodicity = diagnosticThresholdPeriodicity;
    result.diagnosticThresholdFamily = diagnosticThresholdFamily;
    result.diagnosticThresholdCleanliness = diagnosticThresholdCleanliness;
    result.diagnosticThresholdScore = diagnosticThresholdScore;
    result.diagnosticSelectedSourceCorrelation = sourceLagCorrelation(sourceTau);
    result.diagnosticSelectedResidualCorrelation = residualLagCorrelation(sourceTau);
    result.diagnosticSelectedYin = clamp01(
        1.0f - difference_[static_cast<std::size_t>(sourceTau)]);
    const int diagnosticPrimitiveTau = sourceTau >= 2 * tauMinimum
        ? std::clamp(static_cast<int>(std::lround(0.5 * static_cast<double>(sourceTau))),
                     tauMinimum, tauMaximum)
        : -1;
    if (diagnosticPrimitiveTau > 0)
    {
        result.diagnosticPrimitiveSourceCorrelation = sourceLagCorrelation(diagnosticPrimitiveTau);
        result.diagnosticPrimitiveResidualCorrelation = residualLagCorrelation(diagnosticPrimitiveTau);
        result.diagnosticPrimitiveYin = clamp01(
            1.0f - difference_[static_cast<std::size_t>(diagnosticPrimitiveTau)]);
    }
'''
    cpp = one(cpp, result_anchor, result_replacement, 'result provenance')

    cpp_path.write_text(cpp)
    h_path.write_text(h)

print('PERIOD_PROVENANCE_DIAGNOSTIC_V1 materialized')
