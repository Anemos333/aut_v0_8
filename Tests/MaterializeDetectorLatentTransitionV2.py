from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


def all_exact(text, old, new, count, label):
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{label}: expected {count} matches, got {actual}")
    return text.replace(old, new)

cpp_p = Path('Source/ModernPitchEngine.cpp')
h_p = Path('Source/ModernPitchEngine.h')
test_p = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_p.read_text()
h = h_p.read_text()
test = test_p.read_text()

# ---------------------------------------------------------------------------
# 1) Scale-domain nomination is a read-only operation.  Detector evidence may
# nominate a degree without mutating user Hold state in the quantizer.
h = one(h,
'''        [[nodiscard]] float minimumStepCents() const noexcept { return minStepCents_; }\n        [[nodiscard]] float asymmetry() const noexcept { return asymmetry_; }\n        [[nodiscard]] double adjacentTargetLog2(double currentTargetLog2,\n''',
'''        [[nodiscard]] float minimumStepCents() const noexcept { return minStepCents_; }\n        [[nodiscard]] float asymmetry() const noexcept { return asymmetry_; }\n        [[nodiscard]] double nearestTargetLog2(double inputLog2) const noexcept;\n        [[nodiscard]] double adjacentTargetLog2(double currentTargetLog2,\n''',
'nearest target declaration')

h = one(h,
'''        double transportChallengerLog2 = 0.0;\n        int transportChallengerHops = 0;\n\n        // SCALE_OWNS_IDENTITY_V2: detector observations may nominate a new\n''',
'''        double transportChallengerLog2 = 0.0;\n        int transportChallengerHops = 0;\n\n        // LATENT_SCALE_CANDIDATE_V2: a detector coordinate that points outside\n        // the currently owned scale cell is analysis only until the same exact\n        // destination degree persists.  While pending it has zero audible\n        // authority over target, transport and correction.\n        bool latentTargetValid = false;\n        double latentTargetLog2 = 0.0;\n        int latentTargetHops = 0;\n\n        // SCALE_OWNS_IDENTITY_V2: detector observations may nominate a new\n''',
'latent target state')

insert_before_choose = '''double ModernPitchEngine::ScaleQuantizer::chooseTargetLog2(\n'''
nearest_impl = r'''double ModernPitchEngine::ScaleQuantizer::nearestTargetLog2(
    double inputLog2) const noexcept
{
    if (!std::isfinite(inputLog2) || ratioCount_ <= 0)
        return inputLog2;

    const double relative = inputLog2 - rootLog2_;
    const double octave = std::floor(relative);
    double nearest = inputLog2;
    double nearestDistance = std::numeric_limits<double>::infinity();

    for (int i = 0; i < ratioCount_; ++i)
    {
        const double degree = logRatios_[static_cast<std::size_t>(i)];
        for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset)
        {
            const double candidate = rootLog2_ + octave
                + static_cast<double>(octaveOffset) + degree;
            const double distance = std::abs(candidate - inputLog2);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = candidate;
            }
        }
    }
    return nearest;
}

'''
cpp = one(cpp, insert_before_choose, nearest_impl + insert_before_choose,
          'nearest target implementation')

