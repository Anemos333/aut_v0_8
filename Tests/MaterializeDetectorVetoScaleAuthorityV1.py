from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


def between(text, start, end, replacement, label):
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"{label}: start marker not found")
    b = text.find(end, a + len(start))
    if b < 0:
        raise SystemExit(f"{label}: end marker not found")
    if text.find(start, a + 1) >= 0 and text.find(start, a + 1) < b:
        raise SystemExit(f"{label}: ambiguous start marker")
    return text[:a] + replacement + text[b:]


cpp_p = Path('Source/ModernPitchEngine.cpp')
test_p = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_p.read_text()
test = test_p.read_text()

# ---------------------------------------------------------------------------
# 1. A real fresh detector measurement is evidence, not a request for
# permission. Keep the existing YIN/aperiodicity rejection upstream: this
# patch never fabricates an F0 when no detector path measured one.
cpp = one(cpp,
'''    if (candidateCount <= 0)
        return {};

    // DETECTOR_IS_OBSERVER_V1: audio presence is not pitch evidence. A raw
    // detector family may be reported diagnostically, but only the existing
    // consensus/continuity machinery may publish a valid F0. Detector doubt
    // is absorbed downstream by the already-owned scale target/glide.
''',
'''    if (candidateCount <= 0)
        return {};

    // DETECTOR_VETO_NOT_PERMISSION_V1: a fresh finite detector result is a
    // measurement, even when confidence/consensus are poor. Confidence may
    // rank competing measurements, but it may not suppress the only real F0.
    // Upstream analyse() still rejects genuinely aperiodic/invalid material,
    // so this never invents a frequency when no detector path measured one.
    const auto makeFreshRawDecision = [&]() noexcept
    {
        DecoderDecision rawDecision;
        int bestIndex = -1;
        float bestScore = -1.0f;
        for (int index = 0; index < candidateCount; ++index)
        {
            const auto& candidate = candidates[static_cast<std::size_t>(index)];
            if (!candidate.valid || candidate.ageInHops != 0
                || !std::isfinite(candidate.frequencyHz)
                || candidate.frequencyHz < minimumPitchHz_
                || candidate.frequencyHz > maximumPitchHz_)
            {
                continue;
            }
            const float score = candidateBaseScore(candidate)
                * pathReliability(candidate.pathIndex, candidate.frequencyHz);
            if (score > bestScore)
            {
                bestScore = score;
                bestIndex = index;
            }
        }
        if (bestIndex < 0)
            return rawDecision;

        rawDecision.candidate = candidates[static_cast<std::size_t>(bestIndex)];
        rawDecision.candidate.valid = true;
        rawDecision.consensus = 0.0f;
        rawDecision.supportCount = 1;
        rawDecision.directSupportCount = 1;
        rawDecision.freshSupportMask = static_cast<std::uint8_t>(
            1u << rawDecision.candidate.pathIndex);
        rawDecision.decoderOctaveIndex = octaveState_;
        rawDecision.valid = true;
        return rawDecision;
    };

    // Consensus remains useful for ranking/falsification, never as permission
    // to expose a measured F0 to the musical supervisor.
''',
'decoder fresh raw measurement contract')

cpp = one(cpp,
'''    if (hypothesisCount <= 0)
        return {};
''',
'''    if (hypothesisCount <= 0)
        return makeFreshRawDecision();
''',
'consensus empty raw fallback')

cpp = one(cpp,
'''    updateDecoderBeam(hypotheses, hypothesisCount, onsetPending);
    if (!decoderBeam_[0].valid)
        return {};

''',
'''    updateDecoderBeam(hypotheses, hypothesisCount, onsetPending);
    // Decoder history is diagnostic/ranking evidence. It cannot erase the
    // current measured coordinate merely because the transition is unusual.

''',
'decoder history no permission gate')

cpp = between(cpp,
'''    const float decodedFrequency = static_cast<float>(
        std::exp2(decoderBeam_[0].logFrequency));
''',
'''    const auto& hypothesis = hypotheses[static_cast<std::size_t>(matchedHypothesis)];
''',
'''    int matchedHypothesis = -1;
    for (int index = 0; index < hypothesisCount; ++index)
    {
        const auto& current = hypotheses[static_cast<std::size_t>(index)];
        if (current.valid && current.freshSupportMask != 0
            && current.directSupportCount >= 1)
        {
            matchedHypothesis = index;
            break; // hypotheses are already ordered by evidence score
        }
    }
    if (matchedHypothesis < 0)
        return makeFreshRawDecision();

''',
'current evidence replaces beam permission')

