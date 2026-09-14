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

# ---------------------------------------------------------------------------
# Voice/breath labels may veto note identity, but once a real F0 exists they
# must not freeze transport/correction. A stale correction on a moving source is
# an audible dry-like escape even though targetLog2 itself remains owned.
cpp = one(cpp,
'''    const bool confirmedAbsence = state.noteBodyLatched\n        && state.uncertainSamples >= ambiguousReleaseSamples;\n\n    // Breath/absence is positive evidence and therefore wins even if a noisy\n''',
'''    const bool confirmedAbsence = state.noteBodyLatched\n        && state.uncertainSamples >= ambiguousReleaseSamples;\n\n    // VOICE_LABELS_CANNOT_FREEZE_CORRECTION_V1: breath/phonetic/absence labels\n    // are identity vetoes only. If a finite real F0 already exists, they cannot\n    // freeze the owned transport/correction coordinate and thereby make a\n    // moving voice drift away from its still-owned scale target.\n    bool identityOnlyVeto = false;\n\n    // Breath/absence is positive evidence and therefore wins even if a noisy\n''',
'insert identity-only veto state')

cpp = one(cpp,
'''    if (state.targetValid\n        && (confirmedBreath\n            || confirmedAbsence\n            || (richEvidence\n                && (confirmedBreathFrame || confirmedAbsenceFrame))))\n    {\n        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: a breath/absence interval\n        // breaks musical-change persistence but never changes audible state.\n        state.identityChallengerDirection = 0;\n        state.identityChallengerEvidence = 0.0;\n        state.transportChallengerHops = 0;\n        state.transportChallengerLog2 = 0.0;\n        state.stableBodyObservations = 0;\n        if (confirmedAbsence\n            || confirmedAbsenceFrame\n            || state.breathEvidenceSamples > static_cast<int>(0.12 * sampleRate_))\n        {\n            state.pitchCentreValid = false;\n        }\n        return;\n    }\n''',
'''    if (state.targetValid\n        && (confirmedBreath\n            || confirmedAbsence\n            || (richEvidence\n                && (confirmedBreathFrame || confirmedAbsenceFrame))))\n    {\n        // CONSECUTIVE_CHALLENGER_EVIDENCE_V1: a breath/absence interval\n        // breaks musical-change persistence but never changes audible state.\n        state.identityChallengerDirection = 0;\n        state.identityChallengerEvidence = 0.0;\n        state.transportChallengerHops = 0;\n        state.transportChallengerLog2 = 0.0;\n        state.stableBodyObservations = 0;\n\n        if (!validPitch)\n        {\n            // With no real source coordinate there is nothing new to transport.\n            // Hold the existing target and correction exactly, as before.\n            if (confirmedAbsence\n                || confirmedAbsenceFrame\n                || state.breathEvidenceSamples > static_cast<int>(0.12 * sampleRate_))\n            {\n                state.pitchCentreValid = false;\n            }\n            return;\n        }\n\n        // VALID_F0_TRANSPORT_SURVIVES_LABEL_V1: the classifier can say\n        // "uncertain/breath/phonetic", but a real coordinate must still keep\n        // the already-owned correction aligned with the moving source. It gets\n        // no authority to nominate a different musical degree.\n        identityOnlyVeto = true;\n    }\n''',
'breath absence becomes identity-only veto')

cpp = one(cpp,
'''    if (explicitPhoneticFrame && state.targetValid)\n    {\n        // A consonant can veto this observation, but cannot contribute stale\n        // evidence to a later note change or touch target/transport/correction.\n        state.identityChallengerDirection = 0;\n        state.identityChallengerEvidence = 0.0;\n        state.stableBodyObservations = 0;\n        state.transportChallengerHops = 0;\n        state.transportChallengerLog2 = 0.0;\n        return;\n    }\n''',
'''    if (explicitPhoneticFrame && state.targetValid)\n    {\n        // A consonant/event label may veto note identity, but it cannot freeze\n        // source transport when the detector has already supplied a real F0.\n        // Far/outlier coordinates remain protected by the existing transport\n        // innovation and octave safety walls below.\n        state.identityChallengerDirection = 0;\n        state.identityChallengerEvidence = 0.0;\n        state.stableBodyObservations = 0;\n        state.transportChallengerHops = 0;\n        state.transportChallengerLog2 = 0.0;\n        identityOnlyVeto = true;\n    }\n''',
'phonetic label becomes identity-only veto')

cpp = one(cpp,
'''    bool detectorScaleCommit = false;\n    if (state.targetValid)\n    {\n''',
'''    bool detectorScaleCommit = false;\n    if (state.targetValid && !identityOnlyVeto)\n    {\n''',
'block target nomination under label veto')

cpp = one(cpp,
'''    bool liveIdentityBreak = false;\n    bool forceTargetSwitch = false;\n    if (detectorScaleCommit)\n    {\n''',
'''    bool liveIdentityBreak = false;\n    bool forceTargetSwitch = false;\n    if (identityOnlyVeto)\n    {\n        // IDENTITY_VETO_TRANSPORT_CONTINUES_V1: hold musical identity and its\n        // continuity centre, but continue below into local transport/correction.\n        // This is the exact opposite of the old early-return freeze.\n        liveIdentityBreak = false;\n        forceTargetSwitch = false;\n    }\n    else if (detectorScaleCommit)\n    {\n''',
'hold identity centre while transport continues')