# ---------------------------------------------------------------------------
# 2) The beam must actually influence which current hypothesis wins.  Evidence
# ranking alone is too happy to jump to a strong harmonic during a consonant.
cpp = one(cpp,
'''    int matchedHypothesis = -1;\n    for (int index = 0; index < hypothesisCount; ++index)\n    {\n        const auto& current = hypotheses[static_cast<std::size_t>(index)];\n        if (current.valid && current.freshSupportMask != 0\n            && current.directSupportCount >= 1)\n        {\n            matchedHypothesis = index;\n            break; // hypotheses are already ordered by evidence score\n        }\n    }\n    if (matchedHypothesis < 0)\n        return makeFreshRawDecision();\n''',
'''    // BEAM_RANKS_CURRENT_MEASUREMENT_V2: current evidence is still required,\n    // but temporal continuity now ranks competing current hypotheses instead of\n    // being computed and then ignored. This is especially important when a\n    // harmonic one octave away briefly has the strongest instantaneous score.\n    int matchedHypothesis = -1;\n    float bestCurrentScore = -1000.0f;\n    const bool beamValid = decoderBeam_[0].valid;\n    const float beamFrequencyHz = beamValid\n        ? static_cast<float>(std::exp2(decoderBeam_[0].logFrequency)) : 0.0f;\n    for (int index = 0; index < hypothesisCount; ++index)\n    {\n        const auto& current = hypotheses[static_cast<std::size_t>(index)];\n        if (!current.valid || current.freshSupportMask == 0\n            || current.directSupportCount < 1)\n        {\n            continue;\n        }\n\n        float score = current.evidenceScore + 0.20f * current.consensus;\n        if (beamValid && beamFrequencyHz > 0.0f)\n        {\n            const float distance = centsDistance(beamFrequencyHz,\n                                                 current.frequencyHz);\n            score += 0.46f * std::exp(-distance / 95.0f);\n\n            int octaveDelta = 0;\n            float octaveResidual = 0.0f;\n            if (isOctaveLikeTransition(beamFrequencyHz, current.frequencyHz,\n                                       octaveDelta, octaveResidual))\n            {\n                const float singleFamilyPenalty =\n                    current.directSupportCount >= 2 ? 0.20f : 0.46f;\n                score -= singleFamilyPenalty;\n            }\n        }\n\n        if (score > bestCurrentScore)\n        {\n            bestCurrentScore = score;\n            matchedHypothesis = index;\n        }\n    }\n    if (matchedHypothesis < 0)\n        return makeFreshRawDecision();\n''',
'beam ranks current hypothesis')

# ---------------------------------------------------------------------------
# 3) Octave ambiguity gets its own stricter temporal falsification window.  An
# onset is not permission to skip it because consonants can create onsets too.
cpp = all_exact(cpp,
'''        const bool samePending = pendingOctaveDelta_ == rescueOctaveDelta\n            && pendingOctaveFrequencyHz_ > 0.0f\n            && centsDistance(pendingOctaveFrequencyHz_,\n                             decision.candidate.frequencyHz) < 70.0f;\n''',
'''        const bool samePending = pendingOctaveDelta_ == rescueOctaveDelta\n            && pendingOctaveFrequencyHz_ > 0.0f\n            && centsDistance(pendingOctaveFrequencyHz_,\n                             decision.candidate.frequencyHz) < 42.0f;\n''', 1, 'rescue octave candidate stability')

cpp = one(cpp,
'''        const bool multiPathDirect = decision.directSupportCount >= 2;\n        const int requiredObservations = onsetPending\n            ? (rescueOctaveDelta < 0 ? 3 : 2)\n            : multiPathDirect\n                ? (rescueOctaveDelta < 0 ? 7 : 6)\n                : (rescueOctaveDelta < 0 ? 10 : 8);\n''',
'''        // OCTAVE_AMBIGUITY_V3: a sung octave is allowed, but a consonant or\n        // strong harmonic may remain octave-like for several milliseconds.\n        // Onset cannot shortcut this guard. Independent direct paths shorten\n        // the fixed window; confidence never lengthens or shortens it.\n        const bool multiPathDirect = decision.directSupportCount >= 2\n            && decision.consensus >= 0.24f;\n        const int requiredObservations = multiPathDirect\n            ? (rescueOctaveDelta < 0 ? 12 : 10)\n            : (rescueOctaveDelta < 0 ? 28 : 24);\n''',
'rescue octave v3 persistence')

cpp = one(cpp,
'''    const bool samePending = pendingOctaveDelta_ == octaveDelta\n        && pendingOctaveFrequencyHz_ > 0.0f\n        && centsDistance(pendingOctaveFrequencyHz_,\n                         decision.candidate.frequencyHz) < 70.0f;\n''',
'''    const bool samePending = pendingOctaveDelta_ == octaveDelta\n        && pendingOctaveFrequencyHz_ > 0.0f\n        && centsDistance(pendingOctaveFrequencyHz_,\n                         decision.candidate.frequencyHz) < 42.0f;\n''',
'normal octave candidate stability')

