from pathlib import Path
import re


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def replace_region(text: str, start_marker: str, end_marker: str, replacement: str, label: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        raise RuntimeError(f"{label}: start marker not found")
    end = text.find(end_marker, start)
    if end < 0:
        raise RuntimeError(f"{label}: end marker not found")
    return text[:start] + replacement + text[end:]


# -----------------------------------------------------------------------------
# Plugin control curves: Amount is exponential in audible authority; Response
# keeps millisecond semantics but the knob/automation taper is logarithmic.
# -----------------------------------------------------------------------------
processor_path = Path("Source/PluginProcessor.cpp")
processor = processor_path.read_text(encoding="utf-8")

anchor = '''    [[nodiscard]] float constrainRetuneSpeedMs (float speedMs,
                                           int /*mode*/,
                                           bool /*scaleLock*/) noexcept
    {
        // The clean engine owns the mode-aware trajectory mapping. Preserve
        // the complete 0..500 ms GUI range without floors or compression.
        if (! std::isfinite (speedMs))
            speedMs = 50.0f;
        return juce::jlimit (0.0f, 500.0f, speedMs);
    }
'''
helpers = anchor + '''
    [[nodiscard]] juce::NormalisableRange<float> makeLogarithmicResponseRange()
    {
        // LOG_RESPONSE_TAPER_V1: the parameter remains honest milliseconds,
        // while half knob travel lands at 25 ms instead of 250 ms. This gives
        // useful resolution where vocal glides live without changing endpoints
        // or hiding a mode-dependent response remap in DSP.
        juce::NormalisableRange<float> range (0.0f, 500.0f, 1.0f);
        range.setSkewForCentre (25.0f);
        return range;
    }

    [[nodiscard]] float mapAmountPercentToDepth (float amountPct) noexcept
    {
        // EXPONENTIAL_AMOUNT_V1: 0 and 100 remain exact endpoints. Between them
        // the audible correction depth follows a true exponential taper rather
        // than a linear multiplier. Amount is still correction depth, never
        // dry/wet and never detector confidence.
        const double normalised = static_cast<double> (juce::jlimit (
            0.0f, 100.0f, std::isfinite (amountPct) ? amountPct : 0.0f)) / 100.0;
        constexpr double shape = 2.0;
        const double denominator = std::expm1 (shape);
        return static_cast<float> (std::expm1 (shape * normalised) / denominator);
    }
'''
processor = replace_once(processor, anchor, helpers, "processor curve helpers")
processor = replace_once(
    processor,
    '''        juce::ParameterID { "speed", 1 }, "Velocita (ms)",
        juce::NormalisableRange<float> (0.0f, 500.0f, 1.0f), 50.0f));''',
    '''        juce::ParameterID { "speed", 1 }, "Velocita (ms)",
        makeLogarithmicResponseRange(), 50.0f));''',
    "response parameter taper")
processor = replace_once(
    processor,
    '''    const float amount = amountPct / 100.0f;''',
    '''    const float amount = mapAmountPercentToDepth (amountPct);''',
    "amount exponential mapping")
processor_path.write_text(processor, encoding="utf-8")


# -----------------------------------------------------------------------------
# GUI: show the real millisecond value. The APVTS NormalisableRange now owns the
# logarithmic knob taper; there is no hidden per-mode display remap.
# -----------------------------------------------------------------------------
editor_path = Path("Source/PluginEditor.cpp")
editor = editor_path.read_text(encoding="utf-8")
old_display = '''    speedKnob.textFromValueFunction = [this](double val)
    {
        if (scaleLockButton.getToggleState())
        {
            const int mode = processorRef.processingMode.load();
            if (mode > 0)
            {
                const double norm = std::pow(
                    juce::jlimit(0.0, 1.0, val / 500.0), 1.35);
                const double mappedVal = mode == 1 ? 3.0 + 2.0 * norm
                    : mode == 2 ? 1.5 + 1.5 * norm
                                : 0.35 + 1.15 * norm;
                return juce::String(mappedVal, 2) + " ms";
            }
        }
        return juce::String(val, 1) + " ms";
    };'''
new_display = '''    speedKnob.textFromValueFunction = [](double val)
    {
        // LOG_RESPONSE_TAPER_V1: val already is the actual DSP time in ms.
        // The parameter range owns the logarithmic physical taper.
        return juce::String(val, val < 10.0 ? 2 : 1) + " ms";
    };'''
editor = replace_once(editor, old_display, new_display, "response GUI display")
editor_path.write_text(editor, encoding="utf-8")


# -----------------------------------------------------------------------------
# Engine trajectory semantics:
#   stable/voiced = locked (no hidden smoothing),
#   attack/acquire/transition/release = one continuous musical glide,
#   transition/release always keep a tiny 5.5 ms anti-MIDI micro-glide floor.
# No target, Amount, detector, renderer or timbre law is changed here.
# -----------------------------------------------------------------------------
engine_path = Path("Source/ModernPitchEngine.cpp")
engine = engine_path.read_text(encoding="utf-8")

new_response = '''double ModernPitchEngine::responseTimeMs(
    const Parameters& parameters,
    bool targetChanged,
    double targetJumpCents) const noexcept
{
    // GLIDE_STATE_MODEL_V2: Response is now the user's musical trajectory time,
    // not a prudence budget. No latency mode, confidence, Humanize or Scale Lock
    // state is allowed to compress it to a hidden 0.35..5 ms range.
    double response = std::clamp(
        static_cast<double>(finiteOr(parameters.retuneTimeMs, 50.0f)),
        0.0, 500.0);

    // Creative Tempo is an explicit user-requested scheduler and therefore may
    // deliberately lengthen a target transition. With Tempo Off, Response owns
    // the glide time exactly.
    if (targetChanged && std::abs(targetJumpCents) > 0.1
        && parameters.tempo.mode != CreativeTempo::Mode::off)
    {
        const double transitionMs = std::clamp(
            static_cast<double>(finiteOr(parameters.transitionTimeMs, 35.0f)),
            0.0, 500.0);
        response = std::max(response, transitionMs);
    }
    return std::clamp(response, 0.0, 500.0);
}

'''
engine = replace_region(
    engine,
    "double ModernPitchEngine::responseTimeMs(",
    "void ModernPitchEngine::updateCorrectionState(",
    new_response,
    "responseTimeMs")

# Release no longer uses transient-protection prudence timing.
engine = replace_once(
    engine,
    '''        const double protection = static_cast<double>(
            clamp01(parameters.transientProtection));
        state.responseMs = std::clamp(32.0 - 20.0 * protection,
                                      8.0, 32.0);''',
    '''        // Unvoiced material is a musical tail, not permission to drop
        // authority abruptly. The same wet trajectory glides toward unity.
        state.responseMs = std::max(5.5,
            responseTimeMs(parameters, true, state.lastTargetJumpCents));''',
    "confirmed release timing")
engine = replace_once(
    engine,
    '''            state.responseMs = std::clamp(32.0 - 20.0
                * static_cast<double>(clamp01(parameters.transientProtection)),
                8.0, 32.0);''',
    '''            state.responseMs = std::max(5.5,
                responseTimeMs(parameters, true, state.lastTargetJumpCents));''',
    "orphan release timing")

# When a real F0 returns, acquisition is a musical trajectory. A fresh voiced
# body begins in attack; reacquisition becomes transition rather than a hidden
# safety state that later decides authority.
engine = replace_once(
    engine,
    '''    else if (state.trackingState == TrackingState::unvoiced
             || state.trackingState == TrackingState::release)
    {
        setState(TrackingState::acquire);
        state.stableObservations = 0;
    }
''',
    '''    else if (state.trackingState == TrackingState::unvoiced
             || state.trackingState == TrackingState::release)
    {
        setState(TrackingState::attack);
        state.stableObservations = 0;
    }
    else if (state.trackingState == TrackingState::acquire)
    {
        setState(TrackingState::transition);
        state.stableObservations = 0;
    }
''',
    "voiced state entry")

# Remove the evidence-based target veto. Weak/late target changes are allowed to
# be musical changes; transition smoothing handles them without reducing depth.
engine = replace_region(
    engine,
    "    // TAIL_MELODIC_MICRO_GLIDE_V1\n",
    "    const bool targetChanged = !state.targetValid\n",
    '''    // GLIDE_STATE_MODEL_V2: target identity is never vetoed merely because
    // a voiced fragment is weak. If the quantizer selects another real degree,
    // transition owns a continuous glide to that exact destination.
''',
    "tail target prudence")

old_response_state = '''    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);

    // TAIL_MELODIC_MICRO_GLIDE_V1: Response=0 remains literal on a real note
    // body. Only an evidence-backed weak tail that actually changes degree gets
    // 5.5..13 ms of continuous movement. desiredCents remains the exact same
    // destination and the renderer remains 100% wet throughout the glide.
    if (scaleLockTail && targetIdentityChanged)
    {
        const double weakness = std::clamp(
            (0.52 - static_cast<double>(bodyScore)) / 0.24,
            0.0, 1.0);
        const double tailGlideMs = 5.5 + 7.5 * weakness;
        state.responseMs = std::max(state.responseMs, tailGlideMs);
    }

    if (!musicalOnset)
    {
        const int minimumStableSamples = static_cast<int>(std::lround(
            0.012 * sampleRate_));
        if (state.trackingState == TrackingState::transition)
        {
            if (!targetIdentityChanged && bodyPresent
                && state.stableBodyObservations >= 4
                && state.stateAgeSamples >= minimumStableSamples)
            {
                setState(TrackingState::stable);
            }
        }
        else if (state.trackingState == TrackingState::attack
                 || state.trackingState == TrackingState::acquire)
        {
            if (state.noteBodyLatched && bodyPresent
                && state.stableBodyObservations >= 4
                && state.stateAgeSamples >= minimumStableSamples)
            {
                setState(TrackingState::stable);
            }
        }
    }
'''
new_response_state = '''    // GLIDE_STATE_MODEL_V2
    // A stable voiced note is hard-locked: no confidence/mode smoothing remains
    // between the requested correction and the wet renderer. Every non-stable
    // musical state uses the same single trajectory. Real degree changes and
    // release tails keep a tiny 5.5 ms anti-MIDI glide even at Response=0;
    // attack/acquire otherwise follow the user Response literally.
    if (state.trackingState == TrackingState::stable)
    {
        state.responseMs = 0.0;
    }
    else
    {
        state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);
        if (state.trackingState == TrackingState::transition
            || state.trackingState == TrackingState::release)
        {
            state.responseMs = std::max(5.5, state.responseMs);
        }
    }
'''
engine = replace_once(engine, old_response_state, new_response_state, "state glide policy")

new_advance = '''double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept
{
    if (!state.targetValid)
        return 0.0;

    if (state.stateAgeSamples < std::numeric_limits<int>::max())
        ++state.stateAgeSamples;

    const auto settleTrajectory = [&state]() noexcept
    {
        if (state.trackingState == TrackingState::release
            && std::abs(state.currentCents) < 0.001)
        {
            state.trackingState = TrackingState::unvoiced;
            state.stateAgeSamples = 0;
            state.noteBodyLatched = false;
            state.noteBodyConfidence = 0.0f;
            state.transportPeriodHz = 0.0;
            state.pitchStaleSamples = 0;
            state.pitchCentreValid = false;
            state.stableBodyObservations = 0;
        }
        else if ((state.trackingState == TrackingState::attack
                  || state.trackingState == TrackingState::transition)
                 && state.noteBodyLatched)
        {
            // Arrival, not a confidence timer, defines the end of a glide.
            state.trackingState = TrackingState::stable;
            state.stateAgeSamples = 0;
            state.responseMs = 0.0;
        }
    };

    if (state.responseMs <= 0.00001)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
        settleTrajectory();
        return state.currentCents;
    }

    // One critically damped correction trajectory. There is deliberately no
    // 120 ms prudence timeout: a 500 ms user glide remains a 500 ms glide and
    // becomes locked only when the destination is actually reached.
    const double dt = 1.0 / sampleRate_;
    const double responseSeconds = std::max(0.00035, state.responseMs * 0.001);
    const double omega = std::min(0.22 / dt, 4.6 / responseSeconds);
    double acceleration = omega * omega
        * (state.desiredCents - state.currentCents)
        - 2.0 * omega * state.velocityCentsPerSecond;
    const double maximumVelocity = std::max(3600.0,
        10.0 * std::max(120.0, std::abs(state.desiredCents)) / responseSeconds);
    const double maximumAcceleration = maximumVelocity
        / std::max(0.0005, responseSeconds * 0.30);
    acceleration = std::clamp(acceleration,
                              -maximumAcceleration,
                              maximumAcceleration);
    state.velocityCentsPerSecond += acceleration * dt;
    state.velocityCentsPerSecond = std::clamp(state.velocityCentsPerSecond,
                                              -maximumVelocity,
                                              maximumVelocity);
    state.currentCents += state.velocityCentsPerSecond * dt;
    if (std::abs(state.desiredCents - state.currentCents) < 0.001
        && std::abs(state.velocityCentsPerSecond) < 0.02)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
        settleTrajectory();
    }
    return state.currentCents;
}

'''
engine = replace_region(
    engine,
    "double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept\n",
    "void ModernPitchEngine::process(\n",
    new_advance,
    "advanceCorrection")
engine_path.write_text(engine, encoding="utf-8")


# -----------------------------------------------------------------------------
# Regression tests: replace old hidden-fast-range assumptions with explicit
# user-owned glide semantics and remove the weak-tail identity veto expectation.
# -----------------------------------------------------------------------------
test_path = Path("Tests/SupervisorContinuityTest.cpp")
test = test_path.read_text(encoding="utf-8")

test = replace_once(
    test,
    '''    // The target-change path must remain inside Scale Lock's own fast range;
    // the general 35-40 ms transition control may not stretch it again.
    hardDenseParameters.retuneTimeMs = 500.0f;
    hardDenseParameters.transitionTimeMs = 80.0f;
    hardDenseParameters.tempo.mode = CreativeTempo::Mode::off;
    const double denseScaleLockResponse = engine->responseTimeMs(
        hardDenseParameters, true, 25.0);
    std::cerr << "dense_scale_lock_response_ms="
              << denseScaleLockResponse << '\\n';
    success &= check(denseScaleLockResponse <= 3.001,
                     "scale_lock_target_change_stays_in_live_speed_budget");''',
    '''    // GLIDE_STATE_MODEL_V2: no latency mode may silently compress the
    // user's Response during a scale-degree transition.
    hardDenseParameters.retuneTimeMs = 500.0f;
    hardDenseParameters.transitionTimeMs = 80.0f;
    hardDenseParameters.tempo.mode = CreativeTempo::Mode::off;
    const double denseScaleLockResponse = engine->responseTimeMs(
        hardDenseParameters, true, 25.0);
    std::cerr << "dense_scale_lock_response_ms="
              << denseScaleLockResponse << '\\n';
    success &= check(std::abs(denseScaleLockResponse - 500.0) < 1.0e-9,
                     "response_owns_scale_lock_glide_without_mode_prudence");''',
    "dense response test")

test = replace_once(
    test,
    '''    parameters.retuneTimeMs = 0.0f;
    parameters.transitionTimeMs = 40.0f;
    const double transitionResponse = engine->responseTimeMs(parameters, true, 100.0);
    std::cerr << "single_path_transition_response_ms=" << transitionResponse << '\\n';
    success &= check(transitionResponse > 8.0 && transitionResponse < 32.1,
                     "target_revision_uses_bounded_single_path_transition");

    ModernPitchEngine::CorrectionState boundedTransition;
    boundedTransition.targetValid = true;
    boundedTransition.noteBodyLatched = true;
    boundedTransition.noteBodyConfidence = 0.95f;
    boundedTransition.trackingState = ModernPitchEngine::TrackingState::transition;
    boundedTransition.desiredCents = 420.0;
    boundedTransition.currentCents = 0.0;
    boundedTransition.responseMs = 500.0;
    for (int i = 0; i < 5900; ++i)
        static_cast<void>(engine->advanceCorrection(boundedTransition));
    std::cerr << "bounded_transition_velocity="
              << boundedTransition.velocityCentsPerSecond << '\\n';
    success &= check(boundedTransition.trackingState == ModernPitchEngine::TrackingState::stable,
                     "transition_has_hard_musical_time_bound");
    success &= check(std::abs(boundedTransition.velocityCentsPerSecond) > 0.02,
                     "stable_state_does_not_require_zero_controller_velocity");''',
    '''    parameters.retuneTimeMs = 0.0f;
    parameters.transitionTimeMs = 40.0f;
    parameters.tempo.mode = CreativeTempo::Mode::off;
    const double transitionResponse = engine->responseTimeMs(parameters, true, 100.0);
    std::cerr << "single_path_transition_response_ms=" << transitionResponse << '\\n';
    success &= check(std::abs(transitionResponse) < 1.0e-12,
                     "response_zero_has_no_hidden_transition_prudence");

    ModernPitchEngine::CorrectionState boundedTransition;
    boundedTransition.targetValid = true;
    boundedTransition.noteBodyLatched = true;
    boundedTransition.noteBodyConfidence = 0.95f;
    boundedTransition.trackingState = ModernPitchEngine::TrackingState::transition;
    boundedTransition.desiredCents = 420.0;
    boundedTransition.currentCents = 0.0;
    boundedTransition.responseMs = 500.0;
    for (int i = 0; i < 5900; ++i)
        static_cast<void>(engine->advanceCorrection(boundedTransition));
    std::cerr << "long_transition_velocity="
              << boundedTransition.velocityCentsPerSecond << '\\n';
    success &= check(boundedTransition.trackingState == ModernPitchEngine::TrackingState::transition
                     && std::abs(boundedTransition.velocityCentsPerSecond) > 0.02,
                     "long_user_glide_is_not_cut_by_prudence_timeout");
    for (int i = 0; i < 90000; ++i)
        static_cast<void>(engine->advanceCorrection(boundedTransition));
    success &= check(boundedTransition.trackingState == ModernPitchEngine::TrackingState::stable
                     && std::abs(boundedTransition.currentCents - boundedTransition.desiredCents) < 0.001,
                     "transition_locks_only_after_glide_arrives");''',
    "transition trajectory test")

# Tail block: preserve exact destination but a weak fragment is no longer vetoed.
old_tail_comment = '''    // TAIL_MELODIC_MICRO_GLIDE_V1 regression: a strong body keeps literal
    // Response=0 above. A weak-but-still-musical Scale-Lock tail may move to the
    // next real degree, but it must do so on the same wet trajectory and still
    // converge exactly to the unattenuated destination.'''
new_tail_comment = '''    // GLIDE_STATE_MODEL_V2 regression: target identity never loses authority
    // because a fragment is weak. A real degree change becomes transition and
    // receives the same wet musical glide to the exact destination.'''
test = replace_once(test, old_tail_comment, new_tail_comment, "tail test comment")

test = replace_once(
    test,
    '''    success &= check(std::abs(tailState.targetLog2 - bodyTarget) * 1200.0 > 30.0
                     && tailState.responseMs >= 5.5
                     && tailState.responseMs <= 13.1,
                     "weak_tail_target_change_uses_single_wet_micro_glide");''',
    '''    success &= check(std::abs(tailState.targetLog2 - bodyTarget) * 1200.0 > 30.0
                     && tailState.trackingState == ModernPitchEngine::TrackingState::transition
                     && tailState.responseMs >= 5.5,
                     "target_change_becomes_single_wet_musical_glide");''',
    "tail transition test")

test = replace_once(
    test,
    '''    // Once the tail is breath-like and extremely weak, a lone pitch accident
    // may not create another tiny note. The owned degree remains corrected;
    // this is target identity hold, not a bypass or reduction of correction.
    const double ownedTailTarget = tailState.targetLog2;''',
    '''    // Weak late pitch is not grounds for a prudence veto. If the quantizer
    // chooses another degree, it must be allowed to transition there exactly.
    const double ownedTailTarget = tailState.targetLog2;''',
    "weak tail semantics comment")

test = replace_once(
    test,
    '''    success &= check(std::abs(tailState.targetLog2 - ownedTailTarget) < 1.0e-12
                     && std::abs(tailState.desiredCents) > 5.0,
                     "very_weak_tail_keeps_owned_degree_without_dry_bypass");''',
    '''    const double weakTailTargetHz = std::exp2(tailState.targetLog2);
    const double weakTailExpectedCents = 1200.0 * std::log2(
        weakTailTargetHz / static_cast<double>(weakTailAccident.correctionFrequencyHz));
    success &= check(std::abs(tailState.targetLog2 - ownedTailTarget) * 1200.0 > 30.0
                     && tailState.trackingState == ModernPitchEngine::TrackingState::transition
                     && tailState.responseMs >= 5.5
                     && std::abs(tailState.desiredCents - weakTailExpectedCents) < 1.0e-6,
                     "very_weak_target_change_glides_without_prudence_veto");''',
    "weak tail veto test")

test_path.write_text(test, encoding="utf-8")


# -----------------------------------------------------------------------------
# Static GUI/audio contract: log Response taper + exponential Amount are visible
# and old hidden mode response mappings are physically absent.
# -----------------------------------------------------------------------------
contract_path = Path("Tests/GuiAudioControlContractTest.cpp")
contract = contract_path.read_text(encoding="utf-8")
old_contract = '''    success &= check(has(editor, "3.0 + 2.0 * norm")
                         && has(editor, "1.5 + 1.5 * norm")
                         && has(editor, "0.35 + 1.15 * norm")
                         && has(engine, "3.0 + 2.0 * norm")
                         && has(engine, "1.5 + 1.5 * norm")
                         && has(engine, "0.35 + 1.15 * norm"),
                     "response_display_matches_dsp_curve");'''
new_contract = '''    success &= check(has(processor, "LOG_RESPONSE_TAPER_V1")
                         && has(processor, "setSkewForCentre (25.0f)")
                         && has(processor, "EXPONENTIAL_AMOUNT_V1")
                         && has(processor, "std::expm1")
                         && has(editor, "val < 10.0 ? 2 : 1")
                         && has(engine, "GLIDE_STATE_MODEL_V2")
                         && !has(editor, "3.0 + 2.0 * norm")
                         && !has(engine, "3.0 + 2.0 * norm")
                         && !has(engine, "1.5 + 1.5 * norm")
                         && !has(engine, "0.35 + 1.15 * norm"),
                     "response_log_taper_and_amount_exponential_are_explicit");'''
contract = replace_once(contract, old_contract, new_contract, "GUI curve contract")
contract_path.write_text(contract, encoding="utf-8")

print("LOCKED_VOICED_GLIDE_CURVES_V4_PATCH=PASS")
