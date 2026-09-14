from pathlib import Path


def one(text, old, new, label, expected=1):
    count = text.count(old)
    if count != expected:
        raise SystemExit(f"{label}: expected {expected} match(es), got {count}")
    return text.replace(old, new, expected)


def region(text, start, end, replacement, label):
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"{label}: start marker not found")
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f"{label}: end marker not found")
    return text[:a] + replacement + text[b:]


cpp_p = Path('Source/ModernPitchEngine.cpp')
h_p = Path('Source/ModernPitchEngine.h')
test_p = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_p.read_text()
h = h_p.read_text()
test = test_p.read_text()

# ---------------------------------------------------------------------------
# 1. Detector continuum: preserve a physically measured period even when the
# YIN shape is too weak to call it a trusted F0.  It remains provisional and is
# fused with voice-body evidence downstream.  Pure aperiodic material therefore
# remains invalid instead of being promoted merely because samples are non-zero.
cpp = one(cpp,
'''    if (thresholdTau < 0 && globalValue > fallbackThreshold)\n        return result;\n\n''',
'''    // MEASUREMENT_CONTINUUM_V1: do not collapse a weak period estimate into\n    // the same state as "no measurement".  A poor YIN shape stays provisional:\n    // frequency/confidence/periodicity are retained, while valid remains false\n    // unless the normal structural threshold is met.  The supervisor may use\n    // that grey-zone measurement only when independent voice-body evidence says\n    // it belongs to a sung body rather than a consonant/noise event.\n    const bool structurallyTrusted = thresholdTau >= 0\n        || globalValue <= fallbackThreshold;\n\n''',
'measurement continuum keeps weak period')

cpp = one(cpp,
'''    const float minimumCandidateScore = presenceMode_\n        ? 0.0f : (rescueMode_ ? 0.34f : 0.45f);\n    if (bestTau < 2 || bestScore < minimumCandidateScore)\n        return result;\n''',
'''    const float minimumCandidateScore = presenceMode_\n        ? 0.0f : (rescueMode_ ? 0.34f : 0.45f);\n    if (bestTau < 2\n        || (!presenceMode_\n            && (!structurallyTrusted || bestScore < minimumCandidateScore)))\n    {\n        return result;\n    }\n''',
'provisional candidate score')

cpp = one(cpp,
'''    result.frequencyHz = frequency;\n    result.confidence = clamp01(bestScore);\n    result.periodicity = bestPeriodicity;\n    result.valid = true;\n    return result;\n''',
'''    result.frequencyHz = frequency;\n    result.confidence = clamp01(bestScore);\n    result.periodicity = bestPeriodicity;\n    result.valid = structurallyTrusted && bestScore >= minimumCandidateScore;\n    return result;\n''',
'provisional result validity')

h = one(h,
'''        bool valid = false;\n        bool onset = false;\n        bool audioPresent = false;\n''',
'''        bool valid = false;\n        // MEASUREMENT_CONTINUUM_V1: a finite period was physically measured,\n        // but detector evidence may still be too weak to call it a trusted F0.\n        // This is analysis information only and never direct renderer authority.\n        bool measurementAvailable = false;\n        bool onset = false;\n        bool audioPresent = false;\n''',
'pitch observation provisional flag')

