from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')

SOURCE_MARKER = 'SOUND_EQUALS_CORRECTION_V2'
TEST_MARKER = 'zero_consensus_stale_register_never_collapses_to_zero'


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise RuntimeError(f'{label}: expected source block not found')
    return text.replace(old, new, 1)


if SOURCE_MARKER not in cpp:
    # A live F0 that has clearly left the current scale-degree identity must
    # immediately own the pitch centre. Waiting for confidence/consensus here
    # can leave an old register alive long enough for octave wrapping to turn a
    # real correction into exactly zero cents.
    old = '''    const double observedLog2 = safeLog2(observation.frequencyHz);
    if (!state.pitchCentreValid || musicalOnset)
    {
        state.pitchCentreLog2 = observedLog2;
        state.pitchCentreValid = true;
        state.stableObservations = 0;
    }
    else
    {
        const double distanceCents = std::abs(observedLog2 - state.pitchCentreLog2) * 1200.0;
        const double scaleStep = std::max(0.1,
            static_cast<double>(quantizer.minimumStepCents()));
        const double maximumWithinNoteTolerance = std::clamp(
            0.42 * scaleStep, 0.5, 60.0);
        const double withinNoteTolerance = std::min(
            22.0 + 38.0 * static_cast<double>(humanize),
            maximumWithinNoteTolerance);
        double baseAlpha = distanceCents > 95.0 ? 0.30 : 0.07;
        const double observedDistanceFromCurrentTarget = state.targetValid
            ? std::abs(observedLog2 - state.targetLog2) * 1200.0
            : 0.0;
        const double currentIdentityRadius = 0.48 * scaleStep;
        const bool insideCurrentMusicalIdentity = !state.targetValid
            || observedDistanceFromCurrentTarget < currentIdentityRadius;
        if (state.noteBodyLatched
            && insideCurrentMusicalIdentity
            && distanceCents <= withinNoteTolerance)
        {
            baseAlpha = 0.018 + 0.035 * static_cast<double>(1.0f - humanize);
        }
        const double stableGate = 0.35
            + 0.65 * static_cast<double>(clamp01(observation.confidence)
                                      * clamp01(observation.periodicity));
        state.pitchCentreLog2 += baseAlpha * stableGate
            * (observedLog2 - state.pitchCentreLog2);
        ++state.stableObservations;
    }
'''
    new = '''    const double observedLog2 = safeLog2(observation.frequencyHz);
    bool liveIdentityBreak = false;
    if (!state.pitchCentreValid || musicalOnset)
    {
        state.pitchCentreLog2 = observedLog2;
        state.pitchCentreValid = true;
        state.stableObservations = 0;
    }
    else
    {
        const double distanceCents = std::abs(observedLog2 - state.pitchCentreLog2) * 1200.0;
        const double scaleStep = std::max(0.1,
            static_cast<double>(quantizer.minimumStepCents()));
        const double maximumWithinNoteTolerance = std::clamp(
            0.42 * scaleStep, 0.5, 60.0);
        const double withinNoteTolerance = std::min(
            22.0 + 38.0 * static_cast<double>(humanize),
            maximumWithinNoteTolerance);
        double baseAlpha = distanceCents > 95.0 ? 0.30 : 0.07;
        const double observedDistanceFromCurrentTarget = state.targetValid
            ? std::abs(observedLog2 - state.targetLog2) * 1200.0
            : 0.0;
        const double currentIdentityRadius = 0.72 * scaleStep;
        const bool insideCurrentMusicalIdentity = !state.targetValid
            || observedDistanceFromCurrentTarget < currentIdentityRadius;

        // SOUND_EQUALS_CORRECTION_V2: live pitch outside the current musical
        // identity is a new authority immediately. Consensus/confidence may
        // describe evidence quality, but cannot make the supervisor crawl from
        // a stale centre while audible input is already somewhere else.
        liveIdentityBreak = observation.audioPresent
            && state.targetValid
            && !insideCurrentMusicalIdentity
            && distanceCents >= currentIdentityRadius;
        if (liveIdentityBreak)
        {
            state.pitchCentreLog2 = observedLog2;
            state.stableObservations = 0;
        }
        else
        {
            if (state.noteBodyLatched
                && insideCurrentMusicalIdentity
                && distanceCents <= withinNoteTolerance)
            {
                baseAlpha = 0.018 + 0.035 * static_cast<double>(1.0f - humanize);
            }
            const double stableGate = 0.35
                + 0.65 * static_cast<double>(clamp01(observation.confidence)
                                          * clamp01(observation.periodicity));
            state.pitchCentreLog2 += baseAlpha * stableGate
                * (observedLog2 - state.pitchCentreLog2);
            ++state.stableObservations;
        }
    }
'''
    cpp = replace_once(cpp, old, new, 'immediate live identity recenter')

    old = '''        parameters.scaleLock && parameters.hardLockActive,
        musicalOnset,
        pending);
    newTarget += std::round(state.pitchCentreLog2 - newTarget);
'''
    new = '''        parameters.scaleLock && parameters.hardLockActive,
        musicalOnset || liveIdentityBreak,
        pending);

    // SOUND_EQUALS_CORRECTION_V2: target register follows the current live F0,
    // never a stale centre. This keeps an octave/register mistake from becoming
    // a mathematically zero correction.
    newTarget += std::round(observedLog2 - newTarget);
'''
    cpp = replace_once(cpp, old, new, 'force live target commit and register alignment')

    cpp = replace_once(
        cpp,
        '            || musicalOnset || targetIdentityChanged)\n',
        '            || musicalOnset || liveIdentityBreak || targetIdentityChanged)\n',
        'refresh transport period on live identity break')

    old = '''    double errorCents = wrapToNearestOctave(
        (correctedLog2 - observedLog2) * 1200.0);
'''
    new = '''    // SOUND_EQUALS_CORRECTION_V2: targetLog2 and observedLog2 are absolute
    // pitches in the same live register. Never wrap their error by an octave:
    // +/-1200 cents must not collapse to zero and create an audible bypass.
    double errorCents = (correctedLog2 - observedLog2) * 1200.0;
'''
    cpp = replace_once(cpp, old, new, 'remove octave-to-zero correction wrap')

    cpp_path.write_text(cpp, encoding='utf-8')


