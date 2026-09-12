from pathlib import Path


def one(text, old, new, label):
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected 1 match, got {n}")
    return text.replace(old, new, 1)

cpp_p = Path('Source/ModernPitchEngine.cpp')
h_p = Path('Source/ModernPitchEngine.h')
t_p = Path('Tests/CleanModernPitchEngineTest.cpp')
cpp = cpp_p.read_text()
h = h_p.read_text()
t = t_p.read_text()

cpp = one(cpp, '''bool ModernPitchEngine::exactScaleLockAuthority(const Parameters& parameters) noexcept
{
    return parameters.scaleLock
        && clamp01(parameters.amount) >= 0.99999f
        && clamp01(parameters.humanize) <= 0.00001f
        && clamp01(parameters.vibratoPreserve) <= 0.00001f;
}

bool ModernPitchEngine::zeroPrudenceAuthority(const Parameters& parameters) noexcept
{
    // Hold is the only user permission to retain the previous target degree.
    // Response controls glide speed only and cannot weaken target authority.
    return exactScaleLockAuthority(parameters)
        && std::clamp(static_cast<double>(finiteOr(parameters.lockHysteresis, 24.0f)),
                      0.0, 80.0) <= 0.00001;
}
''', '''bool ModernPitchEngine::zeroPrudenceAuthority(const Parameters& parameters) noexcept
{
    // SCALE_CELL_OWNS_SOFTNESS_V1: Hold alone owns target-retention prudence.
    // Amount/Humanize/Vibrato never select a source-authoritative regime.
    return parameters.scaleLock
        && std::clamp(static_cast<double>(finiteOr(parameters.lockHysteresis, 24.0f)),
                      0.0, 80.0) <= 0.00001;
}
''', 'authority split')

cpp = one(cpp, '    const bool exactAuthority = exactScaleLockAuthority(parameters);\n', '', 'exactAuthority local')

cpp = one(cpp, '''    const double amount = static_cast<double>(clamp01(parameters.amount));
    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    state.desiredCents = std::clamp(
        state.rescueBaseDesiredCents
            + amount * (targetDeltaCents - sourceDeltaCents),
        -maximumCents, maximumCents);
''', '''    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    // NO_UNITY_ESCAPE_V1: a detector hole continues the scale-owned transport;
    // Amount cannot pull rescue motion back toward the source coordinate.
    state.desiredCents = std::clamp(
        state.rescueBaseDesiredCents + targetDeltaCents - sourceDeltaCents,
        -maximumCents, maximumCents);
''', 'rescue amount')

start = cpp.index('    const double vibratoComponent = observedLog2 - state.pitchCentreLog2;\n')
end_marker = '    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);\n'
end = cpp.index(end_marker, start) + len(end_marker)
block = '''    const double vibratoComponent = observedLog2 - state.pitchCentreLog2;
    const float stable = clamp01(0.45f * observation.confidence
                               + 0.35f * observation.consensus
                               + 0.20f * std::min(1.0f,
                                   static_cast<float>(state.stableObservations) / 5.0f));
    const float periodic = clamp01(observation.periodicity);
    const double minimumStep = std::max(0.1,
        static_cast<double>(quantizer.minimumStepCents()));
    const double halfStep = 0.5 * minimumStep;
    const double centreError = std::abs((state.targetLog2 - state.pitchCentreLog2) * 1200.0);
    const float boundarySafety = 1.0f - smoothStep(
        static_cast<float>(0.58 * halfStep),
        static_cast<float>(0.92 * halfStep),
        static_cast<float>(centreError));

    const float requestedVibrato = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve)
        : clamp01(parameters.preserveVibrato);
    const float preserve = requestedVibrato * stable * periodic * boundarySafety;

    // SCALE_CELL_OWNS_SOFTNESS_V1: every setting uses the same target-owned
    // law. Softer controls enlarge a bounded cage around the selected degree;
    // they never multiply correction toward unity and never create a dry zone.
    const double amount = static_cast<double>(clamp01(parameters.amount));
    const double softness = std::clamp(
        0.72 * (1.0 - amount) + 0.20 * static_cast<double>(humanize),
        0.0, 0.88);
    const double lockStrictness = parameters.scaleLock
        ? static_cast<double>(clamp01(parameters.lockStrictness)) : 0.0;
    const double cageFraction = parameters.scaleLock
        ? (0.16 + 0.10 * (1.0 - lockStrictness))
        : 0.34;
    const double cageLimit = parameters.scaleLock ? 18.0 : 42.0;
    const double residualBudgetCents = std::clamp(
        minimumStep * cageFraction, 0.25, cageLimit);

    // SOURCE_COORDINATE_REBASE_V1: the dry is only the coordinate from which
    // transport is measured. The latest accepted live F0 is used in every mode;
    // no softer branch is allowed to fall back to a dry-owned reference.
    const double sourceOffsetCents =
        (correctionObservedLog2 - state.targetLog2) * 1200.0;
    const double targetOwnedSourceResidual = residualBudgetCents > 1.0e-9
        ? residualBudgetCents
            * std::tanh(sourceOffsetCents / residualBudgetCents)
            * softness
        : 0.0;
    const double requestedVibratoCents =
        static_cast<double>(preserve) * vibratoComponent * 1200.0;
    const double vibratoBudgetCents = residualBudgetCents
        * static_cast<double>(requestedVibrato);
    const double targetOwnedOffsetCents = std::clamp(
        targetOwnedSourceResidual
            + std::clamp(requestedVibratoCents,
                         -vibratoBudgetCents,
                         vibratoBudgetCents),
        -residualBudgetCents,
        residualBudgetCents);
    const double correctedLog2 = state.targetLog2
        + targetOwnedOffsetCents / 1200.0;

    double errorCents = (correctedLog2 - correctionObservedLog2) * 1200.0;
    const double maximumCents = 100.0 * std::clamp(
        static_cast<double>(finiteOr(parameters.maximumCorrectionSemitones, 12.0f)),
        0.0, 48.0);
    errorCents = std::clamp(errorCents, -maximumCents, maximumCents);
    state.desiredCents = errorCents;
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);
    if (firstOwnedTarget)
    {
        // NO_UNITY_ESCAPE_V1: first acquisition starts already owned by scale;
        // Response may shape later movement, never reveal an initial dry ramp.
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
    }
'''
cpp = cpp[:start] + block + cpp[end:]

