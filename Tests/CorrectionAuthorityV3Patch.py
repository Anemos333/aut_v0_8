from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')

SOURCE_MARKER = 'MICROTONAL_HARD_LOCK_V3'
TEST_MARKER = 'dense_scale_lock_residual_budget_is_degree_safe'


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise RuntimeError(f'{label}: expected source block not found')
    return text.replace(old, new, 1)


if SOURCE_MARKER not in cpp:
    old = '''    const float confidenceFactor = 0.65f + 0.70f * clamp01(observation.confidence);
    const float strictnessFactor = 1.0f - 0.28f * clamp01(parameters.lockStrictness);
    return std::clamp(finiteOr(parameters.lockHysteresis, 24.0f)
        * modeFactor * tempoFactor * densityFactor
        * confidenceFactor * strictnessFactor, 0.0f, 80.0f);
'''
    new = '''    const float confidenceFactor = 0.65f + 0.70f * clamp01(observation.confidence);
    const float lockStrictness = clamp01(parameters.lockStrictness);
    const float strictnessFactor = 1.0f - 0.28f * lockStrictness;
    const float requestedHysteresis = std::clamp(
        finiteOr(parameters.lockHysteresis, 24.0f)
            * modeFactor * tempoFactor * densityFactor
            * confidenceFactor * strictnessFactor,
        0.0f, 80.0f);

    // MICROTONAL_HARD_LOCK_V3: hysteresis may stabilise target identity, but
    // it may never become a significant fraction of a dense scale degree.
    // Otherwise 24/31/48-EDO can legally hold the previous target by one or
    // more notes.  Keep the GUI range, then cap the effective musical margin
    // relative to the actual minimum step of the selected/custom scale.
    const float minimumStep = std::max(0.1f, quantizer.minimumStepCents());
    const float degreeSafeCap = std::clamp(
        minimumStep * (0.18f - 0.06f * lockStrictness),
        0.35f, 36.0f);
    return std::min(requestedHysteresis, degreeSafeCap);
'''
    cpp = replace_once(cpp, old, new, 'degree-safe adaptive hysteresis')

    old = '''    if (parameters.scaleLock)
    {
        const double norm = std::pow(requested / 500.0, 1.35);
        switch (latencyMode_)
        {
            case LatencyMode::quality:   response = 3.0 + 4.0 * norm; break;
            case LatencyMode::live:      response = 1.5 + 3.5 * norm; break;
            case LatencyMode::ultraLive: response = 0.35 + 2.65 * norm; break;
        }
        const double humanTiming = 0.8 * static_cast<double>(clamp01(parameters.humanize));
        response = std::min(7.0, response + humanTiming);
    }
'''
    new = '''    if (parameters.scaleLock)
    {
        const double norm = std::pow(requested / 500.0, 1.35);
        double modeMaximumMs = 3.0;
        switch (latencyMode_)
        {
            // MICROTONAL_HARD_LOCK_V3: Scale Lock owns its documented fast
            // trajectory.  The normal transition controller must not stretch
            // a dense-scale note change into tens of milliseconds.
            case LatencyMode::quality:
                response = 3.0 + 2.0 * norm;
                modeMaximumMs = 5.0;
                break;
            case LatencyMode::live:
                response = 1.5 + 1.5 * norm;
                modeMaximumMs = 3.0;
                break;
            case LatencyMode::ultraLive:
                response = 0.35 + 1.15 * norm;
                modeMaximumMs = 1.5;
                break;
        }
        const double humanTiming = 0.40
            * static_cast<double>(clamp01(parameters.humanize));
        response = std::min(modeMaximumMs, response + humanTiming);
    }
'''
    cpp = replace_once(cpp, old, new, 'strict Scale Lock response ranges')

    old = '''        else
        {
            // Main used a pre-rolled second synthesis layer for note changes.
            // Keep only its useful bounded transition timing: the current
            // single trajectory moves continuously to the exact new target.
            const double jumpWeight = std::clamp(
                std::abs(targetJumpCents) / 600.0, 0.0, 1.0);
            const double trajectoryMs = std::clamp(
                transitionMs * (0.22 + 0.38 * jumpWeight), 0.35, 32.0);
            response = std::max(response, trajectoryMs);
        }
'''
    new = '''        else if (!parameters.scaleLock)
        {
            // Main used a pre-rolled second synthesis layer for note changes.
            // Keep only its useful bounded transition timing outside Scale
            // Lock. Scale Lock already has its own <=5/3/1.5 ms trajectory.
            const double jumpWeight = std::clamp(
                std::abs(targetJumpCents) / 600.0, 0.0, 1.0);
            const double trajectoryMs = std::clamp(
                transitionMs * (0.22 + 0.38 * jumpWeight), 0.35, 32.0);
            response = std::max(response, trajectoryMs);
        }
'''
    cpp = replace_once(cpp, old, new, 'do not stretch Scale Lock target changes')

    old = '''    float preserve = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve + 0.35f * humanize)
        : clamp01(parameters.preserveVibrato);
    preserve *= stable * periodic * boundarySafety;

    const double correctedLog2 = state.targetLog2
        + static_cast<double>(preserve) * vibratoComponent;
    // SOUND_EQUALS_CORRECTION_V2: targetLog2 and observedLog2 are absolute
    // pitches in the same live register. Never wrap their error by an octave:
    // +/-1200 cents must not collapse to zero and create an audible bypass.
    double errorCents = (correctedLog2 - observedLog2) * 1200.0;

    const double humanWindow = parameters.scaleLock
        ? 2.0 + 10.0 * static_cast<double>(humanize)
        : 1.5 + 16.0 * static_cast<double>(humanize);
'''
    new = '''    float preserve = parameters.scaleLock
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
'''
    cpp = replace_once(cpp, old, new, 'scale-relative residual budget')

    cpp_path.write_text(cpp, encoding='utf-8')

