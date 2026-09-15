from pathlib import Path


def one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)

root = Path(__file__).resolve().parents[1]
hpp_path = root / "Source" / "ModernPitchEngine.h"
cpp_path = root / "Source" / "ModernPitchEngine.cpp"
test_path = root / "Tests" / "SupervisorContinuityTest.cpp"

hpp = hpp_path.read_text()
cpp = cpp_path.read_text()
test = test_path.read_text()

# ---------------------------------------------------------------------------
# Detector-only state.  -1 means "legacy/unspecified" so existing synthetic
# unit tests that directly construct candidates keep their old semantics.
hpp = one(hpp,
'''        struct PitchCandidate
        {
            float frequencyHz = 0.0f;
            float confidence = 0.0f;
            float periodicity = 0.0f;
            int pathIndex = -1;
''',
'''        struct PitchCandidate
        {
            float frequencyHz = 0.0f;
            float confidence = 0.0f;
            float periodicity = 0.0f;
            // VOICE_AWARE_F0_FRONTEND_V1: evidence about whether the measured
            // period belongs to a coherent vocal source rather than breath,
            // formant ringing or background noise. Analysis only.
            float harmonicFamily = -1.0f;
            float aperiodicity = -1.0f;
            int pathIndex = -1;
''',
'PitchCandidate voice evidence')

hpp = one(hpp,
'''        struct ConsensusHypothesis
        {
            float frequencyHz = 0.0f;
            float confidence = 0.0f;
            float periodicity = 0.0f;
            float consensus = 0.0f;
''',
'''        struct ConsensusHypothesis
        {
            float frequencyHz = 0.0f;
            float confidence = 0.0f;
            float periodicity = 0.0f;
            float harmonicFamily = 1.0f;
            float consensus = 0.0f;
''',
'Consensus voice family')

hpp = one(hpp,
'''        std::array<float, maxAnalysisSize> frame_ {};
        std::array<float, maxAnalysisSize> difference_ {};
        std::array<DecoderState, decoderBeamWidth> decoderBeam_ {};
''',
'''        std::array<float, maxAnalysisSize> frame_ {};
        // Detector-only inverse-filtered residual. The audible signal never
        // enters this buffer and the renderer never reads it.
        std::array<float, maxAnalysisSize> voiceResidualFrame_ {};
        std::array<float, maxAnalysisSize> difference_ {};
        std::array<DecoderState, decoderBeamWidth> decoderBeam_ {};
''',
'voice residual buffer')

hpp = one(hpp,
'''        float fastEnergy_ = 0.0f;
        float slowEnergy_ = 0.0f;
        float fastEnergyCoefficient_ = 0.0f;
''',
'''        float fastEnergy_ = 0.0f;
        float slowEnergy_ = 0.0f;
        // Lower-envelope estimate used only to rank detector evidence in
        // ordinary room/live noise. It never gates or attenuates audio.
        float noiseFloorEnergy_ = 1.0e-6f;
        float fastEnergyCoefficient_ = 0.0f;
''',
'noise floor detector state')

# ---------------------------------------------------------------------------
# Reset new detector state.
cpp = one(cpp,
'''    frame_.fill(0.0f);
    difference_.fill(1.0f);
''',
'''    frame_.fill(0.0f);
    voiceResidualFrame_.fill(0.0f);
    difference_.fill(1.0f);
''',
'reset residual frame')

cpp = one(cpp,
'''    fastEnergy_ = 0.0f;
    slowEnergy_ = 0.0f;
    onsetEnvelope_ = 0.0f;
''',
'''    fastEnergy_ = 0.0f;
    slowEnergy_ = 0.0f;
    noiseFloorEnergy_ = minimumDetectorRms * minimumDetectorRms;
    onsetEnvelope_ = 0.0f;
''',
'reset noise floor')

