from pathlib import Path

root = Path(__file__).resolve().parents[1]
hpp_path = root / 'Source' / 'ModernPitchEngine.h'
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
test_path = root / 'Tests' / 'SupervisorContinuityTest.cpp'

hpp = hpp_path.read_text()
cpp = cpp_path.read_text()
test = test_path.read_text()


def one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


# ---------------------------------------------------------------------------
# Detector state is epistemic, never musical.  Transition is allowed to wake
# the observer, but it receives no audio/target authority.
hpp = one(hpp,
'''        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }
        void setReacquisitionAnchor(float frequencyHz) noexcept;
        void clearReacquisitionAnchor() noexcept { reacquisitionAnchorHz_ = 0.0f; }
        bool processSample(float inputSample, PitchObservation& observation) noexcept;
''',
'''        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }
        // TRANSITION_WAKES_DETECTOR_NOT_OUTPUT_V1: analysis-only watchdog.
        void setTransitionWake(bool enabled) noexcept { transitionWake_ = enabled; }
        void setReacquisitionAnchor(float frequencyHz) noexcept;
        void clearReacquisitionAnchor() noexcept { reacquisitionAnchorHz_ = 0.0f; }
        bool processSample(float inputSample, PitchObservation& observation) noexcept;
''',
'public transition wake')

hpp = one(hpp,
'''        void push(std::array<float, ringSize>& ring,
                  int& writePosition,
                  int& availableSamples,
                  float sample) noexcept;
''',
'''        // OBSERVATION_MEMORY_SEPARATION_V1: clears detector hypotheses only.
        // It never touches ScaleQuantizer, CorrectionState or renderer state.
        void clearObservationMemory(bool clearAnalysisBuffers) noexcept;
        void push(std::array<float, ringSize>& ring,
                  int& writePosition,
                  int& availableSamples,
                  float sample) noexcept;
''',
'private observation clear helper')

hpp = one(hpp,
'''        bool rescueMode_ = false;
        bool presenceMode_ = false;
        bool presenceSinceLastHop_ = false;
''',
'''        bool rescueMode_ = false;
        bool presenceMode_ = false;
        bool presenceSinceLastHop_ = false;
        bool transitionWake_ = false;
        // True after a physical input discontinuity or watchdog falsification.
        // While true, musical note-body state may not be re-injected as an F0
        // anchor. A fresh measured F0 clears it.
        bool observationContinuityBroken_ = false;
''',
'observation state flags')

# ---------------------------------------------------------------------------
# Reset flags together with the rest of detector state.
cpp = one(cpp,
'''    rescueMode_ = false;
    presenceMode_ = false;
    presenceSinceLastHop_ = false;

    octaveState_ = 0;
''',
'''    rescueMode_ = false;
    presenceMode_ = false;
    presenceSinceLastHop_ = false;
    transitionWake_ = false;
    observationContinuityBroken_ = false;

    octaveState_ = 0;
''',
'reset observation flags')