if TEST_MARKER not in test:
    anchor = '''    success &= check(heldCorrectionState.trackingState
                         == ModernPitchEngine::TrackingState::acquire
                     && heldCorrectionState.targetValid
                     && std::abs(heldCorrectionState.desiredCents + 42.0) < 1.0e-9,
                     "acquire_search_never_mutes_existing_correction");

'''
    insertion = anchor + r'''    // SOUND_EQUALS_CORRECTION_V2 regression: reproduce the audible
    // intermittent bypass. The supervisor starts one octave stale, then a live
    // F0 arrives around 452 Hz with zero consensus and almost no confidence.
    // The old octave wrap converted the stale -1200-ish-cent relation to 0.
    ModernPitchEngine::ScaleQuantizer staleRegisterQuantizer;
    staleRegisterQuantizer.reset();
    const double staleRegisterUnison = 1.0;
    staleRegisterQuantizer.setScale(&staleRegisterUnison, 1, 440.0);
    ModernPitchEngine::Parameters staleRegisterParameters;
    staleRegisterParameters.amount = 1.0f;
    staleRegisterParameters.humanize = 0.0f;
    staleRegisterParameters.preserveVibrato = 0.0f;
    staleRegisterParameters.maximumCorrectionSemitones = 12.0f;
    ModernPitchEngine::CorrectionState staleRegisterState;
    staleRegisterState.pitchCentreValid = true;
    staleRegisterState.pitchCentreLog2 = std::log2(220.0);
    staleRegisterState.targetValid = true;
    staleRegisterState.targetLog2 = std::log2(220.0);
    staleRegisterState.transportPeriodHz = 220.0;
    staleRegisterState.noteBodyLatched = true;
    staleRegisterState.noteBodyConfidence = 1.0f;
    staleRegisterState.trackingState = ModernPitchEngine::TrackingState::stable;
    ModernPitchEngine::PitchObservation zeroConsensusRegisterJump;
    zeroConsensusRegisterJump.valid = true;
    zeroConsensusRegisterJump.audioPresent = true;
    zeroConsensusRegisterJump.frequencyHz = 452.0f;
    zeroConsensusRegisterJump.voicing = 1.0f;
    zeroConsensusRegisterJump.confidence = 0.01f;
    zeroConsensusRegisterJump.periodicity = 0.05f;
    zeroConsensusRegisterJump.consensus = 0.0f;
    zeroConsensusRegisterJump.detectorSupport = 1;
    engine->updateCorrectionState(staleRegisterState,
                                  staleRegisterQuantizer,
                                  zeroConsensusRegisterJump,
                                  staleRegisterParameters);
    const double staleRegisterTargetHz = std::exp2(staleRegisterState.targetLog2);
    success &= check(staleRegisterTargetHz > 430.0
                     && staleRegisterTargetHz < 450.0
                     && std::abs(staleRegisterState.desiredCents) > 5.0
                     && std::abs((staleRegisterState.pitchCentreLog2
                                  - std::log2(452.0)) * 1200.0) < 0.1,
                     "zero_consensus_stale_register_never_collapses_to_zero");

'''
    test = replace_once(test, anchor, insertion, 'insert stale-register zero-consensus regression')
    test_path.write_text(test, encoding='utf-8')

# Guard the exact failure mode at source level as well as runtime.
cpp = cpp_path.read_text(encoding='utf-8')
if 'double errorCents = wrapToNearestOctave(' in cpp:
    raise SystemExit('correction path still wraps absolute pitch error by octave')
if SOURCE_MARKER not in cpp:
    raise SystemExit('sound=correction V2 source marker missing')
if TEST_MARKER not in test_path.read_text(encoding='utf-8'):
    raise SystemExit('sound=correction V2 regression missing')

print('Sound=correction authority V2 applied: live identity immediate, register aligned, octave error never collapses to zero')