cpp = between(cpp,
'''    const bool closeToTrack = trackedPitchHz_ > 0.0f
''',
'''    return decision;
}

bool ModernPitchEngine::MultiRatePitchTracker::confirmOctaveTransition(
''',
'''    // DETECTOR_VETO_NOT_PERMISSION_V1: once a current finite measurement has
    // survived the detector's falsification stages, low confidence/consensus
    // cannot make it invalid. Octave/subharmonic ambiguity is handled below by
    // confirmOctaveTransition() as an explicit, bounded veto.
    decision.valid = true;
''',
'remove confidence permission gate')

# ---------------------------------------------------------------------------
# 2. Octave ambiguity is a bounded negative veto, not an open-ended promotion
# contest. Ordinary non-octave note changes pass immediately; explicit onsets
# may veto one rescue observation as transient evidence.
cpp = between(cpp,
'''    if (trackedPitchHz_ <= 0.0f && rescueMode_ && reacquisitionAnchorHz_ > 0.0f)
    {
''',
'''    // Initial register acquisition is an evidence decision, independent of
''',
'''    if (trackedPitchHz_ <= 0.0f && rescueMode_ && reacquisitionAnchorHz_ > 0.0f)
    {
        int rescueOctaveDelta = 0;
        float rescueResidualCents = 0.0f;
        const bool octaveLike = isOctaveLikeTransition(
            reacquisitionAnchorHz_, decision.candidate.frequencyHz,
            rescueOctaveDelta, rescueResidualCents);

        if (!octaveLike)
        {
            // A non-octave live measurement is not guilty merely because it is
            // far from the stale anchor. An explicit transient onset may veto
            // this one observation; the next measured non-onset F0 passes.
            if (onsetPending)
            {
                decision.valid = false;
                return false;
            }
            pendingOctaveDelta_ = 0;
            pendingOctaveCount_ = 0;
            pendingOctaveFrequencyHz_ = 0.0f;
            committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
            octaveCommitGuardHops_ = 6;
            decision.decoderOctaveIndex = octaveState_;
            return true;
        }

        const bool samePending = pendingOctaveDelta_ == rescueOctaveDelta
            && pendingOctaveFrequencyHz_ > 0.0f
            && centsDistance(pendingOctaveFrequencyHz_,
                             decision.candidate.frequencyHz) < 70.0f;
        if (!samePending)
        {
            pendingOctaveDelta_ = rescueOctaveDelta;
            pendingOctaveCount_ = 0;
            pendingOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        }
        if (decision.freshSupportMask != 0)
            ++pendingOctaveCount_;

        // Downward octave/subharmonic aliases are more common, hence one extra
        // observation. This veto is finite and independent of confidence.
        const int requiredObservations = rescueOctaveDelta < 0 ? 3 : 2;
        if (pendingOctaveCount_ < requiredObservations)
        {
            decision.valid = false;
            return false;
        }

        octaveState_ = std::clamp(octaveState_ + rescueOctaveDelta, -4, 4);
        committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        octaveCommitGuardHops_ = 12;
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        decision.decoderOctaveIndex = octaveState_;
        return true;
    }

''',
'rescue octave bounded veto')

cpp = between(cpp,
'''    if (trackedPitchHz_ <= 0.0f)
    {
''',
'''    if (octaveCommitGuardHops_ > 0)
''',
'''    if (trackedPitchHz_ <= 0.0f)
    {
        // FIRST_MEASUREMENT_OWNS_V1: first ownership requires a current real
        // detector measurement, not a confidence vote. No fresh measurement
        // still means no F0 and therefore no invented target.
        if (decision.freshSupportMask == 0 || decision.directSupportCount < 1)
        {
            decision.valid = false;
            return false;
        }
        committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        octaveCommitGuardHops_ = 6;
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        decision.decoderOctaveIndex = octaveState_;
        return true;
    }

''',
'first measured f0 owns immediately')

