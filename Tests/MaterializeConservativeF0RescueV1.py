from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)

# -----------------------------------------------------------------------------
# Header: supervisor-only state. The detector and renderer receive no new path.
hp = Path('Source/ModernPitchEngine.h')
h = hp.read_text()

h = replace_once(h,
'''        [[nodiscard]] float minimumStepCents() const noexcept { return minStepCents_; }
        [[nodiscard]] float asymmetry() const noexcept { return asymmetry_; }
''',
'''        [[nodiscard]] float minimumStepCents() const noexcept { return minStepCents_; }
        [[nodiscard]] float asymmetry() const noexcept { return asymmetry_; }
        [[nodiscard]] double adjacentTargetLog2(double currentTargetLog2,
                                                int direction) const noexcept;
''',
'adjacent target declaration')

h = replace_once(h,
'''        double transportPeriodHz = 0.0;
        TrackingState trackingState = TrackingState::unvoiced;
''',
'''        double transportPeriodHz = 0.0;

        // CONSERVATIVE_F0_RESCUE_V1: short memory contains detector-derived
        // F0 only. Predicted coordinates are never fed back into the tracker.
        std::array<double, 10> recentRealPitchLog2 {};
        int recentRealPitchCount = 0;
        int rescueQualificationHops = 0;
        bool rescuePredictionActive = false;
        int rescuePredictionHops = 0;
        int rescueDirection = 0;
        bool rescueTargetShifted = false;
        double rescueBaseSourceLog2 = 0.0;
        double rescueSourceLog2 = 0.0;
        double rescueBaseTargetLog2 = 0.0;
        double rescueBaseDesiredCents = 0.0;
        double rescueSlopeCentsPerHop = 0.0;

        TrackingState trackingState = TrackingState::unvoiced;
''',
'rescue state')

h = replace_once(h,
'''    void updateCorrectionState(CorrectionState& state,
                               ScaleQuantizer& quantizer,
                               const PitchObservation& observation,
                               const Parameters& parameters) noexcept;
''',
'''    void updateCorrectionState(CorrectionState& state,
                               ScaleQuantizer& quantizer,
                               const PitchObservation& observation,
                               const Parameters& parameters) noexcept;
    [[nodiscard]] bool advanceConservativeF0Rescue(
        CorrectionState& state,
        ScaleQuantizer& quantizer,
        const PitchObservation& observation,
        const Parameters& parameters,
        bool bodyLikeFrame) noexcept;
''',
'rescue method declaration')

hp.write_text(h)

# -----------------------------------------------------------------------------
# Source.
cp = Path('Source/ModernPitchEngine.cpp')
s = cp.read_text()

# Exact adjacent degree from the already-loaded scale. This is not a new
# quantizer: it only asks the existing scale map for the immediate neighbour.
anchor = '''//==============================================================================
// ModernPitchEngine control and processing
//==============================================================================
'''
adjacent_impl = r'''double ModernPitchEngine::ScaleQuantizer::adjacentTargetLog2(
    double currentTargetLog2,
    int direction) const noexcept
{
    if (!std::isfinite(currentTargetLog2) || direction == 0 || ratioCount_ <= 0)
        return currentTargetLog2;

    const int sign = direction > 0 ? 1 : -1;
    const double relative = currentTargetLog2 - rootLog2_;
    const double octave = std::floor(relative);
    double best = currentTargetLog2;
    double bestDistance = std::numeric_limits<double>::infinity();

    for (int octaveOffset = -2; octaveOffset <= 2; ++octaveOffset)
    {
        for (int degreeIndex = 0; degreeIndex < ratioCount_; ++degreeIndex)
        {
            const double candidate = rootLog2_ + octave
                + static_cast<double>(octaveOffset)
                + logRatios_[static_cast<std::size_t>(degreeIndex)];
            const double signedDistance = static_cast<double>(sign)
                * (candidate - currentTargetLog2);
            if (signedDistance > 1.0e-8 && signedDistance < bestDistance)
            {
                bestDistance = signedDistance;
                best = candidate;
            }
        }
    }
    return best;
}

'''
if s.count(anchor) != 1:
    raise SystemExit('control section anchor mismatch')
