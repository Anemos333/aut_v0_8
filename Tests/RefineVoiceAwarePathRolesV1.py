from pathlib import Path

root = Path(__file__).resolve().parents[1]
hpp_path = root / 'Source' / 'ModernPitchEngine.h'
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
test_path = root / 'Tests' / 'SupervisorContinuityTest.cpp'

hpp = hpp_path.read_text()
cpp = cpp_path.read_text()
test = test_path.read_text()


def one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


def segment(text: str, start: str, end: str, replacement: str, label: str) -> str:
    start_index = text.find(start)
    if start_index < 0:
        raise RuntimeError(f'{label}: start anchor missing')
    end_index = text.find(end, start_index)
    if end_index < 0:
        raise RuntimeError(f'{label}: end anchor missing')
    if text.find(start, start_index + 1) >= 0:
        raise RuntimeError(f'{label}: start anchor not unique')
    return text[:start_index] + replacement + text[end_index:]


# ---------------------------------------------------------------------------
# A candidate now carries two independent meanings:
#   1. where is F0?                -> frequency evidence
#   2. was that estimate extracted from a genuinely tonal vocal source?
#                                    -> tonal cleanliness
# The second quantity is analysis-only and can never attenuate or route audio.
hpp = one(hpp,
'''            float harmonicFamily = -1.0f;
            float aperiodicity = -1.0f;
            int pathIndex = -1;
''',
'''            float harmonicFamily = -1.0f;
            float aperiodicity = -1.0f;
            // PATH_ROLE_SPLIT_V1: orthogonal confidence that this period was
            // measured from a coherent tonal vocal source rather than a formant,
            // breath, hiss or background-noise structure. Analysis only.
            float tonalCleanliness = -1.0f;
            int pathIndex = -1;
''',
'candidate tonal cleanliness')

hpp = one(hpp,
'''            float harmonicFamily = 1.0f;
            float consensus = 0.0f;
            float evidenceScore = -1000.0f;
            int supportCount = 0;
''',
'''            float harmonicFamily = 1.0f;
            float tonalCleanliness = 1.0f;
            float consensus = 0.0f;
            float evidenceScore = -1000.0f;
            int supportCount = 0;
            int cleanSupportCount = 0;
''',
'consensus cleanliness')

hpp = one(hpp,
'''        [[nodiscard]] float pathReliability(int pathIndex, float frequencyHz) const noexcept;
        [[nodiscard]] float candidateBaseScore(const PitchCandidate& candidate) const noexcept;
''',
'''        // PATH_ROLE_SPLIT_V1: decimated paths no longer cast equivalent votes.
        // Pitch authority says how useful a path is for locating F0; cleanliness
        // authority says how useful it is for deciding whether that F0 belongs to
        // tonal voice rather than aperiodic/formant/background material.
        [[nodiscard]] float pathPitchAuthority(int pathIndex, float frequencyHz) const noexcept;
        [[nodiscard]] float pathCleanlinessAuthority(int pathIndex, float frequencyHz) const noexcept;
        [[nodiscard]] float candidateBaseScore(const PitchCandidate& candidate) const noexcept;
''',
'path role declarations')

