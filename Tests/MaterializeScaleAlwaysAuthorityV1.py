from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


engine_p = Path('Source/ModernPitchEngine.cpp')
live_p = Path('Source/LivePitchProcessor.h')
plugin_p = Path('Source/PluginProcessor.cpp')
test_p = Path('Tests/SupervisorContinuityTest.cpp')

engine = engine_p.read_text()
live = live_p.read_text()
plugin = plugin_p.read_text()
test = test_p.read_text()

# ---------------------------------------------------------------------------
# Hold is explicit everywhere. Scale Lock may alter trajectory speed, never
# decide whether hidden target-retention prudence exists.
engine = one(engine,
'''bool ModernPitchEngine::zeroPrudenceAuthority(const Parameters& parameters) noexcept
{
    // SCALE_CELL_OWNS_SOFTNESS_V1: Hold alone owns target-retention prudence.
    // Amount/Humanize/Vibrato never select a source-authoritative regime.
    return parameters.scaleLock
        && std::clamp(static_cast<double>(finiteOr(parameters.lockHysteresis, 24.0f)),
                      0.0, 80.0) <= 0.00001;
}
''',
'''bool ModernPitchEngine::zeroPrudenceAuthority(const Parameters& parameters) noexcept
{
    // HOLD_IS_EXPLICIT_EVERYWHERE_V1: Hold alone owns target-retention
    // prudence. Scale Lock may change trajectory timing, never secretly create
    // or remove hysteresis. Hold=0 therefore means exactly zero in every mode.
    return std::clamp(static_cast<double>(finiteOr(parameters.lockHysteresis, 24.0f)),
                      0.0, 80.0) <= 0.00001;
}
''',
'zero prudence independent of scale lock')

engine = one(engine,
'''    if (!parameters.scaleLock)
    {
        return static_cast<float>(std::clamp(
            2.0 + 0.22 * static_cast<double>(quantizer.minimumStepCents())
                * static_cast<double>(clamp01(parameters.humanize)),
            1.0, 80.0));
    }

    // No mode, tempo, confidence or detector-derived multiplier may add Hold.
''',
'''    // NO_HIDDEN_PRUDENCE_OUTSIDE_LOCK_V1: there is no alternate automatic
    // hysteresis law when Scale Lock is off. The same visible Hold value owns
    // target retention everywhere.

    // No mode, tempo, confidence or detector-derived multiplier may add Hold.
''',
'remove implicit non-lock hysteresis')

# ---------------------------------------------------------------------------
# Breath/phonetic labels freeze identity, never the correction itself. A valid
# measured coordinate continues through the existing transport outlier wall;
# no F0 simply holds the previous scale-owned correction.
old_label_gate = '''    // TREMolo_LOCAL_CONTINUITY_OWNS_TRANSPORT_V2: an amplitude trough can make
    // the presence classifier blink false even while the physical F0 remains a
    // continuous coordinate of the already-owned voice.  Do not turn that
    // telemetry blink into a stale correction.  Conversely, a periodic accident
    // far from the owned source transport during true breath/noise still has no
    // audible authority.  36 cents matches the existing maximum bounded catchup
    // step and is intentionally far below a scale-degree or octave decision.
    const float labelCoordinateHz =
        std::isfinite(observation.correctionFrequencyHz)
        && observation.correctionFrequencyHz > 0.0f
        ? observation.correctionFrequencyHz
        : observation.frequencyHz;
    const bool localOwnedCoordinate = validPitch
        && std::isfinite(labelCoordinateHz)
        && labelCoordinateHz > 0.0f
        && std::isfinite(state.transportPeriodHz)
        && state.transportPeriodHz > 0.0
        && std::abs(1200.0 * std::log2(
            static_cast<double>(labelCoordinateHz) / state.transportPeriodHz)) <= 36.0;
    const bool labelMayTransport = validPitch
        && (observation.audioPresent || localOwnedCoordinate);

'''
engine = one(engine, old_label_gate,
'''    // BREATH_IS_TARGETED_NOT_DRY_V1: voice labels may veto note identity,
    // never correction authority. If a real F0 exists it continues through the
    // ordinary transport outlier wall and is corrected toward the already-owned
    // scale degree. If no F0 exists, the previous correction is held exactly.

''',
'breath authority contract')

