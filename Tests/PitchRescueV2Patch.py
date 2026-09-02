from pathlib import Path

header_path = Path("Source/ModernPitchEngine.h")
engine_path = Path("Source/ModernPitchEngine.cpp")
test_path = Path("Tests/SupervisorContinuityTest.cpp")

header = header_path.read_text(encoding="utf-8")
engine = engine_path.read_text(encoding="utf-8")
test = test_path.read_text(encoding="utf-8")

sentinel = "PITCH_RESCUE_V2_PERSISTENT_ANCHOR"
if sentinel in engine:
    print("pitch rescue V2 persistent anchor already applied")
    raise SystemExit(0)


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


# The tracker owns current detector state separately from the supervisor-owned
# musical reacquisition anchor. The latter is fed from CorrectionState's latched
# transportPeriodHz and therefore survives detector invalidation but not a real
# note-body release.
header = replace_once(
    header,
    "        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }\n        bool processSample(float inputSample, PitchObservation& observation) noexcept;",
    "        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }\n        void setReacquisitionAnchor(float frequencyHz) noexcept;\n        void clearReacquisitionAnchor() noexcept { reacquisitionAnchorHz_ = 0.0f; }\n        bool processSample(float inputSample, PitchObservation& observation) noexcept;",
    "public reacquisition-anchor API",
)
header = replace_once(
    header,
    "        float trackedPitchHz_ = 0.0f;\n        float trackedConfidence_ = 0.0f;",
    "        float trackedPitchHz_ = 0.0f;\n        float reacquisitionAnchorHz_ = 0.0f;\n        float trackedConfidence_ = 0.0f;",
    "persistent anchor storage",
)

engine = replace_once(
    engine,
    "    trackedPitchHz_ = 0.0f;\n    trackedConfidence_ = 0.0f;",
    "    trackedPitchHz_ = 0.0f;\n    reacquisitionAnchorHz_ = 0.0f;\n    trackedConfidence_ = 0.0f;",
    "reset persistent anchor",
)
engine = replace_once(
    engine,
    "void ModernPitchEngine::MultiRatePitchTracker::setSensitivity(float sensitivity) noexcept\n{\n    sensitivity_ = clamp01(sensitivity);\n}\n\nvoid ModernPitchEngine::MultiRatePitchTracker::push(",
    "void ModernPitchEngine::MultiRatePitchTracker::setSensitivity(float sensitivity) noexcept\n{\n    sensitivity_ = clamp01(sensitivity);\n}\n\nvoid ModernPitchEngine::MultiRatePitchTracker::setReacquisitionAnchor(\n    float frequencyHz) noexcept\n{\n    // PITCH_RESCUE_V2_PERSISTENT_ANCHOR\n    // This is musical note-body memory supplied by the supervisor, not current\n    // detector state. It must survive trackedPitchHz_ invalidation.\n    reacquisitionAnchorHz_ = std::isfinite(frequencyHz) && frequencyHz > 0.0f\n        ? std::clamp(frequencyHz, 20.0f, 4000.0f)\n        : 0.0f;\n}\n\nvoid ModernPitchEngine::MultiRatePitchTracker::push(",
    "anchor setter implementation",
)

# Rescue continuity uses the current detector pitch while it exists, otherwise
# the musical anchor retained by the supervisor. Normal tracking still uses only
# trackedPitchHz_, so the anchor cannot make a weak candidate authoritative
# outside explicit rescue mode.
engine = replace_once(
    engine,
    "    const float decodedFrequency = static_cast<float>(\n        std::exp2(decoderBeam_[0].logFrequency));\n\n    int matchedHypothesis = -1;",
    "    const float decodedFrequency = static_cast<float>(\n        std::exp2(decoderBeam_[0].logFrequency));\n    const float rescueReferenceHz = trackedPitchHz_ > 0.0f\n        ? trackedPitchHz_ : reacquisitionAnchorHz_;\n\n    int matchedHypothesis = -1;",
    "rescue reference selection",
)
engine = replace_once(
    engine,
    "    if ((matchedHypothesis < 0 || matchedDistance > 65.0f)\n        && rescueMode_ && trackedPitchHz_ > 0.0f)",
    "    if ((matchedHypothesis < 0 || matchedDistance > 65.0f)\n        && rescueMode_ && rescueReferenceHz > 0.0f)",
    "rescue branch persistent reference",
)
engine = replace_once(
    engine,
    "            const float distance = centsDistance(candidate.frequencyHz,\n                                                 trackedPitchHz_);",
    "            const float distance = centsDistance(candidate.frequencyHz,\n                                                 rescueReferenceHz);",
    "rescue candidate distance",
)
engine = replace_once(
    engine,
    "            matchedDistance = centsDistance(\n                hypotheses[static_cast<std::size_t>(matchedHypothesis)].frequencyHz,\n                trackedPitchHz_);",
    "            matchedDistance = centsDistance(\n                hypotheses[static_cast<std::size_t>(matchedHypothesis)].frequencyHz,\n                rescueReferenceHz);",
    "matched rescue distance",
)
engine = replace_once(
    engine,
    "    const float rescueDistance = trackedPitchHz_ > 0.0f\n        ? centsDistance(trackedPitchHz_, decision.candidate.frequencyHz)\n        : 100000.0f;\n    const bool rescueEvidence = rescueMode_\n        && trackedPitchHz_ > 0.0f",
    "    const float rescueDistance = rescueReferenceHz > 0.0f\n        ? centsDistance(rescueReferenceHz, decision.candidate.frequencyHz)\n        : 100000.0f;\n    const bool rescueEvidence = rescueMode_\n        && rescueReferenceHz > 0.0f",
    "rescue evidence persistent reference",
)

