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


marker = 'PRIMITIVE_CYCLE_GEOMETRY_DIAGNOSTIC_V1'
if marker not in cpp:
    h = one(
        h,
        '''            int pathIndex = -1;\n            int ageInHops = 1000;\n            bool valid = false;\n''',
        '''            int pathIndex = -1;\n            int ageInHops = 1000;\n\n            // PRIMITIVE_CYCLE_GEOMETRY_DIAGNOSTIC_V1\n            // Workspace-only observability. None of these values participate in\n            // detector validity, path authority, scale ownership or rendering.\n            int diagnosticSelectedTau = -1;\n            float diagnosticSelectedSourceCorrelation = -2.0f;\n            float diagnosticSelectedResidualCorrelation = -2.0f;\n            float diagnosticSelectedYin = -2.0f;\n            float diagnosticSelectedCycleConsistency = -2.0f;\n            std::array<float, 3> diagnosticDivisorSourceCorrelation { -2.0f, -2.0f, -2.0f };\n            std::array<float, 3> diagnosticDivisorResidualCorrelation { -2.0f, -2.0f, -2.0f };\n            std::array<float, 3> diagnosticDivisorYin { -2.0f, -2.0f, -2.0f };\n            std::array<float, 3> diagnosticDivisorCycleConsistency { -2.0f, -2.0f, -2.0f };\n            bool valid = false;\n''',
        'diagnostic candidate fields')

    anchor = '''    if (refinedTau <= 0.0)\n        return result;\n\n    const float frequency = static_cast<float>(effectiveSampleRate / refinedTau);\n'''
    replacement = '''    if (refinedTau <= 0.0)\n        return result;\n\n    // PRIMITIVE_CYCLE_GEOMETRY_DIAGNOSTIC_V1\n    // Normalised agreement between adjacent source-frame blocks one candidate\n    // period long. A real primitive period should repeat from block to block.\n    // If sourceTau is an integer multiple of the true period, one of its shorter\n    // divisors should reproduce the same high agreement. Conversely, dividing a\n    // true period again should expose alternating/partial-cycle disagreement,\n    // especially when odd harmonics are present.\n    const auto adjacentCycleConsistencyDiagnosticV1 = [&](int lag) noexcept\n    {\n        if (lag <= 1 || 2 * lag > analysisLength)\n            return -2.0f;\n\n        const int blockCount = analysisLength / lag;\n        if (blockCount < 2)\n            return -2.0f;\n\n        double correlationSum = 0.0;\n        int pairCount = 0;\n        for (int block = 0; block + 1 < blockCount; ++block)\n        {\n            const int aStart = block * lag;\n            const int bStart = (block + 1) * lag;\n            double correlation = 0.0;\n            double energyA = 0.0;\n            double energyB = 0.0;\n            for (int offset = 0; offset < lag; ++offset)\n            {\n                const double a = frame_[static_cast<std::size_t>(aStart + offset)];\n                const double b = frame_[static_cast<std::size_t>(bStart + offset)];\n                correlation += a * b;\n                energyA += a * a;\n                energyB += b * b;\n            }\n            const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));\n            if (denominator > 0.0)\n            {\n                correlationSum += correlation / denominator;\n                ++pairCount;\n            }\n        }\n        return pairCount > 0\n            ? static_cast<float>(correlationSum / static_cast<double>(pairCount))\n            : -2.0f;\n    };\n\n    result.diagnosticSelectedTau = sourceTau;\n    result.diagnosticSelectedSourceCorrelation = sourceLagCorrelation(sourceTau);\n    result.diagnosticSelectedResidualCorrelation = residualLagCorrelation(sourceTau);\n    result.diagnosticSelectedYin = clamp01(\n        1.0f - difference_[static_cast<std::size_t>(sourceTau)]);\n    result.diagnosticSelectedCycleConsistency =\n        adjacentCycleConsistencyDiagnosticV1(sourceTau);\n\n    for (int divisor = 2; divisor <= 4; ++divisor)\n    {\n        const std::size_t slot = static_cast<std::size_t>(divisor - 2);\n        const int primitiveTau = static_cast<int>(std::lround(\n            static_cast<double>(sourceTau) / static_cast<double>(divisor)));\n        if (primitiveTau < tauMinimum || primitiveTau > tauMaximum)\n            continue;\n        result.diagnosticDivisorSourceCorrelation[slot] =\n            sourceLagCorrelation(primitiveTau);\n        result.diagnosticDivisorResidualCorrelation[slot] =\n            residualLagCorrelation(primitiveTau);\n        result.diagnosticDivisorYin[slot] = clamp01(\n            1.0f - difference_[static_cast<std::size_t>(primitiveTau)]);\n        result.diagnosticDivisorCycleConsistency[slot] =\n            adjacentCycleConsistencyDiagnosticV1(primitiveTau);\n    }\n\n    const float frequency = static_cast<float>(effectiveSampleRate / refinedTau);\n'''
    cpp = one(cpp, anchor, replacement, 'primitive cycle geometry instrumentation')

    cpp_path.write_text(cpp)
    h_path.write_text(h)

print('PRIMITIVE_CYCLE_GEOMETRY_DIAGNOSTIC_V1 materialized')
