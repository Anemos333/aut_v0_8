from pathlib import Path
import re

header_path = Path("Source/ModernPitchEngine.h")
engine_path = Path("Source/ModernPitchEngine.cpp")
test_path = Path("Tests/SupervisorContinuityTest.cpp")

header = header_path.read_text(encoding="utf-8")
engine = engine_path.read_text(encoding="utf-8")
test = test_path.read_text(encoding="utf-8")

sentinel = "PITCH_RESCUE_V1"
if sentinel in engine:
    print("pitch rescue V1 already applied")
    raise SystemExit(0)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def sub_once(text, pattern, replacement, label):
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{label}: expected one block, found {count}")
    return updated


# The renderer remains frozen. Rescue is detector state only.
header = replace_once(
    header,
    "        void setSensitivity(float sensitivity) noexcept;\n        bool processSample(float inputSample, PitchObservation& observation) noexcept;",
    "        void setSensitivity(float sensitivity) noexcept;\n        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }\n        bool processSample(float inputSample, PitchObservation& observation) noexcept;",
    "rescue setter",
)
header = replace_once(
    header,
    "        float sensitivity_ = 0.70f;\n\n        std::array<float, ringSize> fullRateRing_ {};",
    "        float sensitivity_ = 0.70f;\n        bool rescueMode_ = false;\n\n        std::array<float, ringSize> fullRateRing_ {};",
    "rescue state",
)

engine = replace_once(
    engine,
    "    invalidHopCount_ = 0;\n\n    octaveState_ = 0;",
    "    invalidHopCount_ = 0;\n    rescueMode_ = false;\n\n    octaveState_ = 0;",
    "reset rescue mode",
)

# During explicit reacquisition only, accept a weaker but still periodic raw
# YIN candidate. Normal tracking thresholds are untouched.
engine = replace_once(
    engine,
    "    const float fallbackThreshold = 0.26f + 0.20f * sensitivity_;",
    "    const float fallbackThreshold = 0.26f + 0.20f * sensitivity_\n        + (rescueMode_ ? 0.08f : 0.0f);",
    "rescue fallback threshold",
)
engine = replace_once(
    engine,
    "    if (bestTau < 2 || bestScore < 0.45f)\n        return result;",
    "    const float minimumCandidateScore = rescueMode_ ? 0.34f : 0.45f;\n    if (bestTau < 2 || bestScore < minimumCandidateScore)\n        return result;",
    "rescue candidate threshold",
)

# The temporal decoder normally rejects a one-family challenger. When the
# supervisor has explicitly declared the old F0 stale, use the best current
# hypothesis that is still musically continuous with the last committed F0.
# This bypasses decoder HOLD, not pitch correction authority.
engine = replace_once(
    engine,
    "    if (matchedHypothesis < 0 || matchedDistance > 65.0f)\n        return {}; // the winning branch is only a decaying hold state\n\n    const auto& hypothesis = hypotheses[static_cast<std::size_t>(matchedHypothesis)];",
    """    if ((matchedHypothesis < 0 || matchedDistance > 65.0f)
        && rescueMode_ && trackedPitchHz_ > 0.0f)
    {
        float bestRescueScore = -1000.0f;
        int bestRescueHypothesis = -1;
        for (int index = 0; index < hypothesisCount; ++index)
        {
            const auto& candidate = hypotheses[static_cast<std::size_t>(index)];
            if (!candidate.valid || candidate.supportCount <= 0)
                continue;
            const float distance = centsDistance(candidate.frequencyHz,
                                                 trackedPitchHz_);
            if (distance > 700.0f || candidate.periodicity < 0.46f
                || candidate.confidence < 0.40f)
            {
                continue;
            }
            const float continuity = 1.0f - smoothStep(180.0f, 700.0f, distance);
            const float rescueScore = candidate.evidenceScore
                + 0.55f * continuity
                + 0.12f * static_cast<float>(candidate.directSupportCount);
            if (rescueScore > bestRescueScore)
            {
                bestRescueScore = rescueScore;
                bestRescueHypothesis = index;
            }
        }
        if (bestRescueHypothesis >= 0)
        {
            matchedHypothesis = bestRescueHypothesis;
            matchedDistance = centsDistance(
                hypotheses[static_cast<std::size_t>(matchedHypothesis)].frequencyHz,
                trackedPitchHz_);
        }
    }

    if (matchedHypothesis < 0 || (!rescueMode_ && matchedDistance > 65.0f))
        return {}; // the winning branch is only a decaying hold state

    const auto& hypothesis = hypotheses[static_cast<std::size_t>(matchedHypothesis)];""",
    "rescue decoder hold bypass",
)

engine = replace_once(
    engine,
    "    const bool sufficientInitialEvidence = decision.supportCount >= 2\n        || decision.candidate.confidence >= 0.78f;\n    decision.valid = closeToTrack || sufficientInitialEvidence;",
    """    const bool sufficientInitialEvidence = decision.supportCount >= 2
        || decision.candidate.confidence >= 0.78f;
    const float rescueDistance = trackedPitchHz_ > 0.0f
        ? centsDistance(trackedPitchHz_, decision.candidate.frequencyHz)
        : 100000.0f;
    const bool rescueEvidence = rescueMode_
        && trackedPitchHz_ > 0.0f
        && rescueDistance <= 700.0f
        && decision.supportCount >= 1
        && decision.candidate.confidence >= 0.40f
        && decision.candidate.periodicity >= 0.46f;
    decision.valid = closeToTrack || sufficientInitialEvidence || rescueEvidence;""",
    "rescue decision authority",
)

