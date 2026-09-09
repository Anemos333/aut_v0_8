from pathlib import Path

ENGINE = Path('Source/ModernPitchEngine.cpp')
TEST = Path('Tests/SupervisorContinuityTest.cpp')

engine = ENGINE.read_text(encoding='utf-8')
old_decode = '''            const bool sameNoteWindow = distance <= sameNoteRescueCents;
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
                + (exceptionalTransition ? 0.08f : 0.0f);'''
new_decode = '''            const bool sameNoteWindow = distance <= sameNoteRescueCents;
            const bool wideTransitionChallenger = distance <= wideRescueCents
                && candidate.supportCount >= 3
                && candidate.directSupportCount >= 2
                && candidate.confidence >= 0.92f
                && candidate.periodicity >= 0.80f
                && candidate.consensus >= 0.78f;
            if ((!sameNoteWindow && !wideTransitionChallenger)
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
                + (wideTransitionChallenger ? 0.02f : 0.0f);'''
if old_decode not in engine:
    raise SystemExit('PitchRescueV4: decode challenger block not found')
engine = engine.replace(old_decode, new_decode, 1)

old_valid = '''    const bool sameNoteRescue = rescueDistance <= sameNoteRescueCents;
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
        && decision.candidate.periodicity >= 0.46f;'''
new_valid = '''    const bool sameNoteRescue = rescueDistance <= sameNoteRescueCents;
    const bool wideRescueChallenger = rescueDistance <= wideRescueCents
        && decision.supportCount >= 3
        && decision.directSupportCount >= 2
        && decision.candidate.confidence >= 0.92f
        && decision.candidate.periodicity >= 0.80f
        && decision.consensus >= 0.78f;
    const bool rescueEvidence = rescueMode_
        && rescueReferenceHz > 0.0f
        && (sameNoteRescue || wideRescueChallenger)
        && decision.supportCount >= 1
        && decision.candidate.confidence >= 0.40f
        && decision.candidate.periodicity >= 0.46f;'''
if old_valid not in engine:
    raise SystemExit('PitchRescueV4: rescue validity block not found')
engine = engine.replace(old_valid, new_valid, 1)

old_confirm = '''        const bool sameNoteWindow = distance <= sameNoteRescueCents;
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
        const int requiredObservations = strongSameRegister ? 1 : 2;'''
new_confirm = '''        const bool sameNoteWindow = distance <= sameNoteRescueCents;
        const bool wideTransitionChallenger = distance <= wideRescueCents
            && decision.supportCount >= 3
            && decision.directSupportCount >= 2
            && decision.candidate.confidence >= 0.92f
            && decision.candidate.periodicity >= 0.80f
            && decision.consensus >= 0.78f;
        if (!sameNoteWindow && !wideTransitionChallenger)
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
        constexpr int wideTransitionObservations = 8;
        const int requiredObservations = sameNoteWindow
            ? (strongSameRegister ? 1 : 2)
            : wideTransitionObservations;'''
if old_confirm not in engine:
    raise SystemExit('PitchRescueV4: confirm challenger block not found')
engine = engine.replace(old_confirm, new_confirm, 1)

marker = '''    success &= check(!bypassDecision.valid
                     || ModernPitchEngine::MultiRatePitchTracker::centsDistance(
                         bypassDecision.candidate.frequencyHz, 220.0f) <= 360.0f,
                     "strong_subharmonic_cannot_bypass_rescue_anchor");
'''
addition = marker + '''

    // A phonetic/raw onset is not a musical note transition.  A wide rescue
    // challenger must therefore persist across several fresh observations
    // before it can replace the latched register.  This models a voiced word
    // onset such as /j/ in "Your": transient periodic structure may be strong,
    // but one or two hops must never become audible pitch control.
    auto phoneticTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    phoneticTracker->prepare(48000.0);
    phoneticTracker->setReacquisitionAnchor(220.0f);
    phoneticTracker->setRescueMode(true);
    auto makeWideChallenger = []
    {
        ModernPitchEngine::MultiRatePitchTracker::DecoderDecision d;
        d.valid = true;
        d.candidate.valid = true;
        d.candidate.frequencyHz = 165.0f; // ~-498 cents: plausible alias / perfect-fourth challenger
        d.candidate.confidence = 0.97f;
        d.candidate.periodicity = 0.93f;
        d.consensus = 0.86f;
        d.supportCount = 4;
        d.directSupportCount = 3;
        d.freshSupportMask = 0x0f;
        return d;
    };
    bool phoneticBurstCommitted = false;
    for (int hop = 0; hop < 4; ++hop)
    {
        auto d = makeWideChallenger();
        phoneticBurstCommitted = phoneticTracker->confirmOctaveTransition(d, true)
            || phoneticBurstCommitted;
    }
    success &= check(!phoneticBurstCommitted,
                     "raw_phonetic_onset_cannot_immediately_authorize_wide_rescue");

    // A genuine large melodic move is still recoverable: strong direct
    // evidence that persists beyond the transient window eventually owns the
    // new register even without relying on raw onsetPending.
    phoneticTracker->pendingOctaveCount_ = 0;
    phoneticTracker->pendingOctaveFrequencyHz_ = 0.0f;
    bool persistentWideCommitted = false;
    for (int hop = 0; hop < 8; ++hop)
    {
        auto d = makeWideChallenger();
        persistentWideCommitted = phoneticTracker->confirmOctaveTransition(d, false);
    }
    success &= check(persistentWideCommitted,
                     "persistent_multi_evidence_wide_rescue_can_commit_real_note_change");
'''
if marker not in TEST.read_text(encoding='utf-8'):
    raise SystemExit('PitchRescueV4: test insertion marker not found')
test = TEST.read_text(encoding='utf-8').replace(marker, addition, 1)

ENGINE.write_text(engine, encoding='utf-8')
TEST.write_text(test, encoding='utf-8')
print('PitchRescueV4 applied: raw onset no longer grants immediate wide-register authority')