cpp = between(cpp,
'''    int requiredObservations = octaveDelta < 0 ? 3 : 2;
''',
'''    octaveState_ = std::clamp(octaveState_ + octaveDelta, -4, 4);
''',
'''    const int requiredObservations = octaveDelta < 0 ? 3 : 2;
    if (pendingOctaveCount_ < requiredObservations)
    {
        // Hold the committed register only while an explicitly octave-like
        // challenger is being falsification-checked. Confidence cannot extend
        // this veto beyond the fixed observation count.
        decision.candidate.frequencyHz = trackedPitchHz_;
        decision.candidate.confidence = trackedConfidence_ * 0.97f;
        decision.candidate.periodicity = trackedPeriodicity_;
        decision.consensus = trackedConsensus_;
        decision.supportCount = trackedSupportCount_;
        decision.decoderOctaveIndex = octaveState_;
        decision.valid = trackedPitchHz_ > 0.0f;
        return false;
    }

''',
'normal octave confidence gate removed')

# ---------------------------------------------------------------------------
# 3. Rescue may extrapolate the source coordinate briefly, but it may never
# invent an adjacent scale degree in the absence of a real F0.
cpp = one(cpp,
'''        // A scale move is permitted only after a strongly directional real-F0
        // history. The helper returns exactly the immediate adjacent degree, so
        // one dropout can never invent a multi-degree melody.
        if (parameters.scaleLock && state.rescueDirection != 0)
        {
            const double adjacent = quantizer.adjacentTargetLog2(
                state.rescueBaseTargetLog2, state.rescueDirection);
            const double jumpCents = (adjacent - state.rescueBaseTargetLog2) * 1200.0;
            if (std::isfinite(adjacent)
                && state.rescueDirection * jumpCents > 0.1)
            {
                state.targetLog2 = adjacent;
                state.rescueTargetShifted = true;
                state.recentRealPitchCount = 0;
                ++state.revision;
                state.lastTargetJumpCents = jumpCents;
            }
        }
''',
'''        // NO_PREDICTED_NOTE_IDENTITY_V1: a detector hole may extrapolate the
        // source coordinate for transport continuity, but it may never create a
        // new musical degree. Only a subsequent real F0 may change targetLog2.
        state.rescueTargetShifted = false;
''',
'rescue cannot invent adjacent target')

# ---------------------------------------------------------------------------
# 4. Positive phonetic evidence is a real veto. Otherwise the accepted live F0
# goes directly to the existing ScaleQuantizer. The quantizer's user Hold logic
# is untouched, as are Amount/Humanize/Vibrato output softness laws.
cpp = one(cpp,
'''    state.invalidObservations = 0;

    // A strong breath/absence before any body latch must not become a note just
''',
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
'phonetic positive veto')

cpp = one(cpp,
'''    const double targetSelectionLog2 = state.pitchCentreLog2;
''',
'''    // LIVE_F0_SELECTS_SCALE_CELL_V1: pitchCentre is continuity/vibrato state,
    // never permission to change note. Every accepted real F0 reaches the
    // existing ScaleQuantizer immediately; only its user-controlled Hold
    // hysteresis may retain the previous degree.
    const double targetSelectionLog2 = observedLog2;
''',
'live f0 selects target cell')

cpp = one(cpp,
'''    if (targetIdentityChanged || musicalOnset || liveIdentityBreak)
    {
        state.recentRealPitchCount = rescueBodyFrame ? 1 : 0;
''',
'''    if (targetIdentityChanged || musicalOnset || liveIdentityBreak)
    {
        if (targetIdentityChanged)
        {
            // Rebase continuity state after the quantizer has accepted a real
            // note change. This prevents the old note centre from masquerading
            // as vibrato/softness around the new exact scale destination.
            state.pitchCentreLog2 = observedLog2;
            state.pitchCentreValid = true;
            state.stableObservations = 0;
        }
        state.recentRealPitchCount = rescueBodyFrame ? 1 : 0;
''',
'rebase continuity after target change')

# ---------------------------------------------------------------------------
# Tests: invert permission-gate expectations and assert exact scale ownership.
test = one(test,
'''    success &= check(!cautiousFirstAccepted && !cautiousFirst.valid,
                     "single_family_initial_register_still_needs_repeat");
    success &= check(cautiousSecondAccepted && cautiousSecond.valid,
                     "repeated_single_family_initial_register_can_commit");
''',
'''    success &= check(cautiousFirstAccepted && cautiousFirst.valid,
                     "fresh_single_family_initial_measurement_owns_immediately");
    success &= check(cautiousSecondAccepted && cautiousSecond.valid,
                     "repeated_single_family_measurement_remains_valid");
''',
'initial single family test')

