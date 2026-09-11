#!/usr/bin/env python3
from pathlib import Path
import re

ENGINE = Path('Source/ModernPitchEngine.cpp')
TEST = Path('Tests/SupervisorContinuityTest.cpp')

engine = ENGINE.read_text(encoding='utf-8')

# These timers became dead authority after breath/absence stopped being allowed
# to release toward unity. Keep raw evidence counters for diagnostics, but remove
# the obsolete temporal confirmation that no longer owns any musical decision.
pattern = re.compile(r'''\n    const int breathConfirmSamples = static_cast<int>\(std::lround\(\n        sampleRate_ \* \(0\.040 \+ 0\.020 \* static_cast<double>\(humanize\)\)\)\);\n    const int ambiguousReleaseSamples = static_cast<int>\(std::lround\(\n        sampleRate_ \* \(0\.160 \+ 0\.080 \* static_cast<double>\(humanize\)\)\)\);\n    const bool confirmedBreath = state\.noteBodyLatched\n        && state\.breathEvidenceSamples >= breathConfirmSamples;\n    const bool confirmedAbsence = state\.noteBodyLatched\n        && state\.uncertainSamples >= ambiguousReleaseSamples;\n''')
engine, n = pattern.subn('\n', engine, count=1)
assert n == 1, f'dead breath/absence authority timers replacement count={n}'

# There must be no automatic musical return to unity in the correction state.
assert 'state.desiredCents = 0.0' not in engine
assert 'setState(TrackingState::release)' not in engine
assert 'breathConfirmSamples' not in engine
assert 'ambiguousReleaseSamples' not in engine
ENGINE.write_text(engine, encoding='utf-8')

test = TEST.read_text(encoding='utf-8')
pattern = re.compile(r'''    // Positive breath evidence releases the note even if the tracker happens to\n    // produce a strong spurious F0 on the noise\..*?\n    parameters\.retuneTimeMs = 0\.0f;''', re.S)
replacement = r'''    // ONE_VOICE_BREATH_GLIDE_V11: breath is part of the same waveform, never
    // permission to release toward unity. If a valid F0 exists it retains full
    // authority and is quantized normally, even when breath evidence is high.
    ModernPitchEngine::CorrectionState spuriousBreath = dropoutState;
    setBreathEvidence(parameters);
    const auto falsePitchOnBreath = strongPitch(231.0f);
    for (int i = 0; i < 100; ++i)
    {
        engine->updateCorrectionState(spuriousBreath, quantizer,
                                      falsePitchOnBreath, parameters);
        for (int s = 0; s < ModernPitchEngine::MultiRatePitchTracker::hopSize(); ++s)
            static_cast<void>(engine->advanceCorrection(spuriousBreath));
    }
    success &= check(spuriousBreath.targetValid
                     && spuriousBreath.trackingState != ModernPitchEngine::TrackingState::release
                     && spuriousBreath.trackingState != ModernPitchEngine::TrackingState::unvoiced
                     && std::abs(spuriousBreath.desiredCents) > 1.0e-6,
                     "breath_evidence_cannot_veto_valid_f0");

    // If periodicity disappears during a breath/tail, the already-owned musical
    // destination is preserved exactly. The state is Acquire/search telemetry;
    // it is not an audio release and it must never imply dry/unity.
    ModernPitchEngine::CorrectionState breathTail = dropoutState;
    const double heldBreathDestination = breathTail.desiredCents;
    for (int i = 0; i < 100; ++i)
    {
        engine->updateCorrectionState(breathTail, quantizer, invalid, parameters);
    }
    success &= check(breathTail.targetValid
                     && breathTail.noteBodyLatched
                     && breathTail.trackingState == ModernPitchEngine::TrackingState::acquire
                     && std::abs(breathTail.desiredCents - heldBreathDestination) < 1.0e-9,
                     "breath_without_f0_holds_exact_musical_destination");

    double maximumTailStep = 0.0;
    double previous = breathTail.currentCents;
    for (int i = 0; i < 4800; ++i)
    {
        const double current = engine->advanceCorrection(breathTail);
        maximumTailStep = std::max(maximumTailStep, std::abs(current - previous));
        previous = current;
    }
    std::cerr << "maximum_breath_tail_glide_step_cents=" << maximumTailStep << '\n';
    success &= check(maximumTailStep < 1.0,
                     "breath_tail_glide_has_no_correction_jump");
    success &= check(std::abs(breathTail.currentCents - heldBreathDestination) < 0.05
                     && std::abs(breathTail.currentCents) > 0.05
                     && breathTail.trackingState == ModernPitchEngine::TrackingState::acquire
                     && breathTail.noteBodyLatched,
                     "breath_tail_never_returns_to_unity_or_clears_voice");

    // Restore ordinary body evidence for the remaining trajectory tests.
    setBodyEvidence(parameters);
    parameters.retuneTimeMs = 0.0f;'''
test, n = pattern.subn(replacement, test, count=1)
assert n == 1, f'obsolete breath-release test replacement count={n}'

for forbidden in (
    'breath_wins_over_spurious_valid_f0',
    'confirmed_breath_releases_missing_f0_note',
    'unvoiced_release_reaches_unity_and_clears_note_latch',
):
    assert forbidden not in test, forbidden
for required in (
    'breath_evidence_cannot_veto_valid_f0',
    'breath_without_f0_holds_exact_musical_destination',
    'breath_tail_never_returns_to_unity_or_clears_voice',
):
    assert required in test, required
TEST.write_text(test, encoding='utf-8')
print('ONE_VOICE_BREATH_GLIDE_V11_SEMANTIC_CLEANUP=PASS')