cpp = one(cpp,
'''    const bool freezeCommittedTransition =\n        state.trackingState == TrackingState::transition && !targetIdentityChanged;\n    if (state.noteBodyLatched && bodyPresent && !freezeCommittedTransition)\n    {\n''',
'''    const bool freezeCommittedTransition =\n        state.trackingState == TrackingState::transition && !targetIdentityChanged;\n    const bool transportBodyPresent = bodyPresent || (identityOnlyVeto && validPitch);\n    if (state.noteBodyLatched && transportBodyPresent && !freezeCommittedTransition)\n    {\n''',
'allow valid F0 transport through voice labels')

# ---------------------------------------------------------------------------
# Regression: a tremolo/phonetic/absence label may not freeze the previous
# correction while the real source coordinate moves. The target must stay the
# same, but the correction must remain aligned to that target.
anchor = '''    ModernPitchEngine::CorrectionState zeroResponseTransition;\n'''
insert = r'''    // VOICE_LABELS_CANNOT_FREEZE_CORRECTION_V1: rich voice labels are
    // allowed to veto note identity, never correction transport for a real F0.
    ModernPitchEngine::Parameters labelTransportParameters = explicitAuthorityParameters;
    labelTransportParameters.amount = 1.0f;
    labelTransportParameters.humanize = 0.0f;
    labelTransportParameters.preserveVibrato = 0.0f;
    labelTransportParameters.vibratoPreserve = 0.0f;
    labelTransportParameters.voiceEvidenceValid = true;

    ModernPitchEngine::ScaleQuantizer phoneticTransportQuantizer;
    phoneticTransportQuantizer.reset();
    phoneticTransportQuantizer.setScale(authorityChromatic.data(),
                                         static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState phoneticTransportState;
    auto transportBase = strongPitch(450.0f);
    transportBase.audioPresent = true;
    transportBase.correctionFrequencyHz = 450.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(phoneticTransportState,
                                      phoneticTransportQuantizer,
                                      transportBase,
                                      labelTransportParameters);
    const double phoneticOwnedTarget = phoneticTransportState.targetLog2;

    ModernPitchEngine::Parameters phoneticTransportParameters = labelTransportParameters;
    phoneticTransportParameters.voiceBodyEnergy = 0.66f;
    phoneticTransportParameters.voiceHarmonicity = 0.68f;
    phoneticTransportParameters.voiceSpectralReliability = 0.72f;
    phoneticTransportParameters.voiceBreathiness = 0.24f;
    phoneticTransportParameters.voiceEventStrength = 0.95f;
    auto phoneticMovingF0 = strongPitch(454.0f);
    phoneticMovingF0.audioPresent = true;
    phoneticMovingF0.correctionFrequencyHz = 454.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(phoneticTransportState,
                                      phoneticTransportQuantizer,
                                      phoneticMovingF0,
                                      phoneticTransportParameters);
    const double phoneticTargetHz = std::exp2(phoneticTransportState.targetLog2);
    const double phoneticCorrectedHz = 454.0 * std::exp2(
        phoneticTransportState.desiredCents / 1200.0);
    const double phoneticResidual = std::abs(1200.0 * std::log2(
        phoneticCorrectedHz / phoneticTargetHz));
    success &= check(std::abs(phoneticTransportState.targetLog2
                              - phoneticOwnedTarget) < 1.0e-12
                     && phoneticResidual < 2.0,
                     "phonetic_label_cannot_freeze_owned_correction");

    ModernPitchEngine::ScaleQuantizer absenceTransportQuantizer;
    absenceTransportQuantizer.reset();
    absenceTransportQuantizer.setScale(authorityChromatic.data(),
                                        static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState absenceTransportState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(absenceTransportState,
                                      absenceTransportQuantizer,
                                      transportBase,
                                      labelTransportParameters);
    const double absenceOwnedTarget = absenceTransportState.targetLog2;

    ModernPitchEngine::Parameters absenceTransportParameters = labelTransportParameters;
    absenceTransportParameters.voiceBodyEnergy = 0.10f;
    absenceTransportParameters.voiceHarmonicity = 0.10f;
    absenceTransportParameters.voiceSpectralReliability = 0.10f;
    absenceTransportParameters.voiceBreathiness = 0.88f;
    absenceTransportParameters.voiceEventStrength = 0.10f;
    auto absenceMovingF0 = strongPitch(446.0f);
    absenceMovingF0.audioPresent = false;
    absenceMovingF0.correctionFrequencyHz = 446.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(absenceTransportState,
                                      absenceTransportQuantizer,
                                      absenceMovingF0,
                                      absenceTransportParameters);
    const double absenceTargetHz = std::exp2(absenceTransportState.targetLog2);
    const double absenceCorrectedHz = 446.0 * std::exp2(
        absenceTransportState.desiredCents / 1200.0);
    const double absenceResidual = std::abs(1200.0 * std::log2(
        absenceCorrectedHz / absenceTargetHz));
    success &= check(std::abs(absenceTransportState.targetLog2
                              - absenceOwnedTarget) < 1.0e-12
                     && absenceResidual < 2.0,
                     "absence_label_cannot_freeze_owned_correction");

    ModernPitchEngine::CorrectionState zeroResponseTransition;
'''
test = one(test, anchor, insert, 'insert label-freeze regressions')

cpp_p.write_text(cpp)
test_p.write_text(test)
print('no-label-freeze v1 materialized')
