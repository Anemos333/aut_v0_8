from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')

SOURCE_MARKER = 'ABSOLUTE_SCALE_LOCK_V4'
TEST_MARKER = 'absolute_scale_lock_zero_residual_48edo'


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise RuntimeError(f'{label}: expected source block not found')
    return text.replace(old, new, 1)


if SOURCE_MARKER not in cpp:
    old = '''    float preserve = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve + 0.35f * humanize)
        : clamp01(parameters.preserveVibrato);
    preserve *= stable * periodic * boundarySafety;

    double correctedLog2 = state.targetLog2
        + static_cast<double>(preserve) * vibratoComponent;
    double humanWindow = 1.5 + 16.0 * static_cast<double>(humanize);

    if (parameters.scaleLock)
    {
        // MICROTONAL_HARD_LOCK_V3: Humanize and preserved vibrato are allowed
        // to live inside the selected target, but their COMBINED steady-state
        // residual is bounded by the scale spacing.  On 48-EDO (25 cents) the
        // full budget is <=4.5 cents, so a 20-25 cent residual can never mean
        // "one nearby degree" while still being called locked.
        const double minimumStep = std::max(0.1,
            static_cast<double>(quantizer.minimumStepCents()));
        const double lockStrictness = static_cast<double>(
            clamp01(parameters.lockStrictness));
        const double residualBudgetCents = std::clamp(
            minimumStep * (0.18 - 0.06 * lockStrictness),
            1.0, 6.0);
        humanWindow = std::min(
            0.40 + 1.60 * static_cast<double>(humanize),
            0.30 * residualBudgetCents);

        const double vibratoBudgetCents = std::max(
            0.0, residualBudgetCents - humanWindow);
        const double requestedVibratoCents =
            static_cast<double>(preserve) * vibratoComponent * 1200.0;
        const double preservedVibratoCents = std::clamp(
            requestedVibratoCents,
            -vibratoBudgetCents,
            vibratoBudgetCents);
        correctedLog2 = state.targetLog2 + preservedVibratoCents / 1200.0;
    }

    // SOUND_EQUALS_CORRECTION_V2: targetLog2 and observedLog2 are absolute
    // pitches in the same live register. Never wrap their error by an octave:
    // +/-1200 cents must not collapse to zero and create an audible bypass.
    double errorCents = (correctedLog2 - observedLog2) * 1200.0;
    if (std::abs(errorCents) <= humanWindow)
        errorCents = 0.0;
    else
        errorCents = std::copysign(std::abs(errorCents) - humanWindow, errorCents);
'''
    new = '''    float preserve = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve + 0.35f * humanize)
        : clamp01(parameters.preserveVibrato);
    preserve *= stable * periodic * boundarySafety;

    const bool absoluteScaleLock = parameters.scaleLock
        && parameters.hardLockActive
        && clamp01(parameters.lockStrictness) >= 0.99999f
        && clamp01(parameters.amount) >= 0.99999f
        && humanize <= 0.00001f
        && clamp01(parameters.vibratoPreserve) <= 0.00001f;

    double correctedLog2 = state.targetLog2
        + static_cast<double>(preserve) * vibratoComponent;
    double humanWindow = 1.5 + 16.0 * static_cast<double>(humanize);

    if (parameters.scaleLock)
    {
        // MICROTONAL_HARD_LOCK_V3: Humanize and preserved vibrato are allowed
        // to live inside the selected target, but their COMBINED steady-state
        // residual is bounded by the scale spacing.
        const double minimumStep = std::max(0.1,
            static_cast<double>(quantizer.minimumStepCents()));
        const double lockStrictness = static_cast<double>(
            clamp01(parameters.lockStrictness));
        const double residualBudgetCents = std::clamp(
            minimumStep * (0.18 - 0.06 * lockStrictness),
            1.0, 6.0);
        humanWindow = std::min(
            0.40 + 1.60 * static_cast<double>(humanize),
            0.30 * residualBudgetCents);

        const double vibratoBudgetCents = std::max(
            0.0, residualBudgetCents - humanWindow);
        const double requestedVibratoCents =
            static_cast<double>(preserve) * vibratoComponent * 1200.0;
        const double preservedVibratoCents = std::clamp(
            requestedVibratoCents,
            -vibratoBudgetCents,
            vibratoBudgetCents);
        correctedLog2 = state.targetLog2 + preservedVibratoCents / 1200.0;

        // ABSOLUTE_SCALE_LOCK_V4: the fully rigid endpoint contains no hidden
        // musical softness. With Amount=100%, Humanize=0, Scale-Lock Vibrato=0
        // and Hard Lock/Strictness at maximum, correction destination is the
        // exact selected reference frequency. Hysteresis may decide WHICH scale
        // degree owns identity, and Speed may decide HOW FAST we arrive, but
        // neither is allowed to leave pitch offset around that chosen target.
        if (absoluteScaleLock)
        {
            preserve = 0.0f;
            correctedLog2 = state.targetLog2;
            humanWindow = 0.0;
        }
    }

    // SOUND_EQUALS_CORRECTION_V2: targetLog2 and observedLog2 are absolute
    // pitches in the same live register. Never wrap their error by an octave:
    // +/-1200 cents must not collapse to zero and create an audible bypass.
    double errorCents = (correctedLog2 - observedLog2) * 1200.0;
    if (!absoluteScaleLock)
    {
        if (std::abs(errorCents) <= humanWindow)
            errorCents = 0.0;
        else
            errorCents = std::copysign(std::abs(errorCents) - humanWindow, errorCents);
    }
'''
    cpp = replace_once(cpp, old, new, 'absolute Scale Lock residual path')

    old = '''    state.desiredCents = errorCents
        * static_cast<double>(clamp01(parameters.amount));
'''
    new = '''    state.desiredCents = absoluteScaleLock
        ? errorCents
        : errorCents * static_cast<double>(clamp01(parameters.amount));
'''
    cpp = replace_once(cpp, old, new, 'absolute Scale Lock full authority')
    cpp_path.write_text(cpp, encoding='utf-8')

