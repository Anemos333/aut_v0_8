from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')

SOURCE_MARKER = 'SOUND_EQUALS_CORRECTION_V1'
TEST_MARKER = 'sound_equals_correction_antiphase_stereo'


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise RuntimeError(f'{label}: expected source block not found')
    return text.replace(old, new, 1)


if SOURCE_MARKER not in cpp:
    # 1) Zero-consensus fallback must report the detector's best live F0, not
    # octave-fold it back toward a stale track/reacquisition anchor. Presence is
    # correction authority; continuity is supervision, never a permission gate.
    old = '''        const float referenceHz = trackedPitchHz_ > 0.0f
            ? trackedPitchHz_ : reacquisitionAnchorHz_;
        float bestScore = -1000.0f;
'''
    new = '''        // SOUND_EQUALS_CORRECTION_V1: when audible input is present, a raw
        // detector candidate is pitch authority. Never fold it toward a stale
        // track/anchor merely to look continuous: that is exactly how a sung
        // note can remain implausibly stuck for seconds after reacquisition.
        const float referenceHz = 0.0f;
        float bestScore = -1000.0f;
'''
    cpp = replace_once(cpp, old, new, 'presence fallback stale-anchor removal')

    # 2) Rescue-mode register guards are useful only when there is no explicit
    # live-presence authority. With sound present, they must not reject the new
    # F0 and leave the renderer on an old correction indefinitely.
    cpp = replace_once(
        cpp,
        '    if (rescueMode_ && rescueReferenceHz > 0.0f)\n',
        '    if (rescueMode_ && !presenceMode_ && rescueReferenceHz > 0.0f)\n',
        'presence bypasses rescue decoder guard')

    old = '''    decision.valid = rescueMode_
        ? rescueEvidence
        : (presenceMode_ || closeToTrack
            || sufficientInitialEvidence || presenceInitialEvidence);
'''
    new = '''    decision.valid = presenceMode_
        ? (decision.candidate.valid && decision.candidate.frequencyHz > 0.0f)
        : (rescueMode_
            ? rescueEvidence
            : (closeToTrack || sufficientInitialEvidence || presenceInitialEvidence));
'''
    cpp = replace_once(cpp, old, new, 'presence owns final decoder validity')

    # 3) Once a finite candidate exists during real audio, commit it immediately.
    # Consensus, octave confirmation counts and the stale rescue anchor may still
    # be metered, but cannot postpone correction. Keep octave telemetry coherent.
    anchor = '''    if (!decision.valid)
    {
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        return false;
    }

'''
    insertion = anchor + '''    // SOUND_EQUALS_CORRECTION_V1: audible input plus a finite F0 is enough
    // to own pitch immediately. No consensus/confirmation gate is allowed to
    // turn a real vocal signal into an effective bypass or hold a stale note.
    if (presenceMode_)
    {
        if (!decision.candidate.valid
            || !std::isfinite(decision.candidate.frequencyHz)
            || decision.candidate.frequencyHz <= 0.0f)
        {
            decision.valid = false;
            return false;
        }

        if (trackedPitchHz_ > 0.0f)
        {
            int octaveDelta = 0;
            float residualCents = 0.0f;
            if (isOctaveLikeTransition(trackedPitchHz_,
                                       decision.candidate.frequencyHz,
                                       octaveDelta,
                                       residualCents))
            {
                octaveState_ = std::clamp(octaveState_ + octaveDelta, -4, 4);
            }
        }

        committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        octaveCommitGuardHops_ = 0;
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        decision.decoderOctaveIndex = octaveState_;
        return true;
    }

'''
    cpp = replace_once(cpp, anchor, insertion, 'immediate live F0 commit')

    # 4) Presence without an F0 is a search state, never a fake Stable state.
    # Crucially, if a target already exists we do NOT clear desired/current cents:
    # Acquire becomes detector telemetry while correction continues uninterrupted.
    old = '''        // Audio presence is authoritative for voiced/unvoiced classification.
        // F0 confidence may fall to zero on rap, consonants or rough phonation,
        // but that is a pitch-search problem, never permission to call a
        // non-silent vocal signal unvoiced or timidly leave it in acquire.
        if (observation.audioPresent)
        {
            state.noteBodyLatched = true;
            state.noteBodyConfidence = 1.0f;
            state.stableBodyObservations = std::max(4, state.stableBodyObservations);
            state.breathEvidenceSamples = 0;
            state.uncertainSamples = 0;
            setState(TrackingState::stable);
            return;
        }
'''
    new = '''        // SOUND_EQUALS_CORRECTION_V1: audio presence owns the voice, but
        // Stable is forbidden until a real target exists. Acquire is now only
        // detector-search telemetry: an already acquired target/correction is
        // preserved exactly while F0 is temporarily missing.
        if (observation.audioPresent)
        {
            state.noteBodyLatched = true;
            state.noteBodyConfidence = 1.0f;
            state.stableBodyObservations = std::max(4, state.stableBodyObservations);
            state.breathEvidenceSamples = 0;
            state.uncertainSamples = 0;
            setState(TrackingState::acquire);
            return;
        }
'''
    cpp = replace_once(cpp, old, new, 'stable without target removal')

    # 5) Linked stereo analysis must never be L+R cancellation. Select one
    # coherent input channel for the whole host block: whichever carries the
    # most energy. Audio rendering remains unchanged and strictly single-wet.
    old = '''    const bool dualMono = safe.stereoMode == StereoMode::dualMono && channels > 1;
    for (int sample = 0; sample < samples; ++sample)
'''
    new = '''    const bool dualMono = safe.stereoMode == StereoMode::dualMono && channels > 1;

    // SOUND_EQUALS_CORRECTION_V1: linked pitch analysis must not average L+R.
    // Anti-phase or side-heavy vocals can cancel in that sum and make a clearly
    // audible signal look pitchless. Choose one coherent, highest-energy input
    // channel for this block; this changes analysis authority only, never audio.
    int linkedAnalysisChannel = 0;
    if (!dualMono && channels > 1)
    {
        double bestEnergy = -1.0;
        for (int channel = 0; channel < channels; ++channel)
        {
            double energy = 0.0;
            const float* channelData = data[static_cast<std::size_t>(channel)];
            for (int sample = 0; sample < samples; ++sample)
            {
                const double value = static_cast<double>(channelData[sample]);
                energy += value * value;
            }
            if (energy > bestEnergy)
            {
                bestEnergy = energy;
                linkedAnalysisChannel = channel;
            }
        }
    }

    for (int sample = 0; sample < samples; ++sample)
'''
    cpp = replace_once(cpp, old, new, 'coherent linked analysis channel selection')

    old = '''        else
        {
            double analysis = 0.0;
            for (int channel = 0; channel < channels; ++channel)
                analysis += data[static_cast<std::size_t>(channel)][sample];
            PitchObservation observation;
'''
    new = '''        else
        {
            const float analysis = data[static_cast<std::size_t>(linkedAnalysisChannel)][sample];
            PitchObservation observation;
'''
    cpp = replace_once(cpp, old, new, 'remove L+R detector average')

    old = '''            if (linkedTracker_.processSample(
                static_cast<float>(analysis / static_cast<double>(channels)), observation))
'''
    new = '''            if (linkedTracker_.processSample(analysis, observation))
'''
    cpp = replace_once(cpp, old, new, 'feed coherent detector signal')

    cpp_path.write_text(cpp, encoding='utf-8')


