from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


def region(text, start, end, replacement, label):
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"{label}: start marker not found")
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f"{label}: end marker not found")
    return text[:a] + replacement + text[b:]


cpp_p = Path('Source/ModernPitchEngine.cpp')
test_p = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_p.read_text()
test = test_p.read_text()

# A detector hole is not permission to synthesize a new source coordinate.
# Keep searching in the detector, but the audible correction is held exactly.
cpp = one(cpp,
'''        // CONSERVATIVE_F0_RESCUE_V1: prediction is an exceptional continuity
        // aid, never generic fallback. It must prove body-like non-phonetic
        // material over consecutive invalid hops and can live only briefly.
        if (advanceConservativeF0Rescue(state, quantizer, observation,
                                        parameters, rescueBodyFrame))
        {
            setState(state.rescueTargetShifted
                ? TrackingState::transition
                : TrackingState::stable);
            return;
        }

''',
'''        // SCALE_OWNS_TRANSPORT_V1: an F0 hole never invents an audible
        // source coordinate. Detector search/reacquisition may continue, but
        // target, transportPeriodHz and desiredCents remain exactly held until
        // a real non-vetoed measurement returns.
        state.rescueQualificationHops = 0;
        state.rescuePredictionActive = false;
        state.rescuePredictionHops = 0;
        state.rescueDirection = 0;
        state.rescueTargetShifted = false;

''',
'no predicted audible source during detector hole')

# Positive phonetic evidence is a veto on updating musical/transport state,
# never a command to dry/bypass. This is the consonant failure mode: a formally
# valid but meaningless periodic accident must not touch correction.
cpp = one(cpp,
'''    // VALID_F0_OUTRANKS_LABEL_V1: once the detector has produced a finite valid
    // F0, a secondary breath/phonetic label cannot erase that coordinate.
    // Falsification belongs in the detector/register logic; confidence and
    // descriptive voice labels never become permission to correct.

    // A strong breath/absence before any body latch must not become a note just
''',
'''    // PHONETIC_VETO_HOLDS_TRANSPORT_V1: a formally valid detector period on
    // a consonant/breathy event is still not a musical source coordinate.
    // Positive phonetic evidence may veto this observation, but it cannot
    // attenuate, release or otherwise modify an already-owned correction.
    if (explicitPhoneticFrame)
    {
        state.stableBodyObservations = 0;
        if (!state.targetValid)
            setState(TrackingState::acquire);
        return;
    }

    // A strong breath/absence before any body latch must not become a note just
''',
'phonetic veto holds transport')

