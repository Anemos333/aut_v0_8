from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


cpp_p = Path('Source/ModernPitchEngine.cpp')
test_p = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_p.read_text()
test = test_p.read_text()

cpp = one(cpp,
'''        if (!validPitch)\n        {\n            // With no real source coordinate there is nothing new to transport.\n''',
'''        if (!validPitch || !observation.audioPresent)\n        {\n            // PRESENT_AUDIO_OWNS_CORRECTION_CONTINUITY_V1: a formally valid\n            // periodic accident inside true breath/absence still has zero\n            // transport authority. Only actual present signal may override a\n            // mistaken breath/absence label and keep the owned correction live.\n''',
'presence gate for breath/absence transport')

cpp = one(cpp,
'''        state.transportChallengerHops = 0;\n        state.transportChallengerLog2 = 0.0;\n        identityOnlyVeto = true;\n    }\n\n    // A strong breath/absence before any body latch must not become a note just\n''',
'''        state.transportChallengerHops = 0;\n        state.transportChallengerLog2 = 0.0;\n        if (!validPitch || !observation.audioPresent)\n            return;\n        identityOnlyVeto = true;\n    }\n\n    // A strong breath/absence before any body latch must not become a note just\n''',
'presence gate for phonetic transport')

test = one(test,
'''    auto absenceMovingF0 = strongPitch(446.0f);\n    absenceMovingF0.audioPresent = false;\n    absenceMovingF0.correctionFrequencyHz = 446.0f;\n''',
'''    auto absenceMovingF0 = strongPitch(446.0f);\n    // The rich classifier says "absence" while the input-presence path still\n    // sees real signal, matching a tremolo trough rather than actual silence.\n    absenceMovingF0.audioPresent = true;\n    absenceMovingF0.correctionFrequencyHz = 446.0f;\n''',
'absence mislabel test uses present signal')

cpp_p.write_text(cpp)
test_p.write_text(test)
print('no-label-freeze v1 presence refinement applied')