engine = one(engine,
'''        if (!labelMayTransport)
        {
            // PRESENT_AUDIO_OWNS_CORRECTION_CONTINUITY_V1: a formally valid
            // periodic accident inside true breath/absence still has zero
            // transport authority unless it is locally continuous with the
            // already-owned source coordinate. Hold exact correction otherwise.
            if (confirmedAbsence
                || confirmedAbsenceFrame
                || state.breathEvidenceSamples > static_cast<int>(0.12 * sampleRate_))
            {
                state.pitchCentreValid = false;
            }
            return;
        }

        // VALID_F0_TRANSPORT_SURVIVES_LABEL_V1: the classifier can say
''',
'''        if (!validPitch)
        {
            // No measured source coordinate exists. Keep the current target,
            // transport and non-zero correction exactly; never manufacture F0.
            if (confirmedAbsence
                || confirmedAbsenceFrame
                || state.breathEvidenceSamples > static_cast<int>(0.12 * sampleRate_))
            {
                state.pitchCentreValid = false;
            }
            return;
        }

        // VALID_F0_TRANSPORT_SURVIVES_LABEL_V1: the classifier can say
''',
'valid breath f0 always reaches transport')

engine = one(engine,
'''        if (!labelMayTransport)
            return;
        identityOnlyVeto = true;
''',
'''        if (!validPitch)
            return;
        identityOnlyVeto = true;
''',
'valid phonetic f0 reaches transport wall')

# ---------------------------------------------------------------------------
# A latent identity challenger is analysis-only. Pending identity may not freeze
# the already-owned wet correction. Existing transport outlier protection still
# blocks isolated large coordinates.
engine = one(engine,
'''                if (state.latentTargetHops < requiredHops)
                {
                    // Stable C -> uncertain material => exactly stable C.
                    // Freeze all audible coordinates until this deep challenger
                    // has actually earned a scale-domain commit.
                    return;
                }

                detectorScaleCommit = true;
                state.latentTargetValid = false;
                state.latentTargetLog2 = 0.0;
                state.latentTargetHops = 0;
''',
'''                if (state.latentTargetHops >= requiredHops)
                {
                    detectorScaleCommit = true;
                    state.latentTargetValid = false;
                    state.latentTargetLog2 = 0.0;
                    state.latentTargetHops = 0;
                }
                // LATENT_IDENTITY_NEVER_FREEZES_WET_V1: while identity is still
                // pending, continue through centre/transport/correction. The
                // challenger has zero target authority, but it may not create a
                // dry-like/stale correction discontinuity on a long note.
''',
'pending identity no longer freezes wet correction')

# ---------------------------------------------------------------------------
# One visible Vibrato Preserve control everywhere; remove the hidden 70% normal
# path. Scale Lock no longer selects a different depth law.
engine = one(engine,
'''    const float requestedVibrato = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve)
        : clamp01(parameters.preserveVibrato);
''',
'''    // SINGLE_VISIBLE_VIBRATO_AUTHORITY_V1: the visible Vibrato Preserve
    // control is authoritative in every mode. No hidden non-lock preserve path.
    const float requestedVibrato = clamp01(parameters.vibratoPreserve);
''',
'unify vibrato authority')

engine = one(engine,
'''    const double lockStrictness = parameters.scaleLock
        ? static_cast<double>(clamp01(parameters.lockStrictness)) : 0.0;
    const double cageFraction = parameters.scaleLock
        ? (0.16 + 0.10 * (1.0 - lockStrictness))
        : 0.34;
    const double cageLimit = parameters.scaleLock ? 18.0 : 42.0;
    const double residualBudgetCents = std::clamp(
        minimumStep * cageFraction, 0.25, cageLimit);
''',
'''    // SCALE_LOCK_NEVER_OWNS_DEPTH_V1: Scale Lock may alter target retention
    // and trajectory timing, never correction depth. Amount/Humanize/Vibrato
    // soften one common target-owned cage in every mode.
    constexpr double cageFraction = 0.26;
    constexpr double cageLimit = 18.0;
    const double residualBudgetCents = std::clamp(
        minimumStep * cageFraction, 0.25, cageLimit);
''',
'remove scale-lock depth branch')