# Preserve the strongest finite provisional period from the detector paths.
cpp = one(cpp,
'''    std::array<PitchCandidate, detectorPathCount> rawCandidates {};\n    const int rawDetectorSupport = collectFreshCandidates(rawCandidates);\n    DecoderDecision decision = decodeCandidate(onsetPending_);\n''',
'''    const auto chooseProvisionalMeasurement = [this]() noexcept\n    {\n        PitchCandidate best {};\n        float bestScore = -1.0f;\n        const auto consider = [&best, &bestScore](const CandidateSlot& slot,\n                                                  int maximumAge) noexcept\n        {\n            const auto& candidate = slot.candidate;\n            if (slot.ageInHops > maximumAge\n                || !std::isfinite(candidate.frequencyHz)\n                || candidate.frequencyHz <= 0.0f)\n            {\n                return;\n            }\n            const float ageWeight = std::exp(-0.22f\n                * static_cast<float>(std::max(0, slot.ageInHops)));\n            const float score = ageWeight\n                * (0.62f * clamp01(candidate.confidence)\n                 + 0.38f * clamp01(candidate.periodicity));\n            if (score > bestScore)\n            {\n                bestScore = score;\n                best = candidate;\n            }\n        };\n        consider(fullRateCandidate_, 2);\n        consider(halfRateCandidate_, 3);\n        consider(quarterRateCandidate_, 5);\n        consider(eighthRateCandidate_, 9);\n        return best;\n    };\n\n    const PitchCandidate provisionalMeasurement = chooseProvisionalMeasurement();\n    std::array<PitchCandidate, detectorPathCount> rawCandidates {};\n    const int rawDetectorSupport = collectFreshCandidates(rawCandidates);\n    DecoderDecision decision = decodeCandidate(onsetPending_);\n''',
'collect provisional measurement')

cpp = one(cpp,
'''        observation.audioPresent = presenceMode_;\n        observation.voicing = detectorVoicing;\n        observation.valid = true; // this branch contains a confirmed F0\n''',
'''        observation.audioPresent = presenceMode_;\n        observation.voicing = detectorVoicing;\n        observation.measurementAvailable = true;\n        observation.valid = true; // this branch contains a confirmed F0\n''',
'trusted observation marks measurement available')

cpp = one(cpp,
'''        observation.frequencyHz = trackedPitchHz_;\n        observation.correctionFrequencyHz = trackedPitchHz_;\n        observation.confidence = trackedConfidence_;\n''',
'''        const bool provisionalAvailable =\n            std::isfinite(provisionalMeasurement.frequencyHz)\n            && provisionalMeasurement.frequencyHz > 0.0f;\n        observation.frequencyHz = trackedPitchHz_;\n        observation.correctionFrequencyHz = provisionalAvailable\n            ? provisionalMeasurement.frequencyHz : trackedPitchHz_;\n        observation.measurementAvailable = provisionalAvailable;\n        observation.confidence = provisionalAvailable\n            ? provisionalMeasurement.confidence : trackedConfidence_;\n''',
'publish provisional measurement without trusted F0')

cpp = one(cpp,
'''        observation.periodicity = trackedPeriodicity_;\n        observation.consensus = trackedConsensus_;\n''',
'''        observation.periodicity = provisionalAvailable\n            ? provisionalMeasurement.periodicity : trackedPeriodicity_;\n        observation.consensus = trackedConsensus_;\n''',
'publish provisional periodicity')

# ---------------------------------------------------------------------------
# 2. Octave ambiguity: an octave-like observation is not a normal note change.
# It must persist for several genuinely fresh hops unless a real onset exists.
# Confidence does not extend or shorten the veto; direct multi-path support only
# changes the fixed geometric observation count.
cpp = one(cpp,
'''        // Downward octave/subharmonic aliases are more common, hence one extra\n        // observation. This veto is finite and independent of confidence.\n        const int requiredObservations = rescueOctaveDelta < 0 ? 3 : 2;\n''',
'''        // OCTAVE_AMBIGUITY_V2: octave/subharmonic aliases are common on real\n        // vocals.  Keep this a finite negative veto, but require enough fresh\n        // geometric persistence that one consonant/harmonic burst cannot own a\n        // register. A real onset remains fast; multi-path direct support is the\n        // only reason to shorten the non-onset count.\n        const bool multiPathDirect = decision.directSupportCount >= 2;\n        const int requiredObservations = onsetPending\n            ? (rescueOctaveDelta < 0 ? 3 : 2)\n            : multiPathDirect\n                ? (rescueOctaveDelta < 0 ? 7 : 6)\n                : (rescueOctaveDelta < 0 ? 10 : 8);\n''',
'rescue octave persistence')

