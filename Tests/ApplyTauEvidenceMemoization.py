from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
source = path.read_text()

old = r'''    float bestScore = -1.0f;
    float bestSelectionScore = -1.0f;
    int bestTau = -1;
    float bestPeriodicity = 0.0f;
    float bestHarmonicFamily = 0.0f;
    float bestTonalCleanliness = 0.0f;

    for (std::size_t candidateIndex = 0;
         candidateIndex < candidateTaus.size();
         ++candidateIndex)
    {
        int tau = std::clamp(candidateTaus[candidateIndex],
                             tauMinimum,
                             tauMaximum);

        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - tau;

        for (int index = 0; index < overlap; ++index)
        {
            const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
            const double b = voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }

        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        const float normalisedCorrelation = denominator > 0.0
            ? static_cast<float>(correlation / denominator)
            : 0.0f;
        // Zero correlation is zero periodic evidence. The old affine mapping
        // made uncorrelated noise start at 0.5 periodicity.
        const float periodicity = clamp01(normalisedCorrelation);
        const float yinConfidence = clamp01(
            1.0f - difference_[static_cast<std::size_t>(tau)]);

        float cycleFamilySum = periodicity;
        float cycleFamilyWeight = 1.0f;
        if (2 * tau < analysisLength - 8)
        {
            cycleFamilySum += 0.70f * residualLagCorrelation(2 * tau);
            cycleFamilyWeight += 0.70f;
        }
        if (3 * tau < analysisLength - 8)
        {
            cycleFamilySum += 0.45f * residualLagCorrelation(3 * tau);
            cycleFamilyWeight += 0.45f;
        }
        const float cycleFamily = clamp01(cycleFamilySum / cycleFamilyWeight);
        const float harmonicContrast = residualHarmonicContrast(tau);
        // Both time-domain repetition and a residual harmonic comb must agree.
        // A weak value in either dimension cannot be hidden by the other one.
        const float harmonicFamily = std::sqrt(std::max(
            0.0f, cycleFamily * harmonicContrast));
        const float repeatedTonalStructure = std::sqrt(std::max(
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

        // Prefer candidates containing at least two periods, but do not reject
        // low notes whose fundamental is mainly inferred from their harmonics.
        const float periodsInWindow = static_cast<float>(analysisLength)
                                    / static_cast<float>(std::max(1, tau));
        const float periodSupport = std::clamp(periodsInWindow / 2.2f, 0.55f, 1.0f);
        const float score = (0.44f * yinConfidence
                           + 0.22f * periodicity
                           + 0.17f * cycleFamily
                           + 0.17f * harmonicContrast)
                          * periodSupport
                          * candidatePriors[candidateIndex]
                          * (0.82f + 0.18f * snrSupport);

        // DIRECT_HIGH_YIN_FIRST_MINIMUM_V1: selection-only preference.
        // Never inflate the published confidence/evidence score.
        const bool directHighThresholdCandidate = thresholdTau >= 0
            && candidateIndex == 0
            && effectiveSampleRate >= sampleRate_ * 0.75
            && effectiveSampleRate / static_cast<double>(std::max(1, tau)) > 900.0
            && harmonicFamily >= 0.60f
            && tonalCleanliness >= 0.68f;
        const float selectionScore = score
            * (directHighThresholdCandidate ? 1.35f : 1.0f);

        if (selectionScore > bestSelectionScore)
        {
            bestSelectionScore = selectionScore;
            bestScore = score;
            bestTau = tau;
            bestPeriodicity = periodicity;
            bestHarmonicFamily = harmonicFamily;
            bestTonalCleanliness = tonalCleanliness;
        }
    }
'''