# ---------------------------------------------------------------------------
# Preserve all four analysis rates, but assign complementary responsibilities.
# Full-rate sees breath/hiss/transients best; half-rate is the primary normal
# singing F0 path; quarter-rate owns low/mid fundamentals; eighth-rate is mainly
# low-register/subharmonic verification and can never certify cleanliness alone.
cpp = segment(cpp,
'''float ModernPitchEngine::MultiRatePitchTracker::pathReliability(''',
'''int ModernPitchEngine::MultiRatePitchTracker::collectFreshCandidates(''',
'''float ModernPitchEngine::MultiRatePitchTracker::pathPitchAuthority(
    int pathIndex,
    float frequencyHz) const noexcept
{
    const auto bandWeight = [](float frequency,
                               float lowerSoft,
                               float lowerFull,
                               float upperFull,
                               float upperSoft) noexcept
    {
        const float lower = smoothStep(lowerSoft, lowerFull, frequency);
        const float upper = 1.0f - smoothStep(upperFull, upperSoft, frequency);
        return std::clamp(lower * upper, 0.03f, 1.0f);
    };

    switch (pathIndex)
    {
        case 0: return bandWeight(frequencyHz, 135.0f, 185.0f, 1250.0f, 2400.0f);
        case 1: return bandWeight(frequencyHz,  62.0f,  84.0f,  720.0f, 1020.0f);
        case 2: return bandWeight(frequencyHz,  28.0f,  42.0f,  360.0f,  520.0f);
        case 3: return bandWeight(frequencyHz,  20.0f,  30.0f,  170.0f,  250.0f);
        default: break;
    }
    return 0.0f;
}

float ModernPitchEngine::MultiRatePitchTracker::pathCleanlinessAuthority(
    int pathIndex,
    float frequencyHz) const noexcept
{
    const auto bandWeight = [](float frequency,
                               float lowerSoft,
                               float lowerFull,
                               float upperFull,
                               float upperSoft) noexcept
    {
        const float lower = smoothStep(lowerSoft, lowerFull, frequency);
        const float upper = 1.0f - smoothStep(upperFull, upperSoft, frequency);
        return std::clamp(lower * upper, 0.05f, 1.0f);
    };

    // The lower the analysis rate, the less high-frequency evidence remains to
    // distinguish glottal periodicity from breath/hiss. Low-rate paths therefore
    // remain valuable frequency estimators but progressively weaker cleanliness
    // witnesses. This is deliberate, not a quality ranking of their F0 estimate.
    switch (pathIndex)
    {
        case 0: return 1.00f * bandWeight(frequencyHz, 130.0f, 175.0f, 1450.0f, 2700.0f);
        case 1: return 0.95f * bandWeight(frequencyHz,  58.0f,  80.0f,  760.0f, 1080.0f);
        case 2: return 0.78f * bandWeight(frequencyHz,  26.0f,  40.0f,  390.0f,  560.0f);
        case 3: return 0.40f * bandWeight(frequencyHz,  20.0f,  30.0f,  175.0f,  255.0f);
        default: break;
    }
    return 0.0f;
}

''',
'path role implementation')

# All old uses of pathReliability were pitch-coordinate uses. Keep that semantic
# explicit now that cleanliness has a separate authority function.
if 'pathReliability(' in cpp:
    raise RuntimeError('path-role implementation left an unexpected pathReliability call')

# ---------------------------------------------------------------------------
# Candidate-local tonal cleanliness. Harmonic contrast is the strongest term;
# multi-cycle repetition prevents a narrow formant from passing; SNR is only a
# weak modifier so a real singer in a noisy room is not rejected merely for being
# quiet. Cleanliness is deliberately separate from the candidate score.
cpp = one(cpp,
'''    float bestScore = -1.0f;
    int bestTau = -1;
    float bestPeriodicity = 0.0f;
    float bestHarmonicFamily = 0.0f;
''',
'''    float bestScore = -1.0f;
    int bestTau = -1;
    float bestPeriodicity = 0.0f;
    float bestHarmonicFamily = 0.0f;
    float bestTonalCleanliness = 0.0f;
''',
'best cleanliness state')

cpp = one(cpp,
'''        const float harmonicFamily = std::sqrt(std::max(
            0.0f, cycleFamily * harmonicContrast));

        // Prefer candidates containing at least two periods, but do not reject
''',
'''        const float harmonicFamily = std::sqrt(std::max(
            0.0f, cycleFamily * harmonicContrast));
        const float repeatedTonalStructure = std::sqrt(std::max(
            0.0f, periodicity * cycleFamily));
        const float tonalCleanliness = clamp01(std::sqrt(std::max(
            0.0f, harmonicContrast * repeatedTonalStructure))
            * (0.90f + 0.10f * snrSupport));

        // Prefer candidates containing at least two periods, but do not reject
''',
'tonal cleanliness metric')

cpp = one(cpp,
'''            bestPeriodicity = periodicity;
            bestHarmonicFamily = harmonicFamily;
        }
''',
'''            bestPeriodicity = periodicity;
            bestHarmonicFamily = harmonicFamily;
            bestTonalCleanliness = tonalCleanliness;
        }
''',
'publish best cleanliness')

