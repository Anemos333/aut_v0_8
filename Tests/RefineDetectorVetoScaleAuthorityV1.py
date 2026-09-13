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

# A secondary voice/phonetic classifier is not stronger evidence than a real
# F0 that already survived the detector. It may classify invalid/pitchless
# material, but it must not veto an already valid measured coordinate.
cpp = one(cpp,
'''    state.invalidObservations = 0;

    // POSITIVE_VETO_ONLY_V1: explicit phonetic/transient evidence can reject a
    // measured F0. Low confidence, zero consensus or an unusual melodic jump
    // cannot. Preserve an existing scale target; before first ownership remain
    // in acquire until a non-falsified real F0 arrives.
    if (explicitPhoneticFrame)
    {
        state.stableBodyObservations = 0;
        if (!state.targetValid)
            setState(TrackingState::acquire);
        return;
    }

    // A strong breath/absence before any body latch must not become a note just
''',
'''    state.invalidObservations = 0;

    // VALID_F0_OUTRANKS_LABEL_V1: once the detector has produced a finite valid
    // F0, a secondary breath/phonetic label cannot erase that coordinate.
    // Falsification belongs in the detector/register logic; confidence and
    // descriptive voice labels never become permission to correct.

    // A strong breath/absence before any body latch must not become a note just
''',
'valid f0 outranks secondary label')

# Restore scale-cell selection through the continuity centre, but remove the
# confidence/periodicity multiplier that made this centre a permission gate.
# A fixed geometric rate deliberately stays close to the old high-confidence
# response while making low-confidence and zero-consensus motion identical.
cpp = one(cpp,
'''            const double stableGate = 0.35
                + 0.65 * static_cast<double>(clamp01(observation.confidence)
                                          * clamp01(observation.periodicity));
            state.pitchCentreLog2 += baseAlpha * stableGate
                * (observedLog2 - state.pitchCentreLog2);
''',
'''            // CONTINUITY_VETO_NOT_CONFIDENCE_V1: centre motion is purely
            // geometric. The fixed 0.90 factor is an anti-vibrato time scale,
            // not detector permission: confidence/consensus cannot slow it,
            // strengthen it or freeze a real sustained note change.
            constexpr double continuityRate = 0.90;
            state.pitchCentreLog2 += baseAlpha * continuityRate
                * (observedLog2 - state.pitchCentreLog2);
''',
'confidence independent bounded continuity centre')

cpp = one(cpp,
'''    // LIVE_F0_SELECTS_SCALE_CELL_V1: pitchCentre is continuity/vibrato state,
    // never permission to change note. Every accepted real F0 reaches the
    // existing ScaleQuantizer immediately; only its user-controlled Hold
    // hysteresis may retain the previous degree.
    const double targetSelectionLog2 = observedLog2;
''',
'''    // CONTINUITY_VETO_NOT_PERMISSION_V1: target identity is read from the
    // deterministic continuity centre, not from detector confidence. This is
    // a bounded anti-vibrato falsification stage; it cannot remain stuck merely
    // because confidence/consensus are low. User Hold remains the only target
    // retention control inside ScaleQuantizer.
    const double targetSelectionLog2 = state.pitchCentreLog2;
''',
'continuity centre selects scale cell')

# The weak-measurement regression must prove bounded acquisition, not demand a
# one-hop identity teleport. Its confidence remains deliberately near zero.
test = one(test,
'''    engine->updateCorrectionState(vetoState, vetoQuantizer,
                                  weakRealChange, absoluteLockParameters);
    const double vetoTargetHz = std::exp2(vetoState.targetLog2);
''',
'''    for (int hop = 0; hop < 8; ++hop)
        engine->updateCorrectionState(vetoState, vetoQuantizer,
                                      weakRealChange, absoluteLockParameters);
    const double vetoTargetHz = std::exp2(vetoState.targetLog2);
''',
'bounded weak real note change')

test = one(test,
'''                     "weak_real_note_change_reaches_exact_scale_degree_immediately");
''',
'''                     "weak_real_note_change_reaches_exact_scale_degree_in_bounded_time");
''',
'bounded note change test label')

cpp_p.write_text(cpp)
test_p.write_text(test)
print('detector veto scale authority v1 refinement materialized')
