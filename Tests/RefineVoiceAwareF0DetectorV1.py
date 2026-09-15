from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
test_path = root / 'Tests' / 'SupervisorContinuityTest.cpp'
cpp = cpp_path.read_text()
test = test_path.read_text()

def replace_one(text, old, new, label):
    c = text.count(old)
    if c != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {c}')
    return text.replace(old, new, 1)

# ---------------------------------------------------------------------------
# SRH-inspired residual harmonic contrast.  YIN proposes periods; this sensor
# asks whether the inverse-filtered residual actually contains a harmonic comb
# at that period rather than broadband breath/noise or a single formant peak.
# It is detector-only and never constructs or routes audio.
cpp = replace_one(cpp,
'''    const auto residualLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return 0.0f;
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
            const double b = voiceResidualFrame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        return denominator > 0.0
            ? clamp01(static_cast<float>(correlation / denominator)) : 0.0f;
    };

    float bestScore = -1.0f;
''',
'''    const auto residualLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return 0.0f;
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
            const double b = voiceResidualFrame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        return denominator > 0.0
            ? clamp01(static_cast<float>(correlation / denominator)) : 0.0f;
    };

    // RESIDUAL_HARMONIC_CONTRAST_V1
    // A real voiced source produces narrow coherent lines at F0 multiples after
    // inverse filtering. Broadband breath/background produces comparable energy
    // between those lines. The half-harmonic subtraction also suppresses the
    // common 2F0 alias without requiring detector history to own the register.
    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
    {
        if (!std::isfinite(cyclesPerSample)
            || cyclesPerSample <= 0.0 || cyclesPerSample >= 0.48)
        {
            return 0.0f;
        }
        double real = 0.0;
        double imag = 0.0;
        double signalEnergy = 0.0;
        double windowEnergy = 0.0;
        const double denominatorN = static_cast<double>(std::max(1, analysisLength - 1));
        for (int index = 0; index < analysisLength; ++index)
        {
            const double window = 0.5 - 0.5 * std::cos(
                twoPi * static_cast<double>(index) / denominatorN);
            const double sample = static_cast<double>(
                voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
            const double phase = twoPi * cyclesPerSample * static_cast<double>(index);
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
            signalEnergy += sample * sample;
            windowEnergy += window * window;
        }
        const double normaliser = std::max(1.0e-20, signalEnergy * windowEnergy);
        return clamp01(static_cast<float>(std::sqrt(
            2.0 * (real * real + imag * imag) / normaliser)));
    };

    const auto residualHarmonicContrast = [&](int tau) noexcept
    {
        if (tau <= 1)
            return 0.0f;
        const double fundamentalCycles = 1.0 / static_cast<double>(tau);
        float harmonicScore = residualLineCoherence(fundamentalCycles);
        float interHarmonicScore = 0.0f;
        float harmonicWeight = 1.0f;
        float interWeight = 0.0f;
        for (int harmonic = 2; harmonic <= 6; ++harmonic)
        {
            const double harmonicCycles = fundamentalCycles
                                        * static_cast<double>(harmonic);
            if (harmonicCycles >= 0.45)
                break;
            const float weight = 1.0f / std::sqrt(static_cast<float>(harmonic));
            harmonicScore += weight * residualLineCoherence(harmonicCycles);
            harmonicWeight += weight;

            const double interCycles = fundamentalCycles
                                     * (static_cast<double>(harmonic) - 0.5);
            if (interCycles < 0.45)
            {
                interHarmonicScore += weight * residualLineCoherence(interCycles);
                interWeight += weight;
            }
        }
        const float harmonicMean = harmonicScore / std::max(1.0e-6f, harmonicWeight);
        const float interMean = interWeight > 1.0e-6f
            ? interHarmonicScore / interWeight : 0.0f;
        // Require harmonic lines to emerge above the local inter-harmonic floor,
        // not merely above absolute amplitude. This remains useful at low SNR.
        return smoothStep(0.025f, 0.30f,
                          harmonicMean - 0.78f * interMean);
    };

    float bestScore = -1.0f;
''', 'insert residual harmonic contrast')