# Replace direct raw-F0 note snapping with a bounded musical centre. A single
# detector outlier can move the observer centre only a small fraction of a
# degree. Once that centre has genuinely exited the owned scale cell, the
# current real observation may select WHICH exact scale degree wins; it never
# becomes an arbitrary output pitch.
cpp = region(cpp,
'''    bool liveIdentityBreak = false;
    bool forceTargetSwitch = false;
''',
'''    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);
''',
'''    bool liveIdentityBreak = false;
    bool forceTargetSwitch = false;
    if (!state.pitchCentreValid || musicalOnset)
    {
        state.pitchCentreLog2 = observedLog2;
        state.pitchCentreValid = true;
        state.stableObservations = 0;
    }
    else
    {
        const double distanceCents = std::abs(observedLog2 - state.pitchCentreLog2) * 1200.0;
        const double scaleStep = std::max(0.1,
            static_cast<double>(quantizer.minimumStepCents()));
        const double maximumWithinNoteTolerance = std::clamp(
            0.42 * scaleStep, 0.5, 60.0);
        const double withinNoteTolerance = std::min(
            22.0 + 38.0 * static_cast<double>(humanize),
            maximumWithinNoteTolerance);
        double baseAlpha = distanceCents > 95.0 ? 0.30 : 0.07;
        const double observedDistanceFromCurrentTarget = state.targetValid
            ? std::abs(observedLog2 - state.targetLog2) * 1200.0
            : 0.0;
        const double currentIdentityRadius = 0.48 * scaleStep;
        const bool insideCurrentMusicalIdentity = !state.targetValid
            || observedDistanceFromCurrentTarget < currentIdentityRadius;

        if (state.noteBodyLatched
            && insideCurrentMusicalIdentity
            && distanceCents <= withinNoteTolerance)
        {
            baseAlpha = 0.018 + 0.035 * static_cast<double>(1.0f - humanize);
        }

        // SCALE_OWNS_IDENTITY_V1: continuity is geometric and bounded per hop.
        // No confidence/consensus gate exists, but neither can one arbitrary
        // detector coordinate jump the centre across a scale cell.
        constexpr double continuityRate = 0.90;
        const double requestedCentreStepCents = baseAlpha * continuityRate
            * (observedLog2 - state.pitchCentreLog2) * 1200.0;
        const double maximumCentreStepCents = std::clamp(
            0.12 * scaleStep, 2.0, 12.0);
        const double boundedCentreStepCents = std::clamp(
            requestedCentreStepCents,
            -maximumCentreStepCents,
             maximumCentreStepCents);
        state.pitchCentreLog2 += boundedCentreStepCents / 1200.0;
        ++state.stableObservations;

        // A new note is acknowledged only after the persistent musical centre,
        // not a raw detector hop, has left the currently owned cell. At that
        // point the scale quantizer may use the live observation solely to pick
        // the destination degree; the resulting destination is still on-scale.
        if (state.targetValid)
        {
            const double centreDistanceFromTarget =
                std::abs(state.pitchCentreLog2 - state.targetLog2) * 1200.0;
            const double confirmedExitRadius = 0.52 * scaleStep;
            if (centreDistanceFromTarget >= confirmedExitRadius)
            {
                liveIdentityBreak = true;
                forceTargetSwitch = true;
            }
        }
    }

''',
'bounded scale-owned identity')

cpp = one(cpp,
'''    // CONTINUITY_VETO_NOT_PERMISSION_V1: target identity is read from the
    // deterministic continuity centre, not from detector confidence. This is
    // a bounded anti-vibrato falsification stage; it cannot remain stuck merely
    // because confidence/consensus are low. User Hold remains the only target
    // retention control inside ScaleQuantizer.
    const double targetSelectionLog2 = state.pitchCentreLog2;
''',
'''    // SCALE_OWNS_IDENTITY_V1: ordinary selection follows the persistent
    // musical centre. Only after that centre has proven a cell exit may the
    // current observation choose the nearest exact destination degree.
    const double targetSelectionLog2 = forceTargetSwitch
        ? observedLog2 : state.pitchCentreLog2;
''',
'target selector uses raw coordinate only after confirmed cell exit')

# Keep the post-commit rebase: it is now reachable only after the bounded centre
# has confirmed a real cell exit. It affects identity memory, not audible source
# transport, which is independently bounded below.