# A supervisor-owned transport period is allowed to help only while physical
# detector continuity still exists. Once the input disappeared, the next F0 is
# a fresh observation and the previous musical degree stays downstream only.
cpp = one(cpp,
'''void ModernPitchEngine::MultiRatePitchTracker::setReacquisitionAnchor(
    float frequencyHz) noexcept
{
    // PITCH_RESCUE_V2_PERSISTENT_ANCHOR
    // This is musical note-body memory supplied by the supervisor, not current
    // detector state. It must survive trackedPitchHz_ invalidation.
    reacquisitionAnchorHz_ = std::isfinite(frequencyHz) && frequencyHz > 0.0f
        ? std::clamp(frequencyHz, 20.0f, 4000.0f)
        : 0.0f;
}

''',
'''void ModernPitchEngine::MultiRatePitchTracker::setReacquisitionAnchor(
    float frequencyHz) noexcept
{
    // OBSERVATION_MEMORY_SEPARATION_V1
    // This value is only a short physical-continuity prior. It is not musical
    // memory. After a real input discontinuity the scale target may remain owned
    // downstream, but that old target may not repopulate detector history.
    if (observationContinuityBroken_)
    {
        reacquisitionAnchorHz_ = 0.0f;
        return;
    }
    reacquisitionAnchorHz_ = std::isfinite(frequencyHz) && frequencyHz > 0.0f
        ? std::clamp(frequencyHz, 20.0f, 4000.0f)
        : 0.0f;
}

void ModernPitchEngine::MultiRatePitchTracker::clearObservationMemory(
    bool clearAnalysisBuffers) noexcept
{
    trackedPitchHz_ = 0.0f;
    reacquisitionAnchorHz_ = 0.0f;
    trackedConfidence_ = 0.0f;
    trackedPeriodicity_ = 0.0f;
    trackedConsensus_ = 0.0f;
    trackedSupportCount_ = 0;
    invalidHopCount_ = 0;
    decoderBeam_.fill({});
    octaveState_ = 0;
    pendingOctaveDelta_ = 0;
    pendingOctaveCount_ = 0;
    pendingOctaveFrequencyHz_ = 0.0f;
    committedOctaveFrequencyHz_ = 0.0f;
    octaveCommitGuardHops_ = 0;
    observationContinuityBroken_ = true;

    if (!clearAnalysisBuffers)
        return;

    // A physical gap means samples on the two sides are not one analysis frame.
    // Drop only detector buffers; the single wet renderer/OLA is untouched.
    fullRateRing_.fill(0.0f);
    halfRateRing_.fill(0.0f);
    quarterRateRing_.fill(0.0f);
    eighthRateRing_.fill(0.0f);
    fullRateWritePosition_ = 0;
    halfRateWritePosition_ = 0;
    quarterRateWritePosition_ = 0;
    eighthRateWritePosition_ = 0;
    fullRateAvailableSamples_ = 0;
    halfRateAvailableSamples_ = 0;
    quarterRateAvailableSamples_ = 0;
    eighthRateAvailableSamples_ = 0;
    halfRateDecimationCounter_ = 0;
    quarterRateDecimationCounter_ = 0;
    eighthRateDecimationCounter_ = 0;
    analysisHopCounter_ = 0;
    fullRateCandidate_ = {};
    halfRateCandidate_ = {};
    quarterRateCandidate_ = {};
    eighthRateCandidate_ = {};
    halfRateAntiAlias_.reset();
    quarterRateAntiAlias_.reset();
    eighthRateAntiAlias_.reset();
    previousInput_ = 0.0f;
    previousDcOutput_ = 0.0f;
    onsetEnvelope_ = 0.0f;
    onsetCooldownSamples_ = 0;
    onsetPending_ = false;
    presenceSinceLastHop_ = false;
}

''',
'anchor becomes physical prior')

# A complete detector hop with no physical input is a hard epistemic boundary.
# Output ownership is deliberately elsewhere and therefore survives unchanged.
cpp = one(cpp,
'''    hopCounter_ = 0;
    ++analysisHopCounter_;
    presenceMode_ = presenceSinceLastHop_;
    presenceSinceLastHop_ = false;

    ++fullRateCandidate_.ageInHops;
''',
'''    hopCounter_ = 0;
    ++analysisHopCounter_;
    presenceMode_ = presenceSinceLastHop_;
    presenceSinceLastHop_ = false;

    // ZERO_INPUT_CLEARS_OBSERVER_NOT_MUSIC_V1
    if (!presenceMode_)
    {
        clearObservationMemory(true);
        observation.audioPresent = false;
        observation.measurementAvailable = false;
        observation.valid = false;
        observation.onset = false;
        observation.onsetStrength = 0.0f;
        return true;
    }

    ++fullRateCandidate_.ageInHops;
''',
'zero input observer boundary')

