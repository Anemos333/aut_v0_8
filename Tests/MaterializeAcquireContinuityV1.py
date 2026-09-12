from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)

cpp_p = Path('Source/ModernPitchEngine.cpp')
supervisor_p = Path('Tests/SupervisorContinuityTest.cpp')
contract_p = Path('Tests/GuiAudioControlContractTest.cpp')

cpp = cpp_p.read_text()
supervisor = supervisor_p.read_text()
contract = contract_p.read_text()

cpp = one(cpp,
'''        const bool sameInitial = pendingOctaveFrequencyHz_ > 0.0f
            && centsDistance(pendingOctaveFrequencyHz_,
                             decision.candidate.frequencyHz) < 80.0f;
''',
'''        // FAST_INITIAL_ACQUIRE_V1: first acquisition may tolerate ordinary
        // vibrato/jitter between fresh observations. Harmonic/register jumps
        // remain far outside this window and still restart confirmation.
        const bool sameInitial = pendingOctaveFrequencyHz_ > 0.0f
            && centsDistance(pendingOctaveFrequencyHz_,
                             decision.candidate.frequencyHz) < 120.0f;
''',
'initial continuity window')

cpp = one(cpp,
'''        const bool exceptionalEvidence = decision.supportCount >= 2
            && decision.directSupportCount >= 2
            && decision.candidate.confidence >= 0.90f
            && decision.consensus >= 0.82f;
        const int requiredObservations = exceptionalEvidence ? 1 : 2;
''',
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
'initial confirmation speed')

cpp = one(cpp,
'''    const bool sufficientInitialEvidence = decision.supportCount >= 2
        || decision.candidate.confidence >= 0.78f;
''',
'''    // FAST_INITIAL_ACQUIRE_V1: relax only a genuinely unowned first lock.
    // Existing tracking and reacquisition anchors keep the previous stronger
    // evidence rule, so a single detector family cannot override musical
    // history just because startup acquisition was made faster.
    const bool genuinelyUnownedInitial = trackedPitchHz_ <= 0.0f
        && reacquisitionAnchorHz_ <= 0.0f;
    const bool freshDirectInitialEvidence = genuinelyUnownedInitial
        && decision.freshSupportMask != 0
        && decision.directSupportCount >= 1
        && decision.candidate.confidence >= 0.46f
        && decision.candidate.periodicity >= 0.52f;
    const bool sufficientInitialEvidence = decision.supportCount >= 2
        || decision.candidate.confidence >= 0.78f
        || freshDirectInitialEvidence;
''',
'initial evidence gate')

cpp = one(cpp,
'''        if (changed)
        {
            // SCALE_CHANGE_REBASE_V1: invalidate ownership until the first
            // trustworthy coordinate is quantized in the new scale. The audio
            // path below fails closed during this tiny reacquisition window.
            channelCorrections_[static_cast<std::size_t>(channel)] = {};
        }
''',
'''        if (changed)
        {
            // SCALE_CHANGE_PRESERVES_AUDIO_CONTINUITY_V1: a UI scale/root
            // change resets the quantizer's identity, not the audible transport.
            // The previous owned correction remains continuous until the next
            // accepted coordinate is immediately quantized in the new scale.
        }
''',
'dual mono scale change continuity')

cpp = one(cpp,
'''    if (linkedScaleChanged)
    {
        // SCALE_CHANGE_REBASE_V1
        linkedCorrection_ = {};
    }
''',
'''    if (linkedScaleChanged)
    {
        // SCALE_CHANGE_PRESERVES_AUDIO_CONTINUITY_V1: never manufacture an
        // audio hole merely because the selected scale changed between blocks.
    }
''',
'linked scale change continuity')

cpp = one(cpp,
'''                // UNOWNED_AUDIO_FAILS_CLOSED_V1: before a degree exists there
                // is no legal unity/source-pitch output in the active path.
                data[static_cast<std::size_t>(channel)][sample] =
                    correction.targetValid ? rendered : 0.0f;
''',
'''                // NO_AUDIO_DROPOUT_ON_UNCERTAINTY_V1: detector uncertainty
                // may delay musical ownership, never audio continuity. Before
                // the first credible F0 the same single renderer runs at its
                // current transport (initially 1:1); after first lock, missing
                // evidence keeps the already-owned correction instead.
                data[static_cast<std::size_t>(channel)][sample] = rendered;
''',
'dual mono no dropout')

