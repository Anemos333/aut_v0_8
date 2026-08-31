from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_path.read_text()
test = test_path.read_text()

MARKER = 'PITCH_RESCUE_V3_REGISTER_GUARD'
TEST_MARKER = 'rescue_subharmonic_cannot_restart_register'

if MARKER not in cpp:
    start = cpp.index('ModernPitchEngine::MultiRatePitchTracker::DecoderDecision\nModernPitchEngine::MultiRatePitchTracker::decodeCandidate(bool onsetPending) noexcept\n{')
    end = cpp.index('\nbool ModernPitchEngine::MultiRatePitchTracker::confirmOctaveTransition(', start)
    replacement = r'''ModernPitchEngine::MultiRatePitchTracker::DecoderDecision
ModernPitchEngine::MultiRatePitchTracker::decodeCandidate(bool onsetPending) noexcept
{
    std::array<PitchCandidate, detectorPathCount> candidates {};
    const int candidateCount = collectFreshCandidates(candidates);
    if (candidateCount <= 0)
        return {};

    std::array<ConsensusHypothesis, maxConsensusHypotheses> hypotheses {};
    const int hypothesisCount = buildConsensusHypotheses(candidates,
                                                         candidateCount,
                                                         hypotheses);
    if (hypothesisCount <= 0)
        return {};

    updateDecoderBeam(hypotheses, hypothesisCount, onsetPending);
    if (!decoderBeam_[0].valid)
        return {};

    const float decodedFrequency = static_cast<float>(
        std::exp2(decoderBeam_[0].logFrequency));
    const float rescueReferenceHz = trackedPitchHz_ > 0.0f
        ? trackedPitchHz_ : reacquisitionAnchorHz_;
    constexpr float sameNoteRescueCents = 360.0f;
    constexpr float wideRescueCents = 700.0f;

    int matchedHypothesis = -1;
    float matchedDistance = 100000.0f;

    // PITCH_RESCUE_V3_REGISTER_GUARD: once a musical note body owns a
    // persistent anchor, rescue is an anchor-constrained register search.  The
    // decoder beam is still useful evidence, but it may not restart the pitch
    // register from an unrelated subharmonic simply because trackedPitchHz_
    // has expired.
    if (rescueMode_ && rescueReferenceHz > 0.0f)
    {
        float bestRescueScore = -1000.0f;
        for (int index = 0; index < hypothesisCount; ++index)
        {
            const auto& candidate = hypotheses[static_cast<std::size_t>(index)];
            if (!candidate.valid || candidate.supportCount <= 0)
                continue;

            const float distance = centsDistance(candidate.frequencyHz,
                                                 rescueReferenceHz);
            const bool sameNoteWindow = distance <= sameNoteRescueCents;
            const bool exceptionalTransition = distance <= wideRescueCents
                && onsetPending
                && candidate.supportCount >= 2
                && candidate.directSupportCount >= 2
                && candidate.confidence >= 0.90f
                && candidate.periodicity >= 0.72f
                && candidate.consensus >= 0.68f;
            if ((!sameNoteWindow && !exceptionalTransition)
                || candidate.periodicity < 0.46f
                || candidate.confidence < 0.40f)
            {
                continue;
            }

            const float continuity = 1.0f - smoothStep(
                120.0f, sameNoteRescueCents, distance);
            const float rescueScore = candidate.evidenceScore
                + 0.62f * continuity
                + 0.14f * static_cast<float>(candidate.directSupportCount)
                + (exceptionalTransition ? 0.08f : 0.0f);
            if (rescueScore > bestRescueScore)
            {
                bestRescueScore = rescueScore;
                matchedHypothesis = index;
                matchedDistance = distance;
            }
        }

        if (matchedHypothesis < 0)
            return {};
    }
    else
    {
        for (int index = 0; index < hypothesisCount; ++index)
        {
            const float distance = centsDistance(
                hypotheses[static_cast<std::size_t>(index)].frequencyHz,
                decodedFrequency);
            if (distance < matchedDistance)
            {
                matchedDistance = distance;
                matchedHypothesis = index;
            }
        }

        if (matchedHypothesis < 0 || matchedDistance > 65.0f)
            return {}; // the winning branch is only a decaying hold state
    }

    const auto& hypothesis = hypotheses[static_cast<std::size_t>(matchedHypothesis)];
    DecoderDecision decision;
    decision.candidate.frequencyHz = hypothesis.frequencyHz;
    decision.candidate.confidence = clamp01(hypothesis.confidence
        * (0.76f + 0.24f * hypothesis.consensus));
    decision.candidate.periodicity = hypothesis.periodicity;
    decision.candidate.valid = true;
    decision.consensus = hypothesis.consensus;
    decision.supportCount = hypothesis.supportCount;
    decision.directSupportCount = hypothesis.directSupportCount;
    decision.freshSupportMask = hypothesis.freshSupportMask;
    decision.decoderOctaveIndex = decoderBeam_[0].octaveIndex;

    const bool closeToTrack = trackedPitchHz_ > 0.0f
        && centsDistance(trackedPitchHz_, decision.candidate.frequencyHz) < 95.0f;
    const bool sufficientInitialEvidence = decision.supportCount >= 2
        || decision.candidate.confidence >= 0.78f;
    const float rescueDistance = rescueReferenceHz > 0.0f
        ? centsDistance(rescueReferenceHz, decision.candidate.frequencyHz)
        : 100000.0f;
    const bool sameNoteRescue = rescueDistance <= sameNoteRescueCents;
    const bool exceptionalRescueTransition = rescueDistance <= wideRescueCents
        && onsetPending
        && decision.supportCount >= 2
        && decision.directSupportCount >= 2
        && decision.candidate.confidence >= 0.90f
        && decision.candidate.periodicity >= 0.72f
        && decision.consensus >= 0.68f;
    const bool rescueEvidence = rescueMode_
        && rescueReferenceHz > 0.0f
        && (sameNoteRescue || exceptionalRescueTransition)
        && decision.supportCount >= 1
        && decision.candidate.confidence >= 0.40f
        && decision.candidate.periodicity >= 0.46f;

    // Strong evidence may acquire an initial register, but it may not bypass a
    // latched note body's rescue anchor.  This closes the path that previously
    // let a strong subharmonic become a new F0 during acquire.
    decision.valid = rescueMode_
        ? rescueEvidence
        : (closeToTrack || sufficientInitialEvidence);
    return decision;
}
'''
    cpp = cpp[:start] + replacement + cpp[end:]

    old = r'''    // Initial register acquisition is deliberately temporal. A single fresh
    // harmonic family can be an octave alias at a vowel onset, so the first
    // non-exceptional decision must repeat before it becomes audible control.
    // This is detector commitment, not reduced correction authority.
    if (trackedPitchHz_ <= 0.0f)
    {
'''
    new = r'''    // If current F0 expired while a musical note body is still latched,
    // reacquisition is NOT an initial register acquisition.  The persistent
    // anchor owns the register and a subharmonic may not restart it.
    if (trackedPitchHz_ <= 0.0f && rescueMode_ && reacquisitionAnchorHz_ > 0.0f)
    {
        constexpr float sameNoteRescueCents = 360.0f;
        constexpr float wideRescueCents = 700.0f;
        const float distance = centsDistance(reacquisitionAnchorHz_,
                                             decision.candidate.frequencyHz);
        const bool sameNoteWindow = distance <= sameNoteRescueCents;
        const bool exceptionalTransition = distance <= wideRescueCents
            && onsetPending
            && decision.supportCount >= 2
            && decision.directSupportCount >= 2
            && decision.candidate.confidence >= 0.90f
            && decision.candidate.periodicity >= 0.72f
            && decision.consensus >= 0.68f;
        if (!sameNoteWindow && !exceptionalTransition)
        {
            decision.valid = false;
            pendingOctaveDelta_ = 0;
            pendingOctaveCount_ = 0;
            pendingOctaveFrequencyHz_ = 0.0f;
            return false;
        }

        const bool samePending = pendingOctaveFrequencyHz_ > 0.0f
            && centsDistance(pendingOctaveFrequencyHz_,
                             decision.candidate.frequencyHz) < 70.0f;
        if (!samePending)
        {
            pendingOctaveDelta_ = 0;
            pendingOctaveCount_ = 0;
            pendingOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        }
        if (decision.freshSupportMask != 0)
            ++pendingOctaveCount_;

        const bool strongSameRegister = distance <= 180.0f
            && decision.directSupportCount >= 1
            && decision.candidate.confidence >= 0.78f
            && decision.candidate.periodicity >= 0.70f;
        const int requiredObservations = strongSameRegister ? 1 : 2;
        if (pendingOctaveCount_ < requiredObservations)
        {
            decision.valid = false;
            return false;
        }

        decision.decoderOctaveIndex = octaveState_;
        committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        octaveCommitGuardHops_ = 6;
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        return true;
    }

    // Initial register acquisition is deliberately temporal. A single fresh
    // harmonic family can be an octave alias at a vowel onset, so the first
    // non-exceptional decision must repeat before it becomes audible control.
    // This is detector commitment, not reduced correction authority.
    if (trackedPitchHz_ <= 0.0f)
    {
'''
    if old not in cpp:
        raise RuntimeError('Initial register block not found')
    cpp = cpp.replace(old, new, 1)
    cpp_path.write_text(cpp)