cpp = one(cpp,
'''    const int requiredObservations = octaveDelta < 0 ? 3 : 2;\n''',
'''    // OCTAVE_AMBIGUITY_V2: normal tracking uses the same finite veto.\n    // Eight single-family fresh hops are only ~5.3 ms at the 32-sample hop:\n    // fast enough for sung note changes, long enough to reject most one-frame\n    // register hallucinations. Downward aliases receive two extra hops.\n    const bool multiPathDirect = decision.directSupportCount >= 2;\n    const int requiredObservations = onsetPending\n        ? (octaveDelta < 0 ? 3 : 2)\n        : multiPathDirect\n            ? (octaveDelta < 0 ? 7 : 6)\n            : (octaveDelta < 0 ? 10 : 8);\n''',
'normal octave persistence')

# ---------------------------------------------------------------------------
# 3. Supervisor fusion: a provisional physical period becomes usable only when
# independent voice-body evidence says this is sung body rather than phonetic or
# breath material.  This is the requested grey scale: confidence alone neither
# grants nor denies correction.
cpp = one(cpp,
'''    const bool zeroPrudence = zeroPrudenceAuthority(parameters); // AUTHORITY_CONTROLS_EXPLICIT_V1\n    const bool richEvidence = parameters.voiceEvidenceValid;\n    const bool validPitch = observation.valid && observation.frequencyHz > 0.0f;\n    if (validPitch)\n''',
'''    const bool zeroPrudence = zeroPrudenceAuthority(parameters); // AUTHORITY_CONTROLS_EXPLICIT_V1\n    const bool richEvidence = parameters.voiceEvidenceValid;\n    const bool trustedPitch = observation.valid\n        && std::isfinite(observation.frequencyHz)\n        && observation.frequencyHz > 0.0f;\n    const bool provisionalMeasurement = !trustedPitch\n        && observation.measurementAvailable\n        && std::isfinite(observation.correctionFrequencyHz)\n        && observation.correctionFrequencyHz > 0.0f;\n    const bool provisionalVoicePitch = provisionalMeasurement\n        && richEvidence\n        && parameters.voiceBodyEnergy >= 0.30f\n        && parameters.voiceHarmonicity >= 0.24f\n        && parameters.voiceSpectralReliability >= 0.34f\n        && parameters.voiceBreathiness <= 0.72f\n        && parameters.voiceEventStrength <= 0.74f;\n    const bool validPitch = trustedPitch || provisionalVoicePitch;\n    if (validPitch)\n''',
'voice body can use provisional measurement')

# Invalid/phonetic frames may not collapse an owned transition to stable or
# unvoiced.  Transition is transport state, not detector-confidence state.
cpp = one(cpp,
'''            if (state.targetValid)\n                setState(explicitPhoneticFrame\n                    ? TrackingState::unvoiced\n                    : TrackingState::stable);\n            else\n''',
'''            if (state.targetValid)\n            {\n                if (state.trackingState != TrackingState::transition)\n                    setState(TrackingState::stable);\n            }\n            else\n''',
'audio-present dropout preserves transition')

cpp = one(cpp,
'''            if (state.targetValid)\n            {\n                setState(explicitPhoneticFrame\n                    ? TrackingState::unvoiced\n                    : TrackingState::stable);\n                return;\n            }\n''',
'''            if (state.targetValid)\n            {\n                if (state.trackingState != TrackingState::transition)\n                    setState(TrackingState::stable);\n                return;\n            }\n''',
'latched dropout preserves transition')

# The fastest accepted coordinate is correctionFrequencyHz. It is still only a
# measurement: identity persistence and the transport innovation gate decide if
# it can affect musical state. This removes the old lag that leaked vibrato even
# with Vibrato/Humanize at zero.
cpp = one(cpp,
'''    const double observedLog2 = safeLog2(observation.frequencyHz);\n    const float correctionFrequencyHz =\n        std::isfinite(observation.correctionFrequencyHz)\n        && observation.correctionFrequencyHz > 0.0f\n        ? observation.correctionFrequencyHz\n        : observation.frequencyHz;\n    const double correctionObservedLog2 = safeLog2(correctionFrequencyHz);\n''',
'''    const float correctionFrequencyHz =\n        std::isfinite(observation.correctionFrequencyHz)\n        && observation.correctionFrequencyHz > 0.0f\n        ? observation.correctionFrequencyHz\n        : observation.frequencyHz;\n    // LOCAL_TRAJECTORY_V1: target nomination uses the fastest accepted physical\n    // coordinate. It still cannot command the renderer directly: challenger\n    // persistence, octave veto and the transport innovation gate remain between\n    // this measurement and audible correction.\n    const double observedLog2 = safeLog2(correctionFrequencyHz);\n    const double correctionObservedLog2 = observedLog2;\n''',
'identity uses fast accepted measurement')