# Replace the old period model, which snapped to raw F0 on note transitions,
# with the persistent audible-source trajectory. It follows smooth vocal motion
# fast enough for vibrato correction, freezes on far unconfirmed challengers,
# and can never jump by an arbitrary detector interval in one hop.
cpp = region(cpp,
'''    // The period model follows musical identity, not vibrato-rate detector
''',
'''    const double vibratoComponent = observedLog2 - state.pitchCentreLog2;
''',
'''    // SCALE_OWNS_TRANSPORT_V1: the audible source coordinate is persistent
    // supervisor state. Detector F0 is an observation of it, never a direct
    // renderer command. Far challengers are frozen until musical identity has
    // actually changed; accepted motion is bandwidth/slew bounded.
    if (state.noteBodyLatched && bodyPresent)
    {
        const double observedSourceLog2 = observedLog2;
        if (!(state.transportPeriodHz > 0.0)
            || !std::isfinite(state.transportPeriodHz)
            || firstOwnedTarget)
        {
            state.transportPeriodHz = std::exp2(observedSourceLog2);
        }
        else
        {
            const double currentSourceLog2 = safeLog2(state.transportPeriodHz);
            const double localScaleStep = std::max(0.1,
                static_cast<double>(quantizer.minimumStepCents()));
            const double observedDistanceFromOwnedTarget = state.targetValid
                ? std::abs(observedSourceLog2 - state.targetLog2) * 1200.0
                : 0.0;
            const bool unconfirmedFarChallenger = state.targetValid
                && !targetIdentityChanged
                && !liveIdentityBreak
                && observedDistanceFromOwnedTarget >= 0.72 * localScaleStep;

            if (!unconfirmedFarChallenger)
            {
                const double sourceDeltaCents =
                    (observedSourceLog2 - currentSourceLog2) * 1200.0;
                constexpr double sourceFollow = 0.35;
                const double maximumSourceStepCents = targetIdentityChanged
                    ? 8.0 : 4.0;
                const double boundedSourceStepCents = std::clamp(
                    sourceFollow * sourceDeltaCents,
                    -maximumSourceStepCents,
                     maximumSourceStepCents);
                state.transportPeriodHz = std::exp2(
                    currentSourceLog2 + boundedSourceStepCents / 1200.0);
            }
        }
    }

    const double audibleSourceLog2 = state.transportPeriodHz > 0.0
        && std::isfinite(state.transportPeriodHz)
        ? safeLog2(state.transportPeriodHz)
        : correctionObservedLog2;

''',
'persistent audible source trajectory')

cpp = one(cpp,
'''    const double sourceOffsetCents =
        (correctionObservedLog2 - state.targetLog2) * 1200.0;
''',
'''    const double sourceOffsetCents =
        (audibleSourceLog2 - state.targetLog2) * 1200.0;
''',
'audible source owns softness coordinate')

cpp = one(cpp,
'''    double errorCents = (correctedLog2 - correctionObservedLog2) * 1200.0;
''',
'''    double errorCents = (correctedLog2 - audibleSourceLog2) * 1200.0;
''',
'audible source owns correction coordinate')

# Replace the old raw live-correction tests with the new authority contract.
test = region(test,
'''    // LIVE_CORRECTION_COORDINATE_V5: continuity smoothing may lag the
''',
'''    // AUTHORITY_CONTROLS_EXPLICIT_V1: the six visible controls define the
''',
'''    // SCALE_OWNS_TRANSPORT_V1: correctionFrequencyHz is detector diagnostic
    // data only. A raw candidate that disagrees with the persistent tracker
    // coordinate must not command the audible correction.
    ModernPitchEngine::ScaleQuantizer liveCoordinateQuantizer;
    liveCoordinateQuantizer.reset();
    const double liveCoordinateUnison = 1.0;
    liveCoordinateQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState liveCoordinateState;
    auto liveCoordinateObservation = strongPitch(445.0f);
    liveCoordinateObservation.audioPresent = true;
    liveCoordinateObservation.correctionFrequencyHz = 452.0f;
    engine->updateCorrectionState(liveCoordinateState,
                                  liveCoordinateQuantizer,
                                  liveCoordinateObservation,
                                  absoluteLockParameters);
    const double liveCoordinateTargetHz = std::exp2(
        liveCoordinateState.targetLog2);
    const double transportExpectedCorrection = 1200.0 * std::log2(
        liveCoordinateTargetHz / liveCoordinateState.transportPeriodHz);
    const double rawDetectorCorrection = 1200.0 * std::log2(
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

    // Existing softness remains target-owned, but it is now measured from the
    // same persistent transport coordinate rather than the raw detector hop.
    ModernPitchEngine::Parameters softCoordinateParameters = absoluteLockParameters;
    softCoordinateParameters.humanize = 0.25f;
    ModernPitchEngine::ScaleQuantizer softCoordinateQuantizer;
    softCoordinateQuantizer.reset();
    softCoordinateQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState softCoordinateState;
    engine->updateCorrectionState(softCoordinateState,
                                  softCoordinateQuantizer,
                                  liveCoordinateObservation,
                                  softCoordinateParameters);
    const double softOutputHz = softCoordinateState.transportPeriodHz
        * std::exp2(softCoordinateState.desiredCents / 1200.0);
    const double softResidualCents = std::abs(
        1200.0 * std::log2(softOutputHz / liveCoordinateTargetHz));
    success &= check(std::abs(softCoordinateState.desiredCents) > 0.5
                     && softResidualCents < 18.1,
                     "soft_scale_lock_uses_persistent_transport_inside_target_cell");

''',
'raw coordinate tests become transport authority tests')