cpp = one(cpp,
'''    result.harmonicFamily = bestHarmonicFamily;
    result.aperiodicity = 1.0f - bestHarmonicFamily;
    const float trustedFamilyFloor = rescueMode_ ? 0.27f : 0.32f;
    result.valid = structurallyTrusted
        && bestScore >= minimumCandidateScore
        && bestHarmonicFamily >= trustedFamilyFloor;
''',
'''    result.harmonicFamily = bestHarmonicFamily;
    result.aperiodicity = 1.0f - bestHarmonicFamily;
    result.tonalCleanliness = bestTonalCleanliness;
    const float trustedFamilyFloor = rescueMode_ ? 0.27f : 0.32f;
    const float trustedCleanlinessFloor = rescueMode_ ? 0.22f : 0.26f;
    result.valid = structurallyTrusted
        && bestScore >= minimumCandidateScore
        && bestHarmonicFamily >= trustedFamilyFloor
        && bestTonalCleanliness >= trustedCleanlinessFloor;
''',
'candidate cleanliness validity')

# ---------------------------------------------------------------------------
# Consensus now has two ledgers. Fresh pitch evidence steers the frequency
# coordinate. Cleanliness evidence is accumulated separately with path-specific
# authority. Stale low-rate paths may still verify a family, but their old
# coordinate rapidly loses steering authority after a real note change.
cpp = one(cpp,
'''        double weightedLogFrequency = 0.0;
        float totalWeight = 0.0f;
        float confidenceSum = 0.0f;
        float periodicitySum = 0.0f;
        float harmonicFamilySum = 0.0f;
        int supportCount = 0;
''',
'''        double weightedLogFrequency = 0.0;
        float coordinateWeightSum = 0.0f;
        float evidenceWeightSum = 0.0f;
        float confidenceSum = 0.0f;
        float periodicitySum = 0.0f;
        float harmonicFamilySum = 0.0f;
        float cleanlinessSum = 0.0f;
        float cleanlinessWeightSum = 0.0f;
        int supportCount = 0;
        int cleanSupportCount = 0;
''',
'consensus dual ledgers')

cpp = one(cpp,
'''            const float reliability = pathReliability(candidate.pathIndex,
                                                       candidate.frequencyHz);
            const float baseScore = candidateBaseScore(candidate);
            const float weight = baseScore * reliability * octavePrior;

            // Octave-transposed support is useful as harmonic evidence, but it
            // must be genuinely strong; otherwise it is ignored rather than
            // being allowed to manufacture a low subharmonic.
            const float minimumOctaveSupport = rescueMode_ ? 0.48f : 0.60f;
            const float minimumWeight = rescueMode_ ? 0.07f : 0.10f;
            if ((!direct && baseScore < minimumOctaveSupport) || weight < minimumWeight)
                continue;

            weightedLogFrequency += static_cast<double>(weight)
                                  * safeLog2(static_cast<double>(bestFrequency));
            totalWeight += weight;
            confidenceSum += weight * candidate.confidence;
            periodicitySum += weight * candidate.periodicity;
            const float candidateFamily = candidate.harmonicFamily >= 0.0f
                ? clamp01(candidate.harmonicFamily) : 1.0f;
            harmonicFamilySum += weight * candidateFamily;
            ++supportCount;
''',
'''            const float pitchAuthority = pathPitchAuthority(candidate.pathIndex,
                                                            candidate.frequencyHz);
            const float cleanAuthority = pathCleanlinessAuthority(candidate.pathIndex,
                                                                  candidate.frequencyHz);
            const float baseScore = candidateBaseScore(candidate);
            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            const float evidenceWeight = baseScore * pitchAuthority * octavePrior;
            // STALE_PATH_VERIFIES_NOT_STEERS_V1: keep stale candidates as
            // evidence, but exponentially remove their ability to pull the live
            // frequency coordinate after a new fresh family appears.
            const float steeringFreshness = std::exp(-0.62f
                * static_cast<float>(std::max(0, candidate.ageInHops)));
            const float coordinateWeight = evidenceWeight * steeringFreshness
                * (0.72f + 0.28f * candidateCleanliness);
            const float cleanlinessFreshness = std::exp(-0.42f
                * static_cast<float>(std::max(0, candidate.ageInHops)));
            const float cleanlinessWeight = baseScore * cleanAuthority
                * cleanlinessFreshness;

            // Octave-transposed support is useful as harmonic evidence, but it
            // must be genuinely strong; otherwise it is ignored rather than
            // being allowed to manufacture a low subharmonic.
            const float minimumOctaveSupport = rescueMode_ ? 0.48f : 0.60f;
            const float minimumWeight = rescueMode_ ? 0.07f : 0.10f;
            if ((!direct && baseScore < minimumOctaveSupport)
                || evidenceWeight < minimumWeight)
            {
                continue;
            }

            weightedLogFrequency += static_cast<double>(coordinateWeight)
                                  * safeLog2(static_cast<double>(bestFrequency));
            coordinateWeightSum += coordinateWeight;
            evidenceWeightSum += evidenceWeight;
            confidenceSum += evidenceWeight * candidate.confidence;
            periodicitySum += evidenceWeight * candidate.periodicity;
            const float candidateFamily = candidate.harmonicFamily >= 0.0f
                ? clamp01(candidate.harmonicFamily) : 1.0f;
            harmonicFamilySum += evidenceWeight * candidateFamily;
            cleanlinessSum += cleanlinessWeight * candidateCleanliness;
            cleanlinessWeightSum += cleanlinessWeight;
            if (cleanlinessWeight >= 0.08f && candidateCleanliness >= 0.24f)
                ++cleanSupportCount;
            ++supportCount;
''',
'consensus split authority weights')

