from pathlib import Path

MARKER = 'SCALE_OWNED_HOLD_SEMANTICS_V1'


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


cpp_path = Path('Source/ModernPitchEngine.cpp')
cpp = cpp_path.read_text()

# Hold is a literal radius in cents around the currently owned scale degree.
# It is not a margin in a race between dry/source coordinates and two scale
# degrees. Outside the user radius the nearest degree may win; inside it the
# already-owned degree remains the musical destination.
old_quantizer = '''    // Keep the existing target until the challenger wins by the requested
    // hysteresis margin. This acts in target selection, never as a dry/wet gate.
    const double previousDistance = std::abs(targetLog2_ - inputLog2);
    const double hysteresisOctaves = std::max(0.0f, hysteresisCents) / 1200.0;
    if (previousDistance <= nearestDistance + hysteresisOctaves)
    {
        pendingValid_ = false;
        pendingCount_ = 0;
        return targetLog2_;
    }

    // Once the explicit Hold boundary is crossed, the nearest degree wins.
    // Confidence/consensus may rank pitch evidence but cannot veto the target.
    (void) strictness;
    (void) confidence;
    (void) hardLock;
    targetLog2_ = nearest;
'''
new_quantizer = '''    // SCALE_OWNED_HOLD_SEMANTICS_V1 / HOLD_IS_CENT_RADIUS_V2:
    // Hold is measured literally from the centre of the already-owned scale
    // degree. It never creates a dry/source preference and never weakens the
    // correction. Hold=0 therefore recovers the ordinary Voronoi boundary;
    // Hold=80 means 80 cents from the owned degree centre.
    const double previousDistanceCents =
        std::abs(targetLog2_ - inputLog2) * 1200.0;
    const double holdRadiusCents = std::max(0.0f, hysteresisCents);
    const bool nearestIsOwned =
        std::abs(nearest - targetLog2_) * 1200.0 < 0.1;
    if (nearestIsOwned || previousDistanceCents <= holdRadiusCents)
    {
        pendingValid_ = false;
        pendingCount_ = 0;
        return targetLog2_;
    }

    // Outside the explicit radius the nearest scale degree wins. Confidence,
    // consensus and hidden strictness may describe evidence but cannot turn
    // target selection into a dry-following gate.
    (void) strictness;
    (void) confidence;
    (void) hardLock;
    targetLog2_ = nearest;
'''
cpp = replace_once(cpp, old_quantizer, new_quantizer, 'quantizer Hold law')

old_adaptive = '''float ModernPitchEngine::adaptiveHysteresis(
    const Parameters& parameters,
    const ScaleQuantizer& quantizer,
    const PitchObservation& observation) const noexcept
{
    if (zeroPrudenceAuthority(parameters))
        return 0.0f; // AUTHORITY_CONTROLS_EXPLICIT_V1: Hold=0 means exactly no target hold.

    // NO_HIDDEN_PRUDENCE_OUTSIDE_LOCK_V1: there is no alternate automatic
    // hysteresis law when Scale Lock is off. The same visible Hold value owns
    // target retention everywhere.

    // No mode, tempo, confidence or detector-derived multiplier may add Hold.
    const float lockStrictness = clamp01(parameters.lockStrictness);
    const float requestedHysteresis = std::clamp(
        finiteOr(parameters.lockHysteresis, 24.0f), 0.0f, 80.0f);

    // MICROTONAL_HARD_LOCK_V3: hysteresis may stabilise target identity, but
    // it may never become a significant fraction of a dense scale degree.
    // Otherwise 24/31/48-EDO can legally hold the previous target by one or
    // more notes.  Keep the GUI range, then cap the effective musical margin
    // relative to the actual minimum step of the selected/custom scale.
    const float minimumStep = std::max(0.1f, quantizer.minimumStepCents());
    // ABSOLUTE_SCALE_LOCK_V4_INTEGRATION: preserve an audible Hysteresis
    // control on sparse/wide scales without weakening the strict microtonal
    // endpoint. At strictness=1 this is still exactly 0.12 of the minimum
    // scale step (3 cents in 48-EDO); at lower strictness the user deliberately
    // requests more target-hold behaviour, capped well below half a degree.
    const float degreeSafeCap = std::clamp(
        minimumStep * (0.30f - 0.18f * lockStrictness),
        0.35f, 36.0f);
    return std::min(requestedHysteresis, degreeSafeCap);
}
'''
new_adaptive = '''float ModernPitchEngine::adaptiveHysteresis(
    const Parameters& parameters,
    const ScaleQuantizer& quantizer,
    const PitchObservation& observation) const noexcept
{
    // HOLD_IS_LITERAL_USER_CENTS_V2: the GUI value already has the complete
    // musical meaning. Scale density, mode, confidence and sensor output may
    // not silently remap it. Dense scales are allowed to have a wide Hold only
    // when the user explicitly asks for one; a qualified new note can still
    // override that radius in the supervisor below.
    (void) quantizer;
    (void) observation;
    return std::clamp(finiteOr(parameters.lockHysteresis, 24.0f), 0.0f, 80.0f);
}
'''
cpp = replace_once(cpp, old_adaptive, new_adaptive, 'literal Hold radius')