cpp = one(cpp,
'''    const bool multiPathDirect = decision.directSupportCount >= 2;\n    const int requiredObservations = onsetPending\n        ? (octaveDelta < 0 ? 3 : 2)\n        : multiPathDirect\n            ? (octaveDelta < 0 ? 7 : 6)\n            : (octaveDelta < 0 ? 10 : 8);\n''',
'''    const bool multiPathDirect = decision.directSupportCount >= 2\n        && decision.consensus >= 0.24f;\n    const int requiredObservations = multiPathDirect\n        ? (octaveDelta < 0 ? 12 : 10)\n        : (octaveDelta < 0 ? 28 : 24);\n''',
'normal octave v3 persistence')

# ---------------------------------------------------------------------------
# 4) Scale-domain latent candidate. A grey-zone F0 inside the owned cell can
# still track local source motion (needed to remove vibrato at zero), but any
# measurement that nominates a different degree has zero audible authority until
# the same destination persists. Single-family provisional octaves never commit.
anchor = '''    const double observedLog2 = safeLog2(correctionFrequencyHz);\n    const double correctionObservedLog2 = observedLog2;\n\n    if (rescueBodyFrame && !observation.onset)\n'''
latent = r'''    const double observedLog2 = safeLog2(correctionFrequencyHz);
    const double correctionObservedLog2 = observedLog2;

    bool detectorScaleCommit = false;
    if (state.targetValid)
    {
        // LATENT_SCALE_CANDIDATE_V2: detector uncertainty is represented in a
        // separate analysis state.  Until a new exact scale degree is proven,
        // the audible state remains bit-for-bit the already owned note.
        const double nominatedTarget = quantizer.nearestTargetLog2(observedLog2);
        const double targetDeltaCents =
            (nominatedTarget - state.targetLog2) * 1200.0;
        const bool nominatesOwnedTarget = std::abs(targetDeltaCents) < 0.5;

        if (nominatesOwnedTarget)
        {
            state.latentTargetValid = false;
            state.latentTargetLog2 = 0.0;
            state.latentTargetHops = 0;
        }
        else
        {
            const bool sameLatent = state.latentTargetValid
                && std::abs(nominatedTarget - state.latentTargetLog2) * 1200.0 < 0.5;
            if (sameLatent)
            {
                state.latentTargetHops = std::min(64, state.latentTargetHops + 1);
            }
            else
            {
                state.latentTargetValid = true;
                state.latentTargetLog2 = nominatedTarget;
                state.latentTargetHops = 1;
            }

            const double absoluteJump = std::abs(targetDeltaCents);
            const int nearestOctave = static_cast<int>(std::lround(
                absoluteJump / 1200.0));
            const bool octaveLikeTarget = nearestOctave >= 1
                && nearestOctave <= 2
                && std::abs(absoluteJump
                    - 1200.0 * static_cast<double>(nearestOctave)) <= 35.0;

            int requiredHops = 0;
            if (octaveLikeTarget)
            {
                if (!trustedPitch && observation.detectorSupport < 2)
                    requiredHops = 1000000; // never from one provisional family
                else if (trustedPitch && observation.detectorSupport >= 2)
                    requiredHops = 8;
                else if (trustedPitch)
                    requiredHops = 14;
                else
                    requiredHops = 18;
            }
            else
            {
                requiredHops = trustedPitch
                    ? (observation.detectorSupport >= 2 ? 2 : 3)
                    : 6;
            }

            if (state.latentTargetHops < requiredHops)
            {
                // Stable C, uncertain material => still C.  No target revision,
                // no transport rewrite, no moving desiredCents, no dry escape.
                return;
            }

            detectorScaleCommit = true;
            state.latentTargetValid = false;
            state.latentTargetLog2 = 0.0;
            state.latentTargetHops = 0;
        }
    }

    if (rescueBodyFrame && !observation.onset)
'''
cpp = one(cpp, anchor, latent, 'latent scale candidate gate')

cpp = one(cpp,
'''    bool liveIdentityBreak = false;\n    bool forceTargetSwitch = false;\n    if (!state.pitchCentreValid || musicalOnset)\n    {\n''',
'''    bool liveIdentityBreak = false;\n    bool forceTargetSwitch = false;\n    if (detectorScaleCommit)\n    {\n        // The destination degree has already passed the detector-domain\n        // persistence test. Rebase the analysis centre and present exactly one\n        // challenger to the user-owned quantizer/Hold logic.\n        state.pitchCentreLog2 = observedLog2;\n        state.pitchCentreValid = true;\n        state.stableObservations = 0;\n        liveIdentityBreak = true;\n        forceTargetSwitch = true;\n    }\n    else if (!state.pitchCentreValid || musicalOnset)\n    {\n''',
'detector scale commit drives one quantizer challenger')

