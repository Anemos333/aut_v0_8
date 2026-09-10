import os
from pathlib import Path

RENDERER = Path('Source/SingleWetSpectralRenderer.cpp')
ENGINE = Path('Source/ModernPitchEngine.cpp')
LOCK = float(os.environ.get('NEUMATON_EXPERIMENTAL_PHASE_LOCK', '0.90'))
if not (0.0 <= LOCK <= 1.0):
    raise SystemExit('NEUMATON_EXPERIMENTAL_PHASE_LOCK must be in [0,1]')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one occurrence, got {count}')
    return text.replace(old, new, 1)

renderer = RENDERER.read_text(encoding='utf-8')

# Preserve the proven V7.1 short-frame geometric lobe transport. Remove only
# semantic/timbral reconstruction from Experimental.
formant_old = '    const float safeFormant = clamp01(formantPreservation);\n'
formant_new = '''    // EXPERIMENTAL_MINIMAL_TRANSPORT_V8: at 128 the renderer does not
    // attempt spectral-envelope/formant reconstruction. It transports the
    // measured spectrum only. Live/Quality keep the explicit formant control.
    const float safeFormant = frameSize_ <= 128
        ? 0.0f
        : clamp01(formantPreservation);
'''
renderer = replace_once(renderer, formant_old, formant_new,
                        'Experimental formant removal')

lock_old = '''        const float lockStrength = clamp01(spatialLock * correctionPhaseNeed
            * (frameSize_ <= 128 ? 0.90f : 1.0f));
'''
lock_new = f'''        // EXPERIMENTAL_MINIMAL_TRANSPORT_V8: this is only local lobe
        // geometry. It has no F0/harmonic/breath/transient semantics.
        const float lockStrength = clamp01(spatialLock * correctionPhaseNeed
            * (frameSize_ <= 128 ? {LOCK:.6f}f : 1.0f));
'''
renderer = replace_once(renderer, lock_old, lock_new,
                        'Experimental phase-lock sweep')

# Guard the already-established rule: Experimental cannot use source F0 in its
# renderer. This is intentionally a validation, not a replacement.
if renderer.count('const bool harmonicGuideValid = frameSize_ == 256') != 1:
    raise SystemExit('Experimental F0 authority guard failed')

RENDERER.write_text(renderer, encoding='utf-8')

engine = ENGINE.read_text(encoding='utf-8')
state_old = '''    const bool exactAuthority = exactScaleLockAuthority(parameters);
    const bool zeroPrudence = zeroPrudenceAuthority(parameters); // AUTHORITY_CONTROLS_EXPLICIT_V1
    const bool richEvidence = parameters.voiceEvidenceValid;
'''
state_new = '''    const bool exactAuthority = exactScaleLockAuthority(parameters);
    const bool zeroPrudence = zeroPrudenceAuthority(parameters); // AUTHORITY_CONTROLS_EXPLICIT_V1
    // EXPERIMENTAL_MINIMAL_TRANSPORT_V8: ultra-live cannot give semantic
    // breath/body/transient classifiers authority over correction trajectory.
    const bool experimentalMinimalTransport = latencyMode_ == LatencyMode::ultraLive;
    const bool richEvidence = parameters.voiceEvidenceValid
        && !experimentalMinimalTransport;
'''
engine = replace_once(engine, state_old, state_new,
                      'Experimental classifier authority removal')

release_old = '    if (state.targetValid && (confirmedBreath || confirmedAbsence))\n'
release_new = '''    if (!experimentalMinimalTransport
        && state.targetValid && (confirmedBreath || confirmedAbsence))
'''
engine = replace_once(engine, release_old, release_new,
                      'Experimental classified release removal')

invalid_old = '''        if (state.targetValid && std::abs(state.currentCents) > 0.001)
        {
            setState(TrackingState::release);
            state.desiredCents = 0.0;
            state.responseMs = std::max(5.5,
                responseTimeMs(parameters, true, state.lastTargetJumpCents));
        }
        else
'''
invalid_new = '''        if (experimentalMinimalTransport && state.targetValid)
        {
            // Missing periodicity cannot request dry/unity. The existing
            // trajectory remains active until fresh pitch replaces it.
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
print(f'EXPERIMENTAL_MINIMAL_TRANSPORT_V8_PATCH=PASS lock={LOCK:.3f}')