s = s.replace(anchor, adjacent_impl + anchor, 1)

# Real F0 instantly terminates prediction. History itself remains available and
# is refreshed below only from real detector observations.
s = replace_once(s,
'''    if (validPitch)
        state.pitchStaleSamples = 0;
    else if (state.noteBodyLatched)
''',
'''    if (validPitch)
    {
        state.pitchStaleSamples = 0;
        state.rescueQualificationHops = 0;
        state.rescuePredictionActive = false;
        state.rescuePredictionHops = 0;
        state.rescueDirection = 0;
        state.rescueTargetShifted = false;
    }
    else if (state.noteBodyLatched)
''',
'real pitch cancels rescue')

# Existing voice evidence is the only rescue permission. Thresholds are
# deliberately restrictive: breath, phonetic event or unreliable spectrum fail
# closed to the already-safe target hold behaviour.
s = replace_once(s,
'''    const bool confirmedAbsenceFrame = richEvidence
        && !observation.audioPresent
        && parameters.voiceBodyEnergy < 0.20f
        && parameters.voiceHarmonicity < 0.22f
        && parameters.voiceSpectralReliability < 0.28f
        && parameters.voiceEventStrength < 0.72f;
''',
'''    const bool confirmedAbsenceFrame = richEvidence
        && !observation.audioPresent
        && parameters.voiceBodyEnergy < 0.20f
        && parameters.voiceHarmonicity < 0.22f
        && parameters.voiceSpectralReliability < 0.28f
        && parameters.voiceEventStrength < 0.72f;

    const bool rescueBodyFrame = richEvidence
        && observation.audioPresent
        && parameters.voiceBodyEnergy >= 0.34f
        && parameters.voiceHarmonicity >= 0.32f
        && parameters.voiceSpectralReliability >= 0.44f
        && parameters.voiceBreathiness <= 0.34f
        && parameters.voiceEventStrength <= 0.30f;
''',
'rescue body gate')

# Try rescue before the generic audio-present acquire hold. First qualifying hop
# only arms it; the second can activate. If it refuses, old behaviour is intact.
s = replace_once(s,
'''        // SOUND_EQUALS_CORRECTION_V1: audio presence owns the voice, but
        // Stable is forbidden until a real target exists. Acquire is now only
        // detector-search telemetry: an already acquired target/correction is
        // preserved exactly while F0 is temporarily missing.
        if (observation.audioPresent)
''',
'''        // CONSERVATIVE_F0_RESCUE_V1: prediction is an exceptional continuity
        // aid, never generic fallback. It must prove body-like non-phonetic
        // material over consecutive invalid hops and can live only briefly.
        if (advanceConservativeF0Rescue(state, quantizer, observation,
                                        parameters, rescueBodyFrame))
        {
            setState(TrackingState::acquire);
            return;
        }

        // SOUND_EQUALS_CORRECTION_V1: audio presence owns the voice, but
        // Stable is forbidden until a real target exists. Acquire is now only
        // detector-search telemetry: an already acquired target/correction is
        // preserved exactly while F0 is temporarily missing.
        if (observation.audioPresent)
''',
'call conservative rescue')

# Record only actual accepted source F0 and only on a clearly body-like frame.
s = replace_once(s,
'''    const double correctionObservedLog2 = safeLog2(correctionFrequencyHz);
    bool liveIdentityBreak = false;
''',
'''    const double correctionObservedLog2 = safeLog2(correctionFrequencyHz);

    if (rescueBodyFrame && !observation.onset)
    {
        if (state.recentRealPitchCount
            < static_cast<int>(state.recentRealPitchLog2.size()))
        {
            state.recentRealPitchLog2[static_cast<std::size_t>(
                state.recentRealPitchCount++)] = correctionObservedLog2;
        }
        else
        {
            for (std::size_t i = 1; i < state.recentRealPitchLog2.size(); ++i)
                state.recentRealPitchLog2[i - 1] = state.recentRealPitchLog2[i];
            state.recentRealPitchLog2.back() = correctionObservedLog2;
        }
    }

    bool liveIdentityBreak = false;
''',
'record real f0 history')

