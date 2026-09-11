from pathlib import Path
import re


def require_once(text: str, needle: str, label: str) -> None:
    count = text.count(needle)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 occurrence, found {count}")


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 occurrence, found {count}")
    return result


engine_path = Path("Source/ModernPitchEngine.cpp")
s = engine_path.read_text()

marker = "        && parameters.voiceEventStrength <= 0.30f;\n\n    const float bodyAttack"
require_once(s, marker, "phonetic insertion marker")
insert = """        && parameters.voiceEventStrength <= 0.30f;

    // SCALE_OWNS_VOICE_V2: detector state describes evidence only. Audible
    // material never receives permission to return to dry/source pitch merely
    // because it is breathy, aperiodic or phonetic.
    const bool explicitPhoneticFrame = richEvidence
        && (parameters.voiceEventStrength >= 0.82f
            || (parameters.voiceBreathiness >= 0.76f
                && parameters.voiceHarmonicity <= 0.48f));

    const float bodyAttack"""
s = s.replace(marker, insert, 1)

start_marker = "        if (advanceConservativeF0Rescue(state, quantizer, observation,"
start = s.index(start_marker)
end_marker = "\n\n        // SOUND_EQUALS_CORRECTION_V1: audio presence owns the voice"
end = s.index(end_marker, start)
old = s[start:end]
if "setState(TrackingState::acquire);" not in old:
    raise SystemExit("rescue block shape changed")
new = """        if (advanceConservativeF0Rescue(state, quantizer, observation,
                                        parameters, rescueBodyFrame))
        {
            setState(state.rescueTargetShifted
                ? TrackingState::transition
                : TrackingState::stable);
            return;
        }"""
s = s[:start] + new + s[end:]

anchor = s.index("// SOUND_EQUALS_CORRECTION_V1: audio presence owns the voice")
start = s.index("        if (observation.audioPresent)\n", anchor)
end_marker = "\n\n        // Missing F0 is not missing voice."
end = s.index(end_marker, start)
new = """        if (observation.audioPresent)
        {
            state.noteBodyLatched = true;
            state.noteBodyConfidence = 1.0f;
            state.stableBodyObservations = std::max(4, state.stableBodyObservations);
            state.breathEvidenceSamples = 0;
            state.uncertainSamples = 0;
            if (state.targetValid)
                setState(explicitPhoneticFrame
                    ? TrackingState::unvoiced
                    : TrackingState::stable);
            else
                setState(TrackingState::acquire);
            return;
        }"""
s = s[:start] + new + s[end:]

anchor = s.index("// Missing F0 is not missing voice.")
start = s.index("        if (state.noteBodyLatched)\n", anchor)
end_marker = "\n\n        // TARGET_AUTHORITY_TAIL_HOLD_V1:"
end = s.index(end_marker, start)
new = """        if (state.noteBodyLatched)
        {
            if (state.targetValid)
            {
                setState(explicitPhoneticFrame
                    ? TrackingState::unvoiced
                    : TrackingState::stable);
                return;
            }
            const int reacquireSamples = static_cast<int>(std::lround(0.070 * sampleRate_));
            if (state.pitchStaleSamples >= reacquireSamples)
                setState(TrackingState::acquire);
            return;
        }"""
s = s[:start] + new + s[end:]

# A formally valid F0 is still enough to define a scale coordinate at the fully
# rigid endpoint, even when the timbral evidence calls that frame breath/noise.
# Softer settings retain the old phonetic behaviour because the user explicitly
# requested less authority.
old = """    if (!state.noteBodyLatched && richEvidence
        && (confirmedBreathFrame || confirmedAbsenceFrame))
    {
        setState(TrackingState::unvoiced);
        state.desiredCents = 0.0;
        return;
    }
"""
require_once(s, old, "pre-body unvoiced zero branch")
new = """    if (!exactAuthority
        && !state.noteBodyLatched && richEvidence
        && (confirmedBreathFrame || confirmedAbsenceFrame))
    {
        setState(TrackingState::unvoiced);
        state.desiredCents = 0.0;
        return;
    }
"""
s = s.replace(old, new, 1)

start = s.index("        // SOUND_EQUALS_CORRECTION_V2_DENSE_SAFE: live pitch outside a clear")
end_marker = "\n        if (liveIdentityBreak)\n"
end = s.index(end_marker, start)
new = """        // SCALE_OWNS_VOICE_V2: raw dry pitch measures error; it does not
        // own note identity. Ordinary degree changes require the continuity
        // centre itself to cross the existing half-cell identity boundary in
        // the same direction. This preserves dense/microtonal scale ownership
        // without letting instantaneous vibrato choose a neighbouring degree.
        const double centreDistanceFromCurrentTarget = state.targetValid
            ? std::abs(state.pitchCentreLog2 - state.targetLog2) * 1200.0
            : 0.0;
        const double observedDirection = observedLog2 - state.targetLog2;
        const double centreDirection = state.pitchCentreLog2 - state.targetLog2;
        const double obviousRegisterBreakCents = std::max(700.0, 3.5 * scaleStep);
        const bool obviousRegisterBreak = observedDistanceFromCurrentTarget
            >= obviousRegisterBreakCents;
        const bool sustainedCellExit = centreDistanceFromCurrentTarget
            >= currentIdentityRadius
            && observedDistanceFromCurrentTarget >= currentIdentityRadius
            && observedDirection * centreDirection > 0.0;
        liveIdentityBreak = observation.audioPresent
            && state.targetValid
            && (obviousRegisterBreak || sustainedCellExit);"""