# ---------------------------------------------------------------------------
# Keep both legacy parameter fields synchronized for API compatibility, but the
# engine consumes only the visible Vibrato Preserve authority above.
live = one(live,
'''        parameters_.vibratoPreserve = std::clamp(vibratoPreserve, 0.0f, 1.0f);

        const float h = parameters_.lockHysteresis / 80.0f;
''',
'''        parameters_.vibratoPreserve = std::clamp(vibratoPreserve, 0.0f, 1.0f);
        parameters_.preserveVibrato = parameters_.vibratoPreserve;

        const float h = parameters_.lockHysteresis / 80.0f;
''',
'synchronize visible vibrato field')

plugin = one(plugin,
'''        35.0f,   // transitionMs
        0.70f,   // preserveVibrato
        humanizeVal,
''',
'''        35.0f,   // transitionMs
        vibratoPreserve, // preserveVibrato: same visible authority in every mode
        humanizeVal,
''',
'remove hard-coded hidden vibrato')

# ---------------------------------------------------------------------------
# Regressions for the exact user contract.
insert_anchor = '''    // DETECTOR_IS_OBSERVER_V1: rigid correction settings do not alter the
    // detector decoder. Static CI below forbids that API from returning.
'''
insert = r'''    // HOLD_IS_EXPLICIT_EVERYWHERE_V1: Hold=0 means zero hysteresis even
    // when Scale Lock is disabled. There is no hidden Humanize-based prudence.
    ModernPitchEngine::Parameters unlockedZeroHold = explicitAuthorityParameters;
    unlockedZeroHold.scaleLock = false;
    unlockedZeroHold.lockHysteresis = 0.0f;
    success &= check(engine->adaptiveHysteresis(unlockedZeroHold,
                                                 explicitAuthorityQuantizer,
                                                 explicitAuthorityObservation) == 0.0f,
                     "hold_zero_has_no_hidden_unlocked_hysteresis");

    // SINGLE_VISIBLE_VIBRATO_AUTHORITY_V1: legacy preserveVibrato may contain
    // an old non-zero value, but visible Vibrato Preserve=0 must remove all
    // deliberate vibrato residual even with Scale Lock off.
    ModernPitchEngine::Parameters unlockedVibrato = explicitAuthorityParameters;
    unlockedVibrato.scaleLock = false;
    unlockedVibrato.lockHysteresis = 0.0f;
    unlockedVibrato.vibratoPreserve = 0.0f;
    unlockedVibrato.preserveVibrato = 0.70f; // deliberately hostile legacy value
    ModernPitchEngine::ScaleQuantizer unlockedVibratoQuantizer;
    unlockedVibratoQuantizer.reset();
    unlockedVibratoQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState unlockedVibratoState;
    double maximumUnlockedResidual = 0.0;
    for (int hop = 0; hop < 500; ++hop)
    {
        const double cents = 38.0 * std::sin(2.0 * 3.14159265358979323846
            * static_cast<double>(hop) / 150.0);
        auto vibratoHop = strongPitch(static_cast<float>(
            440.0 * std::exp2(cents / 1200.0)));
        vibratoHop.audioPresent = true;
        vibratoHop.correctionFrequencyHz = vibratoHop.frequencyHz;
        engine->updateCorrectionState(unlockedVibratoState,
                                      unlockedVibratoQuantizer,
                                      vibratoHop,
                                      unlockedVibrato);
        if (hop > 40 && unlockedVibratoState.targetValid)
        {
            const double actualOutputHz = static_cast<double>(vibratoHop.frequencyHz)
                * std::exp2(unlockedVibratoState.desiredCents / 1200.0);
            const double targetHz = std::exp2(unlockedVibratoState.targetLog2);
            maximumUnlockedResidual = std::max(maximumUnlockedResidual,
                std::abs(1200.0 * std::log2(actualOutputHz / targetHz)));
        }
    }
    success &= check(maximumUnlockedResidual < 0.15,
                     "visible_vibrato_zero_removes_hidden_nonlock_preserve");

    // SCALE_LOCK_NEVER_OWNS_DEPTH_V1: at equal visible softness controls, the
    // steady-state target-owned residual budget is identical with Scale Lock on
    // or off. Scale Lock is not a secret correction-depth switch.
    ModernPitchEngine::Parameters commonSoftness = explicitAuthorityParameters;
    commonSoftness.amount = 0.55f;
    commonSoftness.humanize = 0.45f;
    commonSoftness.vibratoPreserve = 0.0f;
    commonSoftness.preserveVibrato = 0.0f;
    commonSoftness.lockHysteresis = 0.0f;
    auto softInput = strongPitch(450.0f);
    softInput.audioPresent = true;
    softInput.correctionFrequencyHz = 450.0f;
    ModernPitchEngine::ScaleQuantizer lockOffDepthQuantizer;
    ModernPitchEngine::ScaleQuantizer lockOnDepthQuantizer;
    lockOffDepthQuantizer.reset();
    lockOnDepthQuantizer.reset();
    lockOffDepthQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    lockOnDepthQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState lockOffDepthState;
    ModernPitchEngine::CorrectionState lockOnDepthState;
    auto lockOffSoftness = commonSoftness;
    auto lockOnSoftness = commonSoftness;
    lockOffSoftness.scaleLock = false;
    lockOnSoftness.scaleLock = true;
    engine->updateCorrectionState(lockOffDepthState, lockOffDepthQuantizer,
                                  softInput, lockOffSoftness);
    engine->updateCorrectionState(lockOnDepthState, lockOnDepthQuantizer,
                                  softInput, lockOnSoftness);
    const double lockOffResidual = std::abs(1200.0 * std::log2(
        lockOffDepthState.transportPeriodHz
        * std::exp2(lockOffDepthState.desiredCents / 1200.0) / 440.0));
    const double lockOnResidual = std::abs(1200.0 * std::log2(
        lockOnDepthState.transportPeriodHz
        * std::exp2(lockOnDepthState.desiredCents / 1200.0) / 440.0));
    success &= check(std::abs(lockOffResidual - lockOnResidual) < 1.0e-9,
                     "scale_lock_never_changes_correction_depth");

    // BREATH_IS_TARGETED_NOT_DRY_V1: a breathy note with a valid F0 cannot
    // nominate another degree, but it must continue being corrected to the
    // already-owned degree even when audioPresent telemetry is false.
    ModernPitchEngine::ScaleQuantizer breathTargetQuantizer;
    breathTargetQuantizer.reset();
    breathTargetQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState breathTargetState;
    auto breathBase = strongPitch(450.0f);
    breathBase.audioPresent = true;
    breathBase.correctionFrequencyHz = 450.0f;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(breathTargetState, breathTargetQuantizer,
                                      breathBase, explicitAuthorityParameters);
    const double breathOwnedTarget = breathTargetState.targetLog2;
    auto breathParameters = explicitAuthorityParameters;
    setBreathEvidence(breathParameters);
    auto breathF0 = strongPitch(454.0f);
    breathF0.audioPresent = false;
    breathF0.correctionFrequencyHz = 454.0f;
    for (int hop = 0; hop < 16; ++hop)
        engine->updateCorrectionState(breathTargetState, breathTargetQuantizer,
                                      breathF0, breathParameters);
    const double breathActualOutputHz = 454.0
        * std::exp2(breathTargetState.desiredCents / 1200.0);
    const double breathTargetHz = std::exp2(breathTargetState.targetLog2);
    success &= check(std::abs(breathTargetState.targetLog2 - breathOwnedTarget) < 1.0e-12
                     && std::abs(1200.0 * std::log2(
                         breathActualOutputHz / breathTargetHz)) < 0.15,
                     "breathy_valid_f0_stays_on_owned_scale_target");

    // DETECTOR_IS_OBSERVER_V1: rigid correction settings do not alter the
    // detector decoder. Static CI below forbids that API from returning.
'''
test = one(test, insert_anchor, insert, 'insert authority regressions')