# ---------------------------------------------------------------------------
# Build a detector-only inverse-filtered residual before period search. The
# coefficient is estimated per frame from lag-1 correlation, so broad spectral
# tilt/formant envelope is reduced while glottal periodic timing is retained.
cpp = one(cpp,
'''    const float rms = static_cast<float>(std::sqrt(
        squaredSum / static_cast<double>(analysisLength)));
    // DETECTOR_IS_OBSERVER_V1: audio presence does not lower pitch-analysis
    // standards. Aperiodic material may correctly yield no F0 while the
    // downstream scale target remains fully authoritative.
    if (rms < minimumDetectorRms)
        return result;

    const int tauMinimum = std::clamp(
''',
'''    const float rms = static_cast<float>(std::sqrt(
        squaredSum / static_cast<double>(analysisLength)));
    // DETECTOR_IS_OBSERVER_V1: audio presence does not lower pitch-analysis
    // standards. Aperiodic material may correctly yield no F0 while the
    // downstream scale target remains fully authoritative.
    if (rms < minimumDetectorRms)
        return result;

    // VOICE_AWARE_F0_FRONTEND_V1
    // Estimate a first-order vocal-tract predictor from this analysis frame and
    // run period estimation on the inverse-filtered residual. This is analysis
    // only: no sample from voiceResidualFrame_ can reach the audio renderer.
    double predictorNumerator = 0.0;
    double predictorDenominator = 0.0;
    for (int index = 1; index < analysisLength; ++index)
    {
        const double current = frame_[static_cast<std::size_t>(index)];
        const double previous = frame_[static_cast<std::size_t>(index - 1)];
        predictorNumerator += current * previous;
        predictorDenominator += previous * previous;
    }
    const float predictor = static_cast<float>(std::clamp(
        predictorNumerator / std::max(1.0e-20, predictorDenominator),
        -0.92, 0.92));
    voiceResidualFrame_[0] = frame_[0];
    for (int index = 1; index < analysisLength; ++index)
    {
        voiceResidualFrame_[static_cast<std::size_t>(index)] =
            frame_[static_cast<std::size_t>(index)]
            - predictor * frame_[static_cast<std::size_t>(index - 1)];
    }

    const float noiseRms = std::sqrt(std::max(1.0e-12f, noiseFloorEnergy_));
    const float snrRatio = rms / std::max(0.5f * minimumDetectorRms, noiseRms);
    const float snrSupport = smoothStep(1.10f, 3.50f, snrRatio);

    const int tauMinimum = std::clamp(
''',
'insert voice-aware residual')

# YIN/difference and direct correlation must use the residual, not the formant
# envelope dominated waveform.
for old, new, label in [
('''            const float delta0 = frame_[static_cast<std::size_t>(index)]
                               - frame_[static_cast<std::size_t>(index + tau)];
            const float delta1 = frame_[static_cast<std::size_t>(index + 1)]
                               - frame_[static_cast<std::size_t>(index + tau + 1)];
            const float delta2 = frame_[static_cast<std::size_t>(index + 2)]
                               - frame_[static_cast<std::size_t>(index + tau + 2)];
            const float delta3 = frame_[static_cast<std::size_t>(index + 3)]
                               - frame_[static_cast<std::size_t>(index + tau + 3)];
''',
'''            const float delta0 = voiceResidualFrame_[static_cast<std::size_t>(index)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
            const float delta1 = voiceResidualFrame_[static_cast<std::size_t>(index + 1)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau + 1)];
            const float delta2 = voiceResidualFrame_[static_cast<std::size_t>(index + 2)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau + 2)];
            const float delta3 = voiceResidualFrame_[static_cast<std::size_t>(index + 3)]
                               - voiceResidualFrame_[static_cast<std::size_t>(index + tau + 3)];
''', 'vector residual YIN'),
('''            const float delta = frame_[static_cast<std::size_t>(index)]
                              - frame_[static_cast<std::size_t>(index + tau)];
''',
'''            const float delta = voiceResidualFrame_[static_cast<std::size_t>(index)]
                              - voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
''', 'scalar residual YIN'),
('''            const double a = frame_[static_cast<std::size_t>(index)];
            const double b = frame_[static_cast<std::size_t>(index + tau)];
''',
'''            const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
            const double b = voiceResidualFrame_[static_cast<std::size_t>(index + tau)];
''', 'residual candidate correlation')]:
    cpp = one(cpp, old, new, label)

# Multi-cycle consistency: a vocal F0 repeats across successive cycles after
# whitening; colored breath/noise may show one accidental lag peak but not a
# stable family of repeated cycles.
cpp = one(cpp,
'''    float bestScore = -1.0f;
    int bestTau = -1;
    float bestPeriodicity = 0.0f;

    for (std::size_t candidateIndex = 0;
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

    float bestScore = -1.0f;
    int bestTau = -1;
    float bestPeriodicity = 0.0f;
    float bestHarmonicFamily = 0.0f;

    for (std::size_t candidateIndex = 0;
''',
'insert multi-cycle family')

