from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')

cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')

MARKER = 'RAP_VOICING_V2_ZERO_CONSENSUS_CORRECTION'
TEST_MARKER = 'zero_consensus_presence_fallback_yields_valid_f0'

if MARKER not in cpp:
    old = '''    std::array<PitchCandidate, detectorPathCount> candidates {};
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
'''
    new = '''    std::array<PitchCandidate, detectorPathCount> candidates {};
    const int candidateCount = collectFreshCandidates(candidates);
    if (candidateCount <= 0)
        return {};

    // RAP_VOICING_V2_ZERO_CONSENSUS_CORRECTION: consensus is diagnostic
    // evidence, never permission to correct. If audio is present and at least
    // one detector path has a period estimate, publish the best register-safe
    // F0 even when the consensus decoder cannot form an authoritative cluster.
    const auto makePresenceFallback = [&]() noexcept
    {
        DecoderDecision fallback;
        if (!presenceMode_ || candidateCount <= 0)
            return fallback;

        const float referenceHz = trackedPitchHz_ > 0.0f
            ? trackedPitchHz_ : reacquisitionAnchorHz_;
        float bestScore = -1000.0f;

        for (int index = 0; index < candidateCount; ++index)
        {
            const auto& raw = candidates[static_cast<std::size_t>(index)];
            if (!raw.valid || raw.frequencyHz <= 0.0f)
                continue;

            float selectedFrequency = raw.frequencyHz;
            float selectedDistance = referenceHz > 0.0f
                ? centsDistance(selectedFrequency, referenceHz) : 0.0f;
            int selectedOctaveShift = 0;

            if (referenceHz > 0.0f)
            {
                for (int octaveShift = -2; octaveShift <= 2; ++octaveShift)
                {
                    const float shifted = std::ldexp(raw.frequencyHz, octaveShift);
                    if (shifted < minimumPitchHz_ || shifted > maximumPitchHz_)
                        continue;

                    const float distance = centsDistance(shifted, referenceHz);
                    if (distance < selectedDistance)
                    {
                        selectedDistance = distance;
                        selectedFrequency = shifted;
                        selectedOctaveShift = octaveShift;
                    }
                }

                // The existing rescue register guard remains authoritative.
                // Presence fallback can recover weak evidence, not invent a
                // register outside the bounded musical search window.
                if (rescueMode_ && selectedDistance > 700.0f)
                    continue;
            }

            const float continuity = referenceHz > 0.0f
                ? (1.0f - smoothStep(120.0f, 700.0f, selectedDistance))
                : 0.0f;
            const float score = candidateBaseScore(raw)
                * (0.65f + 0.35f * pathReliability(raw.pathIndex, raw.frequencyHz))
                + 0.45f * continuity;
            if (score <= bestScore)
                continue;

            bestScore = score;
            fallback.candidate = raw;
            fallback.candidate.frequencyHz = selectedFrequency;
            fallback.candidate.valid = true;
            fallback.consensus = 0.0f;
            fallback.supportCount = 1;
            fallback.directSupportCount = selectedOctaveShift == 0 ? 1 : 0;
            fallback.freshSupportMask = raw.ageInHops == 0 && raw.pathIndex >= 0
                ? static_cast<std::uint8_t>(1u << raw.pathIndex) : 0;
            fallback.decoderOctaveIndex = octaveState_;
            fallback.valid = true;
        }

        return fallback;
    };

    std::array<ConsensusHypothesis, maxConsensusHypotheses> hypotheses {};
    const int hypothesisCount = buildConsensusHypotheses(candidates,
                                                         candidateCount,
                                                         hypotheses);
    if (hypothesisCount <= 0)
        return makePresenceFallback();

    updateDecoderBeam(hypotheses, hypothesisCount, onsetPending);
    if (!decoderBeam_[0].valid)
        return makePresenceFallback();
'''
    if old not in cpp:
        raise RuntimeError('decodeCandidate prologue not found')
    cpp = cpp.replace(old, new, 1)

    old = '''        if (matchedHypothesis < 0 || matchedDistance > 65.0f)
            return {}; // the winning branch is only a decaying hold state
'''
    new = '''        if (matchedHypothesis < 0 || matchedDistance > 65.0f)
        {
            if (presenceMode_)
                return makePresenceFallback();
            return {}; // the winning branch is only a decaying hold state
        }
'''
    if old not in cpp:
        raise RuntimeError('normal matched-hypothesis gate not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    decision.valid = rescueMode_
        ? rescueEvidence
        : (closeToTrack || sufficientInitialEvidence || presenceInitialEvidence);
'''
    new = '''    decision.valid = rescueMode_
        ? rescueEvidence
        : (presenceMode_ || closeToTrack
            || sufficientInitialEvidence || presenceInitialEvidence);
'''
    if old not in cpp:
        raise RuntimeError('decision validity assignment not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    // Initial register acquisition is deliberately temporal. A single fresh
    // harmonic family can be an octave alias at a vowel onset, so the first
    // non-exceptional decision must repeat before it becomes audible control.
    // This is detector commitment, not reduced correction authority.
    if (trackedPitchHz_ <= 0.0f)
    {
'''
    new = '''    // Presence owns correction authority. With actual input present, the first
    // register-safe F0 becomes audible control immediately even at zero
    // consensus. Subsequent octave/subharmonic changes still pass through the
    // existing register guards, so this removes timidity without weakening
    // continuity after lock.
    if (trackedPitchHz_ <= 0.0f && presenceMode_)
    {
        committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
        octaveCommitGuardHops_ = 6;
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        return true;
    }

    // Initial register acquisition without explicit audio presence remains
    // deliberately temporal for synthetic/offline detector-only use.
    if (trackedPitchHz_ <= 0.0f)
    {
'''
    if old not in cpp:
        raise RuntimeError('initial register acquisition block not found')
    cpp = cpp.replace(old, new, 1)

    cpp_path.write_text(cpp, encoding='utf-8')

if TEST_MARKER not in test:
    anchor = '''    success &= check(rapPresenceHops > 20 && rapMinDetectorSupport > 0,
                     "nonzero_audio_never_reports_zero_detector_paths");

'''
    insertion = anchor + r'''    // Zero consensus is explicitly allowed to drive correction. A single weak
    // path is still better than pitchless "stable": presence must publish an
    // F0 and the supervisor must turn that F0 into a real target/correction.
    auto zeroConsensusTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    zeroConsensusTracker->prepare(48000.0);
    zeroConsensusTracker->presenceMode_ = true;
    auto& zeroConsensusSlot = zeroConsensusTracker->halfRateCandidate_;
    zeroConsensusSlot.candidate.valid = true;
    zeroConsensusSlot.candidate.frequencyHz = 452.0f;
    zeroConsensusSlot.candidate.confidence = 0.01f;
    zeroConsensusSlot.candidate.periodicity = 0.01f;
    zeroConsensusSlot.candidate.pathIndex = 1;
    zeroConsensusSlot.candidate.ageInHops = 0;
    zeroConsensusSlot.ageInHops = 0;
    zeroConsensusTracker->decoderBeam_.fill({});
    const auto zeroConsensusDecision = zeroConsensusTracker->decodeCandidate(false);
    success &= check(zeroConsensusDecision.valid
                     && zeroConsensusDecision.candidate.frequencyHz > 0.0f
                     && std::abs(zeroConsensusDecision.consensus) < 1.0e-7f,
                     "zero_consensus_presence_fallback_yields_valid_f0");

    auto firstPresenceLock = zeroConsensusDecision;
    const bool firstPresenceAccepted = zeroConsensusTracker->confirmOctaveTransition(
        firstPresenceLock, false);
    success &= check(firstPresenceAccepted && firstPresenceLock.valid,
                     "presence_first_lock_does_not_wait_for_consensus");

'''
    if anchor not in test:
        raise RuntimeError('strict rap regression anchor not found')
    test = test.replace(anchor, insertion, 1)

    anchor2 = '''    ModernPitchEngine::ScaleQuantizer quantizer;
    quantizer.reset();
    ModernPitchEngine::Parameters parameters;
'''
    insertion2 = anchor2 + r'''    ModernPitchEngine::ScaleQuantizer zeroConsensusQuantizer;
    zeroConsensusQuantizer.reset();
    const double zeroConsensusUnison = 1.0;
    zeroConsensusQuantizer.setScale(&zeroConsensusUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState zeroConsensusState;
    ModernPitchEngine::PitchObservation zeroConsensusObservation;
    zeroConsensusObservation.audioPresent = true;
    zeroConsensusObservation.valid = true;
    zeroConsensusObservation.frequencyHz = 452.0f;
    zeroConsensusObservation.voicing = 1.0f;
    zeroConsensusObservation.confidence = 0.01f;
    zeroConsensusObservation.periodicity = 0.01f;
    zeroConsensusObservation.consensus = 0.0f;
    zeroConsensusObservation.detectorSupport = 1;
    engine->updateCorrectionState(zeroConsensusState, zeroConsensusQuantizer,
                                  zeroConsensusObservation, parameters);
    success &= check(zeroConsensusState.targetValid
                     && std::abs(zeroConsensusState.desiredCents) > 5.0,
                     "zero_consensus_valid_f0_drives_real_correction");

'''
    if anchor2 not in test:
        raise RuntimeError('engine quantizer test anchor not found')
    test = test.replace(anchor2, insertion2, 1)

    # Tighten the existing rough-speech loop: every present hop after warm-up
    # must expose a usable F0, not merely voice presence and path count.
    old = '''    int rapMaxDetectorSupport = 0;
    int rapMinDetectorSupport = 4;
    float rapMinimumVoicing = 1.0f;
'''
    new = '''    int rapMaxDetectorSupport = 0;
    int rapMinDetectorSupport = 4;
    int rapPitchlessPresentHops = 0;
    float rapMinimumVoicing = 1.0f;
'''
    if old not in test:
        raise RuntimeError('rap counters block not found')
    test = test.replace(old, new, 1)

    old = '''            rapMinimumVoicing = std::min(rapMinimumVoicing,
                                         rapObservation.voicing);
'''
    new = '''            rapMinimumVoicing = std::min(rapMinimumVoicing,
                                         rapObservation.voicing);
            if (!rapObservation.valid || rapObservation.frequencyHz <= 0.0f)
                ++rapPitchlessPresentHops;
'''
    if old not in test:
        raise RuntimeError('rap observation accumulation block not found')
    test = test.replace(old, new, 1)

    anchor3 = '''    success &= check(rapPresenceHops > 20 && rapMinDetectorSupport > 0,
                     "nonzero_audio_never_reports_zero_detector_paths");
'''
    replacement3 = anchor3 + '''    success &= check(rapPresenceHops > 20 && rapPitchlessPresentHops == 0,\n                     "nonzero_audio_never_reports_pitchless_stable");\n'''
    if anchor3 not in test:
        raise RuntimeError('rap strict result block not found')
    test = test.replace(anchor3, replacement3, 1)

    test_path.write_text(test, encoding='utf-8')

print('RapVoicingV2 zero-consensus correction patch applied/verified')