# Transition/acquire watchdog: it never picks a pitch. It can only remove stale
# detector history when current independent voice evidence already contradicts
# that history strongly enough to stand on its own.
cpp = one(cpp,
'''    if (hypothesisCount <= 0)
        return makeFreshRawDecision();

    updateDecoderBeam(hypotheses, hypothesisCount, onsetPending);
''',
'''    if (hypothesisCount <= 0)
        return makeFreshRawDecision();

    // TRANSITION_WAKES_DETECTOR_NOT_OUTPUT_V1
    // A persistent transition/acquire state may falsify detector memory, never
    // manufacture F0. Require multiple direct paths plus strong current source
    // cleanliness before clearing a contradictory old register hypothesis.
    if (transitionWake_)
    {
        const float oldReferenceHz = trackedPitchHz_ > 0.0f
            ? trackedPitchHz_ : reacquisitionAnchorHz_;
        int wakeHypothesis = -1;
        float wakeScore = -1000.0f;
        if (oldReferenceHz > 0.0f)
        {
            for (int index = 0; index < hypothesisCount; ++index)
            {
                const auto& current = hypotheses[static_cast<std::size_t>(index)];
                if (!current.valid
                    || current.freshSupportMask == 0
                    || current.directSupportCount < 2
                    || current.cleanSupportCount < 2
                    || current.tonalCleanliness < 0.62f
                    || current.harmonicFamily < 0.58f
                    || current.periodicity < 0.52f
                    || centsDistance(oldReferenceHz, current.frequencyHz) < 95.0f)
                {
                    continue;
                }
                const float currentScore = current.evidenceScore
                    + 0.18f * current.consensus
                    + 0.18f * current.tonalCleanliness;
                if (currentScore > wakeScore)
                {
                    wakeScore = currentScore;
                    wakeHypothesis = index;
                }
            }
        }
        if (wakeHypothesis >= 0)
            clearObservationMemory(false);
    }

    updateDecoderBeam(hypotheses, hypothesisCount, onsetPending);
''',
'transition detector watchdog')

# A genuinely accepted new F0 re-establishes physical observation continuity.
cpp = one(cpp,
'''    if (decision.valid && decision.candidate.valid)
    {
        const bool firstLock = trackedPitchHz_ <= 0.0f;
''',
'''    if (decision.valid && decision.candidate.valid)
    {
        observationContinuityBroken_ = false;
        const bool firstLock = trackedPitchHz_ <= 0.0f;
''',
'fresh f0 restores observer continuity')

# Wire musical state to the watchdog only. This is a control signal into the
# detector; it does not change CorrectionState, target, controller or renderer.
cpp = one(cpp,
'''                const bool rescueSearch = correction.noteBodyLatched
                    && correction.pitchStaleSamples >= static_cast<int>(0.060 * sampleRate_);
                tracker.setRange(rescueSearch ? std::min(safe.minimumPitchHz, 28.0f) : safe.minimumPitchHz,
''',
'''                const bool rescueSearch = correction.noteBodyLatched
                    && correction.pitchStaleSamples >= static_cast<int>(0.060 * sampleRate_);
                const bool detectorWake = correction.trackingState == TrackingState::transition
                    || (correction.trackingState == TrackingState::acquire
                        && correction.stateAgeSamples >= static_cast<int>(0.060 * sampleRate_));
                tracker.setTransitionWake(detectorWake);
                tracker.setRange(rescueSearch ? std::min(safe.minimumPitchHz, 28.0f) : safe.minimumPitchHz,
''',
'dual mono transition watchdog wiring')

cpp = one(cpp,
'''            const bool rescueSearch = linkedCorrection_.noteBodyLatched
                && linkedCorrection_.pitchStaleSamples >= static_cast<int>(0.060 * sampleRate_);
            linkedTracker_.setRange(rescueSearch ? std::min(safe.minimumPitchHz, 28.0f) : safe.minimumPitchHz,
''',
'''            const bool rescueSearch = linkedCorrection_.noteBodyLatched
                && linkedCorrection_.pitchStaleSamples >= static_cast<int>(0.060 * sampleRate_);
            const bool detectorWake = linkedCorrection_.trackingState == TrackingState::transition
                || (linkedCorrection_.trackingState == TrackingState::acquire
                    && linkedCorrection_.stateAgeSamples >= static_cast<int>(0.060 * sampleRate_));
            linkedTracker_.setTransitionWake(detectorWake);
            linkedTracker_.setRange(rescueSearch ? std::min(safe.minimumPitchHz, 28.0f) : safe.minimumPitchHz,
''',
'linked transition watchdog wiring')

# Existing wrong-register regression now explicitly exercises the transition
# watchdog rather than asking musical rescue memory to solve an observer error.
test = one(test,
'''    staleAnchorTracker->setRescueMode(true);
    staleAnchorTracker->trackedPitchHz_ = 110.0f;
''',
'''    staleAnchorTracker->setRescueMode(true);
    staleAnchorTracker->setTransitionWake(true);
    staleAnchorTracker->trackedPitchHz_ = 110.0f;
''',
'stale register uses transition wake')

