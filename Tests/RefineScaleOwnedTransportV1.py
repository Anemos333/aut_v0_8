from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


cpp_p = Path('Source/ModernPitchEngine.cpp')
h_p = Path('Source/ModernPitchEngine.h')
test_p = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_p.read_text()
h = h_p.read_text()
test = test_p.read_text()

# Existing user Hold must remain the final retention authority once the
# supervisor has established that a challenger is musically persistent.
cpp = one(cpp,
'''        parameters.scaleLock && parameters.hardLockActive,
        musicalOnset || forceTargetSwitch,
        pending);''',
'''        parameters.scaleLock && parameters.hardLockActive,
        // USER_HOLD_REMAINS_AUTHORITY_V1: a supervisor-confirmed musical
        // challenger may select the candidate coordinate, but it cannot reuse
        // the quantizer onset bypass to defeat explicit user Hold. Only a real
        // musical onset gets onset semantics; otherwise existing hysteresis is
        // applied exactly as requested by the user.
        musicalOnset,
        pending);''',
'user Hold remains authority')

# A phonetic/consonant veto protects an already-owned musical transport. It may
# not prevent the very first finite F0 from establishing a scale-owned target,
# otherwise rough/aperiodic voices can fall back into permanent Acquire.
cpp = one(cpp,
'''    if (explicitPhoneticFrame)
    {
        state.stableBodyObservations = 0;
        if (!state.targetValid)
            setState(TrackingState::acquire);
        return;
    }
''',
'''    if (explicitPhoneticFrame && state.targetValid)
    {
        state.stableBodyObservations = 0;
        return;
    }
''',
'phonetic veto only protects existing ownership')

# Identity confirmation needs state that is independent from confidence,
# consensus, detector support and the user Hold control. The evidence is purely
# geometric: how far and how persistently the measured coordinate remains on
# one side of the currently owned scale degree.
h = one(h,
'''        double transportPeriodHz = 0.0;

        // CONSERVATIVE_F0_RESCUE_V1: short memory contains detector-derived
''',
'''        double transportPeriodHz = 0.0;

        // SCALE_OWNS_IDENTITY_V2: detector observations may nominate a new
        // cell, but only bounded same-side geometric persistence may present
        // that challenger to ScaleQuantizer. Confidence/consensus never enter
        // this accumulator; user Hold remains a separate later authority.
        int identityChallengerDirection = 0;
        double identityChallengerEvidence = 0.0;

        // CONSERVATIVE_F0_RESCUE_V1: short memory contains detector-derived
''',
'identity challenger state')

# Replace the previous centre-only commit boundary. Centre motion remains useful
# continuity information, but target identity is frozen until either the centre
# is deeply inside a new wide cell or same-side boundary evidence persists long
# enough. The evidence gain scales with cell density so 31/48/96-EDO does not
# inherit a 12-TET-sized dwell time.
cpp = one(cpp,
'''        // A new note is acknowledged only after the persistent musical centre,
        // not a raw detector hop, has left the currently owned cell. At that
        // point the scale quantizer may use the live observation solely to pick
        // the destination degree; the resulting destination is still on-scale.
        if (state.targetValid)
        {
            const double centreDistanceFromTarget =
                std::abs(state.pitchCentreLog2 - state.targetLog2) * 1200.0;
            const double confirmedExitRadius = 0.52 * scaleStep;
            if (centreDistanceFromTarget >= confirmedExitRadius)
            {
                liveIdentityBreak = true;
                forceTargetSwitch = true;
            }
        }
''',
'''        // SCALE_OWNS_IDENTITY_V2: crossing half a cell is only a nomination,
        // never an immediate target change. Wide vibrato can cross that line on
        // every cycle. Accumulate only same-side excess beyond the half-cell;
        // returning inside or changing direction cancels the nomination.
        if (state.targetValid)
        {
            const double signedObservedCents =
                (observedLog2 - state.targetLog2) * 1200.0;
            const double normalizedDistance = signedObservedCents / scaleStep;
            const int challengerDirection = normalizedDistance > 0.0 ? 1
                : normalizedDistance < 0.0 ? -1 : 0;
            const double boundaryExcess = std::max(
                0.0, std::abs(normalizedDistance) - 0.50);

            if (boundaryExcess <= 0.0 || challengerDirection == 0)
            {
                state.identityChallengerDirection = 0;
                state.identityChallengerEvidence = 0.0;
            }
            else
            {
                if (state.identityChallengerDirection != challengerDirection)
                {
                    state.identityChallengerDirection = challengerDirection;
                    state.identityChallengerEvidence = 0.0;
                }

                const double densityGain = std::clamp(100.0 / scaleStep, 1.0, 4.0);
                state.identityChallengerEvidence += std::min(
                    1.0, boundaryExcess * densityGain);
            }

            const double centreDistanceFromTarget =
                std::abs(state.pitchCentreLog2 - state.targetLog2) * 1200.0;
            const double deepExitRatio = scaleStep <= 50.0 ? 0.52 : 0.72;
            const bool deepCentreExit = centreDistanceFromTarget
                >= deepExitRatio * scaleStep;
            constexpr double persistentEvidenceRequired = 6.0;
            const bool persistentBoundaryExit =
                state.identityChallengerEvidence >= persistentEvidenceRequired;

            liveIdentityBreak = deepCentreExit || persistentBoundaryExit;
            forceTargetSwitch = liveIdentityBreak;
        }
''',
'geometric persistent identity challenger')

