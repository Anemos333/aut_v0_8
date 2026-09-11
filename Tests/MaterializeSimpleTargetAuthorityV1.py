#!/usr/bin/env python3
from pathlib import Path

source_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')
source = source_path.read_text(encoding='utf-8')
tests = test_path.read_text(encoding='utf-8')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)


old_breath = """    if (state.targetValid && (confirmedBreath || confirmedAbsence))
    {
        setState(TrackingState::release);
        state.desiredCents = 0.0;
        state.stableBodyObservations = 0;
        const double protection = static_cast<double>(
            clamp01(parameters.transientProtection));
        state.responseMs = std::clamp(32.0 - 20.0 * protection,
                                      8.0, 32.0);
        if (confirmedAbsence
            || state.breathEvidenceSamples > static_cast<int>(0.12 * sampleRate_))
        {
            state.pitchCentreValid = false;
        }
        return;
    }
"""
new_breath = """    // TARGET_AUTHORITY_TAIL_HOLD_V1: once a musical target exists, breath or
    // temporary absence may stop supplying a trustworthy F0, but it is not
    // permission to move the audio back toward the source pitch. The current
    // frame evidence already exists here, so do not wait for temporal breath
    // confirmation while a spurious periodicity is free to retarget the note.
    if (state.targetValid
        && (confirmedBreath
            || confirmedAbsence
            || (richEvidence
                && (confirmedBreathFrame || confirmedAbsenceFrame))))
    {
        state.stableBodyObservations = 0;
        if (confirmedAbsence
            || confirmedAbsenceFrame
            || state.breathEvidenceSamples > static_cast<int>(0.12 * sampleRate_))
        {
            state.pitchCentreValid = false;
        }
        return;
    }
"""
source = replace_once(source, old_breath, new_breath, 'breath release block')

old_missing = """        if (state.targetValid && std::abs(state.currentCents) > 0.001)
        {
            setState(TrackingState::release);
            state.desiredCents = 0.0;
            state.responseMs = std::clamp(32.0 - 20.0
                * static_cast<double>(clamp01(parameters.transientProtection)),
                8.0, 32.0);
        }
        else
        {
            setState(TrackingState::unvoiced);
        }
        return;
"""
new_missing = """        // TARGET_AUTHORITY_TAIL_HOLD_V1: a previously selected scale degree
        // remains the destination through pitchless material. No hidden release
        // is allowed to undo the correction and reveal the source note.
        if (state.targetValid)
            setState(TrackingState::acquire);
        else
            setState(TrackingState::unvoiced);
        return;
"""
source = replace_once(source, old_missing, new_missing, 'missing-F0 release block')

start_marker = """    // Positive breath evidence releases the note even if the tracker happens to
"""
end_marker = """                     \"unvoiced_release_reaches_unity_and_clears_note_latch\");
"""
start = tests.find(start_marker)
if start < 0:
    raise RuntimeError('old breath-test start marker missing')
end = tests.find(end_marker, start)
if end < 0:
    raise RuntimeError('old breath-test end marker missing')
end += len(end_marker)
replacement = """    // TARGET_AUTHORITY_TAIL_HOLD_V1: once a target exists, breath/noise is
    // allowed to remove confidence in the current F0 but never to undo the
    // requested pitch displacement. A spurious periodic accident in breath
    // must therefore be ignored rather than retargeting or releasing to source.
    ModernPitchEngine::CorrectionState spuriousBreath = dropoutState;
    setBreathEvidence(parameters);
    const auto falsePitchOnBreath = strongPitch(231.0f);
    for (int i = 0; i < 100; ++i)
        engine->updateCorrectionState(spuriousBreath, quantizer,
                                      falsePitchOnBreath, parameters);
    success &= check(spuriousBreath.trackingState != ModernPitchEngine::TrackingState::release
                     && spuriousBreath.targetValid
                     && std::abs(spuriousBreath.desiredCents - 100.0) < 1.0e-9,
                     \"breath_cannot_release_existing_target_to_source\");

    // The same invariant holds when the detector correctly reports no F0.
    ModernPitchEngine::CorrectionState pitchlessTail = dropoutState;
    for (int i = 0; i < 100; ++i)
        engine->updateCorrectionState(pitchlessTail, quantizer, invalid, parameters);
    success &= check(pitchlessTail.trackingState != ModernPitchEngine::TrackingState::release
                     && pitchlessTail.targetValid
                     && std::abs(pitchlessTail.desiredCents - 100.0) < 1.0e-9,
                     \"pitchless_tail_keeps_arrival_scale_degree\");

    for (int i = 0; i < 4800; ++i)
        static_cast<void>(engine->advanceCorrection(pitchlessTail));
    success &= check(std::abs(pitchlessTail.currentCents - 100.0) < 1.0e-9,
                     \"pitchless_tail_never_glides_back_to_source\");
"""
tests = tests[:start] + replacement + tests[end:]

source_path.write_text(source, encoding='utf-8')
test_path.write_text(tests, encoding='utf-8')
print('TARGET_AUTHORITY_TAIL_HOLD_V1=materialized')
