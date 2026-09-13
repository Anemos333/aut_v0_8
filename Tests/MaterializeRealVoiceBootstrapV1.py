from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)

cpp_p = Path('Source/ModernPitchEngine.cpp')
supervisor_p = Path('Tests/SupervisorContinuityTest.cpp')

cpp = cpp_p.read_text()
supervisor = supervisor_p.read_text()

cpp = one(cpp,
'''    const bool freshDirectInitialEvidence = genuinelyUnownedInitial
        && decision.freshSupportMask != 0
        && decision.directSupportCount >= 1
        && decision.candidate.confidence >= 0.46f
        && decision.candidate.periodicity >= 0.52f;
''',
'''    // REAL_VOICE_BOOTSTRAP_V1: cross-path consensus is useful after a note
    // exists, but it must not veto the first musical ownership. During the
    // genuinely unowned bootstrap, judge a fresh direct candidate by the raw
    // hypothesis quality before consensus attenuation. No candidate means no
    // invented F0; this only stops four conservative paths from mutually
    // preventing a real vocal onset from ever acquiring.
    const bool freshDirectInitialEvidence = genuinelyUnownedInitial
        && decision.freshSupportMask != 0
        && decision.directSupportCount >= 1
        && hypothesis.confidence >= 0.40f
        && hypothesis.periodicity >= 0.48f
        && hypothesis.evidenceScore >= 0.22f;
''',
'bootstrap raw evidence gate')

cpp = one(cpp,
'''        // FAST_INITIAL_ACQUIRE_V1: two independent detector families with
        // fresh direct support are enough to establish the first register in
        // one hop. A single family still needs repetition. This is deliberately
        // less permissive than an already-owned octave change: speed is gained
        // only before musical identity exists, not by weakening register guards.
        const bool fastInitialEvidence = decision.supportCount >= 2
            && decision.directSupportCount >= 1
            && decision.freshSupportMask != 0
            && decision.candidate.confidence >= 0.62f
            && decision.candidate.periodicity >= 0.58f
            && decision.consensus >= 0.32f;
        const int requiredObservations = fastInitialEvidence ? 1 : 2;
''',
'''        // REAL_VOICE_BOOTSTRAP_V1: the first target is provisional musical
        // ownership, not a claim of perfect detector certainty. A real fresh
        // direct candidate on audible material may establish it immediately;
        // subsequent tracking, octave changes and rescue still use the normal
        // conservative guards. This prevents permanent acquire/bypass without
        // fabricating an F0 when every detector is genuinely empty.
        const bool realVoiceBootstrapEvidence = reacquisitionAnchorHz_ <= 0.0f
            && presenceMode_
            && decision.directSupportCount >= 1
            && decision.freshSupportMask != 0
            && decision.candidate.confidence >= 0.34f
            && decision.candidate.periodicity >= 0.48f;
        const bool multiPathFastInitialEvidence = decision.supportCount >= 2
            && decision.directSupportCount >= 1
            && decision.freshSupportMask != 0
            && decision.candidate.confidence >= 0.62f
            && decision.candidate.periodicity >= 0.58f
            && decision.consensus >= 0.32f;
        const int requiredObservations = (realVoiceBootstrapEvidence
            || multiPathFastInitialEvidence) ? 1 : 2;
''',
'bootstrap immediate commit')

anchor = '''    success &= check(cautiousSecondAccepted && cautiousSecond.valid,
                     "repeated_single_family_initial_register_can_commit");


    auto rescueTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
'''
insert = '''    success &= check(cautiousSecondAccepted && cautiousSecond.valid,
                     "repeated_single_family_initial_register_can_commit");

    // REAL_VOICE_BOOTSTRAP_V1: exercise the actual raw-candidate -> consensus
    // -> decoder -> initial-register path. The previous test constructed an
    // already-approved DecoderDecision and therefore missed the real vocal
    // failure where consensus attenuation could veto acquire forever.
    auto vocalBootstrapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    vocalBootstrapTracker->prepare(48000.0);
    vocalBootstrapTracker->presenceMode_ = true;
    auto& vocalSlot = vocalBootstrapTracker->halfRateCandidate_;
    vocalSlot.candidate.valid = true;
    vocalSlot.candidate.frequencyHz = 220.0f;
    vocalSlot.candidate.confidence = 0.54f;
    vocalSlot.candidate.periodicity = 0.62f;
    vocalSlot.candidate.pathIndex = 1;
    vocalSlot.candidate.ageInHops = 0;
    vocalSlot.ageInHops = 0;
    auto vocalBootstrapDecision = vocalBootstrapTracker->decodeCandidate(false);
    const bool vocalBootstrapAccepted = vocalBootstrapTracker->confirmOctaveTransition(
        vocalBootstrapDecision, false);
    success &= check(vocalBootstrapDecision.valid && vocalBootstrapAccepted,
                     "real_voice_single_fresh_path_can_bootstrap_first_target");

    auto weakBootstrapTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    weakBootstrapTracker->prepare(48000.0);
    weakBootstrapTracker->presenceMode_ = true;
    auto& weakSlot = weakBootstrapTracker->halfRateCandidate_;
    weakSlot.candidate.valid = true;
    weakSlot.candidate.frequencyHz = 220.0f;
    weakSlot.candidate.confidence = 0.24f;
    weakSlot.candidate.periodicity = 0.38f;
    weakSlot.candidate.pathIndex = 1;
    weakSlot.candidate.ageInHops = 0;
    weakSlot.ageInHops = 0;
    const auto weakBootstrapDecision = weakBootstrapTracker->decodeCandidate(false);
    success &= check(!weakBootstrapDecision.valid,
                     "weak_single_path_does_not_fabricate_first_f0");


    auto rescueTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
'''
supervisor = one(supervisor, anchor, insert, 'real voice bootstrap tests')

cpp_p.write_text(cpp)
supervisor_p.write_text(supervisor)
print('real voice bootstrap patch materialized')
