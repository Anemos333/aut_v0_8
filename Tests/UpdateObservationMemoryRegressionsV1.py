from pathlib import Path

root = Path(__file__).resolve().parents[1]
test_path = root / 'Tests' / 'SupervisorContinuityTest.cpp'
test = test_path.read_text()


def one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


# The historical regression used literal zeros to model a detector hole and
# required the old note-body anchor to survive. That is now intentionally wrong:
# literal zero is a physical discontinuity and must clear detector memory.
# Preserve the useful old contract by modelling what it actually meant: audio is
# still physically present, but it is aperiodic and therefore has no trustworthy
# F0. In that case a short physical-continuity prior may survive.
test = one(test,
'''    ModernPitchEngine::PitchObservation expiredObservation;
    const int dropoutSamples = static_cast<int>(0.075 * 48000.0);
    for (int sample = 0; sample < dropoutSamples; ++sample)
        static_cast<void>(delayedRescueTracker->processSample(0.0f, expiredObservation));
    success &= check(delayedRescueTracker->trackedPitchHz_ == 0.0f
                     && std::abs(delayedRescueTracker->reacquisitionAnchorHz_ - 220.0f) < 0.01f,
                     "current_f0_can_expire_without_erasing_note_body_anchor");
''',
'''    ModernPitchEngine::PitchObservation expiredObservation;
    const int dropoutSamples = static_cast<int>(0.075 * 48000.0);
    std::uint32_t aperiodicGapRng = 0x7f4a7c15u;
    for (int sample = 0; sample < dropoutSamples; ++sample)
    {
        aperiodicGapRng ^= aperiodicGapRng << 13;
        aperiodicGapRng ^= aperiodicGapRng >> 17;
        aperiodicGapRng ^= aperiodicGapRng << 5;
        const float noise = (static_cast<float>(aperiodicGapRng & 0xffffu)
                           / 32767.5f - 1.0f) * 0.004f;
        static_cast<void>(delayedRescueTracker->processSample(noise, expiredObservation));
    }
    success &= check(delayedRescueTracker->trackedPitchHz_ == 0.0f
                     && std::abs(delayedRescueTracker->reacquisitionAnchorHz_ - 220.0f) < 0.01f,
                     "current_f0_can_expire_during_aperiodic_presence_without_erasing_physical_anchor");
''',
'aperiodic detector-hole regression')

test = one(test,
'''                     "rescue_uses_persistent_anchor_after_sixty_ms_detector_hole");
''',
'''                     "rescue_uses_physical_anchor_after_aperiodic_detector_hole");
''',
'physical anchor label')

test_path.write_text(test)
print('OBSERVATION_MEMORY_REGRESSIONS_V1 materialized')
