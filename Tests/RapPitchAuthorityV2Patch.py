from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')

cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')

MARKER = 'RAP_PITCH_AUTHORITY_V2_ZERO_CONSENSUS'
TEST_MARKER = 'presence_single_path_commits_pitch_with_zero_consensus'

if MARKER not in cpp:
    old = '''    std::array<PitchCandidate, detectorPathCount> rawCandidates {};
    const int rawDetectorSupport = collectFreshCandidates(rawCandidates);
    DecoderDecision decision = decodeCandidate(onsetPending_);
    const int previousOctaveState = octaveState_;
'''
    new = '''    std::array<PitchCandidate, detectorPathCount> rawCandidates {};
    const int rawDetectorSupport = collectFreshCandidates(rawCandidates);
    DecoderDecision decision = decodeCandidate(onsetPending_);

    // RAP_PITCH_AUTHORITY_V2_ZERO_CONSENSUS: consensus is telemetry, never
    // permission to correct.  When audio is present and at least one detector
    // path has a finite period, the best available raw path owns an F0 if the
    // consensus decoder produced no decision.  Existing register/anchor guards
    // still run below and can reject an implausible octave/subharmonic jump.
    if (presenceMode_ && !decision.valid && rawDetectorSupport > 0)
    {
        float bestScore = -1.0f;
        int bestIndex = -1;
        for (int index = 0; index < rawDetectorSupport; ++index)
        {
            const auto& candidate = rawCandidates[static_cast<std::size_t>(index)];
            if (!candidate.valid || candidate.frequencyHz <= 0.0f)
                continue;

            const float score = candidateBaseScore(candidate)
                * pathReliability(candidate.pathIndex, candidate.frequencyHz)
                + (candidate.ageInHops == 0 ? 0.001f : 0.0f);
            if (score > bestScore)
            {
                bestScore = score;
                bestIndex = index;
            }
        }

        if (bestIndex >= 0)
        {
            const auto& best = rawCandidates[static_cast<std::size_t>(bestIndex)];
            decision.candidate = best;
            decision.candidate.valid = true;
            decision.consensus = 0.0f;
            decision.supportCount = 1;
            decision.directSupportCount = 1;
            decision.freshSupportMask = best.ageInHops == 0
                ? static_cast<std::uint8_t>(1u << best.pathIndex) : 0;
            decision.decoderOctaveIndex = octaveState_;
            decision.valid = true;
        }
    }
    const int previousOctaveState = octaveState_;
'''
    if old not in cpp:
        raise RuntimeError('raw candidate decoder block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''        const bool exceptionalEvidence = decision.supportCount >= 2
            && decision.directSupportCount >= 2
            && decision.candidate.confidence >= 0.90f
            && decision.consensus >= 0.82f;
        const int requiredObservations = exceptionalEvidence ? 1 : 2;
'''
    new = '''        const bool exceptionalEvidence = decision.supportCount >= 2
            && decision.directSupportCount >= 2
            && decision.candidate.confidence >= 0.90f
            && decision.consensus >= 0.82f;
        // Presence already establishes that there is material to correct. A
        // single live detector path must therefore be allowed to establish the
        // initial F0 immediately even when consensus is exactly zero.
        const bool authoritativePresenceFallback = presenceMode_
            && decision.supportCount >= 1
            && decision.candidate.valid
            && decision.candidate.frequencyHz > 0.0f;
        const int requiredObservations = (exceptionalEvidence
            || authoritativePresenceFallback) ? 1 : 2;
'''
    if old not in cpp:
        raise RuntimeError('initial register observation gate not found')
    cpp = cpp.replace(old, new, 1)

    cpp_path.write_text(cpp, encoding='utf-8')

if TEST_MARKER not in test:
    anchor = '''    // Rap/rough-speech regression: even deliberately aperiodic non-zero audio
'''
    insertion = r'''    // Zero consensus is not zero pitch authority.  Presence plus one finite
    // detector path must be enough to establish an initial F0 immediately;
    // otherwise the UI can report Stable forever while targetValid never forms.
    auto zeroConsensusTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    zeroConsensusTracker->prepare(48000.0);
    zeroConsensusTracker->presenceMode_ = true;
    ModernPitchEngine::MultiRatePitchTracker::DecoderDecision zeroConsensusDecision;
    zeroConsensusDecision.valid = true;
    zeroConsensusDecision.candidate.valid = true;
    zeroConsensusDecision.candidate.frequencyHz = 220.0f;
    zeroConsensusDecision.candidate.confidence = 0.16f;
    zeroConsensusDecision.candidate.periodicity = 0.22f;
    zeroConsensusDecision.consensus = 0.0f;
    zeroConsensusDecision.supportCount = 1;
    zeroConsensusDecision.directSupportCount = 1;
    zeroConsensusDecision.freshSupportMask = 0x01;
    const bool zeroConsensusCommitted = zeroConsensusTracker->confirmOctaveTransition(
        zeroConsensusDecision, false);
    success &= check(zeroConsensusCommitted && zeroConsensusDecision.valid,
                     "presence_single_path_commits_pitch_with_zero_consensus");

''' + anchor
    if anchor not in test:
        raise RuntimeError('rap regression insertion anchor not found')
    test = test.replace(anchor, insertion, 1)

    anchor2 = '''    success &= check(rapPresenceHops > 20 && rapMinDetectorSupport > 0,
                     "nonzero_audio_never_reports_zero_detector_paths");
'''
    replacement2 = anchor2 + r'''
    int rapCorrectableHops = 0;
    int rapZeroConsensusPitchHops = 0;
    rapTracker->reset();
    rapNoise = 0x87654321u;
    for (int sample = 0; sample < 12000; ++sample)
    {
        rapNoise = 1664525u * rapNoise + 1013904223u;
        const float noise = (static_cast<float>((rapNoise >> 8) & 0x00ffffffu)
            / static_cast<float>(0x007fffffu) - 1.0f) * 0.085f;
        const float syllabic = (sample % 1100) < 760 ? 1.0f : 0.22f;
        if (rapTracker->processSample(noise * syllabic, rapObservation)
            && sample > 1400 && rapObservation.audioPresent)
        {
            if (rapObservation.valid && rapObservation.frequencyHz > 0.0f)
            {
                ++rapCorrectableHops;
                if (rapObservation.consensus <= 1.0e-6f)
                    ++rapZeroConsensusPitchHops;
            }
        }
    }
    success &= check(rapCorrectableHops > 20,
                     "nonzero_audio_produces_authoritative_pitch_for_correction");
    success &= check(rapZeroConsensusPitchHops > 0,
                     "zero_consensus_can_still_publish_correctable_f0");
'''
    if anchor2 not in test:
        raise RuntimeError('rap support assertion anchor not found')
    test = test.replace(anchor2, replacement2, 1)

    anchor3 = '''    parameters.transientProtection = 1.0f;
'''
    insertion3 = r'''    ModernPitchEngine::ScaleQuantizer zeroConsensusQuantizer;
    zeroConsensusQuantizer.reset();
    const double zeroConsensusUnison = 1.0;
    zeroConsensusQuantizer.setScale(&zeroConsensusUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState zeroConsensusState;
    ModernPitchEngine::PitchObservation zeroConsensusObservation;
    zeroConsensusObservation.valid = true;
    zeroConsensusObservation.audioPresent = true;
    zeroConsensusObservation.frequencyHz = 430.0f;
    zeroConsensusObservation.voicing = 1.0f;
    zeroConsensusObservation.confidence = 0.12f;
    zeroConsensusObservation.periodicity = 0.20f;
    zeroConsensusObservation.consensus = 0.0f;
    zeroConsensusObservation.detectorSupport = 1;
    for (int hop = 0; hop < 6; ++hop)
        engine->updateCorrectionState(zeroConsensusState, zeroConsensusQuantizer,
                                      zeroConsensusObservation, presenceParameters);
    success &= check(zeroConsensusState.targetValid
                     && std::abs(zeroConsensusState.desiredCents) > 5.0,
                     "zero_consensus_f0_creates_real_correction_target");

''' + anchor3
    if anchor3 not in test:
        raise RuntimeError('correction test insertion anchor not found')
    test = test.replace(anchor3, insertion3, 1)

    test_path.write_text(test, encoding='utf-8')

print('Rap pitch authority V2 applied')