# PITCH_RESCUE_V1 is deliberately attached to the detector path, never to the
# spectral renderer. The supervisor already raises rescueSearch after 60 ms.
engine = replace_once(
    engine,
    "                tracker.setSensitivity(rescueSearch ? std::max(safe.detectorSensitivity, 0.98f)\n                                                    : safe.detectorSensitivity);\n                if (tracker.processSample",
    "                tracker.setSensitivity(rescueSearch ? std::max(safe.detectorSensitivity, 0.98f)\n                                                    : safe.detectorSensitivity);\n                tracker.setRescueMode(rescueSearch); // PITCH_RESCUE_V1\n                if (tracker.processSample",
    "dual mono rescue mode",
)
engine = replace_once(
    engine,
    "            linkedTracker_.setSensitivity(rescueSearch ? std::max(safe.detectorSensitivity, 0.98f)\n                                                       : safe.detectorSensitivity);\n            if (linkedTracker_.processSample",
    "            linkedTracker_.setSensitivity(rescueSearch ? std::max(safe.detectorSensitivity, 0.98f)\n                                                       : safe.detectorSensitivity);\n            linkedTracker_.setRescueMode(rescueSearch); // PITCH_RESCUE_V1\n            if (linkedTracker_.processSample",
    "linked rescue mode",
)

# Focused regression: a single credible detector family 200 cents away is not
# enough during normal tracking, but must recover a deliberately stale F0.
tracker_anchor = '''    success &= check(secondAccepted,
                     "repeated_initial_register_is_committed");
'''
tracker_test = tracker_anchor + r'''

    auto rescueTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    rescueTracker->prepare(48000.0);
    rescueTracker->trackedPitchHz_ = 220.0f;
    rescueTracker->trackedConfidence_ = 0.88f;
    rescueTracker->trackedPeriodicity_ = 0.90f;
    rescueTracker->trackedConsensus_ = 0.75f;
    rescueTracker->trackedSupportCount_ = 2;
    auto& rescueSlot = rescueTracker->halfRateCandidate_;
    rescueSlot.candidate.valid = true;
    rescueSlot.candidate.frequencyHz = static_cast<float>(220.0 * std::exp2(200.0 / 1200.0));
    rescueSlot.candidate.confidence = 0.74f;
    rescueSlot.candidate.periodicity = 0.82f;
    rescueSlot.candidate.pathIndex = 1;
    rescueSlot.candidate.ageInHops = 0;
    rescueSlot.ageInHops = 0;

    rescueTracker->setRescueMode(false);
    auto normalSingleFamily = rescueTracker->decodeCandidate(false);
    success &= check(!normalSingleFamily.valid,
                     "single_family_does_not_override_normal_tracking");

    rescueTracker->decoderBeam_.fill({});
    rescueTracker->setRescueMode(true);
    auto rescuedSingleFamily = rescueTracker->decodeCandidate(false);
    success &= check(rescuedSingleFamily.valid
                     && std::abs(1200.0 * std::log2(
                         rescuedSingleFamily.candidate.frequencyHz / 220.0f)) > 140.0,
                     "stale_f0_accepts_credible_single_family_rescue");
'''
test = replace_once(test, tracker_anchor, tracker_test, "tracker rescue test")

stale_anchor = '''    success &= check(std::abs(dropoutState.transportPeriodHz - 220.0) < 1.0e-9,
                     "pitch_dropout_keeps_latched_transport_period");
'''
stale_test = stale_anchor + r'''

    // Reproduces the real failure mode: normal-level sung body survives while
    // the primary F0 is missing, then a recovered F0 has moved musically. The
    // correction must stop using the stale anchor and return to stable tracking.
    ModernPitchEngine::ScaleQuantizer recoveryQuantizer;
    recoveryQuantizer.reset();
    const double recoveryUnison = 1.0;
    recoveryQuantizer.setScale(&recoveryUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState recoveryState;
    auto recoveryObservation = strongPitch(220.0f);
    for (int hop = 0; hop < 16; ++hop)
    {
        engine->updateCorrectionState(recoveryState, recoveryQuantizer,
                                      recoveryObservation, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(recoveryState));
    }
    for (int hop = 0; hop < 130; ++hop)
    {
        engine->updateCorrectionState(recoveryState, recoveryQuantizer,
                                      invalid, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(recoveryState));
    }
    const double staleCentre = recoveryState.pitchCentreLog2;
    const double staleDesired = recoveryState.desiredCents;
    recoveryObservation = strongPitch(static_cast<float>(220.0
        * std::exp2(200.0 / 1200.0)));
    for (int hop = 0; hop < 24; ++hop)
    {
        engine->updateCorrectionState(recoveryState, recoveryQuantizer,
                                      recoveryObservation, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(recoveryState));
    }
    const double recoveredCentreMove = std::abs(
        (recoveryState.pitchCentreLog2 - staleCentre) * 1200.0);
    std::cerr << "recovered_pitch_centre_move_cents=" << recoveredCentreMove << '\n';
    success &= check(recoveryState.pitchStaleSamples == 0
                     && recoveryState.noteBodyLatched
                     && recoveryState.trackingState != ModernPitchEngine::TrackingState::acquire,
                     "rescued_f0_refreshes_anchor_and_exits_acquire");
    success &= check(recoveredCentreMove > 80.0
                     && std::abs(recoveryState.desiredCents - staleDesired) > 40.0,
                     "rescued_f0_retargets_instead_of_freezing_old_correction");
'''
test = replace_once(test, stale_anchor, stale_test, "supervisor stale-anchor recovery test")

header_path.write_text(header, encoding="utf-8")
engine_path.write_text(engine, encoding="utf-8")
test_path.write_text(test, encoding="utf-8")
print("applied pitch rescue V1: stale F0 can reacquire from bounded raw multirate evidence")
