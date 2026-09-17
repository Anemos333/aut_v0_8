from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()


def one(text, anchor, replacement, label):
    count = text.count(anchor)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(anchor, replacement, 1)


marker = 'PRIMITIVE_DIVISOR_GEOMETRY_V6_6'
if marker not in cpp:
    # V6.6 is deliberately measurement-only and memoryless. The current path's
    # selected lag T is tested against T/4, T/3 and T/2. A shorter divisor may
    # replace T only when the CURRENT source, residual and YIN geometry says it
    # explains essentially the same samples. This is the measured signature of
    # a reducible 2P/3P/4P basin. No detector history, scale, target or renderer
    # state participates.
    #
    # Diagnostic basis (+6 dB stress matrix): matching divisors of wrong family
    # members lost only 0.0018..0.0184 source correlation, while shorter divisors
    # of correct coordinates lost at least ~0.255 even on strong-second controls.
    # The 0.12 source margin therefore leaves a wide measured separation while
    # tolerating substantially more real-world variation than the diagnostic.

    anchor = '''    int sourceTau = bestTau;\n    float sourcePeak = sourceLagCorrelation(bestTau);\n    for (int offset = -2; offset <= 2; ++offset)\n    {\n        const int candidateTau = bestTau + offset;\n        if (candidateTau < tauMinimum || candidateTau > tauMaximum)\n            continue;\n        const float candidatePeak = sourceLagCorrelation(candidateTau);\n        if (candidatePeak > sourcePeak)\n        {\n            sourcePeak = candidatePeak;\n            sourceTau = candidateTau;\n        }\n    }\n\n    double refinedTau = static_cast<double>(sourceTau);\n'''

    replacement = '''    int sourceTau = bestTau;\n    float sourcePeak = sourceLagCorrelation(bestTau);\n    for (int offset = -2; offset <= 2; ++offset)\n    {\n        const int candidateTau = bestTau + offset;\n        if (candidateTau < tauMinimum || candidateTau > tauMaximum)\n            continue;\n        const float candidatePeak = sourceLagCorrelation(candidateTau);\n        if (candidatePeak > sourcePeak)\n        {\n            sourcePeak = candidatePeak;\n            sourceTau = candidateTau;\n        }\n    }\n\n    // PRIMITIVE_DIVISOR_GEOMETRY_V6_6\n    // The existing candidate set can represent global/2 but not global/3 or\n    // global/4. Consequently a path that falls into 3P or 4P literally has no\n    // route back to P before this point. Rather than adding more full candidate\n    // evaluations (and CPU), test only simple divisors of the already selected\n    // basin, using cheap staged current-sample geometry.\n    const int selectedSourceTauV66 = sourceTau;\n    const float selectedSourceCorrelationV66 = sourcePeak;\n    const float selectedResidualCorrelationV66 = residualLagCorrelation(sourceTau);\n    const float selectedYinV66 = clamp01(\n        1.0f - difference_[static_cast<std::size_t>(sourceTau)]);\n\n    int primitiveTauV66 = sourceTau;\n    float primitiveSourceCorrelationV66 = selectedSourceCorrelationV66;\n    float primitiveResidualCorrelationV66 = selectedResidualCorrelationV66;\n    float primitiveYinV66 = selectedYinV66;\n\n    // Highest divisor first means the shortest demonstrated primitive period\n    // wins when several multiples are equally explanatory (e.g. 4P -> 2P -> P).\n    constexpr std::array<int, 3> primitiveDivisorsV66 { 4, 3, 2 };\n    for (const int divisor : primitiveDivisorsV66)\n    {\n        const int candidateTau = static_cast<int>(std::lround(\n            static_cast<double>(selectedSourceTauV66)\n            / static_cast<double>(divisor)));\n        if (candidateTau < tauMinimum\n            || candidateTau > tauMaximum\n            || candidateTau >= selectedSourceTauV66 - 2)\n        {\n            continue;\n        }\n\n        // Stage 1 is intentionally the strongest and cheapest separator. It was\n        // clean by >0.13 correlation even against the hostile strong-second\n        // control. Most true primitive periods therefore stop here with one\n        // extra source-correlation pass and no further work.\n        const float candidateSourceCorrelation = sourceLagCorrelation(candidateTau);\n        if (candidateSourceCorrelation < 0.55f\n            || candidateSourceCorrelation < selectedSourceCorrelationV66 - 0.12f)\n        {\n            continue;\n        }\n\n        // Stage 2 falsifies accidental source-waveform resemblance. These are\n        // current residual/YIN measurements already available to analyse(); no\n        // new hypothesis layer or temporal persistence is introduced.\n        const float candidateResidualCorrelation = residualLagCorrelation(candidateTau);\n        const float candidateYin = clamp01(\n            1.0f - difference_[static_cast<std::size_t>(candidateTau)]);\n        if (candidateResidualCorrelation < 0.30f\n            || candidateYin < 0.30f\n            || candidateResidualCorrelation < selectedResidualCorrelationV66 - 0.12f\n            || candidateYin < selectedYinV66 - 0.12f)\n        {\n            continue;\n        }\n\n        primitiveTauV66 = candidateTau;\n        primitiveSourceCorrelationV66 = candidateSourceCorrelation;\n        primitiveResidualCorrelationV66 = candidateResidualCorrelation;\n        primitiveYinV66 = candidateYin;\n        break;\n    }\n\n    if (primitiveTauV66 != sourceTau)\n    {\n        sourceTau = primitiveTauV66;\n        sourcePeak = primitiveSourceCorrelationV66;\n\n        // The promoted coordinate is the same demonstrated periodic family, but\n        // publish the primitive path's direct periodic evidence rather than the\n        // old multiple's value. Harmonic-family/cleanliness remain the qualified\n        // family evidence that allowed this path measurement to exist at all.\n        bestPeriodicity = clamp01(primitiveResidualCorrelationV66);\n        (void) primitiveYinV66;\n    }\n\n    double refinedTau = static_cast<double>(sourceTau);\n'''

    cpp = one(cpp, anchor, replacement, 'primitive divisor geometry')
    cpp_path.write_text(cpp)

print('PRIMITIVE_DIVISOR_GEOMETRY_V6_6 materialized')
