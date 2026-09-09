from pathlib import Path

header_path = Path('Source/ModernPitchEngine.h')
cpp_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')

header = header_path.read_text(encoding='utf-8')
cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')

HEADER_MARKER = 'correctionFrequencyHz'
SOURCE_MARKER = 'LIVE_CORRECTION_COORDINATE_V5'
TEST_MARKER = 'absolute_scale_lock_uses_live_correction_coordinate'


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise RuntimeError(f'{label}: expected source block not found')
    return text.replace(old, new, 1)


# Keep the pitch used for musical identity/continuity separate from the pitch
# coordinate used to calculate an absolute correction ratio. The former may be
# temporally smoothed; the latter must be the latest accepted live F0 when the
# user explicitly requests the rigid endpoint.
if HEADER_MARKER not in header:
    old = '''    struct PitchObservation\n    {\n        float frequencyHz = 0.0f;\n        float confidence = 0.0f;\n'''
    new = '''    struct PitchObservation\n    {\n        // frequencyHz is the continuity/identity coordinate. It may be\n        // smoothed by the tracker so target ownership remains stable.\n        float frequencyHz = 0.0f;\n\n        // LIVE_CORRECTION_COORDINATE_V5: latest accepted live F0 in the same\n        // register, used only by the fully rigid Scale Lock endpoint. This\n        // prevents continuity smoothing from becoming audible pitch residual.\n        float correctionFrequencyHz = 0.0f;\n        float confidence = 0.0f;\n'''
    header = replace_once(header, old, new, 'PitchObservation correction coordinate')
    header_path.write_text(header, encoding='utf-8')

if SOURCE_MARKER not in cpp:
    old = '''        observation.frequencyHz = trackedPitchHz_;\n        observation.confidence = trackedConfidence_;\n'''
    new = '''        observation.frequencyHz = trackedPitchHz_;\n\n        // LIVE_CORRECTION_COORDINATE_V5: identity remains on the proven\n        // continuity-smoothed track, but a rigid lock must calculate its ratio\n        // from the latest accepted F0 rather than from that delayed identity\n        // coordinate. All non-rigid modes continue to use frequencyHz below.\n        observation.correctionFrequencyHz = presenceMode_\n            && std::isfinite(decision.candidate.frequencyHz)\n            && decision.candidate.frequencyHz > 0.0f\n            ? decision.candidate.frequencyHz\n            : trackedPitchHz_;\n        observation.confidence = trackedConfidence_;\n'''
    cpp = replace_once(cpp, old, new, 'publish live correction coordinate')

    # The invalid-observation branch carries the held coordinate only for
    # telemetry/fallback. updateCorrectionState does not consume it as a new F0
    # because observation.valid remains false.
    old = '''        observation.frequencyHz = trackedPitchHz_;\n        observation.confidence = trackedConfidence_;\n'''
    new = '''        observation.frequencyHz = trackedPitchHz_;\n        observation.correctionFrequencyHz = trackedPitchHz_;\n        observation.confidence = trackedConfidence_;\n'''
    cpp = replace_once(cpp, old, new, 'publish held correction coordinate')

    old = '''    const double observedLog2 = safeLog2(observation.frequencyHz);\n    bool liveIdentityBreak = false;\n'''
    new = '''    const double observedLog2 = safeLog2(observation.frequencyHz);\n    const float correctionFrequencyHz =\n        std::isfinite(observation.correctionFrequencyHz)\n        && observation.correctionFrequencyHz > 0.0f\n        ? observation.correctionFrequencyHz\n        : observation.frequencyHz;\n    const double correctionObservedLog2 = safeLog2(correctionFrequencyHz);\n    bool liveIdentityBreak = false;\n'''
    cpp = replace_once(cpp, old, new, 'derive correction observation coordinate')

    old = '''    // SOUND_EQUALS_CORRECTION_V2: targetLog2 and observedLog2 are absolute\n    // pitches in the same live register. Never wrap their error by an octave:\n    // +/-1200 cents must not collapse to zero and create an audible bypass.\n    double errorCents = (correctedLog2 - observedLog2) * 1200.0;\n'''
    new = '''    // SOUND_EQUALS_CORRECTION_V2: target and F0 are absolute pitches in the\n    // same live register. Never wrap their error by an octave.\n    //\n    // LIVE_CORRECTION_COORDINATE_V5: only the fully rigid endpoint uses the\n    // latest accepted live F0. Musical identity, hysteresis, Humanize and all\n    // softer modes intentionally remain on the previous continuity coordinate,\n    // so this change cannot alter their established sound or target behaviour.\n    const double correctionReferenceLog2 = absoluteScaleLock\n        ? correctionObservedLog2\n        : observedLog2;\n    double errorCents = (correctedLog2 - correctionReferenceLog2) * 1200.0;\n'''
    cpp = replace_once(cpp, old, new, 'absolute lock correction reference')
    cpp_path.write_text(cpp, encoding='utf-8')

