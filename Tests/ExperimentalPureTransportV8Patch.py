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

            // EXPERIMENTAL_PURE_TRANSPORT_V8
            // 128 samples cannot reliably infer harmonic ownership, voiced state,
            // transient class or breath structure. Experimental therefore makes
            // no reconstruction decision at all: every measured instantaneous
            // frequency follows the exact same multiplicative correction ratio.
            // The same coordinate drives phase velocity and spectral placement.
            if (frameSize_ <= 128)
            {
                const double measuredSourceBin =
                    trueSourceBins_[static_cast<std::size_t>(sourceBin)];
                const double transportTargetBin = measuredSourceBin * safeRatio;
                synthesisPhase += expectedPhaseScale * transportTargetBin;
                synthesisPhase -= twoPi * std::nearbyint(synthesisPhase / twoPi);
                propagatedPhases_[static_cast<std::size_t>(sourceBin)] = synthesisPhase;
                continue;
            }

            // LIVE_EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7
'''
renderer = replace_once(renderer, phase_anchor, phase_replacement,
                        'Experimental direct phase transport')

peak_anchor = '''        const int peak = nearestPeak_.empty()
            ? sourceBin
            : nearestPeak_[sourceIndex];
'''
peak_replacement = '''        // Experimental has no peak ownership. Peak regions remain available
        // only to the longer Live/Quality renderers.
        const int peak = frameSize_ <= 128
            ? -1
            : (nearestPeak_.empty() ? sourceBin : nearestPeak_[sourceIndex]);
'''
renderer = replace_once(renderer, peak_anchor, peak_replacement,
                        'Experimental peak ownership removal')

magnitude_old = '''        if (frameSize_ <= 128 && peakValid)
        {
            const double truePeakBin =
                trueSourceBins_[static_cast<std::size_t>(peak)];
            const double peakShiftBins = truePeakBin * safeRatio - truePeakBin;
            targetPosition = static_cast<double>(sourceBin) + peakShiftBins;
        }
        else if (frameSize_ <= 256
'''
magnitude_new = '''        if (frameSize_ <= 128)
        {
            // EXPERIMENTAL_PURE_TRANSPORT_V8: position and phase use the same
            // measured coordinate. No lobe owner, harmonic number or source F0.
            targetPosition = trueSourceBins_[sourceIndex] * safeRatio;
        }
        else if (frameSize_ <= 256
'''
renderer = replace_once(renderer, magnitude_old, magnitude_new,
                        'Experimental direct magnitude transport')

formant_old = '''    const float safeFormant = clamp01(formantPreservation);
'''
formant_new = '''    // EXPERIMENTAL_PURE_TRANSPORT_V8: formant preservation is spectral
    // reconstruction. Experimental is deliberately a pure shifter, so the
    // explicit formant reconstruction remains a Live/Quality facility only.
    const float safeFormant = frameSize_ <= 128
        ? 0.0f
        : clamp01(formantPreservation);
'''
renderer = replace_once(renderer, formant_old, formant_new,
                        'Experimental formant reconstruction removal')

peaks_old = '''    calculatePeakRegions(positiveBins);

    const bool resetAnalysis = phaseResetPending_ || !analysisPhaseInitialised_;
'''
peaks_new = '''    if (frameSize_ <= 128)
    {
        // EXPERIMENTAL_PURE_TRANSPORT_V8: there is intentionally no peak
        // segmentation/ownership to make an under-resolved reconstruction.
        peakBins_.clear();
        std::fill(nearestPeak_.begin(), nearestPeak_.end(), -1);
    }
    else
    {
        calculatePeakRegions(positiveBins);
    }

    const bool resetAnalysis = phaseResetPending_ || !analysisPhaseInitialised_;
'''
renderer = replace_once(renderer, peaks_old, peaks_new,
                        'Experimental peak analysis removal')

RENDERER.write_text(renderer, encoding='utf-8')

engine = ENGINE.read_text(encoding='utf-8')

state_old = '''    const bool exactAuthority = exactScaleLockAuthority(parameters);
    const bool zeroPrudence = zeroPrudenceAuthority(parameters); // AUTHORITY_CONTROLS_EXPLICIT_V1
    const bool richEvidence = parameters.voiceEvidenceValid;
'''
state_new = '''    const bool exactAuthority = exactScaleLockAuthority(parameters);
    const bool zeroPrudence = zeroPrudenceAuthority(parameters); // AUTHORITY_CONTROLS_EXPLICIT_V1
    // EXPERIMENTAL_PURE_TRANSPORT_V8: ultra-live has no time budget for
    // breath/body/transient classification to own correction authority. Those
    // signals may still be metered upstream, but they cannot release or gate
    // the already acquired one-voice trajectory.
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
            // Silence or temporary loss of periodicity is not permission to
            // drift toward dry/unity. Keep the acquired trajectory; the next
            // real F0 may replace it immediately through the normal controller.
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
print('EXPERIMENTAL_PURE_TRANSPORT_V8_PATCH=PASS')