old_observed = '''    const double observedLog2 = safeLog2(correctionFrequencyHz);
    const double correctionObservedLog2 = observedLog2;
'''
new_observed = '''    const double observedLog2 = safeLog2(correctionFrequencyHz);
    const double correctionObservedLog2 = observedLog2;
    // Hold belongs to musical identity, not detector confidence or transport.
    // Compute the literal user radius once and use it only for identity logic.
    const float holdRadiusCents = adaptiveHysteresis(
        parameters, quantizer, observation);
'''
cpp = replace_once(cpp, old_observed, new_observed, 'Hold radius placement')

old_deep = '''            const bool deepCandidate = octaveLikeTarget
                || observedDistanceFromOwnedTarget >= deepExitRatio * scaleStep;
'''
new_deep = '''            const bool outsideUserHold = observedDistanceFromOwnedTarget
                > static_cast<double>(holdRadiusCents) + 1.0e-6;
            const bool deepCandidate = octaveLikeTarget || outsideUserHold;
'''
cpp = replace_once(cpp, old_deep, new_deep, 'latent candidate Hold boundary')

old_hysteresis = '''    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);
    int pending = 0;
'''
new_hysteresis = '''    const float hysteresis = holdRadiusCents;
    // QUALIFIED_NOTE_BYPASSES_HOLD_V2: Hold defines the ordinary same-note
    // radius. Once independent supervisor evidence has already qualified a new
    // identity (persistent same-side boundary exit or detector-scale commit),
    // Hold must not turn a real new note into an eternal old fundamental.
    const float selectionHoldCents = forceTargetSwitch ? 0.0f : hysteresis;
    int pending = 0;
'''
cpp = replace_once(cpp, old_hysteresis, new_hysteresis, 'qualified note bypass')

old_call = '''        newTarget = quantizer.chooseTargetLog2(
            targetSelectionLog2,
            hysteresis,
            targetStrictness,
            targetConfidence,
            parameters.scaleLock && parameters.hardLockActive,
            // USER_HOLD_REMAINS_AUTHORITY_V1: supervisor confirmation only
            // presents a challenger. It never receives onset semantics merely
            // to bypass the Hold explicitly selected by the user.
            musicalOnset,
            pending);
'''
new_call = '''        newTarget = quantizer.chooseTargetLog2(
            targetSelectionLog2,
            selectionHoldCents,
            targetStrictness,
            targetConfidence,
            parameters.scaleLock && parameters.hardLockActive,
            // Onset is explicit new-note evidence. forceTargetSwitch above is
            // the equivalent bounded legato/new-identity evidence.
            musicalOnset,
            pending);
'''
cpp = replace_once(cpp, old_call, new_call, 'quantizer selection Hold')

