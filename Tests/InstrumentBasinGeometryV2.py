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


marker = 'BASIN_GEOMETRY_DIAGNOSTIC_V2'
if marker not in cpp:
    h = one(h,
'''            float diagnosticPrimitiveResidualCorrelation = -2.0f;
            float diagnosticSelectedYin = -1.0f;
            float diagnosticPrimitiveYin = -1.0f;
            bool valid = false;
''',
'''            float diagnosticPrimitiveResidualCorrelation = -2.0f;
            float diagnosticSelectedYin = -1.0f;
            float diagnosticPrimitiveYin = -1.0f;

            // BASIN_GEOMETRY_DIAGNOSTIC_V2: read-only divisor geometry. The
            // detector decision is unchanged; these fields expose whether the
            // selected lag belongs to a 2T/3T/4T family and how close each
            // primitive explanation is in the same physical frame.
            int diagnosticDiv2Tau = -1;
            int diagnosticDiv3Tau = -1;
            int diagnosticDiv4Tau = -1;
            float diagnosticDiv2SourceCorrelation = -2.0f;
            float diagnosticDiv3SourceCorrelation = -2.0f;
            float diagnosticDiv4SourceCorrelation = -2.0f;
            float diagnosticDiv2ResidualCorrelation = -2.0f;
            float diagnosticDiv3ResidualCorrelation = -2.0f;
            float diagnosticDiv4ResidualCorrelation = -2.0f;
            float diagnosticDiv2Yin = -1.0f;
            float diagnosticDiv3Yin = -1.0f;
            float diagnosticDiv4Yin = -1.0f;
            bool valid = false;
''', 'divisor diagnostic fields')

    anchor = '''    if (diagnosticPrimitiveTau > 0)
    {
        result.diagnosticPrimitiveSourceCorrelation = sourceLagCorrelation(diagnosticPrimitiveTau);
        result.diagnosticPrimitiveResidualCorrelation = residualLagCorrelation(diagnosticPrimitiveTau);
        result.diagnosticPrimitiveYin = clamp01(
            1.0f - difference_[static_cast<std::size_t>(diagnosticPrimitiveTau)]);
    }
'''
    replacement = anchor + '''
    // BASIN_GEOMETRY_DIAGNOSTIC_V2
    const auto publishDivisorGeometryV2 = [&](int divisor,
                                               int& tauOut,
                                               float& sourceOut,
                                               float& residualOut,
                                               float& yinOut) noexcept
    {
        if (sourceTau < divisor * tauMinimum)
            return;
        const int tau = std::clamp(static_cast<int>(std::lround(
            static_cast<double>(sourceTau) / static_cast<double>(divisor))),
            tauMinimum, tauMaximum);
        if (tau <= 0 || tau >= static_cast<int>(difference_.size()))
            return;
        tauOut = tau;
        sourceOut = sourceLagCorrelation(tau);
        residualOut = residualLagCorrelation(tau);
        yinOut = clamp01(1.0f - difference_[static_cast<std::size_t>(tau)]);
    };
    publishDivisorGeometryV2(2,
                             result.diagnosticDiv2Tau,
                             result.diagnosticDiv2SourceCorrelation,
                             result.diagnosticDiv2ResidualCorrelation,
                             result.diagnosticDiv2Yin);
    publishDivisorGeometryV2(3,
                             result.diagnosticDiv3Tau,
                             result.diagnosticDiv3SourceCorrelation,
                             result.diagnosticDiv3ResidualCorrelation,
                             result.diagnosticDiv3Yin);
    publishDivisorGeometryV2(4,
                             result.diagnosticDiv4Tau,
                             result.diagnosticDiv4SourceCorrelation,
                             result.diagnosticDiv4ResidualCorrelation,
                             result.diagnosticDiv4Yin);
'''
    cpp = one(cpp, anchor, replacement, 'publish divisor geometry')

    cpp_path.write_text(cpp)
    h_path.write_text(h)

print('BASIN_GEOMETRY_DIAGNOSTIC_V2 materialized')