if TEST_MARKER not in test:
    anchor = '''    // Native API semantics: one semitone means 100 cents, with no adapter hack.\n'''
    insertion = r'''    // ABSOLUTE_SCALE_LOCK_V4: the rigid endpoint is a mathematical
    // reference lock, not a tolerance band.  The selected degree may still be
    // chosen with hysteresis, but once chosen the requested steady-state pitch
    // must contain zero voluntary residual.
    ModernPitchEngine::Parameters absoluteLockParameters;
    setBodyEvidence(absoluteLockParameters);
    absoluteLockParameters.scaleLock = true;
    absoluteLockParameters.hardLockActive = true;
    absoluteLockParameters.lockStrictness = 1.0f;
    absoluteLockParameters.lockHysteresis = 80.0f;
    absoluteLockParameters.amount = 1.0f;
    absoluteLockParameters.retuneTimeMs = 0.0f;
    absoluteLockParameters.humanize = 0.0f;
    absoluteLockParameters.vibratoPreserve = 0.0f;
    absoluteLockParameters.preserveVibrato = 0.0f;
    absoluteLockParameters.maximumCorrectionSemitones = 24.0f;

    const auto checkAbsoluteScaleLock = [&](int edo,
                                            double sourceOffsetCents,
                                            float consensus,
                                            const char* name)
    {
        std::vector<double> ratios(static_cast<std::size_t>(edo));
        for (int degree = 0; degree < edo; ++degree)
            ratios[static_cast<std::size_t>(degree)] = std::exp2(
                static_cast<double>(degree) / static_cast<double>(edo));

        ModernPitchEngine::ScaleQuantizer exactQuantizer;
        exactQuantizer.reset();
        exactQuantizer.setScale(ratios.data(), edo, 440.0);
        ModernPitchEngine::CorrectionState exactState;
        auto exactObservation = strongPitch(static_cast<float>(
            440.0 * std::exp2(sourceOffsetCents / 1200.0)));
        exactObservation.audioPresent = true;
        exactObservation.consensus = consensus;
        if (consensus <= 0.0f)
        {
            exactObservation.confidence = 0.01f;
            exactObservation.periodicity = 0.05f;
        }

        engine->updateCorrectionState(exactState, exactQuantizer,
                                      exactObservation, absoluteLockParameters);
        const double observedLog2 = std::log2(
            static_cast<double>(exactObservation.frequencyHz));
        const double requestedTargetError =
            (exactState.targetLog2 - observedLog2) * 1200.0;
        const double destinationResidual = std::abs(
            (observedLog2 + exactState.desiredCents / 1200.0
             - exactState.targetLog2) * 1200.0);
        std::cerr << name << "_destination_residual_cents="
                  << destinationResidual << '\n';

        bool localSuccess = exactState.targetValid
            && std::abs(exactState.desiredCents - requestedTargetError) < 1.0e-9
            && destinationResidual < 1.0e-9;

        // Verify the single correction trajectory also reaches the exact
        // destination rather than merely requesting it.
        for (int sample = 0; sample < 4800; ++sample)
            static_cast<void>(engine->advanceCorrection(exactState));
        const double convergedResidual = std::abs(
            (observedLog2 + exactState.currentCents / 1200.0
             - exactState.targetLog2) * 1200.0);
        std::cerr << name << "_converged_residual_cents="
                  << convergedResidual << '\n';
        localSuccess = localSuccess && convergedResidual < 0.001;
        return check(localSuccess, name);
    };

    success &= checkAbsoluteScaleLock(12, 37.0, 0.88f,
                                      "absolute_scale_lock_zero_residual_12tet");
    success &= checkAbsoluteScaleLock(31, 15.0, 0.88f,
                                      "absolute_scale_lock_zero_residual_31edo");
    success &= checkAbsoluteScaleLock(48, 10.0, 0.88f,
                                      "absolute_scale_lock_zero_residual_48edo");
    success &= checkAbsoluteScaleLock(96, 5.0, 0.88f,
                                      "absolute_scale_lock_zero_residual_96edo");
    success &= checkAbsoluteScaleLock(48, 10.0, 0.0f,
                                      "absolute_scale_lock_zero_consensus_zero_residual");

    // An asymmetric custom scale with a very narrow local interval receives
    // the same exact-target contract; density changes target selection safety,
    // never steady-state authority.
    std::array<double, 6> asymmetricScale {
        1.0,
        std::exp2(17.0 / 1200.0),
        std::exp2(143.0 / 1200.0),
        std::exp2(311.0 / 1200.0),
        std::exp2(702.0 / 1200.0),
        std::exp2(947.0 / 1200.0)
    };
    ModernPitchEngine::ScaleQuantizer asymmetricQuantizer;
    asymmetricQuantizer.reset();
    asymmetricQuantizer.setScale(asymmetricScale.data(),
                                 static_cast<int>(asymmetricScale.size()),
                                 440.0);
    ModernPitchEngine::CorrectionState asymmetricState;
    auto asymmetricObservation = strongPitch(static_cast<float>(
        440.0 * std::exp2(7.0 / 1200.0)));
    asymmetricObservation.audioPresent = true;
    engine->updateCorrectionState(asymmetricState, asymmetricQuantizer,
                                  asymmetricObservation, absoluteLockParameters);
    const double asymmetricObservedLog2 = std::log2(
        static_cast<double>(asymmetricObservation.frequencyHz));
    const double asymmetricResidual = std::abs(
        (asymmetricObservedLog2 + asymmetricState.desiredCents / 1200.0
         - asymmetricState.targetLog2) * 1200.0);
    std::cerr << "absolute_custom_scale_residual_cents="
              << asymmetricResidual << '\n';
    success &= check(asymmetricState.targetValid && asymmetricResidual < 1.0e-9,
                     "absolute_scale_lock_zero_residual_asymmetric_custom");

'''
    if anchor not in test:
        raise RuntimeError('insert absolute Scale Lock regressions: anchor not found')
    test = test.replace(anchor, insertion + anchor, 1)
    test_path.write_text(test, encoding='utf-8')

cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')
if SOURCE_MARKER not in cpp:
    raise SystemExit('absolute Scale Lock V4 source marker missing')
for required in (
    'absolute_scale_lock_zero_residual_12tet',
    'absolute_scale_lock_zero_residual_31edo',
    'absolute_scale_lock_zero_residual_48edo',
    'absolute_scale_lock_zero_residual_96edo',
    'absolute_scale_lock_zero_consensus_zero_residual',
    'absolute_scale_lock_zero_residual_asymmetric_custom'):
    if required not in test:
        raise SystemExit(f'absolute Scale Lock regression missing: {required}')

print('Absolute Scale Lock V4 applied: fully rigid endpoint requests exact target frequency with zero voluntary residual')