# Octave-like scale challengers cannot use the ordinary deep-cell fast path.
# They remain possible, but only after a short fixed persistence window.
cpp = one(cpp,
'''            const double centreDistanceFromTarget =\n                std::abs(state.pitchCentreLog2 - state.targetLog2) * 1200.0;\n            const double deepExitRatio = scaleStep <= 50.0 ? 0.52 : 0.72;\n            const bool deepCentreExit = centreDistanceFromTarget\n                >= deepExitRatio * scaleStep;\n            constexpr double persistentEvidenceRequired = 6.0;\n            const bool persistentBoundaryExit =\n                state.identityChallengerEvidence >= persistentEvidenceRequired;\n\n            liveIdentityBreak = deepCentreExit || persistentBoundaryExit;\n''',
'''            const double absoluteObservedDistance = std::abs(signedObservedCents);\n            const int nearestOctaveMultiple = static_cast<int>(std::lround(\n                absoluteObservedDistance / 1200.0));\n            const bool octaveAmbiguous = nearestOctaveMultiple >= 1\n                && nearestOctaveMultiple <= 2\n                && std::abs(absoluteObservedDistance\n                    - 1200.0 * static_cast<double>(nearestOctaveMultiple)) <= 95.0;\n\n            const double centreDistanceFromTarget =\n                std::abs(state.pitchCentreLog2 - state.targetLog2) * 1200.0;\n            const double deepExitRatio = scaleStep <= 50.0 ? 0.52 : 0.72;\n            const bool deepCentreExit = !octaveAmbiguous\n                && centreDistanceFromTarget >= deepExitRatio * scaleStep;\n            const double persistentEvidenceRequired = octaveAmbiguous ? 12.0 : 6.0;\n            const bool persistentBoundaryExit =\n                state.identityChallengerEvidence >= persistentEvidenceRequired;\n\n            liveIdentityBreak = deepCentreExit || persistentBoundaryExit;\n''',
'supervisor octave ambiguity persistence')

# New transport state: local vocal motion is predicted/followed quickly, but a
# large innovation has zero audible authority until musical identity commits.
h = one(h,
'''        double transportPeriodHz = 0.0;\n\n        // SCALE_OWNS_IDENTITY_V2: detector observations may nominate a new\n''',
'''        double transportPeriodHz = 0.0;\n        // LOCAL_TRAJECTORY_V1: cents per detector hop, used only to predict\n        // continuous within-note vocal motion. Large innovations are frozen;\n        // confirmed target changes rebase transport explicitly.\n        double transportVelocityCentsPerHop = 0.0;\n\n        // SCALE_OWNS_IDENTITY_V2: detector observations may nominate a new\n''',
'local transport velocity state')

# Reset controller momentum at a committed musical boundary.  The transition is
# a monotonic transport glide, never an underdamped response to detector jitter.
cpp = one(cpp,
'''            setState(TrackingState::transition);\n            state.stableObservations = 0;\n            state.stableBodyObservations = bodyPresent ? 1 : 0;\n''',
'''            setState(TrackingState::transition);\n            state.velocityCentsPerSecond = 0.0;\n            state.stableObservations = 0;\n            state.stableBodyObservations = bodyPresent ? 1 : 0;\n''',
'reset transition controller momentum')

