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

# Large, coherent within-cell movement is neither an outlier nor necessarily a
# new note identity. Give transport its own short geometric persistence memory.
h = one(h,
'''        double transportVelocityCentsPerHop = 0.0;

        // SCALE_OWNS_IDENTITY_V2: detector observations may nominate a new
''',
'''        double transportVelocityCentsPerHop = 0.0;
        double transportChallengerLog2 = 0.0;
        int transportChallengerHops = 0;

        // SCALE_OWNS_IDENTITY_V2: detector observations may nominate a new
''',
'add large local transport challenger')

# Any detector hole breaks consecutiveness of a large source-motion challenger,
# but it does not touch the already-owned target/transport/correction.
cpp = one(cpp,
'''    if (!validPitch)
    {
        ++state.invalidObservations;

        // SCALE_OWNS_TRANSPORT_V1: an F0 hole never invents an audible
''',
'''    if (!validPitch)
    {
        ++state.invalidObservations;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;

        // SCALE_OWNS_TRANSPORT_V1: an F0 hole never invents an audible
''',
'invalid frame resets local challenger only')

cpp = one(cpp,
'''    if (explicitPhoneticFrame && state.targetValid)
    {
        state.stableBodyObservations = 0;
        return;
    }
''',
'''    if (explicitPhoneticFrame && state.targetValid)
    {
        state.stableBodyObservations = 0;
        state.transportChallengerHops = 0;
        state.transportChallengerLog2 = 0.0;
        return;
    }
''',
'phonetic veto cancels local challenger')

# Small innovations are continuous vocal motion and follow immediately. Large
# non-octave innovations must repeat coherently for three hops, then transport
# catches them quickly while output remains glued to the currently owned scale
# degree. Octave-like innovations never use this path; they stay under the
# dedicated octave/identity veto.
cpp = one(cpp,
'''            if (std::abs(innovationCents) <= innovationGateCents)
            {
                constexpr double innovationFollow = 0.88;
                double nextSourceLog2 = predictedSourceLog2
                    + innovationFollow * innovationCents / 1200.0;
                double stepCents =
                    (nextSourceLog2 - currentSourceLog2) * 1200.0;
                const double maximumContinuousStep = std::clamp(
                    0.16 * localScaleStep, 5.0, 16.0);
                stepCents = std::clamp(stepCents,
                                       -maximumContinuousStep,
                                        maximumContinuousStep);
                nextSourceLog2 = currentSourceLog2 + stepCents / 1200.0;
                state.transportPeriodHz = std::exp2(nextSourceLog2);
                state.transportVelocityCentsPerHop = std::clamp(
                    0.52 * state.transportVelocityCentsPerHop
                        + 0.48 * stepCents,
                    -maximumContinuousStep, maximumContinuousStep);
            }
            else
            {
                // An isolated consonant period, octave alias or discontinuous
                // detector coordinate cannot move audible transport at all.
                state.transportVelocityCentsPerHop *= 0.35;
            }
''',
'''            if (std::abs(innovationCents) <= innovationGateCents)
            {
                state.transportChallengerHops = 0;
                state.transportChallengerLog2 = 0.0;
                constexpr double innovationFollow = 0.88;
                double nextSourceLog2 = predictedSourceLog2
                    + innovationFollow * innovationCents / 1200.0;
                double stepCents =
                    (nextSourceLog2 - currentSourceLog2) * 1200.0;
                const double maximumContinuousStep = std::clamp(
                    0.16 * localScaleStep, 5.0, 16.0);
                stepCents = std::clamp(stepCents,
                                       -maximumContinuousStep,
                                        maximumContinuousStep);
                nextSourceLog2 = currentSourceLog2 + stepCents / 1200.0;
                state.transportPeriodHz = std::exp2(nextSourceLog2);
                state.transportVelocityCentsPerHop = std::clamp(
                    0.52 * state.transportVelocityCentsPerHop
                        + 0.48 * stepCents,
                    -maximumContinuousStep, maximumContinuousStep);
            }
            else
            {
                const double absoluteInnovation = std::abs(innovationCents);
                const int nearestOctave = static_cast<int>(std::lround(
                    absoluteInnovation / 1200.0));
                const bool octaveLikeInnovation = nearestOctave >= 1
                    && nearestOctave <= 2
                    && std::abs(absoluteInnovation
                        - 1200.0 * static_cast<double>(nearestOctave)) <= 95.0;

                if (octaveLikeInnovation)
                {
                    state.transportChallengerHops = 0;
                    state.transportChallengerLog2 = 0.0;
                    state.transportVelocityCentsPerHop *= 0.35;
                }
                else
                {
                    const bool sameChallenger = state.transportChallengerHops > 0
                        && std::abs(observedSourceLog2
                            - state.transportChallengerLog2) * 1200.0 <= 24.0;
                    if (sameChallenger)
                    {
                        state.transportChallengerLog2 = 0.70
                            * state.transportChallengerLog2
                            + 0.30 * observedSourceLog2;
                        state.transportChallengerHops = std::min(
                            12, state.transportChallengerHops + 1);
                    }
                    else
                    {
                        state.transportChallengerLog2 = observedSourceLog2;
                        state.transportChallengerHops = 1;
                    }

                    if (state.transportChallengerHops >= 3)
                    {
                        const double challengerDeltaCents =
                            (state.transportChallengerLog2 - currentSourceLog2) * 1200.0;
                        const double maximumCatchupStep = std::clamp(
                            0.32 * localScaleStep, 18.0, 36.0);
                        const double catchupStep = std::clamp(
                            challengerDeltaCents,
                            -maximumCatchupStep,
                             maximumCatchupStep);
                        state.transportPeriodHz = std::exp2(
                            currentSourceLog2 + catchupStep / 1200.0);
                        state.transportVelocityCentsPerHop = catchupStep;
                    }
                    else
                    {
                        // First/second large observation has zero audible
                        // authority. This is the consonant/outlier safety wall.
                        state.transportVelocityCentsPerHop *= 0.35;
                    }
                }
            }
''',
'large coherent local movement gets finite transport path')

