from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
voice_path = root / 'Source' / 'VoiceEvidenceAnalyzer.h'
cpp = cpp_path.read_text()
voice = voice_path.read_text()


def one(text, anchor, replacement, label):
    count = text.count(anchor)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(anchor, replacement, 1)

marker = 'VOICE_BODY_LIVE_PERMISSION_V6_7_1'
if marker not in cpp:
    if 'VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7' not in cpp:
        raise RuntimeError('V6.7 must be materialized before V6.7.1')

    # The first V6.7 experiment proved the separation works but left one piece
    # of old prudence behind: a lower coordinate demonstrated by one strong path
    # plus the independent vocal-body F/2 probe was still forced through the
    # generic octave persistence window. V6.7.1 removes only that redundancy.
    # It also makes the permission deliberately short-lived so an upward note
    # cannot inherit permission that belonged to the previous lower register.

    voice = one(
        voice,
        '''        // Permission must appear quickly on a real downward note change, but\n        // must also disappear faster than broad polyphony suspicion.\n        smoothMetric(smoothed_.lowerFamilyEvidence, target.lowerFamilyEvidence,\n                     fastAttack, coefficient(45.0f));\n''',
        '''        // VOICE_BODY_LIVE_PERMISSION_V6_7_1\n        // Permission is current physical evidence, not register memory. Attack\n        // remains fast and release is intentionally short so a completed upward\n        // transition cannot keep authorising the old lower coordinate.\n        smoothMetric(smoothed_.lowerFamilyEvidence, target.lowerFamilyEvidence,\n                     fastAttack, coefficient(14.0f));\n''',
        'live lower-family release')

    cpp = one(
        cpp,
        '''            const bool downwardFamilyChallengeV67 =\n                challengesCommittedUpperV67(best);\n            directDecision.authoritativeDirect = !downwardFamilyChallengeV67\n                || nativeSupport >= 2;\n            directDecision.valid = true;\n''',
        '''            const bool downwardFamilyChallengeV67 =\n                challengesCommittedUpperV67(best);\n            // VOICE_BODY_LIVE_PERMISSION_V6_7_1\n            // One structurally strong path plus an independent live vocal-body\n            // F/2 signature is already two different physical observations. Do\n            // not demand a second F0 path as a bureaucratic permission layer.\n            // If an upper path is still structurally strong, the ambiguity loop\n            // above has already routed the pair to the resolver instead.\n            directDecision.authoritativeDirect = !downwardFamilyChallengeV67\n                || nativeSupport >= 2\n                || lowerFamilyPermissionV67;\n            directDecision.valid = true;\n''',
        'single lower path plus body authority')

    cpp = one(
        cpp,
        '''        if (decision.authoritativeDirect && decision.directSupportCount >= 2)\n        {\n            int directRescueDelta = 0;\n''',
        '''        if (decision.authoritativeDirect\n            && (decision.directSupportCount >= 2\n                || voiceAllowsLowerFamilyV67()))\n        {\n            // VOICE_BODY_LIVE_PERMISSION_V6_7_1: stale detector memory cannot\n            // demand a second F0 path after current vocal-body physics has\n            // independently corroborated the fresh lower coordinate.\n            int directRescueDelta = 0;\n''',
        'rescue body corroboration')

    cpp_path.write_text(cpp)
    voice_path.write_text(voice)

print('VOICE_BODY_LIVE_PERMISSION_V6_7_1 materialized')