test = one(test,
'''    const auto weakBootstrapDecision = weakBootstrapTracker->decodeCandidate(false);
    success &= check(!weakBootstrapDecision.valid,
                     "weak_single_path_does_not_fabricate_first_f0");
''',
'''    auto weakBootstrapDecision = weakBootstrapTracker->decodeCandidate(false);
    const bool weakBootstrapAccepted = weakBootstrapTracker->confirmOctaveTransition(
        weakBootstrapDecision, false);
    success &= check(weakBootstrapDecision.valid && weakBootstrapAccepted,
                     "weak_real_measurement_is_not_blocked_by_confidence");

    auto emptyBootstrapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    emptyBootstrapTracker->prepare(48000.0);
    emptyBootstrapTracker->presenceMode_ = true;
    const auto emptyBootstrapDecision = emptyBootstrapTracker->decodeCandidate(false);
    success &= check(!emptyBootstrapDecision.valid,
                     "absence_of_measurement_never_invents_f0");
''',
'weak bootstrap becomes measured evidence')

test = one(test,
'''    success &= check(!normalSingleFamily.valid,
                     "single_family_does_not_override_normal_tracking");
''',
'''    success &= check(normalSingleFamily.valid,
                     "fresh_single_family_can_report_real_note_change");
''',
'normal fresh single family')

test = one(test,
'''    success &= check(!delayedNormalDecision.valid,
                     "expired_tracker_anchor_does_not_weaken_normal_tracking");
''',
'''    success &= check(delayedNormalDecision.valid,
                     "stale_anchor_cannot_veto_new_live_measurement");
''',
'delayed normal anchor')

test = one(test,
'''    success &= check(!noBodyAnchorDecision.valid,
                     "released_note_body_removes_rescue_authority");
''',
'''    success &= check(noBodyAnchorDecision.valid,
                     "released_anchor_leaves_live_measurement_authoritative");
''',
'no anchor live measurement')

test = between(test,
'''    // Even high-confidence raw evidence outside the anchor window cannot use
''',
'''    // A phonetic/raw onset is not a musical note transition.  A wide rescue
''',
'''    // An exact octave/subharmonic challenger is measured and reported, but
    // confirmation applies a bounded negative veto before register ownership.
    auto bypassTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    bypassTracker->prepare(48000.0);
    bypassTracker->setReacquisitionAnchor(220.0f);
    bypassTracker->setRescueMode(true);
    auto& bypassSlot = bypassTracker->halfRateCandidate_;
    bypassSlot.candidate.valid = true;
    bypassSlot.candidate.frequencyHz = 110.0f;
    bypassSlot.candidate.confidence = 0.99f;
    bypassSlot.candidate.periodicity = 0.96f;
    bypassSlot.candidate.pathIndex = 1;
    bypassSlot.candidate.ageInHops = 0;
    bypassSlot.ageInHops = 0;
    auto bypassDecision = bypassTracker->decodeCandidate(false);
    const bool bypassCommitted = bypassTracker->confirmOctaveTransition(
        bypassDecision, false);
    success &= check(!bypassCommitted && !bypassDecision.valid,
                     "octave_alias_receives_bounded_negative_veto");


''',
'bypass rescue test')

test = one(test,
'''    const auto zeroConsensusDecision = zeroConsensusTracker->decodeCandidate(false);
    success &= check(!zeroConsensusDecision.valid,
                     "presence_does_not_fabricate_weak_zero_consensus_f0");

    auto firstPresenceLock = zeroConsensusDecision;
    const bool firstPresenceAccepted = zeroConsensusTracker->confirmOctaveTransition(
        firstPresenceLock, false);
    success &= check(!firstPresenceAccepted && !firstPresenceLock.valid,
                     "presence_cannot_bypass_initial_register_evidence");
''',
'''    const auto zeroConsensusDecision = zeroConsensusTracker->decodeCandidate(false);
    success &= check(zeroConsensusDecision.valid,
                     "zero_consensus_does_not_erase_real_measurement");

    auto firstPresenceLock = zeroConsensusDecision;
    const bool firstPresenceAccepted = zeroConsensusTracker->confirmOctaveTransition(
        firstPresenceLock, false);
    success &= check(firstPresenceAccepted && firstPresenceLock.valid,
                     "first_real_measurement_needs_no_confidence_permission");
''',
'zero consensus permission tests')

