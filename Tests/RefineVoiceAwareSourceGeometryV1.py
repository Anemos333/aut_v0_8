from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()


def one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)

# ---------------------------------------------------------------------------
# CLEAN_RESIDUAL_DISCOVERS_SOURCE_GEOMETRY_REFINES_V1
# The inverse-filtered residual is intentionally used to discover/qualify a
# vocal period, but the predictor slightly warps the shape of a high-F0 cycle.
# Once a candidate is voice-qualified, refine only inside a +/-2-sample local
# neighbourhood on the original DC-blocked analysis frame. This cannot discover
# a different note or a new octave; it only removes residual-induced lag bias.
cpp = one(cpp,
'''    double refinedTau = static_cast<double>(bestTau);
    if (bestTau > tauMinimum && bestTau < tauMaximum)
    {
        const double left = difference_[static_cast<std::size_t>(bestTau - 1)];
        const double centre = difference_[static_cast<std::size_t>(bestTau)];
        const double right = difference_[static_cast<std::size_t>(bestTau + 1)];
        const double denominator = left - 2.0 * centre + right;

        if (std::abs(denominator) > 1.0e-12)
            refinedTau += 0.5 * (left - right) / denominator;
    }

    if (refinedTau <= 0.0)
        return result;
''',
'''    // CLEAN_RESIDUAL_DISCOVERS_SOURCE_GEOMETRY_REFINES_V1
    const auto sourceLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return -1.0f;
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = frame_[static_cast<std::size_t>(index)];
            const double b = frame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        return denominator > 0.0
            ? static_cast<float>(correlation / denominator) : -1.0f;
    };

    int sourceTau = bestTau;
    float sourcePeak = sourceLagCorrelation(bestTau);
    for (int offset = -2; offset <= 2; ++offset)
    {
        const int candidateTau = bestTau + offset;
        if (candidateTau < tauMinimum || candidateTau > tauMaximum)
            continue;
        const float candidatePeak = sourceLagCorrelation(candidateTau);
        if (candidatePeak > sourcePeak)
        {
            sourcePeak = candidatePeak;
            sourceTau = candidateTau;
        }
    }

    double refinedTau = static_cast<double>(sourceTau);
    if (sourceTau > tauMinimum && sourceTau < tauMaximum)
    {
        const double left = sourceLagCorrelation(sourceTau - 1);
        const double centre = sourceLagCorrelation(sourceTau);
        const double right = sourceLagCorrelation(sourceTau + 1);
        const double denominator = left - 2.0 * centre + right;
        if (std::abs(denominator) > 1.0e-12)
        {
            // Parabolic peak interpolation. Clamp the fractional correction so
            // source refinement cannot escape the already-qualified lag basin.
            const double fractional = std::clamp(
                0.5 * (left - right) / denominator, -0.75, 0.75);
            refinedTau += fractional;
        }
    }

    if (refinedTau <= 0.0)
        return result;
''',
'local source lag refinement')

# ---------------------------------------------------------------------------
# SUBFRAME_GLOTTAL_STABILITY_V1
# A narrow formant/ringing pole may create a high short-term autocorrelation and
# even a few spectral peaks. A sung source repeats the *same candidate period*
# through both halves of the analysis frame. Require that independent evidence
# before granting tonal cleanliness; do not globally raise confidence/SNR floors.
cpp = one(cpp,
'''        const float repeatedTonalStructure = std::sqrt(std::max(
            0.0f, periodicity * cycleFamily));
        const float tonalCleanliness = clamp01(std::sqrt(std::max(
            0.0f, harmonicContrast * repeatedTonalStructure))
            * (0.90f + 0.10f * snrSupport));
''',
'''        const float repeatedTonalStructure = std::sqrt(std::max(
            0.0f, periodicity * cycleFamily));

        // SUBFRAME_GLOTTAL_STABILITY_V1
        const auto subframeLagCorrelation = [&](int start, int length, int lag) noexcept
        {
            if (lag <= 0 || length <= lag + 8 || start < 0
                || start + length > analysisLength)
            {
                return 0.0f;
            }
            double corr = 0.0;
            double energyA = 0.0;
            double energyB = 0.0;
            const int stop = start + length - lag;
            for (int index = start; index < stop; ++index)
            {
                const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
                const double b = voiceResidualFrame_[static_cast<std::size_t>(index + lag)];
                corr += a * b;
                energyA += a * a;
                energyB += b * b;
            }
            const double denominator = std::sqrt(std::max(1.0e-20,
                                                          energyA * energyB));
            return denominator > 0.0
                ? clamp01(static_cast<float>(corr / denominator)) : 0.0f;
        };

        const int halfLength = analysisLength / 2;
        const float firstHalfPeriodicity = subframeLagCorrelation(0,
                                                                  halfLength,
                                                                  tau);
        const float secondHalfPeriodicity = subframeLagCorrelation(
            analysisLength - halfLength, halfLength, tau);
        const float subframeStability = std::sqrt(std::max(
            0.0f, firstHalfPeriodicity * secondHalfPeriodicity));
        const float stableRepeatedStructure = std::sqrt(std::max(
            0.0f, repeatedTonalStructure * subframeStability));
        const float tonalCleanliness = clamp01(std::sqrt(std::max(
            0.0f, harmonicContrast * stableRepeatedStructure))
            * (0.90f + 0.10f * snrSupport));
''',
'subframe glottal stability')

# Provisional F0 is allowed to exist only when the same candidate period is at
# least moderately voice-clean. This still leaves the musical target untouched:
# no detector measurement simply means the quantizer keeps its owned degree.
cpp = one(cpp,
'''            if (candidate.tonalCleanliness >= 0.0f && candidateCleanliness < 0.16f)
                return;
''',
'''            if (candidate.tonalCleanliness >= 0.0f && candidateCleanliness < 0.22f)
                return;
''',
'provisional cleanliness floor')

for marker in [
    'CLEAN_RESIDUAL_DISCOVERS_SOURCE_GEOMETRY_REFINES_V1',
    'SUBFRAME_GLOTTAL_STABILITY_V1'
]:
    if marker not in cpp:
        raise RuntimeError(f'missing marker {marker}')

cpp_path.write_text(cpp)
print('VOICE_AWARE_SOURCE_GEOMETRY_V1 materialized')