cpp = region(cpp,
'''    // SCALE_OWNS_TRANSPORT_V1: the audible source coordinate is persistent\n''',
'''    const double audibleSourceLog2 = state.transportPeriodHz > 0.0\n''',
'''    // LOCAL_TRAJECTORY_V1: the audible source coordinate is a persistent\n    // local trajectory, not raw F0 and not a slow detector average. Continuous\n    // within-note motion (including real vibrato that must be removed at zero\n    // Vibrato) is followed rapidly when the innovation is physically small.\n    // Large jumps have exactly zero transport authority until scale identity has\n    // committed; a committed note change then rebases to the accepted live F0\n    // in one supervisor event so target and source jump coherently.\n    if (state.noteBodyLatched && bodyPresent)\n    {\n        const double observedSourceLog2 = correctionObservedLog2;\n        if (!(state.transportPeriodHz > 0.0)\n            || !std::isfinite(state.transportPeriodHz)\n            || firstOwnedTarget)\n        {\n            state.transportPeriodHz = std::exp2(observedSourceLog2);\n            state.transportVelocityCentsPerHop = 0.0;\n        }\n        else if (targetIdentityChanged)\n        {\n            // The challenger has already passed persistence + quantizer Hold.\n            // Rebase source and target together; do not spend transition time\n            // dragging an obsolete old-note source coordinate toward the new F0.\n            state.transportPeriodHz = std::exp2(observedSourceLog2);\n            state.transportVelocityCentsPerHop = 0.0;\n        }\n        else\n        {\n            const double currentSourceLog2 = safeLog2(state.transportPeriodHz);\n            const double predictedSourceLog2 = currentSourceLog2\n                + state.transportVelocityCentsPerHop / 1200.0;\n            const double innovationCents =\n                (observedSourceLog2 - predictedSourceLog2) * 1200.0;\n            const double localScaleStep = std::max(0.1,\n                static_cast<double>(quantizer.minimumStepCents()));\n            const double innovationGateCents = std::clamp(\n                0.28 * localScaleStep, 8.0, 28.0);\n\n            if (std::abs(innovationCents) <= innovationGateCents)\n            {\n                constexpr double innovationFollow = 0.88;\n                double nextSourceLog2 = predictedSourceLog2\n                    + innovationFollow * innovationCents / 1200.0;\n                double stepCents =\n                    (nextSourceLog2 - currentSourceLog2) * 1200.0;\n                const double maximumContinuousStep = std::clamp(\n                    0.16 * localScaleStep, 5.0, 16.0);\n                stepCents = std::clamp(stepCents,\n                                       -maximumContinuousStep,\n                                        maximumContinuousStep);\n                nextSourceLog2 = currentSourceLog2 + stepCents / 1200.0;\n                state.transportPeriodHz = std::exp2(nextSourceLog2);\n                state.transportVelocityCentsPerHop = std::clamp(\n                    0.52 * state.transportVelocityCentsPerHop\n                        + 0.48 * stepCents,\n                    -maximumContinuousStep, maximumContinuousStep);\n            }\n            else\n            {\n                // An isolated consonant period, octave alias or discontinuous\n                // detector coordinate cannot move audible transport at all.\n                state.transportVelocityCentsPerHop *= 0.35;\n            }\n        }\n    }\n\n''',
'local innovation-bounded transport')

# Leaving transition through the normal supervisor path cannot carry controller
# momentum into stable voiced material.
cpp = one(cpp,
'''    const auto setState = [&state](TrackingState next) noexcept\n    {\n        if (state.trackingState != next)\n        {\n            state.trackingState = next;\n            state.stateAgeSamples = 0;\n        }\n    };\n''',
'''    const auto setState = [&state](TrackingState next) noexcept\n    {\n        if (state.trackingState != next)\n        {\n            if (state.trackingState == TrackingState::transition\n                && next == TrackingState::stable)\n            {\n                state.velocityCentsPerSecond = 0.0;\n            }\n            state.trackingState = next;\n            state.stateAgeSamples = 0;\n        }\n    };\n''',
'clear momentum when transition settles')