cpp_path.write_text(cpp)

# Supervisor-level regressions: literal radius, no microtonal remapping, and
# Hold=0 retaining ordinary nearest-degree geometry.
sup_path = Path('Tests/SupervisorContinuityTest.cpp')
sup = sup_path.read_text()
old_dense = '''    // MICROTONAL_HARD_LOCK_V3: even with the GUI hysteresis at its
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
              << denseEffectiveHysteresis << '\\n';
    success &= check(denseEffectiveHysteresis <= 3.01f,
                     "dense_scale_lock_hysteresis_is_degree_safe");
'''
new_dense = '''    // HOLD_IS_LITERAL_USER_CENTS_V2: Hold is a literal user radius, not a
    // density-dependent hidden fraction of a scale degree. A wide Hold on a
    // microtonal scale is therefore explicit user intent, while independently
    // qualified new-note evidence can still override it.
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
    std::cerr << "dense_effective_hold_radius_cents="
              << denseEffectiveHysteresis << '\\n';
    success &= check(std::abs(denseEffectiveHysteresis - 80.0f) < 1.0e-6f,
                     "hold_radius_is_literal_on_dense_scales");

    const double holdSemitone = std::exp2(1.0 / 12.0);
    const std::array<double, 2> holdScale { 1.0, holdSemitone };
    ModernPitchEngine::ScaleQuantizer holdGeometryQuantizer;
    holdGeometryQuantizer.reset();
    holdGeometryQuantizer.setScale(holdScale.data(), 2, 440.0);
    int holdPending = 0;
    const double owned440 = holdGeometryQuantizer.chooseTargetLog2(
        std::log2(440.0), 80.0f, 0.0f, 1.0f, true, true, holdPending);
    const double insideHold = holdGeometryQuantizer.chooseTargetLog2(
        std::log2(440.0 * std::exp2(60.0 / 1200.0)),
        80.0f, 0.0f, 1.0f, true, false, holdPending);
    success &= check(std::abs((insideHold - owned440) * 1200.0) < 0.1,
                     "hold_keeps_owned_degree_inside_user_cent_radius");
    const double outsideHold = holdGeometryQuantizer.chooseTargetLog2(
        std::log2(440.0 * std::exp2(81.0 / 1200.0)),
        80.0f, 0.0f, 1.0f, true, false, holdPending);
    success &= check(std::abs((outsideHold - owned440) * 1200.0) > 90.0,
                     "hold_releases_identity_outside_user_cent_radius");

    ModernPitchEngine::ScaleQuantizer zeroHoldGeometryQuantizer;
    zeroHoldGeometryQuantizer.reset();
    zeroHoldGeometryQuantizer.setScale(holdScale.data(), 2, 440.0);
    static_cast<void>(zeroHoldGeometryQuantizer.chooseTargetLog2(
        std::log2(440.0), 0.0f, 0.0f, 1.0f, true, true, holdPending));
    const double zeroHoldNext = zeroHoldGeometryQuantizer.chooseTargetLog2(
        std::log2(440.0 * std::exp2(51.0 / 1200.0)),
        0.0f, 0.0f, 1.0f, true, false, holdPending);
    success &= check(std::abs(1200.0 * (zeroHoldNext - std::log2(440.0))) > 90.0,
                     "hold_zero_uses_normal_scale_cell_boundary");
'''
sup = replace_once(sup, old_dense, new_dense, 'Supervisor Hold regressions')
sup_path.write_text(sup)