# Exact authority expectation now uses persistent transport, not the raw
# correctionFrequencyHz diagnostic.
test = one(test,
'''    const double explicitExpectedCents = 1200.0 * std::log2(
        explicitTargetHz / static_cast<double>(explicitAuthorityObservation.correctionFrequencyHz));
''',
'''    const double explicitExpectedCents = 1200.0 * std::log2(
        explicitTargetHz / explicitAuthorityState.transportPeriodHz);
''',
'explicit exact authority uses persistent transport')

# Add regressions for valid-but-phonetic outliers and single-hop far detector
# outliers. Neither may change target or correction. A sustained real change
# must still reach its exact scale degree in bounded time.
anchor = '''    success &= check(explicitAuthorityState.trackingState
                         == ModernPitchEngine::TrackingState::stable
                     && std::abs(explicitAuthorityState.desiredCents - heldExplicitCents) < 1.0e-9
                     && std::abs(dropoutController - heldExplicitCents) < 1.0e-9,
                     "transient_f0_hole_keeps_authoritative_wet_correction");
'''
insert = anchor + '''

    // A formally valid F0 on a consonant is not allowed to touch transport.
    ModernPitchEngine::Parameters phoneticHoldParameters = explicitAuthorityParameters;
    setBodyEvidence(phoneticHoldParameters);
    phoneticHoldParameters.voiceEventStrength = 0.95f;
    phoneticHoldParameters.voiceHarmonicity = 0.18f;
    auto phoneticOutlier = strongPitch(760.0f);
    phoneticOutlier.audioPresent = true;
    phoneticOutlier.correctionFrequencyHz = 760.0f;
    const double prePhoneticTarget = explicitAuthorityState.targetLog2;
    const double prePhoneticTransport = explicitAuthorityState.transportPeriodHz;
    const double prePhoneticCents = explicitAuthorityState.desiredCents;
    engine->updateCorrectionState(explicitAuthorityState,
                                  explicitAuthorityQuantizer,
                                  phoneticOutlier,
                                  phoneticHoldParameters);
    success &= check(std::abs(explicitAuthorityState.targetLog2 - prePhoneticTarget) < 1.0e-12
                     && std::abs(explicitAuthorityState.transportPeriodHz - prePhoneticTransport) < 1.0e-12
                     && std::abs(explicitAuthorityState.desiredCents - prePhoneticCents) < 1.0e-12,
                     "valid_phonetic_outlier_cannot_touch_transport");

    // A single far detector hop cannot directly revise note identity or source
    // transport. Persistent non-phonetic evidence can still produce a bounded
    // real note change, whose destination remains an exact scale degree.
    std::array<double, 12> authorityChromatic {};
    for (int degree = 0; degree < 12; ++degree)
        authorityChromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    ModernPitchEngine::ScaleQuantizer boundedOutlierQuantizer;
    boundedOutlierQuantizer.reset();
    boundedOutlierQuantizer.setScale(authorityChromatic.data(),
                                     static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState boundedOutlierState;
    auto base440 = strongPitch(440.0f);
    base440.audioPresent = true;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(boundedOutlierState,
                                      boundedOutlierQuantizer,
                                      base440,
                                      explicitAuthorityParameters);
    const double boundedTargetBefore = boundedOutlierState.targetLog2;
    const double boundedTransportBefore = boundedOutlierState.transportPeriodHz;
    const double boundedCentsBefore = boundedOutlierState.desiredCents;
    auto oneHopOutlier = strongPitch(493.8833f);
    oneHopOutlier.audioPresent = true;
    oneHopOutlier.correctionFrequencyHz = 493.8833f;
    engine->updateCorrectionState(boundedOutlierState,
                                  boundedOutlierQuantizer,
                                  oneHopOutlier,
                                  explicitAuthorityParameters);
    success &= check(std::abs(boundedOutlierState.targetLog2 - boundedTargetBefore) < 1.0e-12
                     && std::abs(boundedOutlierState.transportPeriodHz - boundedTransportBefore) < 1.0e-12
                     && std::abs(boundedOutlierState.desiredCents - boundedCentsBefore) < 1.0e-9,
                     "single_far_f0_outlier_has_zero_audible_authority");

    for (int hop = 0; hop < 24; ++hop)
        engine->updateCorrectionState(boundedOutlierState,
                                      boundedOutlierQuantizer,
                                      oneHopOutlier,
                                      explicitAuthorityParameters);
    const double sustainedTargetHz = std::exp2(boundedOutlierState.targetLog2);
    const double sustainedTransportOutputHz = boundedOutlierState.transportPeriodHz
        * std::exp2(boundedOutlierState.desiredCents / 1200.0);
    success &= check(std::abs(sustainedTargetHz - 493.8833) < 0.4
                     && std::abs(1200.0 * std::log2(
                         sustainedTransportOutputHz / sustainedTargetHz)) < 1.0e-6,
                     "sustained_real_change_reaches_exact_scale_degree_without_raw_snap");
'''
test = one(test, anchor, insert, 'transport/outlier regressions')