# Update the old breath regression: the new contract is stronger. A sustained
# measured breath F0 may refine transport, but never retarget or return to unity.
old_breath_test = '''    // TARGET_AUTHORITY_TAIL_HOLD_V1: once a target exists, breath/noise is
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
                     "breath_cannot_release_existing_target_to_source");
'''
new_breath_test = '''    // BREATH_IS_TARGETED_NOT_DRY_V1: once a target exists, breath/noise may
    // not nominate another degree. A sustained measured breath F0 may refine
    // source transport, but the correction remains non-zero and scale-owned.
    ModernPitchEngine::CorrectionState spuriousBreath = dropoutState;
    const double breathHoldTargetHz = 220.0 * std::exp2(100.0 / 1200.0);
    spuriousBreath.targetLog2 = std::log2(breathHoldTargetHz);
    setBreathEvidence(parameters);
    auto falsePitchOnBreath = strongPitch(231.0f);
    falsePitchOnBreath.audioPresent = false;
    falsePitchOnBreath.correctionFrequencyHz = 231.0f;
    const double breathTargetBefore = spuriousBreath.targetLog2;
    for (int i = 0; i < 100; ++i)
        engine->updateCorrectionState(spuriousBreath, quantizer,
                                      falsePitchOnBreath, parameters);
    success &= check(spuriousBreath.trackingState != ModernPitchEngine::TrackingState::release
                     && spuriousBreath.targetValid
                     && std::abs(spuriousBreath.targetLog2 - breathTargetBefore) < 1.0e-12
                     && std::abs(spuriousBreath.desiredCents) > 1.0,
                     "breath_cannot_release_existing_target_to_source");
'''
test = one(test, old_breath_test, new_breath_test, 'update breath regression semantics')