cpp = one(cpp, '''    const bool targetChanged = !state.targetValid
        || std::abs(newTarget - state.targetLog2) * 1200.0 > 0.1;
''', '''    const bool firstOwnedTarget = !state.targetValid;
    const bool targetChanged = firstOwnedTarget
        || std::abs(newTarget - state.targetLog2) * 1200.0 > 0.1;
''', 'first target capture')

# Keep scale changes fail-closed instead of resetting to an audible unity path.
cpp = cpp.replace('''        if (changed)
            channelCorrections_[static_cast<std::size_t>(channel)] = {};
''', '''        if (changed)
        {
            // SCALE_CHANGE_REBASE_V1: invalidate ownership until the first
            // trustworthy coordinate is quantized in the new scale. The audio
            // path below fails closed during this tiny reacquisition window.
            channelCorrections_[static_cast<std::size_t>(channel)] = {};
        }
''', 1)
cpp = cpp.replace('''    if (linkedScaleChanged)
        linkedCorrection_ = {};
''', '''    if (linkedScaleChanged)
    {
        // SCALE_CHANGE_REBASE_V1
        linkedCorrection_ = {};
    }
''', 1)

cpp = one(cpp, '''                data[static_cast<std::size_t>(channel)][sample] =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audible,
                        safe.formantPreservation);
''', '''                const float rendered =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audible,
                        safe.formantPreservation);
                // UNOWNED_AUDIO_FAILS_CLOSED_V1: before a degree exists there
                // is no legal unity/source-pitch output in the active path.
                data[static_cast<std::size_t>(channel)][sample] =
                    correction.targetValid ? rendered : 0.0f;
''', 'dual mono fail closed')

cpp = one(cpp, '''            for (int channel = 0; channel < channels; ++channel)
                data[static_cast<std::size_t>(channel)][sample] =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audibleCorrectionCents_,
                        safe.formantPreservation);
''', '''            for (int channel = 0; channel < channels; ++channel)
            {
                const float rendered =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audibleCorrectionCents_,
                        safe.formantPreservation);
                // UNOWNED_AUDIO_FAILS_CLOSED_V1
                data[static_cast<std::size_t>(channel)][sample] =
                    linkedCorrection_.targetValid ? rendered : 0.0f;
            }
''', 'linked fail closed')

h = one(h,
        '    [[nodiscard]] static bool exactScaleLockAuthority(const Parameters& parameters) noexcept; // AUTHORITY_CONTROLS_EXPLICIT_V1\n',
        '', 'header exact authority')

# Update the old Amount=0 expectation: softness changes tolerance, never grants dry.
t = one(t, '''    success &= check(std::abs(dryTrajectory.outputFrequencyHz - 452.0) < 6.0,
                     "amount_zero_keeps_pitch");
    success &= check(std::abs(fullTrajectory.outputFrequencyHz
                              - dryTrajectory.outputFrequencyHz) > 7.0,
                     "amount_changes_audio_without_dry_wet_mix");
''', '''    success &= check(std::abs(dryTrajectory.outputFrequencyHz - 440.0)
                         < std::abs(dryTrajectory.outputFrequencyHz - 452.0),
                     "amount_zero_still_corrects_toward_scale");
    success &= check(std::abs(dryTrajectory.finalMeter.correctionCents) > 0.5f,
                     "amount_zero_never_grants_unity_off_target");
    success &= check(std::abs(fullTrajectory.outputFrequencyHz
                              - dryTrajectory.outputFrequencyHz) > 2.0,
                     "amount_changes_scale_owned_tolerance");
''', 'amount tests')
t = t.replace('"amount_zero_output_hz="', '"amount_zero_scale_owned_output_hz="', 1)

cpp_p.write_text(cpp)
h_p.write_text(h)
t_p.write_text(t)
print('scale-owned softness cleanup materialized')