if TEST_MARKER not in test:
    # Existing expectation encoded the bug: nonzero audio without F0 was allowed
    # to claim Stable despite having no target. Replace it with the real invariant.
    old = '''    success &= check(presenceState.noteBodyLatched
                     && presenceState.trackingState == ModernPitchEngine::TrackingState::stable,
                     "audio_presence_overrides_unvoiced_and_acquire_timidity");
'''
    new = '''    success &= check(presenceState.noteBodyLatched
                     && !presenceState.targetValid
                     && presenceState.trackingState == ModernPitchEngine::TrackingState::acquire,
                     "presence_without_f0_cannot_claim_stable_without_target");
'''
    test = replace_once(test, old, new, 'replace fake-stable regression')

    anchor = new + '''
'''
    insertion = anchor + r'''    // SOUND_EQUALS_CORRECTION_V1 regression: a stereo vocal that is
    // exactly anti-phase must still create a target and audible correction.
    // The old linked L+R detector input cancelled this signal to zero forever.
    ModernPitchEngine antiphaseEngine;
    antiphaseEngine.prepare(48000.0, 256, 2, ModernPitchEngine::LatencyMode::live);
    ModernPitchEngine::Parameters antiphaseParameters;
    antiphaseParameters.amount = 1.0f;
    antiphaseParameters.retuneTimeMs = 0.0f;
    antiphaseParameters.humanize = 0.0f;
    antiphaseParameters.preserveVibrato = 0.0f;
    antiphaseParameters.minimumPitchHz = 70.0f;
    antiphaseParameters.maximumPitchHz = 1000.0f;
    antiphaseParameters.stereoMode = ModernPitchEngine::StereoMode::linkedMidSide;
    const double authorityUnison = 1.0;
    double antiphasePhase = 0.0;
    constexpr double antiphaseFrequency = 452.0;
    juce::AudioBuffer<float> antiphaseBlock(2, 256);
    for (int blockIndex = 0; blockIndex < 220; ++blockIndex)
    {
        auto* left = antiphaseBlock.getWritePointer(0);
        auto* right = antiphaseBlock.getWritePointer(1);
        for (int sample = 0; sample < antiphaseBlock.getNumSamples(); ++sample)
        {
            antiphasePhase += 2.0 * 3.14159265358979323846
                * antiphaseFrequency / 48000.0;
            if (antiphasePhase >= 2.0 * 3.14159265358979323846)
                antiphasePhase -= 2.0 * 3.14159265358979323846;
            const float value = static_cast<float>(0.28 * std::sin(antiphasePhase));
            left[sample] = value;
            right[sample] = -value;
        }
        antiphaseEngine.process(antiphaseBlock, &authorityUnison, 1, 440.0,
                                antiphaseParameters);
    }
    const auto antiphaseMeter = antiphaseEngine.getMetering();
    success &= check(antiphaseMeter.detectedPitchHz > 430.0f
                     && antiphaseMeter.detectedPitchHz < 470.0f
                     && antiphaseMeter.targetPitchHz > 430.0f
                     && std::abs(antiphaseMeter.correctionCents) > 5.0f,
                     "sound_equals_correction_antiphase_stereo");

    // A stale rescue anchor is never allowed to veto a new live F0. This is the
    // sung-note failure mode that could otherwise hold an implausible register
    // for seconds after pitch reacquisition.
    auto liveRescueTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    liveRescueTracker->prepare(48000.0);
    liveRescueTracker->presenceMode_ = true;
    liveRescueTracker->setRescueMode(true);
    liveRescueTracker->setReacquisitionAnchor(220.0f);
    ModernPitchEngine::MultiRatePitchTracker::DecoderDecision liveRescueDecision;
    liveRescueDecision.valid = true;
    liveRescueDecision.candidate.valid = true;
    liveRescueDecision.candidate.frequencyHz = 440.0f;
    liveRescueDecision.candidate.confidence = 0.18f;
    liveRescueDecision.candidate.periodicity = 0.24f;
    liveRescueDecision.consensus = 0.0f;
    liveRescueDecision.supportCount = 1;
    liveRescueDecision.directSupportCount = 1;
    liveRescueDecision.freshSupportMask = 0x01;
    const bool liveRescueAccepted = liveRescueTracker->confirmOctaveTransition(
        liveRescueDecision, false);
    success &= check(liveRescueAccepted && liveRescueDecision.valid
                     && std::abs(liveRescueDecision.candidate.frequencyHz - 440.0f) < 0.1f,
                     "live_presence_replaces_stale_rescue_register_immediately");

    // Acquire is permitted to describe detector search, but it must never mute
    // an already acquired correction. Presence plus a temporary F0 dropout holds
    // the exact destination until the next live F0 arrives.
    ModernPitchEngine::CorrectionState heldCorrectionState;
    heldCorrectionState.targetValid = true;
    heldCorrectionState.targetLog2 = std::log2(440.0);
    heldCorrectionState.desiredCents = -42.0;
    heldCorrectionState.currentCents = -42.0;
    heldCorrectionState.noteBodyLatched = true;
    heldCorrectionState.trackingState = ModernPitchEngine::TrackingState::stable;
    ModernPitchEngine::PitchObservation presentDropout;
    presentDropout.audioPresent = true;
    presentDropout.voicing = 1.0f;
    engine->updateCorrectionState(heldCorrectionState, presenceQuantizer,
                                  presentDropout, presenceParameters);
    success &= check(heldCorrectionState.trackingState
                         == ModernPitchEngine::TrackingState::acquire
                     && heldCorrectionState.targetValid
                     && std::abs(heldCorrectionState.desiredCents + 42.0) < 1.0e-9,
                     "acquire_search_never_mutes_existing_correction");

'''
    test = replace_once(test, anchor, insertion, 'insert correction-authority regressions')
    test_path.write_text(test, encoding='utf-8')

print('Sound=correction authority V1 applied: live F0 immediate, no stale rescue veto, no stereo cancellation')
