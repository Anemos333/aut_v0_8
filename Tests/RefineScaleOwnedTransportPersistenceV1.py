from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)

p = Path('Source/ModernPitchEngine.cpp')
text = p.read_text()

text = one(text,
'''    if (state.targetValid
        && (confirmedBreath
            || confirmedAbsence
            || (richEvidence
                && (confirmedBreathFrame || confirmedAbsenceFrame))))
    {
        state.stableBodyObservations = 0;
''',
'''    if (state.targetValid
        && (confirmedBreath
            || confirmedAbsence
            || (richEvidence
                && (confirmedBreathFrame || confirmedAbsenceFrame))))
    {
        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: a breath/absence interval
        // breaks musical-change persistence but never changes audible state.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.stableBodyObservations = 0;
''',
'breath resets challenger persistence')

text = one(text,
'''    if (!validPitch)
    {
        ++state.invalidObservations;
''',
'''    if (!validPitch)
    {
        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: detector holes cannot bridge
        // two unrelated challenger fragments into a target revision.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        ++state.invalidObservations;
''',
'invalid F0 resets challenger persistence')

text = one(text,
'''    if (explicitPhoneticFrame && state.targetValid)
    {
        state.stableBodyObservations = 0;
        return;
    }
''',
'''    if (explicitPhoneticFrame && state.targetValid)
    {
        // A consonant can veto this observation, but cannot contribute stale
        // evidence to a later note change or touch target/transport/correction.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.stableBodyObservations = 0;
        return;
    }
''',
'phonetic veto resets challenger persistence')

p.write_text(text)
print('scale-owned challenger persistence refinement materialized')