# A confirmed real note identity change invalidates old-note trajectory memory.
s = replace_once(s,
'''    const bool targetIdentityChanged = state.targetValid
        && std::abs(targetJump) >= identityThreshold;
    if (targetChanged)
''',
'''    const bool targetIdentityChanged = state.targetValid
        && std::abs(targetJump) >= identityThreshold;
    if (targetIdentityChanged || musicalOnset || liveIdentityBreak)
    {
        state.recentRealPitchCount = rescueBodyFrame ? 1 : 0;
        if (rescueBodyFrame)
            state.recentRealPitchLog2[0] = correctionObservedLog2;
    }
    if (targetChanged)
''',
'reset history on real transition')

# Insert conservative rescue implementation immediately before normal supervisor.
update_anchor = '''void ModernPitchEngine::updateCorrectionState(
'''
if s.count(update_anchor) != 1:
    raise SystemExit('updateCorrectionState anchor mismatch')
rescue_impl = r'''bool ModernPitchEngine::advanceConservativeF0Rescue(
    CorrectionState& state,
    ScaleQuantizer& quantizer,
    const PitchObservation& observation,
    const Parameters& parameters,
    bool bodyLikeFrame) noexcept
{
    constexpr int minimumHistory = 6;
    const int hopSamples = MultiRatePitchTracker::hopSize();
    const int maximumPredictionHops = std::max(2, static_cast<int>(std::ceil(
        0.024 * sampleRate_ / static_cast<double>(hopSamples))));

    const bool forbiddenState = state.trackingState == TrackingState::attack
        || state.trackingState == TrackingState::transition
        || state.trackingState == TrackingState::release
        || state.trackingState == TrackingState::unvoiced;
    const bool continuationOfQualifiedDropout = state.rescueQualificationHops > 0
        || state.rescuePredictionActive;
    const bool eligible = observation.audioPresent
        && state.targetValid
        && state.noteBodyLatched
        && bodyLikeFrame
        && !observation.onset
        && !forbiddenState
        && state.recentRealPitchCount >= minimumHistory
        && (state.trackingState == TrackingState::stable
            || continuationOfQualifiedDropout);

    if (!eligible)
    {
        state.rescueQualificationHops = 0;
        state.rescuePredictionActive = false;
        state.rescuePredictionHops = 0;
        state.rescueDirection = 0;
        state.rescueTargetShifted = false;
        return false;
    }

    if (!state.rescuePredictionActive)
    {
        ++state.rescueQualificationHops;
        if (state.rescueQualificationHops < 2)
            return false;

        const int count = state.recentRealPitchCount;
        const double first = state.recentRealPitchLog2[0];
        const double last = state.recentRealPitchLog2[static_cast<std::size_t>(count - 1)];
        double minimum = first;
        double maximum = first;
        int positiveSteps = 0;
        int negativeSteps = 0;
        int signChanges = 0;
        int previousSign = 0;
        double lastStepCents = 0.0;

        for (int i = 1; i < count; ++i)
        {
            const double value = state.recentRealPitchLog2[static_cast<std::size_t>(i)];
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            const double stepCents = (value
                - state.recentRealPitchLog2[static_cast<std::size_t>(i - 1)]) * 1200.0;
            lastStepCents = stepCents;
            const int sign = stepCents > 0.6 ? 1 : (stepCents < -0.6 ? -1 : 0);
            if (sign > 0)
                ++positiveSteps;
            else if (sign < 0)
                ++negativeSteps;
            if (sign != 0)
            {
                if (previousSign != 0 && sign != previousSign)
                    ++signChanges;
                previousSign = sign;
            }
        }

        const double degreeCents = std::min(100.0,
            std::max(0.1, static_cast<double>(quantizer.minimumStepCents())));
        const double netCents = (last - first) * 1200.0;
        const double rangeCents = (maximum - minimum) * 1200.0;
        const int activeSteps = positiveSteps + negativeSteps;
        const int direction = netCents > 0.0 ? 1 : (netCents < 0.0 ? -1 : 0);
        const int consistentSteps = direction > 0 ? positiveSteps : negativeSteps;
        const double signConsistency = activeSteps > 0
            ? static_cast<double>(consistentSteps) / static_cast<double>(activeSteps)
            : 0.0;
        const double fromCentreCents = state.pitchCentreValid
            ? (last - state.pitchCentreLog2) * 1200.0 : 0.0;
        const bool nearBoundary = direction > 0
            ? fromCentreCents >= 0.32 * degreeCents
            : direction < 0 && fromCentreCents <= -0.32 * degreeCents;
        const bool strongDirectionalHistory = direction != 0
            && std::abs(netCents) >= 0.30 * degreeCents
            && signConsistency >= 0.80
            && signChanges <= 1
            && (nearBoundary || std::abs(netCents) >= 0.60 * degreeCents)
            && rangeCents >= 0.34 * degreeCents;

        state.rescueDirection = strongDirectionalHistory ? direction : 0;
        const double averageSlope = netCents
            / static_cast<double>(std::max(1, count - 1));
        const double slopeLimit = state.rescueDirection == 0
            ? std::min(4.0, 0.08 * degreeCents)
            : std::min(8.0, 0.12 * degreeCents);
        const double requestedSlope = state.rescueDirection == 0
            ? lastStepCents : averageSlope;
        state.rescueSlopeCentsPerHop = std::clamp(
            requestedSlope, -slopeLimit, slopeLimit);
        if (state.rescueDirection > 0)
            state.rescueSlopeCentsPerHop = std::max(0.0, state.rescueSlopeCentsPerHop);
        else if (state.rescueDirection < 0)
            state.rescueSlopeCentsPerHop = std::min(0.0, state.rescueSlopeCentsPerHop);

        state.rescueBaseSourceLog2 = last;
        state.rescueSourceLog2 = last;
        state.rescueBaseTargetLog2 = state.targetLog2;
        state.rescueBaseDesiredCents = state.desiredCents;
        state.rescuePredictionHops = 0;
        state.rescueTargetShifted = false;
        state.rescuePredictionActive = true;

        // A scale move is permitted only after a strongly directional real-F0
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
                ++state.revision;
                state.lastTargetJumpCents = jumpCents;
            }
        }
    }

    if (++state.rescuePredictionHops > maximumPredictionHops)
    {
        state.rescueQualificationHops = 0;
        state.rescuePredictionActive = false;
        state.rescuePredictionHops = 0;
        state.rescueDirection = 0;
        state.rescueTargetShifted = false;
        return false;
    }

    const double degreeCents = std::min(100.0,
        std::max(0.1, static_cast<double>(quantizer.minimumStepCents())));
    const double decay = state.rescueDirection == 0
        ? std::pow(0.55, static_cast<double>(state.rescuePredictionHops - 1))
        : std::pow(0.82, static_cast<double>(state.rescuePredictionHops - 1));
    const double stepCents = state.rescueSlopeCentsPerHop * decay;
    const double proposedLog2 = state.rescueSourceLog2 + stepCents / 1200.0;
    const double proposedDeltaCents = (proposedLog2 - state.rescueBaseSourceLog2) * 1200.0;
    const double maximumDeltaCents = (state.rescueDirection == 0 ? 0.20 : 0.75)
        * degreeCents;
    const double boundedDeltaCents = std::clamp(
        proposedDeltaCents, -maximumDeltaCents, maximumDeltaCents);
    state.rescueSourceLog2 = state.rescueBaseSourceLog2
        + boundedDeltaCents / 1200.0;

    const double targetDeltaCents = (state.targetLog2
        - state.rescueBaseTargetLog2) * 1200.0;
    const double sourceDeltaCents = (state.rescueSourceLog2
        - state.rescueBaseSourceLog2) * 1200.0;
    const double amount = static_cast<double>(clamp01(parameters.amount));
    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    state.desiredCents = std::clamp(
        state.rescueBaseDesiredCents
            + amount * (targetDeltaCents - sourceDeltaCents),
        -maximumCents, maximumCents);
    return true;
}

'''
s = s.replace(update_anchor, rescue_impl + update_anchor, 1)