# A committed target change always restarts transition cleanly, even if another
# very fast note arrives while the previous response trajectory is still active.
cpp = one(cpp,
'''        if (targetIdentityChanged && state.trackingState != TrackingState::transition)\n        {\n            setState(TrackingState::transition);\n            state.velocityCentsPerSecond = 0.0;\n            state.stableObservations = 0;\n            state.stableBodyObservations = bodyPresent ? 1 : 0;\n        }\n''',
'''        if (targetIdentityChanged)\n        {\n            // TRANSITION_DESTINATION_FROZEN_V2: every committed note boundary\n            // starts one clean monotonic trajectory. A later detector candidate\n            // may be analysed, but cannot continuously rewrite this destination.\n            state.trackingState = TrackingState::transition;\n            state.stateAgeSamples = 0;\n            state.velocityCentsPerSecond = 0.0;\n            state.stableObservations = 0;\n            state.stableBodyObservations = bodyPresent ? 1 : 0;\n        }\n''',
'transition restart on committed target')

cpp = one(cpp,
'''    if (state.noteBodyLatched && bodyPresent)\n    {\n        const double observedSourceLog2 = correctionObservedLog2;\n''',
'''    const bool freezeCommittedTransition =\n        state.trackingState == TrackingState::transition && !targetIdentityChanged;\n    if (state.noteBodyLatched && bodyPresent && !freezeCommittedTransition)\n    {\n        const double observedSourceLog2 = correctionObservedLog2;\n''',
'freeze source coordinate during transition')

# At Response=0 the target is already reached; keeping an internal transition
# alive for 12 ms only creates a needless window in which later detector state can
# interact with it. End it immediately. The same applies once a non-zero response
# trajectory has actually reached its fixed destination.
cpp = one(cpp,
'''    if (state.responseMs <= 0.00001)\n    {\n        state.currentCents = state.desiredCents;\n        state.velocityCentsPerSecond = 0.0;\n        return state.currentCents;\n    }\n''',
'''    if (state.responseMs <= 0.00001)\n    {\n        state.currentCents = state.desiredCents;\n        state.velocityCentsPerSecond = 0.0;\n        if (state.trackingState == TrackingState::transition)\n        {\n            state.trackingState = TrackingState::stable;\n            state.stateAgeSamples = 0;\n        }\n        return state.currentCents;\n    }\n''',
'zero response completes transition')

cpp = one(cpp,
'''        if (std::abs(state.desiredCents - state.currentCents) < 0.001)\n        {\n            state.currentCents = state.desiredCents;\n            state.velocityCentsPerSecond = 0.0;\n        }\n        return state.currentCents;\n''',
'''        if (std::abs(state.desiredCents - state.currentCents) < 0.001)\n        {\n            state.currentCents = state.desiredCents;\n            state.velocityCentsPerSecond = 0.0;\n            state.trackingState = TrackingState::stable;\n            state.stateAgeSamples = 0;\n        }\n        return state.currentCents;\n''',
'transition completes when destination reached')

# ---------------------------------------------------------------------------
# 5) Update old octave timing expectation to the stronger single-family guard.
test = one(test,
'''    bool prematureOctaveCommit = false;\n    for (int hop = 0; hop < 7; ++hop)\n    {\n        auto d = makeOctaveChallenger();\n        prematureOctaveCommit = octavePersistenceTracker->confirmOctaveTransition(d, false)\n            || prematureOctaveCommit;\n    }\n    auto finalOctave = makeOctaveChallenger();\n    const bool finiteOctaveCommit = octavePersistenceTracker->confirmOctaveTransition(\n        finalOctave, false);\n    success &= check(!prematureOctaveCommit && finiteOctaveCommit,\n                     "octave_ambiguity_has_short_finite_persistence_not_confidence_gate");\n''',
'''    bool prematureOctaveCommit = false;\n    for (int hop = 0; hop < 23; ++hop)\n    {\n        auto d = makeOctaveChallenger();\n        prematureOctaveCommit = octavePersistenceTracker->confirmOctaveTransition(d, false)\n            || prematureOctaveCommit;\n    }\n    auto finalOctave = makeOctaveChallenger();\n    const bool finiteOctaveCommit = octavePersistenceTracker->confirmOctaveTransition(\n        finalOctave, false);\n    success &= check(!prematureOctaveCommit && finiteOctaveCommit,\n                     "single_family_octave_requires_strict_finite_persistence");\n''',
'update octave persistence test')