# Transition controller itself is monotonic.  No overshoot means no ratio bounce
# that can turn into long-vowel tremolo/level pumping in the spectral renderer.
cpp = one(cpp,
'''    const double dt = 1.0 / sampleRate_;\n    const double responseSeconds = std::max(0.00035, state.responseMs * 0.001);\n    const double omega = std::min(0.22 / dt, 4.6 / responseSeconds);\n''',
'''    const double dt = 1.0 / sampleRate_;\n    const double responseSeconds = std::max(0.00035, state.responseMs * 0.001);\n\n    if (state.trackingState == TrackingState::transition)\n    {\n        // TRANSITION_IS_TRANSPORT_V1: one monotonic trajectory owns voiced,\n        // aperiodic and transient material alike. Detector holes only stop new\n        // observations; they never interrupt this glide or expose unity/dry.\n        const double delta = state.desiredCents - state.currentCents;\n        const double alpha = std::clamp(\n            1.0 - std::exp(-4.6 * dt / responseSeconds), 0.0, 1.0);\n        double step = alpha * delta;\n        if (std::abs(step) > std::abs(delta))\n            step = delta;\n        state.currentCents += step;\n        state.velocityCentsPerSecond = step / dt;\n        if (std::abs(state.desiredCents - state.currentCents) < 0.001)\n        {\n            state.currentCents = state.desiredCents;\n            state.velocityCentsPerSecond = 0.0;\n        }\n        return state.currentCents;\n    }\n\n    const double omega = std::min(0.22 / dt, 4.6 / responseSeconds);\n''',
'monotonic transition transport controller')

cpp = one(cpp,
'''        state.trackingState = TrackingState::stable;\n        state.stateAgeSamples = 0;\n''',
'''        state.trackingState = TrackingState::stable;\n        state.stateAgeSamples = 0;\n        state.velocityCentsPerSecond = 0.0;\n''',
'clear momentum at hard transition timeout')