# New regressions make the two memories explicit: true zero destroys detector
# history; the following tonal source is a fresh acquisition; noise under the
# watchdog is still allowed to mean "unknown F0".
test = one(test,
'''    ModernPitchEngine::CorrectionState zeroResponseTransition;
''',
'''    // OBSERVATION_MEMORY_SEPARATION_V1: exact digital absence clears detector
    // history immediately, but downstream target-hold invariants remain separate.
    auto zeroGapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    zeroGapTracker->prepare(48000.0);
    zeroGapTracker->setRange(70.0f, 700.0f);
    bool acquiredBeforeZero = false;
    for (int sample = 0; sample < 12000; ++sample)
    {
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * 220.0 * static_cast<double>(sample) / 48000.0);
        ModernPitchEngine::PitchObservation observed;
        if (zeroGapTracker->processSample(0.075f * std::sin(phase), observed)
            && observed.valid)
        {
            acquiredBeforeZero = true;
        }
    }
    zeroGapTracker->setRescueMode(true);
    zeroGapTracker->setReacquisitionAnchor(220.0f);
    ModernPitchEngine::PitchObservation zeroObserved;
    for (int sample = 0; sample < 512; ++sample)
        zeroGapTracker->processSample(0.0f, zeroObserved);
    const bool beamClearedByZero = std::none_of(
        zeroGapTracker->decoderBeam_.begin(), zeroGapTracker->decoderBeam_.end(),
        [](const auto& state) { return state.valid; });
    success &= check(acquiredBeforeZero
                     && zeroGapTracker->trackedPitchHz_ == 0.0f
                     && zeroGapTracker->reacquisitionAnchorHz_ == 0.0f
                     && beamClearedByZero
                     && zeroGapTracker->observationContinuityBroken_,
                     "exact_zero_clears_detector_observation_memory");
    zeroGapTracker->setReacquisitionAnchor(110.0f);
    success &= check(zeroGapTracker->reacquisitionAnchorHz_ == 0.0f,
                     "musical_anchor_cannot_reenter_detector_after_zero");

    int postZeroReacquiredAt = -1;
    constexpr float postZeroHz = 246.94165f;
    for (int sample = 0; sample < 12000; ++sample)
    {
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * static_cast<double>(postZeroHz) * static_cast<double>(sample) / 48000.0);
        ModernPitchEngine::PitchObservation observed;
        if (zeroGapTracker->processSample(0.075f * std::sin(phase), observed)
            && observed.valid)
        {
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / postZeroHz));
            if (cents < 45.0f)
            {
                postZeroReacquiredAt = sample;
                break;
            }
        }
    }
    success &= check(postZeroReacquiredAt >= 0 && postZeroReacquiredAt < 6000,
                     "post_zero_tone_is_fresh_detector_acquisition");

    auto watchdogNoiseTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    watchdogNoiseTracker->prepare(48000.0);
    watchdogNoiseTracker->setRange(70.0f, 700.0f);
    watchdogNoiseTracker->trackedPitchHz_ = 110.0f;
    watchdogNoiseTracker->trackedConfidence_ = 0.90f;
    watchdogNoiseTracker->trackedPeriodicity_ = 0.90f;
    watchdogNoiseTracker->setReacquisitionAnchor(110.0f);
    watchdogNoiseTracker->setTransitionWake(true);
    std::uint32_t watchdogRng = 0x4f1bbcdcu;
    float watchdogColored = 0.0f;
    int watchdogNoiseValid = 0;
    for (int sample = 0; sample < 18000; ++sample)
    {
        watchdogRng ^= watchdogRng << 13;
        watchdogRng ^= watchdogRng >> 17;
        watchdogRng ^= watchdogRng << 5;
        const float white = (static_cast<float>(watchdogRng & 0xffffu) / 32767.5f) - 1.0f;
        watchdogColored = 0.94f * watchdogColored + 0.06f * white;
        ModernPitchEngine::PitchObservation observed;
        if (watchdogNoiseTracker->processSample(
                0.03f * (0.45f * white + 0.55f * watchdogColored), observed)
            && sample > 4096 && observed.valid)
        {
            ++watchdogNoiseValid;
        }
    }
    success &= check(watchdogNoiseValid == 0,
                     "transition_watchdog_never_invents_f0_on_noise");

    ModernPitchEngine::CorrectionState zeroResponseTransition;
''',
'observation separation regressions')

hpp_path.write_text(hpp)
cpp_path.write_text(cpp)
test_path.write_text(test)
print('OBSERVATION_MEMORY_SEPARATION_V1 materialized')