if TEST_MARKER not in test:
    anchor = '''    // Native API semantics: one semitone means 100 cents, with no adapter hack.\n'''
    insertion = r'''    // MICROTONAL_HARD_LOCK_V3: even with the GUI hysteresis at its
    // maximum, a 48-EDO target selector must not be allowed to hold the old
    // degree by a musically significant portion of the 25-cent step.
    ModernPitchEngine::Parameters hardDenseParameters = denseParameters;
    hardDenseParameters.scaleLock = true;
    hardDenseParameters.hardLockActive = true;
    hardDenseParameters.lockHysteresis = 80.0f;
    hardDenseParameters.lockStrictness = 1.0f;
    hardDenseParameters.humanize = 1.0f;
    hardDenseParameters.vibratoPreserve = 1.0f;
    auto hardDenseObservation = strongPitch(440.0f);
    const float denseEffectiveHysteresis = engine->adaptiveHysteresis(
        hardDenseParameters, denseQuantizer, hardDenseObservation);
    std::cerr << "dense_effective_hysteresis_cents="
              << denseEffectiveHysteresis << '\n';
    success &= check(denseEffectiveHysteresis <= 3.01f,
                     "dense_scale_lock_hysteresis_is_degree_safe");

    // At maximum Humanize + Vibrato Preserve, a 10-cent input deviation on
    // 48-EDO must still request enough correction to leave <=3 cents residual
    // under strict hard lock. This is the steady-state pitch contract; renderer
    // and trajectory tests cover convergence separately.
    ModernPitchEngine::CorrectionState hardDenseState;
    auto hardDenseOffset = strongPitch(static_cast<float>(
        440.0 * std::exp2(10.0 / 1200.0)));
    hardDenseOffset.audioPresent = true;
    engine->updateCorrectionState(hardDenseState, denseQuantizer,
                                  hardDenseOffset, hardDenseParameters);
    const double hardDenseObservedCents = 1200.0 * std::log2(
        static_cast<double>(hardDenseOffset.frequencyHz) / 440.0);
    const double hardDenseResidualCents = std::abs(
        hardDenseObservedCents + hardDenseState.desiredCents);
    std::cerr << "dense_scale_lock_residual_budget_cents="
              << hardDenseResidualCents << '\n';
    success &= check(hardDenseResidualCents <= 3.05,
                     "dense_scale_lock_residual_budget_is_degree_safe");

    // The target-change path must remain inside Scale Lock's own fast range;
    // the general 35-40 ms transition control may not stretch it again.
    hardDenseParameters.retuneTimeMs = 500.0f;
    hardDenseParameters.transitionTimeMs = 80.0f;
    hardDenseParameters.tempo.mode = CreativeTempo::Mode::off;
    const double denseScaleLockResponse = engine->responseTimeMs(
        hardDenseParameters, true, 25.0);
    std::cerr << "dense_scale_lock_response_ms="
              << denseScaleLockResponse << '\n';
    success &= check(denseScaleLockResponse <= 3.001,
                     "scale_lock_target_change_stays_in_live_speed_budget");

'''
    if anchor not in test:
        raise RuntimeError('insert V3 dense lock regressions: anchor not found')
    test = test.replace(anchor, insertion + anchor, 1)
    test_path.write_text(test, encoding='utf-8')

cpp = cpp_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')
if SOURCE_MARKER not in cpp:
    raise SystemExit('microtonal hard-lock V3 source marker missing')
if TEST_MARKER not in test:
    raise SystemExit('microtonal hard-lock V3 regression missing')
if 'scale_lock_target_change_stays_in_live_speed_budget' not in test:
    raise SystemExit('Scale Lock speed-budget regression missing')

print('Microtonal hard-lock V3 applied: degree-safe hysteresis, <=6-cent residual budget, strict dense-scale residuals, and Scale Lock speed ranges preserved')