if TEST_MARKER not in test:
    anchor = '''    // Native API semantics: one semitone means 100 cents, with no adapter hack.\n'''
    insertion = r'''    // LIVE_CORRECTION_COORDINATE_V5: continuity smoothing may lag the
    // instantaneous vocal F0, but that lag must never become residual pitch in
    // the fully rigid endpoint. Target identity still follows frequencyHz;
    // correction depth follows correctionFrequencyHz.
    ModernPitchEngine::ScaleQuantizer liveCoordinateQuantizer;
    liveCoordinateQuantizer.reset();
    const double liveCoordinateUnison = 1.0;
    liveCoordinateQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState liveCoordinateState;
    auto liveCoordinateObservation = strongPitch(445.0f);
    liveCoordinateObservation.audioPresent = true;
    liveCoordinateObservation.correctionFrequencyHz = 452.0f;
    engine->updateCorrectionState(liveCoordinateState,
                                  liveCoordinateQuantizer,
                                  liveCoordinateObservation,
                                  absoluteLockParameters);
    const double liveCoordinateTargetHz = std::exp2(
        liveCoordinateState.targetLog2);
    const double expectedLiveCorrection = 1200.0 * std::log2(
        liveCoordinateTargetHz
        / static_cast<double>(liveCoordinateObservation.correctionFrequencyHz));
    const double continuityCoordinateCorrection = 1200.0 * std::log2(
        liveCoordinateTargetHz
        / static_cast<double>(liveCoordinateObservation.frequencyHz));
    const double liveCoordinateResidual = std::abs(
        1200.0 * std::log2(
            static_cast<double>(liveCoordinateObservation.correctionFrequencyHz)
            * std::exp2(liveCoordinateState.desiredCents / 1200.0)
            / liveCoordinateTargetHz));
    std::cerr << "live_correction_coordinate_expected_cents="
              << expectedLiveCorrection
              << " desired_cents=" << liveCoordinateState.desiredCents
              << " residual_cents=" << liveCoordinateResidual << '\n';
    success &= check(liveCoordinateState.targetValid
                     && std::abs(liveCoordinateState.desiredCents
                                 - expectedLiveCorrection) < 1.0e-6
                     && std::abs(liveCoordinateState.desiredCents
                                 - continuityCoordinateCorrection) > 20.0
                     && liveCoordinateResidual < 1.0e-6,
                     "absolute_scale_lock_uses_live_correction_coordinate");

    // Softer Scale Lock must remain bit-for-contract on the continuity F0. The
    // new field is deliberately ignored unless every absolute-lock condition
    // is active.
    ModernPitchEngine::Parameters softCoordinateParameters = absoluteLockParameters;
    softCoordinateParameters.humanize = 0.25f;
    ModernPitchEngine::ScaleQuantizer softCoordinateQuantizer;
    softCoordinateQuantizer.reset();
    softCoordinateQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState softCoordinateState;
    engine->updateCorrectionState(softCoordinateState,
                                  softCoordinateQuantizer,
                                  liveCoordinateObservation,
                                  softCoordinateParameters);
    const double softReferenceError = std::abs(
        softCoordinateState.desiredCents - expectedLiveCorrection);
    success &= check(softReferenceError > 1.0,
                     "soft_scale_lock_ignores_live_correction_coordinate");

'''
    if anchor not in test:
        raise RuntimeError('insert live correction coordinate regressions: anchor not found')
    test = test.replace(anchor, insertion + anchor, 1)
    test_path.write_text(test, encoding='utf-8')

header = header_path.read_text(encoding='utf-8')
cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')
if HEADER_MARKER not in header:
    raise SystemExit('live correction coordinate field missing')
if SOURCE_MARKER not in cpp:
    raise SystemExit('live correction coordinate source marker missing')
for required in (
    'absolute_scale_lock_uses_live_correction_coordinate',
    'soft_scale_lock_ignores_live_correction_coordinate'):
    if required not in test:
        raise SystemExit(f'live correction coordinate regression missing: {required}')

print('Correction authority V5 applied: rigid Scale Lock uses live F0 for ratio while musical identity and softer modes remain unchanged')
