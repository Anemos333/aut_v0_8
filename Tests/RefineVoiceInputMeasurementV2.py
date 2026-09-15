from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()


def one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


# GLOTTAL_EVIDENCE_FUSION_V2
# A single low-rate resonant path may localise a plausible period, but that is
# not enough to call it a vocal F0.  Real analysed candidates (tonalCleanliness
# >= 0) need either independent direct-path corroboration or exceptionally clean
# source evidence.  Legacy hand-built unit-test candidates keep their old
# semantics because tonalCleanliness remains -1 for those tests.
cpp = one(cpp,
'''            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            if (candidate.tonalCleanliness >= 0.0f && candidateCleanliness < 0.24f)
                continue;
            const float score = candidateBaseScore(candidate)
''',
'''            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            // SINGLE_PATH_RESONANCE_IS_NOT_VOICE_V1: raw fallback has no
            // independent path corroboration, so a real analyzer candidate must
            // be substantially cleaner than the ordinary multi-path floor.
            const float solitaryCleanFloor = candidate.pathIndex <= 1 ? 0.52f
                : (candidate.pathIndex == 2 ? 0.62f : 0.70f);
            if (candidate.tonalCleanliness >= 0.0f
                && candidateCleanliness < solitaryCleanFloor)
            {
                continue;
            }
            const float score = candidateBaseScore(candidate)
''',
'raw single-path vocal evidence')

cpp = one(cpp,
'''        const float minimumHypothesisEvidence = rescueMode_ ? 0.15f : 0.20f;
        const float cleanFloor = rescueMode_ ? 0.22f : 0.26f;
        const bool cleanEnough = hypothesis.tonalCleanliness >= cleanFloor
            || (hypothesis.cleanSupportCount >= 2
                && hypothesis.harmonicFamily >= 0.42f
                && hypothesis.tonalCleanliness >= 0.18f);
        hypothesis.valid = hypothesis.evidenceScore > minimumHypothesisEvidence
            && cleanEnough;
''',
'''        const float minimumHypothesisEvidence = rescueMode_ ? 0.15f : 0.20f;
        const float cleanFloor = rescueMode_ ? 0.22f : 0.26f;
        const bool cleanEnough = hypothesis.tonalCleanliness >= cleanFloor
            || (hypothesis.cleanSupportCount >= 2
                && hypothesis.harmonicFamily >= 0.42f
                && hypothesis.tonalCleanliness >= 0.18f);
        // GLOTTAL_EVIDENCE_FUSION_V2: a resonant pole can look periodic on one
        // decimated path.  Two direct paths constitute independent geometric
        // evidence; otherwise demand much stronger cleanliness from the lone
        // path.  This is detector evidence fusion, not a global confidence gate.
        const float solitaryHypothesisFloor = rescueMode_ ? 0.56f : 0.60f;
        const bool sourceStructureCredible = hypothesis.directSupportCount >= 2
            || hypothesis.tonalCleanliness >= solitaryHypothesisFloor;
        hypothesis.valid = hypothesis.evidenceScore > minimumHypothesisEvidence
            && cleanEnough
            && sourceStructureCredible;
''',
'consensus single-resonance veto')

# PROVISIONAL_PATH_CLEANLINESS_V1
# Low-rate paths deliberately trade spectral detail for period geometry.  They
# therefore need stronger source-cleanliness evidence before their coordinate is
# even published as a provisional F0. This does not raise the detector-wide
# threshold: full/half rate remain sensitive, while quarter/eighth rate can
# still contribute strongly once corroborated in consensus.
cpp = one(cpp,
'''            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            if (candidate.tonalCleanliness >= 0.0f && candidateCleanliness < 0.22f)
                return;
            const float score = ageWeight
''',
'''            const float candidateCleanliness = candidate.tonalCleanliness >= 0.0f
                ? clamp01(candidate.tonalCleanliness) : 1.0f;
            const float provisionalPathFloor = candidate.pathIndex <= 0 ? 0.22f
                : (candidate.pathIndex == 1 ? 0.24f
                   : (candidate.pathIndex == 2 ? 0.46f : 0.54f));
            if (candidate.tonalCleanliness >= 0.0f
                && candidateCleanliness < provisionalPathFloor)
            {
                return;
            }
            const float score = ageWeight
''',
'path-specific provisional cleanliness')

cpp_path.write_text(cpp)
print('GLOTTAL_EVIDENCE_FUSION_V2 materialized')