s = s[:start] + new + s[end:]

# Keep the existing quantizer and visible Hold control active, but feed musical
# identity from the continuity centre rather than the instantaneous dry F0.
start = s.index("    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);")
end_marker = "\n\n    const bool targetChanged = !state.targetValid"
end = s.index(end_marker, start)
new = """    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);
    int pending = 0;
    const double targetSelectionLog2 = state.pitchCentreLog2;
    const float targetStrictness = zeroPrudence
        ? 0.0f : parameters.lockStrictness;
    const float targetConfidence = zeroPrudence
        ? 1.0f : observation.confidence;
    double newTarget = quantizer.chooseTargetLog2(
        targetSelectionLog2,
        hysteresis,
        targetStrictness,
        targetConfidence,
        parameters.scaleLock && parameters.hardLockActive,
        musicalOnset || liveIdentityBreak,
        pending);
    newTarget += std::round(state.pitchCentreLog2 - newTarget);"""
s = s[:start] + new + s[end:]
engine_path.write_text(s)

test_path = Path("Tests/SupervisorContinuityTest.cpp")
t = test_path.read_text()
for variable in ("heldCorrectionState", "dropoutState", "acquireState"):
    pattern = rf"({variable}\.trackingState\s*==\s*ModernPitchEngine::TrackingState::)acquire"
    t = regex_once(t, pattern, r"\1stable", f"{variable} acquire assertion")

# The explicit dropout is not phonetic evidence, so an acquired target must be
# reported stable rather than search/acquire.
pattern = r"(explicitAuthorityState\.trackingState\s*==\s*ModernPitchEngine::TrackingState::)acquire"
t = regex_once(t, pattern, r"\1stable", "explicitAuthorityState acquire assertion")

renames = [
    ('"acquire_search_never_mutes_existing_correction"',
     '"present_f0_search_remains_stable_on_existing_target"'),
    ('"stale_pitch_reacquires_without_reducing_correction"',
     '"body_evidence_keeps_stale_pitch_musically_stable"'),
    ('"stale_f0_cannot_masquerade_as_stable_note"',
     '"body_signal_cannot_be_stuck_in_acquire"'),
    ("34.0 * std::sin", "70.0 * std::sin"),
]
for old, new in renames:
    require_once(t, old, f"test replacement {old}")
    t = t.replace(old, new, 1)

# Add explicit rigid-authority regressions before the native semitone-cap test.
anchor = "    // Native API semantics: one semitone means 100 cents, with no adapter hack.\n"
require_once(t, anchor, "scale-owned unvoiced test insertion")
extra = r'''    // SCALE_OWNS_VOICE_V2: once a scale destination exists, an explicitly
    // aperiodic/breathy frame may change the detector label but may never undo
    // or attenuate the scale transport.
    ModernPitchEngine::Parameters rigidBreathParameters = explicitAuthorityParameters;
    setBreathEvidence(rigidBreathParameters);
    ModernPitchEngine::CorrectionState rigidBreathState = explicitAuthorityState;
    const double rigidBreathTarget = rigidBreathState.targetLog2;
    const double rigidBreathCents = rigidBreathState.desiredCents;
    ModernPitchEngine::PitchObservation rigidAperiodic;
    rigidAperiodic.valid = false;
    rigidAperiodic.audioPresent = false;
    engine->updateCorrectionState(rigidBreathState,
                                  explicitAuthorityQuantizer,
                                  rigidAperiodic,
                                  rigidBreathParameters);
    success &= check(rigidBreathState.targetValid
                     && std::abs(rigidBreathState.targetLog2 - rigidBreathTarget) < 1.0e-12
                     && std::abs(rigidBreathState.desiredCents - rigidBreathCents) < 1.0e-9
                     && rigidBreathState.trackingState != ModernPitchEngine::TrackingState::release,
                     "rigid_aperiodic_material_never_leaves_scale_target");

    // Even before the body latch, a formally valid pitch coordinate at maximum
    // authority is quantized into the selected scale instead of being discarded
    // as breath and replaced by zero correction.
    ModernPitchEngine::CorrectionState rigidPreBodyBreath;
    auto rigidPreBodyObservation = strongPitch(452.0f);
    rigidPreBodyObservation.audioPresent = false;
    rigidPreBodyObservation.correctionFrequencyHz = 452.0f;
    engine->updateCorrectionState(rigidPreBodyBreath,
                                  explicitAuthorityQuantizer,
                                  rigidPreBodyObservation,
                                  rigidBreathParameters);
    success &= check(rigidPreBodyBreath.targetValid
                     && std::abs(std::exp2(rigidPreBodyBreath.targetLog2) - 440.0) < 0.1
                     && std::abs(rigidPreBodyBreath.desiredCents) > 5.0,
                     "exact_authority_valid_aperiodic_input_enters_scale");

'''
t = t.replace(anchor, extra + anchor, 1)
test_path.write_text(t)