# ---------------------------------------------------------------------------
# Tests: encode the listening contract directly at supervisor/tracker level.
anchor = '''    return success ? 0 : 1;\n}'''
insert = r'''
    // MEASUREMENT_CONTINUUM_V1: weak-but-physical period evidence is usable for
    // sung body when independent voice evidence supports it. It may not remain
    // forever in Acquire merely because detector confidence is low.
    ModernPitchEngine::ScaleQuantizer provisionalQuantizer;
    provisionalQuantizer.reset();
    provisionalQuantizer.setScale(authorityChromatic.data(),
                                   static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState provisionalVoiceState;
    ModernPitchEngine::Parameters provisionalVoiceParameters = explicitAuthorityParameters;
    setBodyEvidence(provisionalVoiceParameters);
    ModernPitchEngine::PitchObservation provisionalVoice;
    provisionalVoice.audioPresent = true;
    provisionalVoice.valid = false;
    provisionalVoice.measurementAvailable = true;
    provisionalVoice.correctionFrequencyHz = 452.0f;
    provisionalVoice.confidence = 0.08f;
    provisionalVoice.periodicity = 0.22f;
    for (int hop = 0; hop < 8; ++hop)
        engine->updateCorrectionState(provisionalVoiceState, provisionalQuantizer,
                                      provisionalVoice, provisionalVoiceParameters);
    success &= check(provisionalVoiceState.targetValid
                     && provisionalVoiceState.noteBodyLatched
                     && provisionalVoiceState.trackingState != ModernPitchEngine::TrackingState::acquire
                     && std::abs(provisionalVoiceState.desiredCents) > 5.0,
                     "provisional_voice_measurement_cannot_stall_in_acquire");

    // The same provisional period on breath/sibilant evidence is not a note.
    ModernPitchEngine::CorrectionState provisionalSibilantState;
    ModernPitchEngine::Parameters provisionalSibilantParameters = explicitAuthorityParameters;
    setBreathEvidence(provisionalSibilantParameters);
    provisionalSibilantParameters.voiceEventStrength = 0.94f;
    engine->updateCorrectionState(provisionalSibilantState, provisionalQuantizer,
                                  provisionalVoice, provisionalSibilantParameters);
    success &= check(!provisionalSibilantState.targetValid,
                     "provisional_sibilant_measurement_cannot_invent_target");

    // TRANSITION_IS_TRANSPORT_V1: an invalid/phonetic frame cannot turn an
    // already-owned transition into unvoiced/stable or modify its destination.
    ModernPitchEngine::CorrectionState gluedTransition;
    gluedTransition.targetValid = true;
    gluedTransition.targetLog2 = std::log2(493.8833012561241);
    gluedTransition.transportPeriodHz = 500.0;
    gluedTransition.desiredCents = -21.318f;
    gluedTransition.currentCents = -10.0;
    gluedTransition.noteBodyLatched = true;
    gluedTransition.trackingState = ModernPitchEngine::TrackingState::transition;
    const double gluedTarget = gluedTransition.targetLog2;
    const double gluedTransport = gluedTransition.transportPeriodHz;
    const double gluedDesired = gluedTransition.desiredCents;
    ModernPitchEngine::PitchObservation gluedPhonetic;
    gluedPhonetic.audioPresent = true;
    gluedPhonetic.valid = false;
    ModernPitchEngine::Parameters gluedParameters = explicitAuthorityParameters;
    setBodyEvidence(gluedParameters);
    gluedParameters.voiceEventStrength = 0.96f;
    engine->updateCorrectionState(gluedTransition, provisionalQuantizer,
                                  gluedPhonetic, gluedParameters);
    success &= check(gluedTransition.trackingState == ModernPitchEngine::TrackingState::transition
                     && std::abs(gluedTransition.targetLog2 - gluedTarget) < 1.0e-12
                     && std::abs(gluedTransition.transportPeriodHz - gluedTransport) < 1.0e-12
                     && std::abs(gluedTransition.desiredCents - gluedDesired) < 1.0e-12,
                     "phonetic_frame_is_glued_to_owned_transition");

    // LOCAL_TRAJECTORY_V1: correctionFrequencyHz may be faster than the
    // continuity/identity coordinate, but only continuous small innovations may
    // move transport. At zero Vibrato this keeps the physical source estimate
    // within a few cents so the requested output remains nailed to the target.
    ModernPitchEngine::ScaleQuantizer localTrajectoryQuantizer;
    localTrajectoryQuantizer.reset();
    localTrajectoryQuantizer.setScale(authorityChromatic.data(),
                                       static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState localTrajectoryState;
    auto localBase = strongPitch(440.0f);
    localBase.audioPresent = true;
    localBase.correctionFrequencyHz = 440.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(localTrajectoryState, localTrajectoryQuantizer,
                                      localBase, explicitAuthorityParameters);
    double maximumLocalResidual = 0.0;
    bool localTargetChanged = false;
    const double localTargetReference = localTrajectoryState.targetLog2;
    for (int hop = 0; hop < 900; ++hop)
    {
        const double phase = 2.0 * 3.14159265358979323846
            * static_cast<double>(hop) / 150.0;
        const double cents = 70.0 * std::sin(phase);
        const double rawHz = 440.0 * std::exp2(cents / 1200.0);
        auto localObservation = strongPitch(440.0f); // deliberately lagged identity coordinate
        localObservation.audioPresent = true;
        localObservation.correctionFrequencyHz = static_cast<float>(rawHz);
        engine->updateCorrectionState(localTrajectoryState, localTrajectoryQuantizer,
                                      localObservation, explicitAuthorityParameters);
        const double transportResidual = std::abs(1200.0 * std::log2(
            rawHz / localTrajectoryState.transportPeriodHz));
        if (hop > 50)
            maximumLocalResidual = std::max(maximumLocalResidual, transportResidual);
        if (std::abs(localTrajectoryState.targetLog2 - localTargetReference) * 1200.0 > 0.5)
            localTargetChanged = true;
    }
    std::cerr << "local_trajectory_max_residual_cents=" << maximumLocalResidual << '\n';
    success &= check(!localTargetChanged && maximumLocalResidual < 4.0,
                     "zero_vibrato_lock_tracks_continuous_source_without_target_chatter");

    // A discontinuous local measurement still has zero transport authority.
    const double localTransportBeforeOutlier = localTrajectoryState.transportPeriodHz;
    auto localOutlier = strongPitch(440.0f);
    localOutlier.audioPresent = true;
    localOutlier.correctionFrequencyHz = 880.0f;
    engine->updateCorrectionState(localTrajectoryState, localTrajectoryQuantizer,
                                  localOutlier, explicitAuthorityParameters);
    success &= check(std::abs(localTrajectoryState.transportPeriodHz
                              - localTransportBeforeOutlier) < 1.0e-12,
                     "single_octave_innovation_has_zero_transport_authority");

    // OCTAVE_AMBIGUITY_V2: a single-family octave challenger must persist for a
    // fixed short window. It is neither immediately hallucinated nor blocked by
    // confidence forever.
    auto octavePersistenceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    octavePersistenceTracker->prepare(48000.0);
    octavePersistenceTracker->trackedPitchHz_ = 440.0f;
    octavePersistenceTracker->trackedConfidence_ = 0.9f;
    octavePersistenceTracker->trackedPeriodicity_ = 0.9f;
    octavePersistenceTracker->trackedConsensus_ = 0.0f;
    octavePersistenceTracker->trackedSupportCount_ = 1;
    octavePersistenceTracker->committedOctaveFrequencyHz_ = 440.0f;
    octavePersistenceTracker->octaveCommitGuardHops_ = 0;
    const auto makeOctaveChallenger = []
    {
        ModernPitchEngine::MultiRatePitchTracker::DecoderDecision d;
        d.valid = true;
        d.candidate.valid = true;
        d.candidate.frequencyHz = 880.0f;
        d.candidate.confidence = 0.01f;
        d.candidate.periodicity = 0.10f;
        d.consensus = 0.0f;
        d.supportCount = 1;
        d.directSupportCount = 1;
        d.freshSupportMask = 0x01;
        return d;
    };
    bool prematureOctaveCommit = false;
    for (int hop = 0; hop < 7; ++hop)
    {
        auto d = makeOctaveChallenger();
        prematureOctaveCommit = octavePersistenceTracker->confirmOctaveTransition(d, false)
            || prematureOctaveCommit;
    }
    auto finalOctave = makeOctaveChallenger();
    const bool finiteOctaveCommit = octavePersistenceTracker->confirmOctaveTransition(
        finalOctave, false);
    success &= check(!prematureOctaveCommit && finiteOctaveCommit,
                     "octave_ambiguity_has_short_finite_persistence_not_confidence_gate");

    // TRANSITION_IS_TRANSPORT_V1: controller motion is strictly monotonic and
    // cannot overshoot/bounce while a transient is being carried to the new
    // destination. This removes one upstream source of long-vowel pumping.
    ModernPitchEngine::CorrectionState monotonicTransition;
    monotonicTransition.targetValid = true;
    monotonicTransition.noteBodyLatched = true;
    monotonicTransition.trackingState = ModernPitchEngine::TrackingState::transition;
    monotonicTransition.currentCents = -80.0;
    monotonicTransition.desiredCents = 25.0;
    monotonicTransition.responseMs = 5.0;
    double previousMonotonic = monotonicTransition.currentCents;
    bool monotonic = true;
    bool overshot = false;
    for (int sample = 0; sample < 400; ++sample)
    {
        const double value = engine->advanceCorrection(monotonicTransition);
        if (value + 1.0e-12 < previousMonotonic)
            monotonic = false;
        if (value > monotonicTransition.desiredCents + 1.0e-9)
            overshot = true;
        previousMonotonic = value;
    }
    success &= check(monotonic && !overshot,
                     "transition_controller_is_monotonic_without_ratio_bounce");

    return success ? 0 : 1;
}'''
if anchor not in test:
    raise SystemExit('test insertion anchor missing')
test = test.replace(anchor, insert, 1)

cpp_p.write_text(cpp)
h_p.write_text(h)
test_p.write_text(test)
print('voice transition continuum v1 materialized')