if TEST_MARKER not in test:
    anchor = r'''    delayedRescueTracker->clearReacquisitionAnchor();
    delayedRescueTracker->decoderBeam_.fill({});
    const auto noBodyAnchorDecision = delayedRescueTracker->decodeCandidate(false);
    success &= check(!noBodyAnchorDecision.valid,
                     "released_note_body_removes_rescue_authority");
'''
    insertion = anchor + r'''

    // A strong low-period alias must never be allowed to restart the register
    // just because trackedPitchHz_ expired.  This was the real-audio failure:
    // acquire became shorter, but a subharmonic could be promoted to F0.
    auto subharmonicTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    subharmonicTracker->prepare(48000.0);
    subharmonicTracker->setReacquisitionAnchor(220.0f);
    subharmonicTracker->setRescueMode(true);
    ModernPitchEngine::MultiRatePitchTracker::DecoderDecision subharmonicDecision;
    subharmonicDecision.valid = true;
    subharmonicDecision.candidate.valid = true;
    subharmonicDecision.candidate.frequencyHz = 110.0f;
    subharmonicDecision.candidate.confidence = 0.99f;
    subharmonicDecision.candidate.periodicity = 0.99f;
    subharmonicDecision.consensus = 0.94f;
    subharmonicDecision.supportCount = 4;
    subharmonicDecision.directSupportCount = 4;
    subharmonicDecision.freshSupportMask = 0x0f;
    const bool subharmonicCommitted = subharmonicTracker->confirmOctaveTransition(
        subharmonicDecision, false);
    success &= check(!subharmonicCommitted && !subharmonicDecision.valid,
                     "rescue_subharmonic_cannot_restart_register");

    // Even high-confidence raw evidence outside the anchor window cannot use
    // sufficientInitialEvidence to bypass rescue continuity.
    auto bypassTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    bypassTracker->prepare(48000.0);
    bypassTracker->setReacquisitionAnchor(220.0f);
    bypassTracker->setRescueMode(true);
    auto& bypassSlot = bypassTracker->halfRateCandidate_;
    bypassSlot.candidate.valid = true;
    bypassSlot.candidate.frequencyHz = 130.0f;
    bypassSlot.candidate.confidence = 0.99f;
    bypassSlot.candidate.periodicity = 0.96f;
    bypassSlot.candidate.pathIndex = 1;
    bypassSlot.candidate.ageInHops = 0;
    bypassSlot.ageInHops = 0;
    const auto bypassDecision = bypassTracker->decodeCandidate(false);
    success &= check(!bypassDecision.valid
                     || ModernPitchEngine::MultiRatePitchTracker::centsDistance(
                         bypassDecision.candidate.frequencyHz, 220.0f) <= 360.0f,
                     "strong_subharmonic_cannot_bypass_rescue_anchor");
'''
    if anchor not in test:
        raise RuntimeError('Test insertion anchor not found')
    test = test.replace(anchor, insertion, 1)
    test_path.write_text(test)

print('Pitch rescue V3 register guard applied')