test = one(test,
'''    const bool liveRescueAccepted = liveRescueTracker->confirmOctaveTransition(
        liveRescueDecision, false);
    success &= check(!liveRescueAccepted && !liveRescueDecision.valid,
                     "presence_cannot_override_rescue_register_without_evidence");
''',
'''    const bool liveRescueAccepted = liveRescueTracker->confirmOctaveTransition(
        liveRescueDecision, false);
    auto liveRescueDecision2 = liveRescueDecision;
    liveRescueDecision2.valid = true;
    liveRescueDecision2.candidate.frequencyHz = 440.0f;
    liveRescueDecision2.candidate.confidence = 0.18f;
    liveRescueDecision2.candidate.periodicity = 0.24f;
    liveRescueDecision2.directSupportCount = 1;
    liveRescueDecision2.supportCount = 1;
    liveRescueDecision2.freshSupportMask = 0x01;
    const bool liveRescueAccepted2 = liveRescueTracker->confirmOctaveTransition(
        liveRescueDecision2, false);
    success &= check(!liveRescueAccepted && liveRescueAccepted2
                     && liveRescueDecision2.valid,
                     "octave_veto_is_bounded_not_confidence_gated");
''',
'bounded rescue octave test')

test = one(test,
'''    success &= check(risingRescue.rescuePredictionActive
                     && risingRescue.rescueDirection == 1
                     && risingTargetHz > 460.0 && risingTargetHz < 472.0,
                     "strong_rising_history_may_choose_only_adjacent_upper_degree");
''',
'''    success &= check(risingRescue.rescuePredictionActive
                     && risingRescue.rescueDirection == 1
                     && std::abs((risingRescue.targetLog2 - std::log2(440.0)) * 1200.0) < 0.1,
                     "rising_dropout_cannot_invent_adjacent_upper_degree");
''',
'rising rescue target hold')

test = one(test,
'''    success &= check(fallingRescue.rescuePredictionActive
                     && fallingRescue.rescueDirection == -1
                     && fallingTargetHz > 410.0 && fallingTargetHz < 420.0,
                     "strong_falling_history_may_choose_only_adjacent_lower_degree");
''',
'''    success &= check(fallingRescue.rescuePredictionActive
                     && fallingRescue.rescueDirection == -1
                     && std::abs((fallingRescue.targetLog2 - std::log2(440.0)) * 1200.0) < 0.1,
                     "falling_dropout_cannot_invent_adjacent_lower_degree");
''',
'falling rescue target hold')

# Add the core note-change + exact-scale regression after the existing absolute
# lock matrix. This catches the old pitchCentre/confidence permission gate.
test = one(test,
'''    success &= checkAbsoluteScaleLock(48, 10.0, 0.0f,
                                      "absolute_scale_lock_zero_consensus_zero_residual");

''',
'''    success &= checkAbsoluteScaleLock(48, 10.0, 0.0f,
                                      "absolute_scale_lock_zero_consensus_zero_residual");

    ModernPitchEngine::ScaleQuantizer vetoQuantizer;
    vetoQuantizer.reset();
    std::array<double, 12> vetoChromatic {};
    for (int degree = 0; degree < 12; ++degree)
        vetoChromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    vetoQuantizer.setScale(vetoChromatic.data(), 12, 440.0);
    ModernPitchEngine::CorrectionState vetoState;
    auto vetoInitial = strongPitch(440.0f);
    vetoInitial.audioPresent = true;
    engine->updateCorrectionState(vetoState, vetoQuantizer,
                                  vetoInitial, absoluteLockParameters);
    auto weakRealChange = strongPitch(505.0f);
    weakRealChange.audioPresent = true;
    weakRealChange.confidence = 0.01f;
    weakRealChange.periodicity = 0.05f;
    weakRealChange.consensus = 0.0f;
    weakRealChange.detectorSupport = 1;
    engine->updateCorrectionState(vetoState, vetoQuantizer,
                                  weakRealChange, absoluteLockParameters);
    const double vetoTargetHz = std::exp2(vetoState.targetLog2);
    const double vetoDestinationHz = 505.0 * std::exp2(vetoState.desiredCents / 1200.0);
    success &= check(vetoTargetHz > 490.0 && vetoTargetHz < 497.0
                     && std::abs(1200.0 * std::log2(vetoDestinationHz / vetoTargetHz)) < 1.0e-9,
                     "weak_real_note_change_reaches_exact_scale_degree_immediately");

''',
'exact scale weak note change regression')

cpp_p.write_text(cpp)
test_p.write_text(test)
print('detector veto scale authority v1 materialized')