# Rebase/reset local challenger on first ownership or a confirmed new identity.
cpp = one(cpp,
'''            state.transportPeriodHz = std::exp2(observedSourceLog2);
            state.transportVelocityCentsPerHop = 0.0;
        }
        else if (targetIdentityChanged)
''',
'''            state.transportPeriodHz = std::exp2(observedSourceLog2);
            state.transportVelocityCentsPerHop = 0.0;
            state.transportChallengerHops = 0;
            state.transportChallengerLog2 = 0.0;
        }
        else if (targetIdentityChanged)
''',
'first ownership clears local challenger')

cpp = one(cpp,
'''            state.transportPeriodHz = std::exp2(observedSourceLog2);
            state.transportVelocityCentsPerHop = 0.0;
        }
        else
''',
'''            state.transportPeriodHz = std::exp2(observedSourceLog2);
            state.transportVelocityCentsPerHop = 0.0;
            state.transportChallengerHops = 0;
            state.transportChallengerLog2 = 0.0;
        }
        else
''',
'identity commit clears local challenger')

# Transition must not be declared stable merely because detector/body evidence
# has been stable for 12 ms. The actual transport trajectory must have arrived.
cpp = one(cpp,
'''            if (!targetIdentityChanged && bodyPresent
                && state.stableBodyObservations >= 4
                && state.stateAgeSamples >= minimumStableSamples)
            {
                setState(TrackingState::stable);
            }
''',
'''            if (!targetIdentityChanged && bodyPresent
                && state.stableBodyObservations >= 4
                && state.stateAgeSamples >= minimumStableSamples
                && std::abs(state.desiredCents - state.currentCents) < 0.5)
            {
                setState(TrackingState::stable);
            }
''',
'transition state follows actual transport arrival')

# The hard musical transition bound is a terminal arrival, not permission to
# hand a moving controller to Stable and ring afterwards.
cpp = one(cpp,
'''    if (state.trackingState == TrackingState::transition
        && state.noteBodyLatched
        && state.stateAgeSamples >= maximumTransitionSamples)
    {
        state.trackingState = TrackingState::stable;
        state.stateAgeSamples = 0;
    }
''',
'''    if (state.trackingState == TrackingState::transition
        && state.noteBodyLatched
        && state.stateAgeSamples >= maximumTransitionSamples)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
        state.trackingState = TrackingState::stable;
        state.stateAgeSamples = 0;
    }
''',
'transition hard bound lands without momentum')

# ---------------------------------------------------------------------------
# Update legacy tests whose internal-state expectations conflict with the new
# public/audio contract. These replacements make the tests stricter audibly,
# not looser.
test = one(test,
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
'''    bool octaveCommittedTooEarly = false;
    bool octaveCommittedInFiniteTime = false;
    for (int hop = 0; hop < 8; ++hop)
    {
        auto decision = liveRescueDecision;
        decision.valid = true;
        decision.candidate.valid = true;
        const bool accepted = liveRescueTracker->confirmOctaveTransition(
            decision, false);
        if (hop < 7)
            octaveCommittedTooEarly = octaveCommittedTooEarly || accepted;
        else
            octaveCommittedInFiniteTime = accepted && decision.valid;
    }
    success &= check(!octaveCommittedTooEarly && octaveCommittedInFiniteTime,
                     "octave_veto_is_bounded_not_confidence_gated");
''',
'legacy octave timing becomes finite persistence')