cpp = replace_one(cpp,
'''        float familySum = periodicity;
        float familyWeight = 1.0f;
        if (2 * tau < analysisLength - 8)
        {
            familySum += 0.70f * residualLagCorrelation(2 * tau);
            familyWeight += 0.70f;
        }
        if (3 * tau < analysisLength - 8)
        {
            familySum += 0.45f * residualLagCorrelation(3 * tau);
            familyWeight += 0.45f;
        }
        const float harmonicFamily = clamp01(familySum / familyWeight);

        // Prefer candidates containing at least two periods, but do not reject
        // low notes whose fundamental is mainly inferred from their harmonics.
        const float periodsInWindow = static_cast<float>(analysisLength)
                                    / static_cast<float>(std::max(1, tau));
        const float periodSupport = std::clamp(periodsInWindow / 2.2f, 0.55f, 1.0f);
        const float score = (0.52f * yinConfidence
                           + 0.28f * periodicity
                           + 0.20f * harmonicFamily)
                          * periodSupport
                          * candidatePriors[candidateIndex]
                          * (0.82f + 0.18f * snrSupport);

        if (score > bestScore)
        {
            bestScore = score;
            bestTau = tau;
            bestPeriodicity = periodicity;
            bestHarmonicFamily = harmonicFamily;
        }
''',
'''        float cycleFamilySum = periodicity;
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

        if (score > bestScore)
        {
            bestScore = score;
            bestTau = tau;
            bestPeriodicity = periodicity;
            bestHarmonicFamily = harmonicFamily;
        }
''', 'combine cycle and spectral family')

# The family metric is now much more specific than raw autocorrelation, so lower
# its numerical floors while preserving the original candidate score threshold.
cpp = replace_one(cpp,
'''    const float provisionalFamilyFloor = rescueMode_ ? 0.20f : 0.24f;
    const float provisionalScoreFloor = rescueMode_ ? 0.20f : 0.24f;
''',
'''    const float provisionalFamilyFloor = rescueMode_ ? 0.16f : 0.19f;
    const float provisionalScoreFloor = rescueMode_ ? 0.20f : 0.24f;
''', 'specific provisional family floor')
cpp = replace_one(cpp,
'''    const float trustedFamilyFloor = rescueMode_ ? 0.32f : 0.38f;
''',
'''    const float trustedFamilyFloor = rescueMode_ ? 0.27f : 0.32f;
''', 'specific trusted family floor')

# ---------------------------------------------------------------------------
# Diagnostics and regression metrics.
test = replace_one(test,
'''    success &= check(breathValidF0 == 0 && breathMeasurements <= 2,
                     "colored_breath_is_not_promoted_to_f0");
''',
'''    std::cerr << "voice_aware_breath_valid_f0=" << breathValidF0
              << " provisional=" << breathMeasurements << '\\n';
    success &= check(breathValidF0 == 0 && breathMeasurements <= 2,
                     "colored_breath_is_not_promoted_to_f0");
''', 'breath metrics')

test = replace_one(test,
'''    int nearToneCount = 0;
    int voicedDecisionCount = 0;
''',
'''    int nearToneCount = 0;
    int nearHalfToneCount = 0;
    int nearDoubleToneCount = 0;
    int voicedDecisionCount = 0;
''', 'alias counters')

test = replace_one(test,
'''            if (cents < 45.0f)
                ++nearToneCount;
''',
'''            if (cents < 45.0f)
                ++nearToneCount;
            const float halfCents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / 110.0f));
            const float doubleCents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / 440.0f));
            if (halfCents < 45.0f)
                ++nearHalfToneCount;
            if (doubleCents < 45.0f)
                ++nearDoubleToneCount;
''', 'alias metrics accumulation')

test = replace_one(test,
'''    success &= check(voicedDecisionCount > 20
                     && nearToneCount * 4 >= voicedDecisionCount * 3,
                     "low_snr_vocal_family_isolated_from_colored_noise");
''',
'''    std::cerr << "voice_aware_noisy_voice_decisions=" << voicedDecisionCount
              << " near_220=" << nearToneCount
              << " near_110=" << nearHalfToneCount
              << " near_440=" << nearDoubleToneCount << '\\n';
    success &= check(voicedDecisionCount > 20
                     && nearToneCount * 4 >= voicedDecisionCount * 3,
                     "low_snr_vocal_family_isolated_from_colored_noise");
''', 'voice metrics')

cpp_path.write_text(cpp)
test_path.write_text(test)
print('VOICE_AWARE_F0_DETECTOR_V1 residual harmonic contrast refined')
