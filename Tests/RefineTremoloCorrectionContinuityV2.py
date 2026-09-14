from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


def exact(text, old, new, count, label):
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{label}: expected {count} matches, got {actual}")
    return text.replace(old, new)


cpp_p = Path('Source/ModernPitchEngine.cpp')
test_p = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_p.read_text()
test = test_p.read_text()

cpp = one(cpp,
'''    // VOICE_LABELS_CANNOT_FREEZE_CORRECTION_V1: breath/phonetic/absence labels\n    // are identity vetoes only. If a finite real F0 already exists, they cannot\n    // freeze the owned transport/correction coordinate and thereby make a\n    // moving voice drift away from its still-owned scale target.\n    bool identityOnlyVeto = false;\n\n    // Breath/absence is positive evidence and therefore wins even if a noisy\n''',
'''    // VOICE_LABELS_CANNOT_FREEZE_CORRECTION_V1: breath/phonetic/absence labels\n    // are identity vetoes only. If a finite real F0 already exists, they cannot\n    // freeze the owned transport/correction coordinate and thereby make a\n    // moving voice drift away from its still-owned scale target.\n    bool identityOnlyVeto = false;\n\n    // TREMolo_LOCAL_CONTINUITY_OWNS_TRANSPORT_V2: an amplitude trough can make\n    // the presence classifier blink false even while the physical F0 remains a\n    // continuous coordinate of the already-owned voice.  Do not turn that\n    // telemetry blink into a stale correction.  Conversely, a periodic accident\n    // far from the owned source transport during true breath/noise still has no\n    // audible authority.  36 cents matches the existing maximum bounded catchup\n    // step and is intentionally far below a scale-degree or octave decision.\n    const float labelCoordinateHz =\n        std::isfinite(observation.correctionFrequencyHz)\n        && observation.correctionFrequencyHz > 0.0f\n        ? observation.correctionFrequencyHz\n        : observation.frequencyHz;\n    const bool localOwnedCoordinate = validPitch\n        && std::isfinite(labelCoordinateHz)\n        && labelCoordinateHz > 0.0f\n        && std::isfinite(state.transportPeriodHz)\n        && state.transportPeriodHz > 0.0\n        && std::abs(1200.0 * std::log2(\n            static_cast<double>(labelCoordinateHz) / state.transportPeriodHz)) <= 36.0;\n    const bool labelMayTransport = validPitch\n        && (observation.audioPresent || localOwnedCoordinate);\n\n    // Breath/absence is positive evidence and therefore wins even if a noisy\n''',
'insert local owned coordinate gate')

cpp = exact(cpp,
'''        if (!validPitch || !observation.audioPresent)\n''',
'''        if (!labelMayTransport)\n''', 2, 'replace presence-only label gates')

cpp = one(cpp,
'''            // PRESENT_AUDIO_OWNS_CORRECTION_CONTINUITY_V1: a formally valid\n            // periodic accident inside true breath/absence still has zero\n            // transport authority. Only actual present signal may override a\n            // mistaken breath/absence label and keep the owned correction live.\n            // Hold the existing target and correction exactly, as before.\n''',
'''            // PRESENT_AUDIO_OWNS_CORRECTION_CONTINUITY_V1: a formally valid\n            // periodic accident inside true breath/absence still has zero\n            // transport authority unless it is locally continuous with the\n            // already-owned source coordinate. Hold exact correction otherwise.\n''',
'update breath gate comment')

test = one(test,
'''    auto absenceMovingF0 = strongPitch(446.0f);\n    // The rich classifier says "absence" while the input-presence path still\n    // sees real signal, matching a tremolo trough rather than actual silence.\n    absenceMovingF0.audioPresent = true;\n    absenceMovingF0.correctionFrequencyHz = 446.0f;\n''',
'''    auto absenceMovingF0 = strongPitch(446.0f);\n    // Reproduce the pathological trough directly: the presence bit blinks off,\n    // the rich classifier says absence, but the measured F0 is still a small\n    // continuous movement of the already-owned source coordinate.\n    absenceMovingF0.audioPresent = false;\n    absenceMovingF0.correctionFrequencyHz = 446.0f;\n''',
'absence regression exercises false-negative presence')

test = one(test,
'''                     "absence_label_cannot_freeze_owned_correction");\n\n    ModernPitchEngine::CorrectionState zeroResponseTransition;\n''',
'''                     "absence_label_cannot_freeze_owned_correction");\n    success &= check(absenceResidual < 2.0,\n                     "tremolo_presence_blink_cannot_create_dry_like_escape");\n\n    ModernPitchEngine::CorrectionState zeroResponseTransition;\n''',
'add explicit tremolo regression label')

cpp_p.write_text(cpp)
test_p.write_text(test)
print('tremolo correction continuity v2 materialized')