# New listening-contract regressions.
end_anchor = '''    return success ? 0 : 1;\n}'''
new_tests = r'''
    // LATENT_SCALE_CANDIDATE_V2: a grey-zone measurement may help identify the
    // next note, but until it persists the audible note is exactly the previous
    // owned degree. This is the C -> uncertain C -> stable D contract.
    ModernPitchEngine::ScaleQuantizer latentQuantizer;
    latentQuantizer.reset();
    latentQuantizer.setScale(authorityChromatic.data(),
                             static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState latentState;
    auto latentC = strongPitch(440.0f);
    latentC.audioPresent = true;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(latentState, latentQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double latentCTarget = latentState.targetLog2;
    const double latentCTransport = latentState.transportPeriodHz;
    const double latentCDesired = latentState.desiredCents;

    ModernPitchEngine::PitchObservation greyD;
    greyD.audioPresent = true;
    greyD.valid = false;
    greyD.measurementAvailable = true;
    greyD.correctionFrequencyHz = 493.8833f;
    greyD.confidence = 0.06f;
    greyD.periodicity = 0.20f;
    greyD.detectorSupport = 1;
    for (int hop = 0; hop < 5; ++hop)
    {
        engine->updateCorrectionState(latentState, latentQuantizer,
                                      greyD, explicitAuthorityParameters);
    }
    success &= check(std::abs(latentState.targetLog2 - latentCTarget) < 1.0e-12
                     && std::abs(latentState.transportPeriodHz - latentCTransport) < 1.0e-12
                     && std::abs(latentState.desiredCents - latentCDesired) < 1.0e-12,
                     "uncertain_new_degree_has_zero_audible_authority");

    engine->updateCorrectionState(latentState, latentQuantizer,
                                  greyD, explicitAuthorityParameters);
    const double latentDHz = std::exp2(latentState.targetLog2);
    success &= check(latentDHz > 492.0 && latentDHz < 496.0,
                     "persistent_grey_voice_can_commit_new_scale_degree");

    // A single provisional octave family can persist indefinitely without
    // stealing the register. It must become trusted/corroborated first.
    ModernPitchEngine::ScaleQuantizer provisionalOctaveQuantizer;
    provisionalOctaveQuantizer.reset();
    provisionalOctaveQuantizer.setScale(authorityChromatic.data(),
                                         static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState provisionalOctaveState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(provisionalOctaveState,
                                      provisionalOctaveQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double provisionalOctaveTarget = provisionalOctaveState.targetLog2;
    const double provisionalOctaveTransport = provisionalOctaveState.transportPeriodHz;
    const double provisionalOctaveDesired = provisionalOctaveState.desiredCents;
    ModernPitchEngine::PitchObservation greyOctave = greyD;
    greyOctave.correctionFrequencyHz = 880.0f;
    greyOctave.detectorSupport = 1;
    for (int hop = 0; hop < 80; ++hop)
        engine->updateCorrectionState(provisionalOctaveState,
                                      provisionalOctaveQuantizer,
                                      greyOctave, explicitAuthorityParameters);
    success &= check(std::abs(provisionalOctaveState.targetLog2
                              - provisionalOctaveTarget) < 1.0e-12
                     && std::abs(provisionalOctaveState.transportPeriodHz
                                 - provisionalOctaveTransport) < 1.0e-12
                     && std::abs(provisionalOctaveState.desiredCents
                                 - provisionalOctaveDesired) < 1.0e-12,
                     "single_family_provisional_octave_never_owns_register");

    // Grey-zone motion inside the already owned scale cell is allowed to refine
    // only the local source coordinate. This removes vibrato residual without
    // giving uncertainty permission to change note identity.
    ModernPitchEngine::ScaleQuantizer greyLocalQuantizer;
    greyLocalQuantizer.reset();
    greyLocalQuantizer.setScale(authorityChromatic.data(),
                                static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState greyLocalState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(greyLocalState, greyLocalQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double greyLocalTarget = greyLocalState.targetLog2;
    const double greyLocalBefore = greyLocalState.transportPeriodHz;
    ModernPitchEngine::PitchObservation greySameCell;
    greySameCell.audioPresent = true;
    greySameCell.valid = false;
    greySameCell.measurementAvailable = true;
    greySameCell.correctionFrequencyHz = static_cast<float>(
        440.0 * std::exp2(18.0 / 1200.0));
    greySameCell.confidence = 0.05f;
    greySameCell.periodicity = 0.18f;
    greySameCell.detectorSupport = 1;
    for (int hop = 0; hop < 8; ++hop)
        engine->updateCorrectionState(greyLocalState, greyLocalQuantizer,
                                      greySameCell, explicitAuthorityParameters);
    success &= check(std::abs(greyLocalState.targetLog2 - greyLocalTarget) < 1.0e-12
                     && std::abs(greyLocalState.transportPeriodHz - greyLocalBefore) > 0.1,
                     "grey_same_cell_measurement_refines_transport_not_identity");

    // TRANSITION_DESTINATION_FROZEN_V2: once D is committed, detector wobble on
    // the way there cannot rewrite source coordinate or desired correction.
    ModernPitchEngine::ScaleQuantizer frozenTransitionQuantizer;
    frozenTransitionQuantizer.reset();
    frozenTransitionQuantizer.setScale(authorityChromatic.data(),
                                        static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState frozenTransitionState;
    auto frozenD = strongPitch(493.8833f);
    frozenD.audioPresent = true;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(frozenTransitionState,
                                      frozenTransitionQuantizer,
                                      frozenD, explicitAuthorityParameters);
    frozenTransitionState.trackingState = ModernPitchEngine::TrackingState::transition;
    frozenTransitionState.responseMs = 8.0;
    frozenTransitionState.currentCents = frozenTransitionState.desiredCents - 40.0;
    const double frozenTarget = frozenTransitionState.targetLog2;
    const double frozenTransport = frozenTransitionState.transportPeriodHz;
    const double frozenDesired = frozenTransitionState.desiredCents;
    auto frozenWobble = frozenD;
    frozenWobble.frequencyHz = 500.0f;
    frozenWobble.correctionFrequencyHz = 500.0f;
    engine->updateCorrectionState(frozenTransitionState,
                                  frozenTransitionQuantizer,
                                  frozenWobble, explicitAuthorityParameters);
    success &= check(std::abs(frozenTransitionState.targetLog2 - frozenTarget) < 1.0e-12
                     && std::abs(frozenTransitionState.transportPeriodHz - frozenTransport) < 1.0e-12
                     && std::abs(frozenTransitionState.desiredCents - frozenDesired) < 1.0e-12,
                     "transition_detector_wobble_cannot_move_destination");

    ModernPitchEngine::CorrectionState zeroResponseTransition;
    zeroResponseTransition.targetValid = true;
    zeroResponseTransition.noteBodyLatched = true;
    zeroResponseTransition.trackingState = ModernPitchEngine::TrackingState::transition;
    zeroResponseTransition.currentCents = -90.0;
    zeroResponseTransition.desiredCents = 35.0;
    zeroResponseTransition.responseMs = 0.0;
    const double zeroResponseValue = engine->advanceCorrection(zeroResponseTransition);
    success &= check(std::abs(zeroResponseValue - 35.0) < 1.0e-12
                     && zeroResponseTransition.trackingState
                        == ModernPitchEngine::TrackingState::stable,
                     "zero_response_has_no_hidden_transition_window");

'''
test = one(test, end_anchor, new_tests + end_anchor, 'append latent transition tests')

cpp_p.write_text(cpp)
h_p.write_text(h)
test_p.write_text(test)
print('detector latent transition v2 materialized')