new = r'''    float bestScore = -1.0f;
    float bestSelectionScore = -1.0f;
    int bestTau = -1;
    float bestPeriodicity = 0.0f;
    float bestHarmonicFamily = 0.0f;
    float bestTonalCleanliness = 0.0f;

    // TAU_EVIDENCE_MEMOIZATION_V1
    // Candidate priors and selection order remain independent, but two slots
    // resolving to the same clamped tau must not recompute identical physical
    // evidence. Every cached field is already a float in the golden path, so
    // reuse preserves the exact inputs to the unchanged score expression.
    struct CandidateTauEvidence
    {
        int tau = -1;
        float periodicity = 0.0f;
        float yinConfidence = 0.0f;
        float cycleFamily = 0.0f;
        float harmonicContrast = 0.0f;
        float harmonicFamily = 0.0f;
        float tonalCleanliness = 0.0f;
        float periodSupport = 0.0f;
    };
    std::array<CandidateTauEvidence, 5> tauEvidenceCache {};
    std::size_t tauEvidenceCount = 0;

    for (std::size_t candidateIndex = 0;
         candidateIndex < candidateTaus.size();
         ++candidateIndex)
    {
        int tau = std::clamp(candidateTaus[candidateIndex],
                             tauMinimum,
                             tauMaximum);

        const CandidateTauEvidence* cachedEvidence = nullptr;
        for (std::size_t cachedIndex = 0;
             cachedIndex < tauEvidenceCount;
             ++cachedIndex)
        {
            if (tauEvidenceCache[cachedIndex].tau == tau)
            {
                cachedEvidence = &tauEvidenceCache[cachedIndex];
                break;
            }
        }

        CandidateTauEvidence evidence;
        if (cachedEvidence != nullptr)
        {
            evidence = *cachedEvidence;
        }
        else
        {
            evidence.tau = tau;

            double correlation = 0.0;
            double energyA = 0.0;
            double energyB = 0.0;
            const int overlap = analysisLength - tau;

            for (int index = 0; index < overlap; ++index)
            {
                const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
                const double b = voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
                correlation += a * b;
                energyA += a * a;
                energyB += b * b;
            }

            const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
            const float normalisedCorrelation = denominator > 0.0
                ? static_cast<float>(correlation / denominator)
                : 0.0f;
            // Zero correlation is zero periodic evidence. The old affine mapping
            // made uncorrelated noise start at 0.5 periodicity.
            evidence.periodicity = clamp01(normalisedCorrelation);
            evidence.yinConfidence = clamp01(
                1.0f - difference_[static_cast<std::size_t>(tau)]);

            float cycleFamilySum = evidence.periodicity;
            float cycleFamilyWeight = 1.0f;
            if (2 * tau < analysisLength - 8)
            {
                cycleFamilySum += 0.70f * residualLagCorrelation(2 * tau);
                cycleFamilyWeight += 0.70f;
            }
            if (3 * tau < analysisLength - 8)
            {
                cycleFamilySum += 0.45f * residualLagCorrelation(3 * tau);
                cycleFamilyWeight += 0.45f;
            }
            evidence.cycleFamily = clamp01(cycleFamilySum / cycleFamilyWeight);
            evidence.harmonicContrast = residualHarmonicContrast(tau);
            // Both time-domain repetition and a residual harmonic comb must agree.
            // A weak value in either dimension cannot be hidden by the other one.
            evidence.harmonicFamily = std::sqrt(std::max(
                0.0f, evidence.cycleFamily * evidence.harmonicContrast));
            const float repeatedTonalStructure = std::sqrt(std::max(
                0.0f, evidence.periodicity * evidence.cycleFamily));

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
            evidence.tonalCleanliness = clamp01(std::sqrt(std::max(
                0.0f, evidence.harmonicContrast * stableRepeatedStructure))
                * (0.90f + 0.10f * snrSupport));

            // Prefer candidates containing at least two periods, but do not reject
            // low notes whose fundamental is mainly inferred from their harmonics.
            const float periodsInWindow = static_cast<float>(analysisLength)
                                        / static_cast<float>(std::max(1, tau));
            evidence.periodSupport = std::clamp(
                periodsInWindow / 2.2f, 0.55f, 1.0f);

            tauEvidenceCache[tauEvidenceCount++] = evidence;
        }

        const float periodicity = evidence.periodicity;
        const float yinConfidence = evidence.yinConfidence;
        const float cycleFamily = evidence.cycleFamily;
        const float harmonicContrast = evidence.harmonicContrast;
        const float harmonicFamily = evidence.harmonicFamily;
        const float tonalCleanliness = evidence.tonalCleanliness;
        const float periodSupport = evidence.periodSupport;

        const float score = (0.44f * yinConfidence
                           + 0.22f * periodicity
                           + 0.17f * cycleFamily
                           + 0.17f * harmonicContrast)
                          * periodSupport
                          * candidatePriors[candidateIndex]
                          * (0.82f + 0.18f * snrSupport);

        // DIRECT_HIGH_YIN_FIRST_MINIMUM_V1: selection-only preference.
        // Never inflate the published confidence/evidence score.
        const bool directHighThresholdCandidate = thresholdTau >= 0
            && candidateIndex == 0
            && effectiveSampleRate >= sampleRate_ * 0.75
            && effectiveSampleRate / static_cast<double>(std::max(1, tau)) > 900.0
            && harmonicFamily >= 0.60f
            && tonalCleanliness >= 0.68f;
        const float selectionScore = score
            * (directHighThresholdCandidate ? 1.35f : 1.0f);

        if (selectionScore > bestSelectionScore)
        {
            bestSelectionScore = selectionScore;
            bestScore = score;
            bestTau = tau;
            bestPeriodicity = periodicity;
            bestHarmonicFamily = harmonicFamily;
            bestTonalCleanliness = tonalCleanliness;
        }
    }
'''

if source.count(old) != 1:
    raise SystemExit(f"expected exactly one golden candidate loop, found {source.count(old)}")

patched = source.replace(old, new, 1)

required = [
    "ANALYSIS_WORKSPACE_REENTRANCY_V1",
    "VOICE_BODY_LIVE_PERMISSION_V6_7_1",
    "RESIDUAL_HARMONIC_CONTRAST_V1",
    "TAU_EVIDENCE_MEMOIZATION_V1",
    "* candidatePriors[candidateIndex]",
    "* (0.82f + 0.18f * snrSupport);",
    "candidateIndex == 0",
]
for marker in required:
    if marker not in patched:
        raise SystemExit(f"missing required marker/expression: {marker}")

for rejected in ["RESIDUAL_LINE_INVARIANT_HOIST_V1",
                 "RESIDUAL_LINE_COHERENCE_CPU_V1"]:
    if rejected in patched:
        raise SystemExit(f"refusing to build with rejected optimization: {rejected}")

if patched.count("TAU_EVIDENCE_MEMOIZATION_V1") != 1:
    raise SystemExit("unexpected tau memoization marker count")

path.write_text(patched)
print("TAU_EVIDENCE_MEMOIZATION_PATCH=APPLIED")