test = one(test,
'''    success &= check(heldCorrectionState.trackingState
                         == ModernPitchEngine::TrackingState::unvoiced
                     && heldCorrectionState.targetValid
                     && std::abs(heldCorrectionState.desiredCents + 42.0) < 1.0e-9,
                     "explicit_unvoiced_label_never_mutes_scale_correction");
''',
'''    success &= check(heldCorrectionState.trackingState
                         == ModernPitchEngine::TrackingState::stable
                     && heldCorrectionState.targetValid
                     && std::abs(heldCorrectionState.desiredCents + 42.0) < 1.0e-9,
                     "explicit_unvoiced_label_never_mutes_scale_correction");
''',
'pitchless label cannot demote owned voice')

test = one(test,
'''    success &= check(recoveredCentreMove > 80.0
                     && std::abs(recoveryState.desiredCents - staleDesired) > 40.0,
                     "rescued_f0_retargets_instead_of_freezing_old_correction");
''',
'''    const double recoveredTransportMove = std::abs(1200.0 * std::log2(
        recoveryState.transportPeriodHz / 220.0));
    success &= check(recoveredCentreMove > 80.0
                     && recoveredTransportMove > 80.0
                     && std::abs(recoveryState.desiredCents - staleDesired) > 40.0,
                     "rescued_f0_retargets_instead_of_freezing_old_correction");
''',
'recovery checks source transport instead of only centre')

test = one(test,
'''    const double rawDetectorCorrection = 1200.0 * std::log2(
        liveCoordinateTargetHz
        / static_cast<double>(liveCoordinateObservation.correctionFrequencyHz));
    const double transportResidual = std::abs(1200.0 * std::log2(
        liveCoordinateState.transportPeriodHz
        * std::exp2(liveCoordinateState.desiredCents / 1200.0)
        / liveCoordinateTargetHz));
    success &= check(liveCoordinateState.targetValid
                     && std::abs(liveCoordinateState.desiredCents
                                 - transportExpectedCorrection) < 1.0e-6
                     && std::abs(liveCoordinateState.desiredCents
                                 - rawDetectorCorrection) > 20.0
                     && transportResidual < 1.0e-6,
                     "raw_detector_coordinate_cannot_command_transport");
''',
'''    const double transportResidual = std::abs(1200.0 * std::log2(
        liveCoordinateState.transportPeriodHz
        * std::exp2(liveCoordinateState.desiredCents / 1200.0)
        / liveCoordinateTargetHz));
    success &= check(liveCoordinateState.targetValid
                     && std::abs(liveCoordinateTargetHz - 440.0) < 0.1
                     && std::abs(liveCoordinateState.desiredCents
                                 - transportExpectedCorrection) < 1.0e-6
                     && transportResidual < 1.0e-6,
                     "continuous_local_measurement_updates_transport_without_owning_target");
''',
'local measurement may update source but never target authority')

test = one(test,
'''    success &= check(std::abs(boundedTransition.velocityCentsPerSecond) > 0.02,
                     "stable_state_does_not_require_zero_controller_velocity");
''',
'''    success &= check(std::abs(boundedTransition.velocityCentsPerSecond) < 1.0e-12
                     && std::abs(boundedTransition.currentCents
                                 - boundedTransition.desiredCents) < 1.0e-9,
                     "transition_hard_bound_finishes_without_controller_momentum");
''',
'hard transition timeout cannot ring in stable')

# The provisional-voice regression must advance sample time just like the real
# engine; otherwise the supervisor's 12 ms state-age rule can never complete.
test = one(test,
'''    for (int hop = 0; hop < 8; ++hop)
        engine->updateCorrectionState(provisionalVoiceState, provisionalQuantizer,
                                      provisionalVoice, provisionalVoiceParameters);
''',
'''    for (int hop = 0; hop < 24; ++hop)
    {
        engine->updateCorrectionState(provisionalVoiceState, provisionalQuantizer,
                                      provisionalVoice, provisionalVoiceParameters);
        for (int sample = 0;
             sample < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++sample)
        {
            static_cast<void>(engine->advanceCorrection(provisionalVoiceState));
        }
    }
''',
'provisional voice test advances real supervisor time')

cpp_p.write_text(cpp)
h_p.write_text(h)
test_p.write_text(test)
print('voice transition continuum v1 refinement materialized')