cpp = one(cpp,
'''        if (supportCount <= 0 || totalWeight <= 1.0e-6f)
            continue;

        ConsensusHypothesis hypothesis;
        hypothesis.frequencyHz = static_cast<float>(std::exp2(
            weightedLogFrequency / static_cast<double>(totalWeight)));
        hypothesis.confidence = clamp01(confidenceSum / totalWeight);
        hypothesis.periodicity = clamp01(periodicitySum / totalWeight);
        hypothesis.harmonicFamily = clamp01(harmonicFamilySum / totalWeight);
        hypothesis.supportCount = supportCount;
''',
'''        if (supportCount <= 0 || coordinateWeightSum <= 1.0e-6f
            || evidenceWeightSum <= 1.0e-6f)
        {
            continue;
        }

        ConsensusHypothesis hypothesis;
        hypothesis.frequencyHz = static_cast<float>(std::exp2(
            weightedLogFrequency / static_cast<double>(coordinateWeightSum)));
        hypothesis.confidence = clamp01(confidenceSum / evidenceWeightSum);
        hypothesis.periodicity = clamp01(periodicitySum / evidenceWeightSum);
        hypothesis.harmonicFamily = clamp01(harmonicFamilySum / evidenceWeightSum);
        hypothesis.tonalCleanliness = cleanlinessWeightSum > 1.0e-6f
            ? clamp01(cleanlinessSum / cleanlinessWeightSum) : 0.0f;
        hypothesis.supportCount = supportCount;
        hypothesis.cleanSupportCount = cleanSupportCount;
''',
'consensus publish split evidence')

cpp = one(cpp,
'''        const float meanEvidence = clamp01(totalWeight
            / static_cast<float>(std::max(1, supportCount)));
        const float directPenalty = directSupportCount == 0 ? 0.16f : 0.0f;
        hypothesis.evidenceScore = meanEvidence
                                 * (0.70f + 0.30f * hypothesis.consensus)
                                 + 0.045f * static_cast<float>(directSupportCount)
                                 - directPenalty;
        const float minimumHypothesisEvidence = rescueMode_ ? 0.15f : 0.20f;
        hypothesis.valid = hypothesis.evidenceScore > minimumHypothesisEvidence;
''',
'''        const float meanEvidence = clamp01(evidenceWeightSum
            / static_cast<float>(std::max(1, supportCount)));
        const float directPenalty = directSupportCount == 0 ? 0.16f : 0.0f;
        hypothesis.evidenceScore = meanEvidence
                                 * (0.58f
                                  + 0.22f * hypothesis.consensus
                                  + 0.20f * hypothesis.tonalCleanliness)
                                 + 0.045f * static_cast<float>(directSupportCount)
                                 - directPenalty;
        const float minimumHypothesisEvidence = rescueMode_ ? 0.15f : 0.20f;
        const float cleanFloor = rescueMode_ ? 0.22f : 0.26f;
        const bool cleanEnough = hypothesis.tonalCleanliness >= cleanFloor
            || (hypothesis.cleanSupportCount >= 2
                && hypothesis.harmonicFamily >= 0.42f
                && hypothesis.tonalCleanliness >= 0.18f);
        hypothesis.valid = hypothesis.evidenceScore > minimumHypothesisEvidence
            && cleanEnough;
''',
'consensus clean permission')

