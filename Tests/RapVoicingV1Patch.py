from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
h_path = Path('Source/ModernPitchEngine.h')
test_path = Path('Tests/SupervisorContinuityTest.cpp')

cpp = cpp_path.read_text()
h = h_path.read_text()
test = test_path.read_text()

MARKER = 'RAP_VOICING_V1_AUDIO_PRESENCE'
TEST_MARKER = 'nonzero_audio_cannot_be_unvoiced'

if MARKER not in cpp:
    cpp = cpp.replace(
        'constexpr float minimumDetectorRms = 0.0012f;\n',
        'constexpr float minimumDetectorRms = 0.0012f;\n'
        'constexpr float numericalPresenceSample = 1.0e-8f;\n'
        'constexpr float numericalPresenceRms = 1.0e-8f;\n',
        1)

    old = '''    const float rms = static_cast<float>(std::sqrt(\n        squaredSum / static_cast<double>(analysisLength)));\n    if (rms < minimumDetectorRms)\n        return result;\n'''
    new = '''    const float rms = static_cast<float>(std::sqrt(\n        squaredSum / static_cast<double>(analysisLength)));\n    // RAP_VOICING_V1_AUDIO_PRESENCE: detector confidence may be poor, but a\n    // numerically non-silent input is never allowed to erase the voice.  In\n    // presence mode analyse the best available period instead of returning no\n    // path merely because YIN confidence is low.\n    const float rmsFloor = presenceMode_ ? numericalPresenceRms : minimumDetectorRms;\n    if (rms < rmsFloor)\n        return result;\n'''
    if old not in cpp:
        raise RuntimeError('RMS gate block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    if (thresholdTau < 0 && globalValue > fallbackThreshold)\n        return result;\n'''
    new = '''    if (!presenceMode_ && thresholdTau < 0 && globalValue > fallbackThreshold)\n        return result;\n'''
    if old not in cpp:
        raise RuntimeError('YIN fallback gate not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    const float minimumCandidateScore = rescueMode_ ? 0.34f : 0.45f;\n    if (bestTau < 2 || bestScore < minimumCandidateScore)\n        return result;\n'''
    new = '''    const float minimumCandidateScore = presenceMode_\n        ? 0.0f : (rescueMode_ ? 0.34f : 0.45f);\n    if (bestTau < 2 || bestScore < minimumCandidateScore)\n        return result;\n'''
    if old not in cpp:
        raise RuntimeError('candidate score gate not found')
    cpp = cpp.replace(old, new, 1)

    old = '''            if ((!direct && baseScore < 0.60f) || weight < 0.10f)\n                continue;\n'''
    new = '''            const float minimumOctaveSupport = presenceMode_ ? 0.24f : 0.60f;\n            const float minimumWeight = presenceMode_ ? 0.02f : 0.10f;\n            if ((!direct && baseScore < minimumOctaveSupport) || weight < minimumWeight)\n                continue;\n'''
    if old not in cpp:
        raise RuntimeError('consensus support gate not found')
    cpp = cpp.replace(old, new, 1)

    old = '''        hypothesis.valid = hypothesis.evidenceScore > 0.20f;\n'''
    new = '''        hypothesis.valid = hypothesis.evidenceScore\n            > (presenceMode_ ? 0.055f : 0.20f);\n'''
    if old not in cpp:
        raise RuntimeError('hypothesis validity gate not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    const bool sufficientInitialEvidence = decision.supportCount >= 2\n        || decision.candidate.confidence >= 0.78f;\n'''
    new = '''    const bool sufficientInitialEvidence = decision.supportCount >= 2\n        || decision.candidate.confidence >= 0.78f;\n    const bool presenceInitialEvidence = presenceMode_\n        && decision.supportCount >= 1\n        && decision.candidate.confidence >= 0.05f\n        && decision.candidate.periodicity >= 0.18f;\n'''
    if old not in cpp:
        raise RuntimeError('initial evidence block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    decision.valid = rescueMode_\n        ? rescueEvidence\n        : (closeToTrack || sufficientInitialEvidence);\n'''
    new = '''    decision.valid = rescueMode_\n        ? rescueEvidence\n        : (closeToTrack || sufficientInitialEvidence || presenceInitialEvidence);\n'''
    if old not in cpp:
        raise RuntimeError('decision validity block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    observation = {};\n    inputSample = sanitiseAudioSample(inputSample);\n\n    const float dcBlocked = inputSample - previousInput_\n'''
    new = '''    observation = {};\n    inputSample = sanitiseAudioSample(inputSample);\n    if (std::abs(inputSample) > numericalPresenceSample)\n        presenceSinceLastHop_ = true;\n\n    const float dcBlocked = inputSample - previousInput_\n'''
    if old not in cpp:
        raise RuntimeError('processSample input block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    hopCounter_ = 0;\n    ++analysisHopCounter_;\n\n    ++fullRateCandidate_.ageInHops;\n'''
    new = '''    hopCounter_ = 0;\n    ++analysisHopCounter_;\n    presenceMode_ = presenceSinceLastHop_;\n    presenceSinceLastHop_ = false;\n\n    ++fullRateCandidate_.ageInHops;\n'''
    if old not in cpp:
        raise RuntimeError('hop block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    DecoderDecision decision = decodeCandidate(onsetPending_);\n'''
    new = '''    std::array<PitchCandidate, detectorPathCount> rawCandidates {};\n    const int rawDetectorSupport = collectFreshCandidates(rawCandidates);\n    DecoderDecision decision = decodeCandidate(onsetPending_);\n'''
    if old not in cpp:
        raise RuntimeError('decoder call not found')
    cpp = cpp.replace(old, new, 1)

    old = '''        observation.detectorSupport = trackedSupportCount_;\n        observation.octaveState = octaveState_;\n        observation.pendingOctaveObservations = pendingOctaveCount_;\n        observation.voicing = clamp01(rmsGate\n            * (0.48f * confidenceGate\n             + 0.30f * periodicityGate\n             + 0.22f * consensusGate));\n        observation.valid = observation.voicing > 0.08f;\n'''
    new = '''        observation.detectorSupport = std::max(trackedSupportCount_, rawDetectorSupport);\n        observation.octaveState = octaveState_;\n        observation.pendingOctaveObservations = pendingOctaveCount_;\n        const float detectorVoicing = clamp01(rmsGate\n            * (0.48f * confidenceGate\n             + 0.30f * periodicityGate\n             + 0.22f * consensusGate));\n        observation.audioPresent = presenceMode_;\n        observation.voicing = presenceMode_ ? 1.0f : detectorVoicing;\n        observation.valid = presenceMode_ || detectorVoicing > 0.08f;\n'''
    if old not in cpp:
        raise RuntimeError('valid observation block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''        observation.detectorSupport = trackedSupportCount_;\n        observation.octaveState = octaveState_;\n        observation.pendingOctaveObservations = pendingOctaveCount_;\n        observation.voicing = 0.0f;\n        observation.valid = false;\n'''
    new = '''        observation.detectorSupport = rawDetectorSupport;\n        observation.octaveState = octaveState_;\n        observation.pendingOctaveObservations = pendingOctaveCount_;\n        observation.audioPresent = presenceMode_;\n        observation.voicing = presenceMode_ ? 1.0f : 0.0f;\n        observation.valid = false;\n'''
    if old not in cpp:
        raise RuntimeError('invalid observation block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    const bool bodyPresent = bodyScore >= bodyThreshold\n        && (!richEvidence || parameters.voiceBreathiness < 0.76f\n            || parameters.voiceHarmonicity > 0.48f);\n'''
    new = '''    const bool bodyPresent = observation.audioPresent\n        || (bodyScore >= bodyThreshold\n            && (!richEvidence || parameters.voiceBreathiness < 0.76f\n                || parameters.voiceHarmonicity > 0.48f));\n'''
    if old not in cpp:
        raise RuntimeError('bodyPresent block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    const bool confirmedBreathFrame = richEvidence\n        && breathScore > 0.62f\n'''
    new = '''    const bool confirmedBreathFrame = richEvidence\n        && !observation.audioPresent\n        && breathScore > 0.62f\n'''
    if old not in cpp:
        raise RuntimeError('breath frame block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    const bool confirmedAbsenceFrame = richEvidence\n        && parameters.voiceBodyEnergy < 0.20f\n'''
    new = '''    const bool confirmedAbsenceFrame = richEvidence\n        && !observation.audioPresent\n        && parameters.voiceBodyEnergy < 0.20f\n'''
    if old not in cpp:
        raise RuntimeError('absence frame block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    if (!validPitch)\n    {\n        ++state.invalidObservations;\n\n        // Missing F0 is not missing voice. A latched note keeps the exact\n'''
    new = '''    if (!validPitch)\n    {\n        ++state.invalidObservations;\n\n        // Audio presence is authoritative for voiced/unvoiced classification.\n        // F0 confidence may fall to zero on rap, consonants or rough phonation,\n        // but that is a pitch-search problem, never permission to call a\n        // non-silent vocal signal unvoiced or timidly leave it in acquire.\n        if (observation.audioPresent)\n        {\n            state.noteBodyLatched = true;\n            state.noteBodyConfidence = 1.0f;\n            state.stableBodyObservations = std::max(4, state.stableBodyObservations);\n            state.breathEvidenceSamples = 0;\n            state.uncertainSamples = 0;\n            setState(TrackingState::stable);\n            return;\n        }\n\n        // Missing F0 is not missing voice. A latched note keeps the exact\n'''
    if old not in cpp:
        raise RuntimeError('invalid pitch state block not found')
    cpp = cpp.replace(old, new, 1)

    old = '''    rescueMode_ = false;\n\n    octaveState_ = 0;\n'''
    new = '''    rescueMode_ = false;\n    presenceMode_ = false;\n    presenceSinceLastHop_ = false;\n\n    octaveState_ = 0;\n'''
    if old not in cpp:
        raise RuntimeError('tracker reset state not found')
    cpp = cpp.replace(old, new, 1)

    cpp_path.write_text(cpp)

if 'audioPresent = false' not in h:
    old = '''        bool valid = false;\n        bool onset = false;\n'''
    new = '''        bool valid = false;\n        bool onset = false;\n        bool audioPresent = false;\n'''
    if old not in h:
        raise RuntimeError('PitchObservation bool block not found')
    h = h.replace(old, new, 1)

if 'presenceMode_ = false' not in h:
    old = '''        float sensitivity_ = 0.70f;\n        bool rescueMode_ = false;\n'''
    new = '''        float sensitivity_ = 0.70f;\n        bool rescueMode_ = false;\n        bool presenceMode_ = false;\n        bool presenceSinceLastHop_ = false;\n'''
    if old not in h:
        raise RuntimeError('tracker mode fields not found')
    h = h.replace(old, new, 1)

h_path.write_text(h)

if TEST_MARKER not in test:
    anchor = '''    auto engine = std::make_unique<ModernPitchEngine>();\n    engine->prepare(48000.0, 256, 1, ModernPitchEngine::LatencyMode::live);\n'''
    insertion = r'''    // Rap/rough-speech regression: even deliberately aperiodic non-zero audio
    // must publish authoritative voice presence and must keep detector paths
    // alive instead of collapsing to 0/4 before the supervisor can search F0.
    auto rapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    rapTracker->prepare(48000.0);
    rapTracker->setRange(45.0f, 900.0f);
    rapTracker->setSensitivity(0.70f);
    std::uint32_t rapNoise = 0x12345678u;
    int rapPresenceHops = 0;
    int rapMaxDetectorSupport = 0;
    float rapMinimumVoicing = 1.0f;
    ModernPitchEngine::PitchObservation rapObservation;
    for (int sample = 0; sample < 12000; ++sample)
    {
        rapNoise = 1664525u * rapNoise + 1013904223u;
        const float noise = (static_cast<float>((rapNoise >> 8) & 0x00ffffffu)
            / static_cast<float>(0x007fffffu) - 1.0f) * 0.085f;
        const float syllabic = (sample % 1100) < 760 ? 1.0f : 0.22f;
        if (rapTracker->processSample(noise * syllabic, rapObservation)
            && sample > 1400 && rapObservation.audioPresent)
        {
            ++rapPresenceHops;
            rapMaxDetectorSupport = std::max(rapMaxDetectorSupport,
                                             rapObservation.detectorSupport);
            rapMinimumVoicing = std::min(rapMinimumVoicing,
                                         rapObservation.voicing);
        }
    }
    success &= check(rapPresenceHops > 20 && rapMinimumVoicing > 0.99f,
                     "nonzero_audio_cannot_be_unvoiced");
    success &= check(rapMaxDetectorSupport > 0,
                     "nonzero_audio_keeps_detector_paths_alive");

''' + anchor
    if anchor not in test:
        raise RuntimeError('engine creation test anchor not found')
    test = test.replace(anchor, insertion, 1)

    anchor2 = '''    ModernPitchEngine::ScaleQuantizer quantizer;\n    quantizer.reset();\n    ModernPitchEngine::Parameters parameters;\n'''
    replacement2 = anchor2 + r'''    // Even if the rich analyzer describes the current block as breath/noise,
    // explicit input presence owns the voice state. Detector uncertainty is
    // allowed to affect F0 search, never voiced/unvoiced authority.
    ModernPitchEngine::ScaleQuantizer presenceQuantizer;
    presenceQuantizer.reset();
    ModernPitchEngine::Parameters presenceParameters;
    setBreathEvidence(presenceParameters);
    ModernPitchEngine::CorrectionState presenceState;
    ModernPitchEngine::PitchObservation presentWithoutF0;
    presentWithoutF0.audioPresent = true;
    presentWithoutF0.voicing = 1.0f;
    engine->updateCorrectionState(presenceState, presenceQuantizer,
                                  presentWithoutF0, presenceParameters);
    success &= check(presenceState.noteBodyLatched
                     && presenceState.trackingState == ModernPitchEngine::TrackingState::stable,
                     "audio_presence_overrides_unvoiced_and_acquire_timidity");

'''
    if anchor2 not in test:
        raise RuntimeError('parameter test anchor not found')
    test = test.replace(anchor2, replacement2, 1)
    test_path.write_text(test)

print('Rap voicing V1 audio-presence authority applied')