# The quantizer is not allowed to observe a half-cell crossing until the
# supervisor has confirmed a musical challenger. This prevents its nearest-cell
# rule from undoing the anti-vibrato stage. First acquisition and real onsets
# remain immediate; once a challenger is presented, explicit user Hold is still
# applied by chooseTargetLog2.
old_quantizer = '''    double newTarget = quantizer.chooseTargetLog2(
        targetSelectionLog2,
        hysteresis,
        targetStrictness,
        targetConfidence,
        parameters.scaleLock && parameters.hardLockActive,
        // USER_HOLD_REMAINS_AUTHORITY_V1: a supervisor-confirmed musical
        // challenger may select the candidate coordinate, but it cannot reuse
        // the quantizer onset bypass to defeat explicit user Hold. Only a real
        // musical onset gets onset semantics; otherwise existing hysteresis is
        // applied exactly as requested by the user.
        musicalOnset,
        pending);
    newTarget += std::round(state.pitchCentreLog2 - newTarget);
'''
new_quantizer = '''    double newTarget = state.targetLog2;
    if (!state.targetValid || musicalOnset || forceTargetSwitch)
    {
        newTarget = quantizer.chooseTargetLog2(
            targetSelectionLog2,
            hysteresis,
            targetStrictness,
            targetConfidence,
            parameters.scaleLock && parameters.hardLockActive,
            // USER_HOLD_REMAINS_AUTHORITY_V1: supervisor confirmation only
            // presents a challenger. It never receives onset semantics merely
            // to bypass the Hold explicitly selected by the user.
            musicalOnset,
            pending);
        newTarget += std::round(state.pitchCentreLog2 - newTarget);
    }
'''
cpp = one(cpp, old_quantizer, new_quantizer,
          'freeze scale identity before geometric confirmation')

cpp = one(cpp,
'''        if (targetIdentityChanged)
        {
            // Rebase continuity state after the quantizer has accepted a real
            // note change. This prevents the old note centre from masquerading
            // as vibrato/softness around the new exact scale destination.
            state.pitchCentreLog2 = observedLog2;
            state.pitchCentreValid = true;
            state.stableObservations = 0;
        }
''',
'''        if (targetIdentityChanged)
        {
            // Rebase continuity state after the quantizer has accepted a real
            // note change. This prevents the old note centre from masquerading
            // as vibrato/softness around the new exact scale destination.
            state.pitchCentreLog2 = observedLog2;
            state.pitchCentreValid = true;
            state.stableObservations = 0;
            state.identityChallengerDirection = 0;
            state.identityChallengerEvidence = 0.0;
        }
''',
'reset challenger after target commit')

# The old stale-register regression required one weak zero-consensus hop to jump
# an octave. That is exactly the raw-detector authority now forbidden. Replace
# it with the stronger contract: the first hop cannot alter audible ownership,
# while persistent zero-consensus evidence must still retarget in finite time.
old_stale = '''    engine->updateCorrectionState(staleRegisterState,
                                  staleRegisterQuantizer,
                                  zeroConsensusRegisterJump,
                                  staleRegisterParameters);
    const double staleRegisterTargetHz = std::exp2(staleRegisterState.targetLog2);
    success &= check(staleRegisterTargetHz > 430.0
                     && staleRegisterTargetHz < 450.0
                     && std::abs(staleRegisterState.desiredCents) > 5.0
                     && std::abs((staleRegisterState.pitchCentreLog2
                                  - std::log2(452.0)) * 1200.0) < 0.1,
                     "zero_consensus_stale_register_never_collapses_to_zero");
'''
new_stale = '''    const double staleInitialTarget = staleRegisterState.targetLog2;
    const double staleInitialTransport = staleRegisterState.transportPeriodHz;
    const double staleInitialCorrection = staleRegisterState.desiredCents;
    engine->updateCorrectionState(staleRegisterState,
                                  staleRegisterQuantizer,
                                  zeroConsensusRegisterJump,
                                  staleRegisterParameters);
    success &= check(std::abs(staleRegisterState.targetLog2 - staleInitialTarget) < 1.0e-12
                     && std::abs(staleRegisterState.transportPeriodHz - staleInitialTransport) < 1.0e-12
                     && std::abs(staleRegisterState.desiredCents - staleInitialCorrection) < 1.0e-12,
                     "single_zero_consensus_register_hop_has_zero_audible_authority");
    for (int hop = 0; hop < 120; ++hop)
        engine->updateCorrectionState(staleRegisterState,
                                      staleRegisterQuantizer,
                                      zeroConsensusRegisterJump,
                                      staleRegisterParameters);
    const double staleRegisterTargetHz = std::exp2(staleRegisterState.targetLog2);
    const double staleRegisterOutputHz = staleRegisterState.transportPeriodHz
        * std::exp2(staleRegisterState.desiredCents / 1200.0);
    success &= check(staleRegisterTargetHz > 430.0
                     && staleRegisterTargetHz < 450.0
                     && std::abs(staleRegisterState.desiredCents) > 5.0
                     && std::abs(1200.0 * std::log2(
                         staleRegisterOutputHz / staleRegisterTargetHz)) < 1.0e-6,
                     "persistent_zero_consensus_register_change_reaches_scale");
'''
test = one(test, old_stale, new_stale,
           'zero-consensus stale register becomes bounded persistence test')

# The weak-note test is about exact audible destination. Under scale-owned
# transport that destination must be evaluated from the supervisor transport,
# not from the raw detector coordinate which deliberately has no output authority.
test = one(test,
'''    const double vetoDestinationHz = 505.0 * std::exp2(vetoState.desiredCents / 1200.0);
''',
'''    const double vetoDestinationHz = vetoState.transportPeriodHz
        * std::exp2(vetoState.desiredCents / 1200.0);
''',
'weak-note destination uses audible transport')

cpp_p.write_text(cpp)
h_p.write_text(h)
test_p.write_text(test)
print('scale-owned transport v1 identity refinement materialized')