cpp = one(cpp,
'''                // UNOWNED_AUDIO_FAILS_CLOSED_V1
                data[static_cast<std::size_t>(channel)][sample] =
                    linkedCorrection_.targetValid ? rendered : 0.0f;
''',
'''                // NO_AUDIO_DROPOUT_ON_UNCERTAINTY_V1
                data[static_cast<std::size_t>(channel)][sample] = rendered;
''',
'linked no dropout')

supervisor = one(supervisor,
'''    auto first = makeInitialDecision();
    const bool firstAccepted = tracker->confirmOctaveTransition(first, true);
    auto second = makeInitialDecision();
    const bool secondAccepted = tracker->confirmOctaveTransition(second, true);
    success &= check(!firstAccepted && !first.valid,
                     "initial_register_waits_for_repeat");
    success &= check(secondAccepted,
                     "repeated_initial_register_is_committed");
''',
'''    auto first = makeInitialDecision();
    const bool firstAccepted = tracker->confirmOctaveTransition(first, true);
    success &= check(firstAccepted && first.valid,
                     "credible_multi_evidence_initial_register_commits_immediately");

    auto cautiousTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    cautiousTracker->prepare(48000.0);
    auto makeSingleFamilyInitial = []
    {
        ModernPitchEngine::MultiRatePitchTracker::DecoderDecision d;
        d.valid = true;
        d.candidate.valid = true;
        d.candidate.frequencyHz = 440.0f;
        d.candidate.confidence = 0.54f;
        d.candidate.periodicity = 0.62f;
        d.consensus = 0.18f;
        d.supportCount = 1;
        d.directSupportCount = 1;
        d.freshSupportMask = 1;
        return d;
    };
    auto cautiousFirst = makeSingleFamilyInitial();
    const bool cautiousFirstAccepted = cautiousTracker->confirmOctaveTransition(
        cautiousFirst, false);
    auto cautiousSecond = makeSingleFamilyInitial();
    const bool cautiousSecondAccepted = cautiousTracker->confirmOctaveTransition(
        cautiousSecond, false);
    success &= check(!cautiousFirstAccepted && !cautiousFirst.valid,
                     "single_family_initial_register_still_needs_repeat");
    success &= check(cautiousSecondAccepted && cautiousSecond.valid,
                     "repeated_single_family_initial_register_can_commit");
''',
'initial acquire tests')

contract = one(contract,
'''    success &= check(has(processor, "HOST_BYPASS_ONLY_DRY_V1")
                         && has(processor, "livePitchProcessor.processBypassed (buffer)")
                         && has(renderer, "ACTIVE_PATH_NEVER_DRY_FALLBACK_V1"),
                     "dry_exists_only_as_explicit_host_bypass");
''',
'''    success &= check(has(processor, "HOST_BYPASS_ONLY_DRY_V1")
                         && has(processor, "livePitchProcessor.processBypassed (buffer)")
                         && has(renderer, "ACTIVE_PATH_NEVER_DRY_FALLBACK_V1"),
                     "dry_exists_only_as_explicit_host_bypass");

    success &= check(has(engine, "NO_AUDIO_DROPOUT_ON_UNCERTAINTY_V1")
                         && has(engine, "FAST_INITIAL_ACQUIRE_V1")
                         && has(engine, "SCALE_CHANGE_PRESERVES_AUDIO_CONTINUITY_V1")
                         && !has(engine, "UNOWNED_AUDIO_FAILS_CLOSED_V1")
                         && !has(engine, "correction.targetValid ? rendered : 0.0f")
                         && !has(engine, "linkedCorrection_.targetValid ? rendered : 0.0f"),
                     "detector_uncertainty_never_mutes_active_audio");
''',
'no dropout static contract')

cpp_p.write_text(cpp)
supervisor_p.write_text(supervisor)
contract_p.write_text(contract)
print('acquire continuity patch materialized')