# Fresh raw fallback remains possible when consensus clustering has no usable
# hypothesis, but dirty candidates no longer get a back door around cleanliness.
cpp = one(cpp,
'''            const float score = candidateBaseScore(candidate)
                * pathReliability(candidate.pathIndex, candidate.frequencyHz);
            if (score > bestScore)
''',
'''            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            if (candidate.tonalCleanliness >= 0.0f && candidateCleanliness < 0.24f)
                continue;
            const float score = candidateBaseScore(candidate)
                * pathPitchAuthority(candidate.pathIndex, candidate.frequencyHz)
                * (0.70f + 0.30f * candidateCleanliness);
            if (score > bestScore)
''',
'clean raw fallback')

# Provisional measurements are useful for the supervisor's grey zone, but their
# coordinate must also prefer fresh, clean, frequency-authoritative paths.
cpp = one(cpp,
'''        const auto consider = [&best, &bestScore](const CandidateSlot& slot,
                                                  int maximumAge) noexcept
''',
'''        const auto consider = [this, &best, &bestScore](const CandidateSlot& slot,
                                                        int maximumAge) noexcept
''',
'provisional captures path roles')

cpp = one(cpp,
'''            const float ageWeight = std::exp(-0.22f
                * static_cast<float>(std::max(0, slot.ageInHops)));
            const float score = ageWeight
                * (0.62f * clamp01(candidate.confidence)
                 + 0.38f * clamp01(candidate.periodicity));
''',
'''            const float ageWeight = std::exp(-0.55f
                * static_cast<float>(std::max(0, slot.ageInHops)));
            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            if (candidate.tonalCleanliness >= 0.0f && candidateCleanliness < 0.16f)
                return;
            const float score = ageWeight
                * pathPitchAuthority(candidate.pathIndex, candidate.frequencyHz)
                * (0.46f * clamp01(candidate.confidence)
                 + 0.28f * clamp01(candidate.periodicity)
                 + 0.26f * candidateCleanliness);
''',
'provisional fresh clean score')

# Strong current evidence may demote detector history only when it is also clean.
cpp = cpp.replace(
'''hypothesis.harmonicFamily >= 0.58f
                    && hypothesis.periodicity >= 0.52f''',
'''hypothesis.harmonicFamily >= 0.58f
                    && hypothesis.periodicity >= 0.52f
                    && hypothesis.tonalCleanliness >= 0.34f''')
cpp = cpp.replace(
'''current.harmonicFamily >= 0.58f
                && current.periodicity >= 0.52f''',
'''current.harmonicFamily >= 0.58f
                && current.periodicity >= 0.52f
                && current.tonalCleanliness >= 0.34f''')
cpp = cpp.replace(
'''decision.candidate.harmonicFamily >= 0.58f
            && decision.candidate.periodicity >= 0.52f''',
'''decision.candidate.harmonicFamily >= 0.58f
            && decision.candidate.periodicity >= 0.52f
            && decision.candidate.tonalCleanliness >= 0.34f''')

cpp = one(cpp,
'''    decision.candidate.harmonicFamily = hypothesis.harmonicFamily;
    decision.candidate.aperiodicity = 1.0f - hypothesis.harmonicFamily;
    decision.candidate.valid = true;
''',
'''    decision.candidate.harmonicFamily = hypothesis.harmonicFamily;
    decision.candidate.aperiodicity = 1.0f - hypothesis.harmonicFamily;
    decision.candidate.tonalCleanliness = hypothesis.tonalCleanliness;
    decision.candidate.valid = true;
''',
'decision carries cleanliness')

