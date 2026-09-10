import os
from pathlib import Path

RENDERER = Path('Source/SingleWetSpectralRenderer.cpp')
ENGINE = Path('Source/ModernPitchEngine.cpp')
LOCK = float(os.environ.get('NEUMATON_EXPERIMENTAL_GEOMETRIC_LOCK', '0.90'))
if not (0.0 <= LOCK <= 1.0):
    raise SystemExit('NEUMATON_EXPERIMENTAL_GEOMETRIC_LOCK must be in [0,1]')


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
            // Experimental never infers a harmonic number or semantic voice
            // class. A local peak is only geometric support for the short FFT
            // lobe. The whole lobe receives one additive displacement derived
            // from the exact correction ratio; the bin keeps its measured local
            // instantaneous-frequency offset around that peak.
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
                const double transportTargetBin = measuredSourceBin + shiftBins;
                synthesisPhase += expectedPhaseScale * transportTargetBin;
                synthesisPhase -= twoPi * std::nearbyint(synthesisPhase / twoPi);
                propagatedPhases_[static_cast<std::size_t>(sourceBin)] = synthesisPhase;
                continue;
            }

            // LIVE_EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7
'''
renderer = replace_once(renderer, phase_anchor, phase_replacement,
                        'Experimental geometric phase transport')

# Keep the existing V7.1 128-sample magnitude lobe translation: unlike the
# rejected per-bin experiments it actually moves the complete short-frame lobe.
# Remove only semantic/timbral reconstruction around that geometric transport.
formant_old = '''    const float safeFormant = clamp01(formantPreservation);
'''
formant_new = '''    // EXPERIMENTAL_PURE_TRANSPORT_V8: formant preservation is spectral
    // reconstruction. Experimental is deliberately a pure geometric shifter;
    // formant reconstruction remains a Live/Quality facility only.
    const float safeFormant = frameSize_ <= 128
        ? 0.0f
        : clamp01(formantPreservation);
'''
renderer = replace_once(renderer, formant_old, formant_new,
                        'Experimental formant reconstruction removal')

lock_old = '''        const float lockStrength = clamp01(spatialLock * correctionPhaseNeed
            * (frameSize_ <= 128 ? 0.90f : 1.0f));
'''
lock_value = f'{LOCK:.6f}f'
lock_new = f'''        // EXPERIMENTAL_PURE_TRANSPORT_V8: this is geometric lobe phase
        // coherence only. It has no F0, harmonic, breath or transient authority.
        const float lockStrength = clamp01(spatialLock * correctionPhaseNeed
            * (frameSize_ <= 128 ? {lock_value} : 1.0f));
'''
renderer = replace_once(renderer, lock_old, lock_new,
                        'Experimental geometric phase coherence')

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
print(f'EXPERIMENTAL_PURE_TRANSPORT_V8_PATCH=PASS lock={LOCK:.3f}')
