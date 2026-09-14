from pathlib import Path

p = Path('Tests/RefineVoiceTransitionContinuumV1.py')
text = p.read_text()
old = r'''cpp = one(cpp,
''' + "'''" + r'''        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.stableBodyObservations = 0;
''' + "'''" + r''',
''' + "'''" + r'''        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;
        state.stableBodyObservations = 0;
''' + "'''" + r''',
'breath interval cancels local challenger')'''
new = r'''cpp = one(cpp,
''' + "'''" + r'''        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: a breath/absence interval
        // breaks musical-change persistence but never changes audible state.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.stableBodyObservations = 0;
''' + "'''" + r''',
''' + "'''" + r'''        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: a breath/absence interval
        // breaks musical-change persistence but never changes audible state.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;
        state.stableBodyObservations = 0;
''' + "'''" + r''',
'breath interval cancels local challenger')'''
if text.count(old) != 1:
    raise SystemExit(f'breath refiner block count={text.count(old)}')
p.write_text(text.replace(old, new, 1))
print('voice transition breath anchor disambiguated')