# Feed the tracker from the supervisor-owned note-body period every sample.
# transportPeriodHz is held during F0 loss and cleared only once release reaches
# unvoiced, so this fixes the temporal hole without touching renderer or Amount.
dual_anchor = """                auto& tracker = channelTrackers_[static_cast<std::size_t>(channel)];
                auto& correction = channelCorrections_[static_cast<std::size_t>(channel)];
                const bool rescueSearch = correction.noteBodyLatched
"""
dual_replacement = """                auto& tracker = channelTrackers_[static_cast<std::size_t>(channel)];
                auto& correction = channelCorrections_[static_cast<std::size_t>(channel)];
                if (correction.noteBodyLatched && correction.transportPeriodHz > 0.0)
                    tracker.setReacquisitionAnchor(static_cast<float>(correction.transportPeriodHz));
                else
                    tracker.clearReacquisitionAnchor();
                const bool rescueSearch = correction.noteBodyLatched
"""
engine = replace_once(engine, dual_anchor, dual_replacement,
                      "dual-mono supervisor anchor feed")

linked_anchor = """            PitchObservation observation;
            const bool rescueSearch = linkedCorrection_.noteBodyLatched
"""
linked_replacement = """            PitchObservation observation;
            if (linkedCorrection_.noteBodyLatched && linkedCorrection_.transportPeriodHz > 0.0)
                linkedTracker_.setReacquisitionAnchor(
                    static_cast<float>(linkedCorrection_.transportPeriodHz));
            else
                linkedTracker_.clearReacquisitionAnchor();
            const bool rescueSearch = linkedCorrection_.noteBodyLatched
"""
engine = replace_once(engine, linked_anchor, linked_replacement,
                      "linked supervisor anchor feed")

# Structural regression: reproduce the actual temporal order. Current tracker
# state is allowed to expire for >60 ms first. Only then is rescue enabled. A
# single weak family must be rejected normally and accepted using the still-live
# supervisor anchor even though trackedPitchHz_ is already zero.
test_anchor = '''    success &= check(rescuedSingleFamily.valid
                     && std::abs(1200.0 * std::log2(
                         rescuedSingleFamily.candidate.frequencyHz / 220.0f)) > 140.0,
                     "stale_f0_accepts_credible_single_family_rescue");
'''
test_block = test_anchor + r'''

    auto delayedRescueTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    delayedRescueTracker->prepare(48000.0);
    delayedRescueTracker->trackedPitchHz_ = 220.0f;
    delayedRescueTracker->trackedConfidence_ = 0.88f;
    delayedRescueTracker->trackedPeriodicity_ = 0.90f;
    delayedRescueTracker->trackedConsensus_ = 0.75f;
    delayedRescueTracker->trackedSupportCount_ = 2;
    delayedRescueTracker->setReacquisitionAnchor(220.0f);
    ModernPitchEngine::PitchObservation expiredObservation;
    const int dropoutSamples = static_cast<int>(0.075 * 48000.0);
    for (int sample = 0; sample < dropoutSamples; ++sample)
        static_cast<void>(delayedRescueTracker->processSample(0.0f, expiredObservation));
    success &= check(delayedRescueTracker->trackedPitchHz_ == 0.0f
                     && std::abs(delayedRescueTracker->reacquisitionAnchorHz_ - 220.0f) < 0.01f,
                     "current_f0_can_expire_without_erasing_note_body_anchor");

    auto& delayedSlot = delayedRescueTracker->halfRateCandidate_;
    delayedSlot.candidate.valid = true;
    delayedSlot.candidate.frequencyHz = static_cast<float>(220.0 * std::exp2(200.0 / 1200.0));
    delayedSlot.candidate.confidence = 0.74f;
    delayedSlot.candidate.periodicity = 0.82f;
    delayedSlot.candidate.pathIndex = 1;
    delayedSlot.candidate.ageInHops = 0;
    delayedSlot.ageInHops = 0;
    delayedRescueTracker->decoderBeam_.fill({});
    delayedRescueTracker->setRescueMode(false);
    const auto delayedNormalDecision = delayedRescueTracker->decodeCandidate(false);
    success &= check(!delayedNormalDecision.valid,
                     "expired_tracker_anchor_does_not_weaken_normal_tracking");

    delayedRescueTracker->decoderBeam_.fill({});
    delayedRescueTracker->setRescueMode(true);
    const auto delayedRescueDecision = delayedRescueTracker->decodeCandidate(false);
    success &= check(delayedRescueDecision.valid
                     && delayedRescueTracker->trackedPitchHz_ == 0.0f,
                     "rescue_uses_persistent_anchor_after_sixty_ms_detector_hole");
    delayedRescueTracker->clearReacquisitionAnchor();
    delayedRescueTracker->decoderBeam_.fill({});
    const auto noBodyAnchorDecision = delayedRescueTracker->decodeCandidate(false);
    success &= check(!noBodyAnchorDecision.valid,
                     "released_note_body_removes_rescue_authority");
'''
test = replace_once(test, test_anchor, test_block,
                    "delayed real-order rescue regression")

header_path.write_text(header, encoding="utf-8")
engine_path.write_text(engine, encoding="utf-8")
test_path.write_text(test, encoding="utf-8")
print("applied pitch rescue V2: persistent supervisor anchor survives detector expiry")
