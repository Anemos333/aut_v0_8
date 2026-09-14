from pathlib import Path

p = Path('Tests/RefineVoiceTransitionContinuumV1.py')
text = p.read_text()

old_invalid = r'''cpp = one(cpp,
''' + "'''" + r'''    if (!validPitch)
    {
        ++state.invalidObservations;

        // SCALE_OWNS_TRANSPORT_V1: an F0 hole never invents an audible
''' + "'''" + r''',
''' + "'''" + r'''    if (!validPitch)
    {
        ++state.invalidObservations;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;

        // SCALE_OWNS_TRANSPORT_V1: an F0 hole never invents an audible
''' + "'''" + r''',
'invalid frame resets local challenger only')'''

new_invalid = r'''cpp = one(cpp,
''' + "'''" + r'''    if (!validPitch)
    {
        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: detector holes cannot bridge
        // two unrelated challenger fragments into a target revision.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        ++state.invalidObservations;

        // SCALE_OWNS_TRANSPORT_V1: an F0 hole never invents an audible
''' + "'''" + r''',
''' + "'''" + r'''    if (!validPitch)
    {
        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: detector holes cannot bridge
        // two unrelated challenger fragments into a target revision.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        ++state.invalidObservations;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;

        // SCALE_OWNS_TRANSPORT_V1: an F0 hole never invents an audible
''' + "'''" + r''',
'invalid frame resets local challenger only')'''

if text.count(old_invalid) != 1:
    raise SystemExit(f'invalid refiner anchor count={text.count(old_invalid)}')
text = text.replace(old_invalid, new_invalid, 1)

old_phonetic = r'''cpp = one(cpp,
''' + "'''" + r'''    if (explicitPhoneticFrame && state.targetValid)
    {
        state.stableBodyObservations = 0;
        return;
    }
''' + "'''" + r''',
''' + "'''" + r'''    if (explicitPhoneticFrame && state.targetValid)
    {
        state.stableBodyObservations = 0;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;
        return;
    }
''' + "'''" + r''',
'phonetic veto cancels local challenger')'''

new_phonetic = r'''cpp = one(cpp,
''' + "'''" + r'''    if (explicitPhoneticFrame && state.targetValid)
    {
        // A consonant can veto this observation, but cannot contribute stale
        // evidence to a later note change or touch target/transport/correction.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.stableBodyObservations = 0;
        return;
    }
''' + "'''" + r''',
''' + "'''" + r'''    if (explicitPhoneticFrame && state.targetValid)
    {
        // A consonant can veto this observation, but cannot contribute stale
        // evidence to a later note change or touch target/transport/correction.
        state.identityChallengerDirection = 0;
        state.identityChallengerEvidence = 0.0;
        state.stableBodyObservations = 0;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;
        return;
    }
''' + "'''" + r''',
'phonetic veto cancels local challenger')'''

if text.count(old_phonetic) != 1:
    raise SystemExit(f'phonetic refiner anchor count={text.count(old_phonetic)}')
text = text.replace(old_phonetic, new_phonetic, 1)

marker = "# Small innovations are continuous vocal motion and follow immediately."
extra = r'''# Confirmed breath/absence also breaks consecutiveness of local source motion,
# without changing target, transport or correction.
cpp = one(cpp,
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
'breath interval cancels local challenger')

'''
if text.count(marker) != 1:
    raise SystemExit(f'insertion marker count={text.count(marker)}')
text = text.replace(marker, extra + marker, 1)

p.write_text(text)
print('voice transition refinement anchors aligned')