# Pending identity must not stall a smoothly moving source coordinate.
last_anchor = '''    // OCTAVE_AMBIGUITY_V2: a single-family octave challenger must persist for a
'''
latent_test = r'''    // LATENT_IDENTITY_NEVER_FREEZES_WET_V1: when a smooth long-note
    // trajectory enters the next cell but has not yet earned identity, target
    // stays put while transport/correction continue. No brief stale/dry notch.
    ModernPitchEngine::ScaleQuantizer pendingIdentityQuantizer;
    pendingIdentityQuantizer.reset();
    pendingIdentityQuantizer.setScale(authorityChromatic.data(),
                                       static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState pendingIdentityState;
    for (int cents = 0; cents <= 70; cents += 5)
    {
        auto ramp = strongPitch(static_cast<float>(
            440.0 * std::exp2(static_cast<double>(cents) / 1200.0)));
        ramp.audioPresent = true;
        ramp.correctionFrequencyHz = ramp.frequencyHz;
        engine->updateCorrectionState(pendingIdentityState,
                                      pendingIdentityQuantizer,
                                      ramp, explicitAuthorityParameters);
    }
    const double pendingTargetBefore = pendingIdentityState.targetLog2;
    const double pendingTransportBefore = pendingIdentityState.transportPeriodHz;
    auto firstDeepPending = strongPitch(static_cast<float>(
        440.0 * std::exp2(75.0 / 1200.0)));
    firstDeepPending.audioPresent = true;
    firstDeepPending.correctionFrequencyHz = firstDeepPending.frequencyHz;
    engine->updateCorrectionState(pendingIdentityState,
                                  pendingIdentityQuantizer,
                                  firstDeepPending, explicitAuthorityParameters);
    success &= check(std::abs(pendingIdentityState.targetLog2 - pendingTargetBefore) < 1.0e-12
                     && pendingIdentityState.transportPeriodHz > pendingTransportBefore,
                     "pending_identity_never_freezes_owned_wet_transport");

    // OCTAVE_AMBIGUITY_V2: a single-family octave challenger must persist for a
'''
test = one(test, last_anchor, latent_test, 'insert pending identity regression')

engine_p.write_text(engine)
live_p.write_text(live)
plugin_p.write_text(plugin)
test_p.write_text(test)
print('scale-always-authority v1 materialized')
