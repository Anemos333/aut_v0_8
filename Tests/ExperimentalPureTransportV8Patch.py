from pathlib import Path

RENDERER = Path('Source/SingleWetSpectralRenderer.cpp')
ENGINE = Path('Source/ModernPitchEngine.cpp')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one occurrence, got {count}')
    return text.replace(old, new, 1)

renderer = RENDERER.read_text(encoding='utf-8')

phase_anchor = '''            double& synthesisPhase =
                layer.synthesisPhases[static_cast<std::size_t>(sourceBin)];

            // LIVE_EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7
'''
phase_replacement = '''            double& synthesisPhase =
                layer.synthesisPhases[static_cast<std::size_t>(sourceBin)];

            // EXPERIMENTAL_PURE_TRANSPORT_V8_1
            // No F0/harmonic reconstruction. nearestPeak_ is geometry only: it
            // partitions the short FFT into measured local lobes. Every bin in a
            // lobe receives the same exact additive displacement of its measured
            // peak. The owning peak itself therefore advances at peak*safeRatio.
            if (frameSize_ <= 128)
            {
                const double measuredSourceBin =
                    trueSourceBins_[static_cast<std::size_t>(sourceBin)];
                const int geometryPeak = nearestPeak_.empty()
                    ? sourceBin
                    : nearestPeak_[static_cast<std::size_t>(sourceBin)];
                const bool geometryPeakValid =
                    geometryPeak >= 0 && geometryPeak <= positiveBins;
                const double measuredPeakBin = geometryPeakValid
                    ? trueSourceBins_[static_cast<std::size_t>(geometryPeak)]
                    : measuredSourceBin;
                const double shiftBins = measuredPeakBin * (safeRatio - 1.0);
                synthesisPhase += expectedPhaseScale * (measuredSourceBin + shiftBins);
                synthesisPhase -= twoPi * std::nearbyint(synthesisPhase / twoPi);
                propagatedPhases_[static_cast<std::size_t>(sourceBin)] = synthesisPhase;
                continue;
            }

            // LIVE_EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7
'''
renderer = replace_once(renderer, phase_anchor, phase_replacement,
                        'Experimental lobe translation phase')

formant_old = '''    const float safeFormant = clamp01(formantPreservation);
'''
formant_new = '''    // EXPERIMENTAL_PURE_TRANSPORT_V8_1: no spectral-envelope
    // reconstruction in Experimental. Live/Quality keep the explicit control.
    const float safeFormant = frameSize_ <= 128
        ? 0.0f
        : clamp01(formantPreservation);
'''
renderer = replace_once(renderer, formant_old, formant_new,
                        'Experimental formant reconstruction removal')

output_phase_old = '''        const double ownPhase = propagatedPhases_[sourceIndex];
        const double lockedPhase = peakValid
            ? propagatedPhases_[static_cast<std::size_t>(peak)]
                + wrapPhase(static_cast<double>(analysisPhases_[sourceIndex])
                    - static_cast<double>(analysisPhases_[static_cast<std::size_t>(peak)]))
            : ownPhase;
        const double phaseDelta = wrapPhase(lockedPhase - ownPhase);
        const double outputPhase = ownPhase
            + static_cast<double>(lockStrength) * phaseDelta;
'''
output_phase_new = '''        const double ownPhase = propagatedPhases_[sourceIndex];
        const double lockedPhase = peakValid
            ? propagatedPhases_[static_cast<std::size_t>(peak)]
                + wrapPhase(static_cast<double>(analysisPhases_[sourceIndex])
                    - static_cast<double>(analysisPhases_[static_cast<std::size_t>(peak)]))
            : ownPhase;
        const double phaseDelta = wrapPhase(lockedPhase - ownPhase);
        // EXPERIMENTAL_PURE_TRANSPORT_V8_1: one measured lobe has one phase
        // trajectory. There is no second per-bin phase population to separate
        // from the shifted peak. The relative within-frame phase is measured,
        // never inferred from F0 or harmonic number.
        const double outputPhase = frameSize_ <= 128 && peakValid
            ? lockedPhase
            : ownPhase + static_cast<double>(lockStrength) * phaseDelta;
'''
renderer = replace_once(renderer, output_phase_old, output_phase_new,
                        'Experimental single lobe phase trajectory')

RENDERER.write_text(renderer, encoding='utf-8')

engine = ENGINE.read_text(encoding='utf-8')
state_old = '''    const bool exactAuthority = exactScaleLockAuthority(parameters);
    const bool zeroPrudence = zeroPrudenceAuthority(parameters); // AUTHORITY_CONTROLS_EXPLICIT_V1
    const bool richEvidence = parameters.voiceEvidenceValid;
'''
state_new = '''    const bool exactAuthority = exactScaleLockAuthority(parameters);
    const bool zeroPrudence = zeroPrudenceAuthority(parameters); // AUTHORITY_CONTROLS_EXPLICIT_V1
    // EXPERIMENTAL_PURE_TRANSPORT_V8_1: ultra-live cannot wait for semantic
    // breath/body/transient classification. Evidence remains telemetry only.
    const bool experimentalDirectTransport = latencyMode_ == LatencyMode::ultraLive;
    const bool richEvidence = parameters.voiceEvidenceValid
        && !experimentalDirectTransport;
'''
engine = replace_once(engine, state_old, state_new,
                      'Experimental analysis authority removal')
release_old = '''    if (state.targetValid && (confirmedBreath || confirmedAbsence))
'''
release_new = '''    if (!experimentalDirectTransport
        && state.targetValid && (confirmedBreath || confirmedAbsence))
'''
engine = replace_once(engine, release_old, release_new,
                      'Experimental breath release removal')
invalid_old = '''        if (state.targetValid && std::abs(state.currentCents) > 0.001)
        {
            setState(TrackingState::release);
            state.desiredCents = 0.0;
            state.responseMs = std::max(5.5,
                responseTimeMs(parameters, true, state.lastTargetJumpCents));
        }
        else
'''
invalid_new = '''        if (experimentalDirectTransport && state.targetValid)
        {
            // No F0 is not evidence for unity. Keep the already acquired glide
            // coordinate until a new measurement replaces it.
            setState(TrackingState::acquire);
            return;
        }

        if (state.targetValid && std::abs(state.currentCents) > 0.001)
        {
            setState(TrackingState::release);
            state.desiredCents = 0.0;
            state.responseMs = std::max(5.5,
                responseTimeMs(parameters, true, state.lastTargetJumpCents));
        }
        else
'''
engine = replace_once(engine, invalid_old, invalid_new,
                      'Experimental invalid-F0 unity release removal')
ENGINE.write_text(engine, encoding='utf-8')
print('EXPERIMENTAL_PURE_TRANSPORT_V8_1_PATCH=PASS')