cpp = one(cpp,
'''        const float normalisedCorrelation = denominator > 0.0
            ? static_cast<float>(correlation / denominator)
            : 0.0f;
        const float periodicity = clamp01(0.5f * (normalisedCorrelation + 1.0f));
        const float yinConfidence = clamp01(
            1.0f - difference_[static_cast<std::size_t>(tau)]);

        // Prefer candidates containing at least two periods, but do not reject
        // low notes whose fundamental is mainly inferred from their harmonics.
        const float periodsInWindow = static_cast<float>(analysisLength)
                                    / static_cast<float>(std::max(1, tau));
        const float periodSupport = std::clamp(periodsInWindow / 2.2f, 0.55f, 1.0f);
        const float score = (0.67f * yinConfidence + 0.33f * periodicity)
                          * periodSupport
                          * candidatePriors[candidateIndex];

        if (score > bestScore)
        {
            bestScore = score;
            bestTau = tau;
            bestPeriodicity = periodicity;
        }
''',
'''        const float normalisedCorrelation = denominator > 0.0
            ? static_cast<float>(correlation / denominator)
            : 0.0f;
        // Zero correlation is zero periodic evidence. The old affine mapping
        // made uncorrelated noise start at 0.5 periodicity.
        const float periodicity = clamp01(normalisedCorrelation);
        const float yinConfidence = clamp01(
            1.0f - difference_[static_cast<std::size_t>(tau)]);

        float familySum = periodicity;
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
'voice family candidate score')

# Presence describes audio existence only. It must never lower F0 standards.
cpp = one(cpp,
'''    const float minimumCandidateScore = presenceMode_
        ? 0.0f : (rescueMode_ ? 0.34f : 0.45f);
    if (bestTau < 2
        || (!presenceMode_
            && (!structurallyTrusted || bestScore < minimumCandidateScore)))
    {
        return result;
    }
''',
'''    const float minimumCandidateScore = rescueMode_ ? 0.34f : 0.45f;
    const float provisionalFamilyFloor = rescueMode_ ? 0.20f : 0.24f;
    const float provisionalScoreFloor = rescueMode_ ? 0.20f : 0.24f;
    if (bestTau < 2
        || bestHarmonicFamily < provisionalFamilyFloor
        || bestScore < provisionalScoreFloor)
    {
        return result;
    }
''',
'presence cannot grant F0')

cpp = one(cpp,
'''    result.frequencyHz = frequency;
    result.confidence = clamp01(bestScore);
    result.periodicity = bestPeriodicity;
    result.valid = structurallyTrusted && bestScore >= minimumCandidateScore;
''',
'''    result.frequencyHz = frequency;
    result.confidence = clamp01(bestScore);
    result.periodicity = bestPeriodicity;
    result.harmonicFamily = bestHarmonicFamily;
    result.aperiodicity = 1.0f - bestHarmonicFamily;
    const float trustedFamilyFloor = rescueMode_ ? 0.32f : 0.38f;
    result.valid = structurallyTrusted
        && bestScore >= minimumCandidateScore
        && bestHarmonicFamily >= trustedFamilyFloor;
''',
'publish voice family candidate')

# Consensus carries current voice-family evidence. Legacy hand-built candidates
# default to -1 and are treated as unspecified/neutral in old regression tests.
cpp = one(cpp,
'''        float confidenceSum = 0.0f;
        float periodicitySum = 0.0f;
        int supportCount = 0;
''',
'''        float confidenceSum = 0.0f;
        float periodicitySum = 0.0f;
        float harmonicFamilySum = 0.0f;
        int supportCount = 0;
''',
'consensus family sum')

cpp = one(cpp,
'''            confidenceSum += weight * candidate.confidence;
            periodicitySum += weight * candidate.periodicity;
            ++supportCount;
''',
'''            confidenceSum += weight * candidate.confidence;
            periodicitySum += weight * candidate.periodicity;
            const float candidateFamily = candidate.harmonicFamily >= 0.0f
                ? clamp01(candidate.harmonicFamily) : 1.0f;
            harmonicFamilySum += weight * candidateFamily;
            ++supportCount;
''',
'consensus family accumulation')

cpp = one(cpp,
'''        hypothesis.confidence = clamp01(confidenceSum / totalWeight);
        hypothesis.periodicity = clamp01(periodicitySum / totalWeight);
        hypothesis.supportCount = supportCount;
''',
'''        hypothesis.confidence = clamp01(confidenceSum / totalWeight);
        hypothesis.periodicity = clamp01(periodicitySum / totalWeight);
        hypothesis.harmonicFamily = clamp01(harmonicFamilySum / totalWeight);
        hypothesis.supportCount = supportCount;
''',
'consensus family publish')

cpp = one(cpp,
'''            const float minimumOctaveSupport = presenceMode_ ? 0.24f : 0.60f;
            const float minimumWeight = presenceMode_ ? 0.02f : 0.10f;
''',
'''            const float minimumOctaveSupport = rescueMode_ ? 0.48f : 0.60f;
            const float minimumWeight = rescueMode_ ? 0.07f : 0.10f;
''',
'presence consensus thresholds')

cpp = one(cpp,
'''        hypothesis.valid = hypothesis.evidenceScore
            > (presenceMode_ ? 0.055f : 0.20f);
''',
'''        const float minimumHypothesisEvidence = rescueMode_ ? 0.15f : 0.20f;
        hypothesis.valid = hypothesis.evidenceScore > minimumHypothesisEvidence;
''',
'presence hypothesis permission')

# Strong current vocal structure falsifies detector history quickly. History is
# still useful when evidence is weak, but it can no longer be an authority.
cpp = one(cpp,
'''                const float continuityBonus = 0.30f * std::exp(-absoluteCents / 85.0f);
                const float transitionPenalty = onsetPending
                    ? 0.10f * std::min(1.0f, absoluteCents / 1800.0f)
                    : 0.19f * std::min(2.0f, absoluteCents / 650.0f);
''',
'''                const bool strongCurrentFamily = hypothesis.harmonicFamily >= 0.58f
                    && hypothesis.periodicity >= 0.52f;
                const float continuityBonus = (strongCurrentFamily ? 0.10f : 0.30f)
                    * std::exp(-absoluteCents / 85.0f);
                const float transitionPenalty = onsetPending
                    ? 0.10f * std::min(1.0f, absoluteCents / 1800.0f)
                    : (strongCurrentFamily ? 0.10f : 0.19f)
                        * std::min(2.0f, absoluteCents / 650.0f);
''',
'current family falsifies continuity')

cpp = one(cpp,
'''                const float historyWeight = onsetPending ? 0.24f : 0.72f;
''',
'''                // OBSERVATION_MEMORY_IS_FALSIFIABLE_V1: a strong current
                // vocal family demotes history to a prior; it never owns F0.
                const float historyWeight = onsetPending ? 0.24f
                    : (strongCurrentFamily ? 0.28f : 0.72f);
''',
'falsifiable history weight')

cpp = one(cpp,
'''        float score = current.evidenceScore + 0.20f * current.consensus;
        if (beamValid && beamFrequencyHz > 0.0f)
        {
            const float distance = centsDistance(beamFrequencyHz,
                                                 current.frequencyHz);
            score += 0.46f * std::exp(-distance / 95.0f);
''',
'''        float score = current.evidenceScore + 0.20f * current.consensus;
        if (beamValid && beamFrequencyHz > 0.0f)
        {
            const bool strongCurrentFamily = current.harmonicFamily >= 0.58f
                && current.periodicity >= 0.52f;
            const float distance = centsDistance(beamFrequencyHz,
                                                 current.frequencyHz);
            score += (strongCurrentFamily ? 0.12f : 0.46f)
                * std::exp(-distance / 95.0f);
''',
'beam current evidence')

cpp = one(cpp,
'''                const float singleFamilyPenalty =
                    current.directSupportCount >= 2 ? 0.20f : 0.46f;
                score -= singleFamilyPenalty;
''',
'''                const float singleFamilyPenalty = strongCurrentFamily
                    ? (current.directSupportCount >= 2 ? 0.06f : 0.12f)
                    : (current.directSupportCount >= 2 ? 0.20f : 0.46f);
                score -= singleFamilyPenalty;
''',
'strong current octave evidence')

cpp = one(cpp,
'''    decision.candidate.periodicity = hypothesis.periodicity;
    decision.candidate.valid = true;
''',
'''    decision.candidate.periodicity = hypothesis.periodicity;
    decision.candidate.harmonicFamily = hypothesis.harmonicFamily;
    decision.candidate.aperiodicity = 1.0f - hypothesis.harmonicFamily;
    decision.candidate.valid = true;
''',
'decision family evidence')

cpp = one(cpp,
'''        const bool multiPathDirect = decision.directSupportCount >= 2
            && decision.consensus >= 0.24f;
        const int requiredObservations = multiPathDirect
            ? (rescueOctaveDelta < 0 ? 12 : 10)
            : (rescueOctaveDelta < 0 ? 28 : 24);
''',
'''        const bool multiPathDirect = decision.directSupportCount >= 2
            && decision.consensus >= 0.24f;
        const bool strongCurrentFamily = decision.candidate.harmonicFamily >= 0.58f
            && decision.candidate.periodicity >= 0.52f;
        const int requiredObservations = strongCurrentFamily
            ? (multiPathDirect ? 4 : 8)
            : (multiPathDirect
                ? (rescueOctaveDelta < 0 ? 12 : 10)
                : (rescueOctaveDelta < 0 ? 28 : 24));
''',
'falsifiable rescue anchor')

# Provisional F0 also needs actual periodic-family evidence when it came from
# the real analyzer; an unspecified synthetic unit-test candidate remains neutral.
cpp = one(cpp,
'''            if (slot.ageInHops > maximumAge
                || !std::isfinite(candidate.frequencyHz)
                || candidate.frequencyHz <= 0.0f)
            {
                return;
            }
''',
'''            if (slot.ageInHops > maximumAge
                || !std::isfinite(candidate.frequencyHz)
                || candidate.frequencyHz <= 0.0f
                || (candidate.harmonicFamily >= 0.0f
                    && candidate.harmonicFamily < 0.20f))
            {
                return;
            }
''',
'provisional family requirement')

# Adaptive lower-envelope noise estimate. Slow upward movement avoids learning a
# sustained sung note as noise; fast downward movement follows quieter rooms.
cpp = one(cpp,
'''    const float energy = dcBlocked * dcBlocked;
    fastEnergy_ += fastEnergyCoefficient_ * (energy - fastEnergy_);
    slowEnergy_ += slowEnergyCoefficient_ * (energy - slowEnergy_);
''',
'''    const float energy = dcBlocked * dcBlocked;
    const float floorTarget = std::max(1.0e-12f, energy);
    const float floorCoefficient = floorTarget < noiseFloorEnergy_
        ? 0.020f : 0.000003f;
    noiseFloorEnergy_ += floorCoefficient * (floorTarget - noiseFloorEnergy_);
    noiseFloorEnergy_ = std::max(1.0e-12f, noiseFloorEnergy_);
    fastEnergy_ += fastEnergyCoefficient_ * (energy - fastEnergy_);
    slowEnergy_ += slowEnergyCoefficient_ * (energy - slowEnergy_);
''',
'adaptive detector noise floor')

# ---------------------------------------------------------------------------
# Real-waveform regressions. These exercise the detector itself, not hand-built
# observations, and deliberately include colored breath/noise and stale memory.
test = one(test,
'''    ModernPitchEngine::CorrectionState zeroResponseTransition;
''',
'''    // VOICE_AWARE_F0_FRONTEND_V1: colored breath/noise is signal, but it is
    // not an F0. Presence must never manufacture a pitch measurement.
    auto breathNoiseTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    breathNoiseTracker->prepare(48000.0);
    breathNoiseTracker->setRange(70.0f, 700.0f);
    std::uint32_t breathRng = 0x12345678u;
    float breathFilter = 0.0f;
    int breathValidF0 = 0;
    int breathMeasurements = 0;
    for (int sample = 0; sample < 24000; ++sample)
    {
        breathRng ^= breathRng << 13;
        breathRng ^= breathRng >> 17;
        breathRng ^= breathRng << 5;
        const float white = (static_cast<float>(breathRng & 0xffffu) / 32767.5f) - 1.0f;
        breathFilter = 0.91f * breathFilter + 0.09f * white;
        const float breath = 0.030f * (0.62f * white + 0.38f * breathFilter);
        ModernPitchEngine::PitchObservation observed;
        if (breathNoiseTracker->processSample(breath, observed) && sample > 4096)
        {
            breathValidF0 += observed.valid ? 1 : 0;
            breathMeasurements += observed.measurementAvailable ? 1 : 0;
        }
    }
    success &= check(breathValidF0 == 0 && breathMeasurements <= 2,
                     "colored_breath_is_not_promoted_to_f0");

    // A real low-SNR vocal tone embedded in the same colored noise must remain
    // measurable: the detector rejects noise, not difficult voices.
    auto noisyVoiceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    noisyVoiceTracker->prepare(48000.0);
    noisyVoiceTracker->setRange(70.0f, 700.0f);
    std::uint32_t voiceRng = 0x9e3779b9u;
    float voiceNoiseFilter = 0.0f;
    int nearToneCount = 0;
    int voicedDecisionCount = 0;
    for (int sample = 0; sample < 30000; ++sample)
    {
        voiceRng ^= voiceRng << 13;
        voiceRng ^= voiceRng >> 17;
        voiceRng ^= voiceRng << 5;
        const float white = (static_cast<float>(voiceRng & 0xffffu) / 32767.5f) - 1.0f;
        voiceNoiseFilter = 0.91f * voiceNoiseFilter + 0.09f * white;
        const float noise = 0.024f * (0.60f * white + 0.40f * voiceNoiseFilter);
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * 220.0 * static_cast<double>(sample) / 48000.0);
        // Fundamental + weak second/third harmonic approximates a real source
        // family without making the test unrealistically clean.
        const float voice = 0.050f * std::sin(phase)
                          + 0.017f * std::sin(2.0f * phase + 0.31f)
                          + 0.009f * std::sin(3.0f * phase + 0.73f);
        ModernPitchEngine::PitchObservation observed;
        if (noisyVoiceTracker->processSample(voice + noise, observed)
            && sample > 8000 && observed.valid)
        {
            ++voicedDecisionCount;
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / 220.0f));
            if (cents < 45.0f)
                ++nearToneCount;
        }
    }
    success &= check(voicedDecisionCount > 20
                     && nearToneCount * 4 >= voicedDecisionCount * 3,
                     "low_snr_vocal_family_isolated_from_colored_noise");

    // OBSERVATION_MEMORY_IS_FALSIFIABLE_V1: emulate a stale wrong register
    // continuously supplied as a rescue anchor. Strong current vocal evidence
    // must recover autonomously; changing preset/reset is never required.
    auto staleAnchorTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    staleAnchorTracker->prepare(48000.0);
    staleAnchorTracker->setRange(70.0f, 700.0f);
    staleAnchorTracker->setRescueMode(true);
    staleAnchorTracker->trackedPitchHz_ = 110.0f;
    staleAnchorTracker->trackedConfidence_ = 0.88f;
    staleAnchorTracker->trackedPeriodicity_ = 0.88f;
    staleAnchorTracker->trackedConsensus_ = 0.70f;
    staleAnchorTracker->decoderBeam_[0].valid = true;
    staleAnchorTracker->decoderBeam_[0].logFrequency = std::log2(110.0);
    staleAnchorTracker->decoderBeam_[0].score = 1.0f;
    int recoveredAtSample = -1;
    for (int sample = 0; sample < 12000; ++sample)
    {
        staleAnchorTracker->setReacquisitionAnchor(110.0f);
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * 220.0 * static_cast<double>(sample) / 48000.0);
        const float voice = 0.070f * std::sin(phase)
                          + 0.020f * std::sin(2.0f * phase + 0.2f)
                          + 0.010f * std::sin(3.0f * phase + 0.5f);
        ModernPitchEngine::PitchObservation observed;
        if (staleAnchorTracker->processSample(voice, observed) && observed.valid)
        {
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / 220.0f));
            if (cents < 45.0f)
            {
                recoveredAtSample = sample;
                break;
            }
        }
    }
    success &= check(recoveredAtSample >= 0 && recoveredAtSample < 6000,
                     "stale_wrong_f0_memory_is_falsified_without_preset_reset");

    ModernPitchEngine::CorrectionState zeroResponseTransition;
''',
'append voice-aware detector tests')

hpp_path.write_text(hpp)
cpp_path.write_text(cpp)
test_path.write_text(test)
print('VOICE_AWARE_F0_DETECTOR_V1 materialized')