# The old end-to-end invariant incorrectly required a sustained real new note to
# end on different scale degrees solely because Hold changed. New-note evidence
# must win for both settings; Hold is not permission to keep an eternal F0.
clean_path = Path('Tests/CleanModernPitchEngineTest.cpp')
clean = clean_path.read_text()
old_clean = '''    success &= check(std::abs(lowHysteresisResult.finalMeter.targetPitchHz
                              - highHysteresisResult.finalMeter.targetPitchHz) > 15.0f,
                     "lock_hysteresis_changes_target_identity");
'''
new_clean = '''    const double expectedUpperTargetHz = 440.0 * semitone;
    success &= check(std::abs(lowHysteresisResult.finalMeter.targetPitchHz
                              - expectedUpperTargetHz) < 0.5f
                     && std::abs(highHysteresisResult.finalMeter.targetPitchHz
                                 - expectedUpperTargetHz) < 0.5f
                     && std::abs(lowHysteresisResult.finalMeter.targetPitchHz
                                 - highHysteresisResult.finalMeter.targetPitchHz) < 0.5f,
                     "qualified_new_note_is_not_blocked_by_hold");
    success &= check(std::abs(lowHysteresisResult.finalMeter.correctionCents) > 0.5f
                     && std::abs(highHysteresisResult.finalMeter.correctionCents) > 0.5f,
                     "hold_never_returns_owned_note_to_dry");
'''
clean = replace_once(clean, old_clean, new_clean, 'Clean engine Hold invariant')
clean_path.write_text(clean)

# Keep the GUI-level semantic test aligned with the engine contract: Scale Lock
# and a wide Hold may change resistance to micro-motion, but they cannot veto a
# sustained new note that the musical supervisor has qualified.
gui_path = Path('Tests/GuiAudibilityTest.cpp')
gui = gui_path.read_text()
old_gui = '''    // Scale Lock itself must change target hold, not merely expose sub-controls.
    const double semitone = std::exp2(1.0 / 12.0);
    const std::vector<double> twoNoteScale { 1.0, semitone };
    const auto boundaryStep = [](double seconds)
    {
        return seconds < 2.5 ? 450.0 : 456.0;
    };
    auto unlocked = base;
    unlocked.scaleLock = false;
    auto locked = base;
    locked.scaleLock = true;
    locked.hardLockActive = true;
    locked.lockHysteresis = 80.0f;
    const auto unlockedResult = render(
        ModernPitchEngine::LatencyMode::quality, unlocked,
        twoNoteScale, 440.0, 5.0, boundaryStep);
    const auto lockedResult = render(
        ModernPitchEngine::LatencyMode::quality, locked,
        twoNoteScale, 440.0, 5.0, boundaryStep);
    success &= check(std::abs(unlockedResult.meter.targetPitchHz
                              - lockedResult.meter.targetPitchHz) > 15.0f,
                     "scale_lock_switch_changes_target_hold");
'''
new_gui = '''    // HOLD_IS_CENT_RADIUS_V2: a sustained, qualified new note must reach the
    // same scale degree regardless of Hold. Hold controls the ordinary same-note
    // radius; it is not an eternal veto on real note identity.
    const double semitone = std::exp2(1.0 / 12.0);
    const std::vector<double> twoNoteScale { 1.0, semitone };
    const auto boundaryStep = [](double seconds)
    {
        return seconds < 2.5 ? 450.0 : 456.0;
    };
    auto unlocked = base;
    unlocked.scaleLock = false;
    auto locked = base;
    locked.scaleLock = true;
    locked.hardLockActive = true;
    locked.lockHysteresis = 80.0f;
    const auto unlockedResult = render(
        ModernPitchEngine::LatencyMode::quality, unlocked,
        twoNoteScale, 440.0, 5.0, boundaryStep);
    const auto lockedResult = render(
        ModernPitchEngine::LatencyMode::quality, locked,
        twoNoteScale, 440.0, 5.0, boundaryStep);
    const double expectedLockedTarget = 440.0 * semitone;
    success &= check(std::abs(unlockedResult.meter.targetPitchHz
                              - expectedLockedTarget) < 0.5f
                     && std::abs(lockedResult.meter.targetPitchHz
                                 - expectedLockedTarget) < 0.5f,
                     "scale_lock_hold_does_not_block_qualified_new_note");
'''
gui = replace_once(gui, old_gui, new_gui, 'GUI Hold invariant')
gui_path.write_text(gui)

print(f'{MARKER} materialized')