# Prediction itself is no longer part of the audible state machine. Replace the
# old rescue-activation suite with the stronger invariant: detector holes hold
# target, source transport and correction exactly, regardless of prior slope.
test = region(test,
'''    // CONSERVATIVE_F0_RESCUE_V1: rescue requires a real-F0 history plus two
''',
'''    return success ? 0 : 1;
''',
'''    // SCALE_OWNS_TRANSPORT_V1: no detector-hole prediction is audible.
    std::array<double, 12> rescueChromatic {};
    for (int degree = 0; degree < 12; ++degree)
        rescueChromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    ModernPitchEngine::ScaleQuantizer rescueQuantizer;
    rescueQuantizer.reset();
    rescueQuantizer.setScale(rescueChromatic.data(),
                             static_cast<int>(rescueChromatic.size()), 440.0);
    ModernPitchEngine::Parameters rescueParameters = explicitAuthorityParameters;
    setBodyEvidence(rescueParameters);
    ModernPitchEngine::CorrectionState noPredictionState;
    auto noPredictionVoice = strongPitch(452.0f);
    noPredictionVoice.audioPresent = true;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(noPredictionState, rescueQuantizer,
                                      noPredictionVoice, rescueParameters);
    const double noPredictionTarget = noPredictionState.targetLog2;
    const double noPredictionTransport = noPredictionState.transportPeriodHz;
    const double noPredictionDesired = noPredictionState.desiredCents;
    ModernPitchEngine::PitchObservation noPredictionHole;
    noPredictionHole.audioPresent = true;
    noPredictionHole.valid = false;
    for (int hop = 0; hop < 120; ++hop)
    {
        engine->updateCorrectionState(noPredictionState, rescueQuantizer,
                                      noPredictionHole, rescueParameters);
        static_cast<void>(engine->advanceCorrection(noPredictionState));
    }
    success &= check(!noPredictionState.rescuePredictionActive
                     && noPredictionState.rescueQualificationHops == 0
                     && std::abs(noPredictionState.targetLog2 - noPredictionTarget) < 1.0e-12
                     && std::abs(noPredictionState.transportPeriodHz - noPredictionTransport) < 1.0e-12
                     && std::abs(noPredictionState.desiredCents - noPredictionDesired) < 1.0e-12,
                     "detector_hole_holds_target_transport_and_correction_exactly");

''',
'prediction suite becomes exact hold suite')

cpp_p.write_text(cpp)
test_p.write_text(test)
print('scale-owned transport v1 materialized')