# ---------------------------------------------------------------------------
# Functional regressions: (1) formant-shaped noise may be energetic/resonant but
# must not become F0; (2) path roles remain complementary; (3) a fresh sung note
# change must be observed before musical Hold gets involved.
anchor = '''    // RAPID_F0_OBSERVATION_MUST_PRECEDE_HOLD_V1\n'''
if test.count(anchor) != 1:
    raise RuntimeError(f'path-role test anchor: expected one, found {test.count(anchor)}')

extra_tests = r'''    // PATH_ROLE_SPLIT_V1 regression: the low-rate path is useful for pitch
    // geometry, but the full/half-rate observations carry more information about
    // whether that geometry was extracted from clean tonal voice.
    auto pathRoleTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    pathRoleTracker->prepare(48000.0);
    success &= check(pathRoleTracker->pathCleanlinessAuthority(0, 220.0f)
                         > pathRoleTracker->pathCleanlinessAuthority(3, 220.0f)
                     && pathRoleTracker->pathPitchAuthority(2, 90.0f)
                         > pathRoleTracker->pathPitchAuthority(0, 90.0f),
                     "multirate_paths_split_pitch_and_cleanliness_authority");

    // Resonant/formant-shaped noise: two narrow resonances are deliberately
    // capable of producing autocorrelation peaks, but there is no harmonic vocal
    // family and therefore no trustworthy F0.
    auto formantNoiseTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    formantNoiseTracker->prepare(48000.0);
    formantNoiseTracker->setRange(70.0f, 700.0f);
    std::uint32_t formantRng = 0x9e3779b9u;
    float f1y1 = 0.0f, f1y2 = 0.0f;
    float f2y1 = 0.0f, f2y2 = 0.0f;
    int formantNoiseValid = 0;
    int formantNoiseMeasurements = 0;
    const float r1 = 0.965f;
    const float r2 = 0.955f;
    const float c1 = 2.0f * r1 * std::cos(2.0f * 3.14159265358979323846f * 720.0f / 48000.0f);
    const float c2 = 2.0f * r2 * std::cos(2.0f * 3.14159265358979323846f * 1280.0f / 48000.0f);
    for (int sample = 0; sample < 32000; ++sample)
    {
        formantRng ^= formantRng << 13;
        formantRng ^= formantRng >> 17;
        formantRng ^= formantRng << 5;
        const float noise = (static_cast<float>(formantRng & 0xffffu) / 32767.5f - 1.0f) * 0.006f;
        const float y1 = noise + c1 * f1y1 - r1 * r1 * f1y2;
        const float y2 = noise + c2 * f2y1 - r2 * r2 * f2y2;
        f1y2 = f1y1; f1y1 = y1;
        f2y2 = f2y1; f2y1 = y2;
        const float shaped = std::clamp(0.020f * (y1 + 0.65f * y2), -0.20f, 0.20f);
        ModernPitchEngine::PitchObservation observed;
        if (formantNoiseTracker->processSample(shaped, observed))
        {
            if (observed.measurementAvailable)
                ++formantNoiseMeasurements;
            if (observed.valid)
                ++formantNoiseValid;
        }
    }
    std::cerr << "voice_aware_formant_noise_valid=" << formantNoiseValid
              << " provisional=" << formantNoiseMeasurements << '\n';
    success &= check(formantNoiseValid == 0 && formantNoiseMeasurements <= 4,
                     "formant_shaped_noise_never_becomes_vocal_f0");

'''
test = test.replace(anchor, extra_tests + anchor, 1)

# Ensure the new semantics are really present before writing anything.
for marker in [
    'PATH_ROLE_SPLIT_V1',
    'STALE_PATH_VERIFIES_NOT_STEERS_V1',
    'pathPitchAuthority',
    'pathCleanlinessAuthority',
    'tonalCleanliness'
]:
    if marker not in hpp and marker not in cpp:
        raise RuntimeError(f'missing materialized marker {marker}')

hpp_path.write_text(hpp)
cpp_path.write_text(cpp)
test_path.write_text(test)
print('VOICE_AWARE_PATH_ROLES_V1 materialized')