cp.write_text(s)

# -----------------------------------------------------------------------------
# Tests: update obsolete Response=0 expectation and add strict rescue invariants.
tp = Path('Tests/SupervisorContinuityTest.cpp')
t = tp.read_text()
t = replace_once(t,
'''    success &= check(transitionResponse > 8.0 && transitionResponse < 32.1,
                     "target_revision_uses_bounded_single_path_transition");
''',
'''    success &= check(std::abs(transitionResponse) < 1.0e-12,
                     "response_zero_remains_literal_across_target_revision");
''',
'obsolete response test')

insert = r'''
    // CONSERVATIVE_F0_RESCUE_V1: rescue requires a real-F0 history plus two
    // consecutive body-like invalid hops. It may not activate on breath,
    // phonetic events or an already-active transition.
    std::array<double, 12> rescueChromatic {};
    for (int degree = 0; degree < 12; ++degree)
        rescueChromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    ModernPitchEngine::ScaleQuantizer rescueQuantizer;
    rescueQuantizer.reset();
    rescueQuantizer.setScale(rescueChromatic.data(),
                             static_cast<int>(rescueChromatic.size()), 440.0);
    ModernPitchEngine::Parameters rescueParameters;
    setBodyEvidence(rescueParameters);
    rescueParameters.scaleLock = true;
    rescueParameters.lockHysteresis = 0.0f;
    rescueParameters.amount = 1.0f;
    rescueParameters.retuneTimeMs = 0.0f;
    rescueParameters.humanize = 0.0f;
    rescueParameters.vibratoPreserve = 0.0f;
    rescueParameters.maximumCorrectionSemitones = 24.0f;

    const auto makeRescueState = [](const std::array<double, 6>& frequencies)
    {
        ModernPitchEngine::CorrectionState state;
        state.targetValid = true;
        state.targetLog2 = std::log2(440.0);
        state.pitchCentreValid = true;
        state.pitchCentreLog2 = std::log2(440.0);
        state.noteBodyLatched = true;
        state.noteBodyConfidence = 0.95f;
        state.trackingState = ModernPitchEngine::TrackingState::stable;
        state.stableBodyObservations = 12;
        state.recentRealPitchCount = static_cast<int>(frequencies.size());
        for (std::size_t i = 0; i < frequencies.size(); ++i)
            state.recentRealPitchLog2[i] = std::log2(frequencies[i]);
        const double last = frequencies.back();
        state.desiredCents = 1200.0 * std::log2(440.0 / last);
        state.currentCents = state.desiredCents;
        return state;
    };
    ModernPitchEngine::PitchObservation rescueHole;
    rescueHole.audioPresent = true;
    rescueHole.valid = false;
    rescueHole.onset = false;

    auto risingRescue = makeRescueState({438.0, 440.5, 443.0, 446.0, 449.0, 452.0});
    engine->updateCorrectionState(risingRescue, rescueQuantizer,
                                  rescueHole, rescueParameters);
    success &= check(!risingRescue.rescuePredictionActive
                     && risingRescue.rescueQualificationHops == 1,
                     "rescue_requires_consecutive_invalid_body_frames");
    engine->updateCorrectionState(risingRescue, rescueQuantizer,
                                  rescueHole, rescueParameters);
    const double risingTargetHz = std::exp2(risingRescue.targetLog2);
    success &= check(risingRescue.rescuePredictionActive
                     && risingRescue.rescueDirection == 1
                     && risingTargetHz > 460.0 && risingTargetHz < 472.0,
                     "strong_rising_history_may_choose_only_adjacent_upper_degree");

    auto vibratoRescue = makeRescueState({440.0, 445.0, 439.5, 444.0, 440.5, 443.0});
    engine->updateCorrectionState(vibratoRescue, rescueQuantizer,
                                  rescueHole, rescueParameters);
    engine->updateCorrectionState(vibratoRescue, rescueQuantizer,
                                  rescueHole, rescueParameters);
    success &= check(vibratoRescue.rescuePredictionActive
                     && vibratoRescue.rescueDirection == 0
                     && std::abs((vibratoRescue.targetLog2 - std::log2(440.0)) * 1200.0) < 0.1,
                     "oscillating_history_rescues_same_scale_degree");

    auto fallingRescue = makeRescueState({442.0, 439.5, 437.0, 434.0, 431.0, 428.0});
    fallingRescue.pitchCentreLog2 = std::log2(440.0);
    engine->updateCorrectionState(fallingRescue, rescueQuantizer,
                                  rescueHole, rescueParameters);
    engine->updateCorrectionState(fallingRescue, rescueQuantizer,
                                  rescueHole, rescueParameters);
    const double fallingTargetHz = std::exp2(fallingRescue.targetLog2);
    success &= check(fallingRescue.rescuePredictionActive
                     && fallingRescue.rescueDirection == -1
                     && fallingTargetHz > 410.0 && fallingTargetHz < 420.0,
                     "strong_falling_history_may_choose_only_adjacent_lower_degree");

    auto breathRescue = makeRescueState({438.0, 440.5, 443.0, 446.0, 449.0, 452.0});
    ModernPitchEngine::Parameters breathRescueParameters = rescueParameters;
    setBreathEvidence(breathRescueParameters);
    engine->updateCorrectionState(breathRescue, rescueQuantizer,
                                  rescueHole, breathRescueParameters);
    engine->updateCorrectionState(breathRescue, rescueQuantizer,
                                  rescueHole, breathRescueParameters);
    success &= check(!breathRescue.rescuePredictionActive
                     && breathRescue.rescueQualificationHops == 0,
                     "breath_cannot_activate_f0_prediction");

    auto consonantRescue = makeRescueState({438.0, 440.5, 443.0, 446.0, 449.0, 452.0});
    ModernPitchEngine::Parameters consonantParameters = rescueParameters;
    consonantParameters.voiceEventStrength = 0.92f;
    consonantParameters.voiceHarmonicity = 0.18f;
    engine->updateCorrectionState(consonantRescue, rescueQuantizer,
                                  rescueHole, consonantParameters);
    engine->updateCorrectionState(consonantRescue, rescueQuantizer,
                                  rescueHole, consonantParameters);
    success &= check(!consonantRescue.rescuePredictionActive,
                     "phonetic_event_cannot_activate_f0_prediction");

    auto transitionRescue = makeRescueState({438.0, 440.5, 443.0, 446.0, 449.0, 452.0});
    transitionRescue.trackingState = ModernPitchEngine::TrackingState::transition;
    engine->updateCorrectionState(transitionRescue, rescueQuantizer,
                                  rescueHole, rescueParameters);
    engine->updateCorrectionState(transitionRescue, rescueQuantizer,
                                  rescueHole, rescueParameters);
    success &= check(!transitionRescue.rescuePredictionActive,
                     "active_transition_cannot_start_f0_prediction");

    for (int hop = 0; hop < 60; ++hop)
        engine->updateCorrectionState(risingRescue, rescueQuantizer,
                                      rescueHole, rescueParameters);
    success &= check(!risingRescue.rescuePredictionActive,
                     "f0_prediction_has_hard_short_time_limit");

    auto realReturns = makeRescueState({438.0, 440.5, 443.0, 446.0, 449.0, 452.0});
    engine->updateCorrectionState(realReturns, rescueQuantizer,
                                  rescueHole, rescueParameters);
    engine->updateCorrectionState(realReturns, rescueQuantizer,
                                  rescueHole, rescueParameters);
    auto returnedRealPitch = strongPitch(454.0f);
    returnedRealPitch.audioPresent = true;
    returnedRealPitch.correctionFrequencyHz = 454.0f;
    engine->updateCorrectionState(realReturns, rescueQuantizer,
                                  returnedRealPitch, rescueParameters);
    success &= check(!realReturns.rescuePredictionActive
                     && realReturns.rescueQualificationHops == 0,
                     "real_f0_immediately_cancels_prediction");
'''

end = '''    return success ? 0 : 1;\n}'''
if t.count(end) != 1:
    raise SystemExit('test end anchor mismatch')
t = t.replace(end, insert + '\n' + end, 1)
tp.write_text(t)

print('CONSERVATIVE_F0_RESCUE_V1_MATERIALIZED')
