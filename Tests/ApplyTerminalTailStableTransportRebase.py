from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
source = path.read_text()

if "TERMINAL_TAIL_STABLE_TRANSPORT_REBASE_V1" in source:
    raise SystemExit("tail rebase already present")

for marker in [
    "TAU_EVIDENCE_MEMOIZATION_V1",
    "RESIDUAL_LINE_EXACT_MEMOIZATION_V1",
    "TERMINAL_TAIL_KEEPS_OWNED_DEGREE_V1",
    "SOURCE_COORDINATE_REBASE_V1",
]:
    if marker not in source:
        raise SystemExit(f"missing required baseline marker {marker}")

for rejected in [
    "LAG_CORRELATION_MEMOIZATION_V1",
    "RESIDUAL_LINE_INVARIANT_HOIST_V1",
    "RESIDUAL_LINE_COHERENCE_CPU_V1",
]:
    if rejected in source:
        raise SystemExit(f"rejected marker present: {rejected}")

old_tail = r'''    bool terminalTailIdentityVeto = false;
    if (state.targetValid
        && state.noteBodyLatched
        && richEvidence
        && !observation.onset)
    {
'''
new_tail = r'''    bool terminalTailIdentityVeto = false;
    bool terminalTailStableCompensation = false;
    if (state.targetValid
        && state.noteBodyLatched
        && richEvidence
        && !observation.onset)
    {
'''
if source.count(old_tail) != 1:
    raise SystemExit(f"tail declaration anchor count {source.count(old_tail)}")
source = source.replace(old_tail, new_tail, 1)

old_structure = r'''        const bool terminalStructure = parameters.voiceEventStrength < 0.55f
            && degradationVotes >= 2;
        const bool outsideStableCore =
            std::abs(signedTailDistanceCents) >= 0.32 * localTailStep;

        if (terminalStructure && sameTailSide && outsideStableCore)
'''
new_structure = r'''        const bool terminalStructure = parameters.voiceEventStrength < 0.55f
            && degradationVotes >= 2;

        // TERMINAL_TAIL_STABLE_TRANSPORT_REBASE_V1
        // A weakening same-note tail may keep reporting valid F0 while its
        // physical source coordinate falls or rises quickly. Stable still owns
        // the already selected scale degree, so Response must not turn that
        // transport motion into a temporary audible pitch escape. This flag
        // grants no target identity authority and does not freeze transport.
        terminalTailStableCompensation = terminalStructure
            && sameTailSide
            && state.trackingState == TrackingState::stable;

        const bool outsideStableCore =
            std::abs(signedTailDistanceCents) >= 0.32 * localTailStep;

        if (terminalStructure && sameTailSide && outsideStableCore)
'''
if source.count(old_structure) != 1:
    raise SystemExit(f"tail structure anchor count {source.count(old_structure)}")
source = source.replace(old_structure, new_structure, 1)

transport_anchor = r'''    state.targetLog2 = newTarget;
    state.targetValid = true;

    // LOCAL_TRAJECTORY_V1: the audible source coordinate is a persistent
'''
transport_insert = r'''    state.targetLog2 = newTarget;
    state.targetValid = true;

    // Preserve the pre-update transport coordinate only for the stable terminal
    // tail rebase below. It is not a detector fallback and never reaches the
    // renderer directly.
    double preTailTransportSourceLog2 = correctionObservedLog2;
    if (terminalTailStableCompensation
        && state.transportPeriodHz > 0.0
        && std::isfinite(state.transportPeriodHz))
    {
        preTailTransportSourceLog2 = safeLog2(state.transportPeriodHz);
    }

    // LOCAL_TRAJECTORY_V1: the audible source coordinate is a persistent
'''
if source.count(transport_anchor) != 1:
    raise SystemExit(f"transport anchor count {source.count(transport_anchor)}")
source = source.replace(transport_anchor, transport_insert, 1)

old_end = r'''    double errorCents = (correctedLog2 - audibleSourceLog2) * 1200.0;
    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    errorCents = std::clamp(errorCents, -maximumCents, maximumCents);
    state.desiredCents = errorCents;
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);
'''
new_end = r'''    double errorCents = (correctedLog2 - audibleSourceLog2) * 1200.0;
    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    errorCents = std::clamp(errorCents, -maximumCents, maximumCents);

    if (terminalTailStableCompensation
        && state.trackingState == TrackingState::stable
        && !targetIdentityChanged)
    {
        // Re-evaluate only the same target-owned law at the previous transport
        // coordinate. The difference to the new desired value is therefore the
        // correction delta caused by source transport, not a target/Response
        // change. Shift current by exactly that delta so the pre-existing
        // desired-current Response error is preserved rather than snapped.
        const double priorSourceOffsetCents =
            (preTailTransportSourceLog2 - state.targetLog2) * 1200.0;
        const double priorTargetOwnedSourceResidual =
            residualBudgetCents > 1.0e-9
            ? residualBudgetCents
                * std::tanh(priorSourceOffsetCents / residualBudgetCents)
                * softness
            : 0.0;
        const double currentVibratoOffsetCents = std::clamp(
            requestedVibratoCents,
            -vibratoBudgetCents,
            vibratoBudgetCents);
        const double priorTargetOwnedOffsetCents = std::clamp(
            priorTargetOwnedSourceResidual + currentVibratoOffsetCents,
            -residualBudgetCents,
            residualBudgetCents);
        const double priorCorrectedLog2 = state.targetLog2
            + priorTargetOwnedOffsetCents / 1200.0;
        const double priorDesiredAtCurrentControls = std::clamp(
            (priorCorrectedLog2 - preTailTransportSourceLog2) * 1200.0,
            -maximumCents,
            maximumCents);
        const double transportCorrectionDelta =
            errorCents - priorDesiredAtCurrentControls;

        if (std::isfinite(transportCorrectionDelta))
        {
            state.currentCents = std::clamp(
                state.currentCents + transportCorrectionDelta,
                -maximumCents,
                maximumCents);
        }
    }

    state.desiredCents = errorCents;
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);
'''
if source.count(old_end) != 1:
    raise SystemExit(f"correction end anchor count {source.count(old_end)}")
source = source.replace(old_end, new_end, 1)

if "TERMINAL_TAIL_STABLE_TRANSPORT_REBASE_V1" not in source:
    raise SystemExit("new marker missing after patch")

path.write_text(source)
print("TERMINAL_TAIL_STABLE_TRANSPORT_REBASE_PATCH=APPLIED")
