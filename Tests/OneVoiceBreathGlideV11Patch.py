#!/usr/bin/env python3
from pathlib import Path
import re

ENGINE = Path('Source/ModernPitchEngine.cpp')
RENDERER = Path('Source/SingleWetSpectralRenderer.cpp')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise SystemExit(f'{label}: expected 1 occurrence, got {n}')
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, repl: str, label: str) -> str:
    out, n = re.subn(pattern, repl, text, count=1, flags=re.S)
    if n != 1:
        raise SystemExit(f'{label}: expected 1 match, got {n}')
    return out


# -----------------------------------------------------------------------------
# Controller: breath/absence/F0 holes never request unity. They may only keep
# the already-owned destination alive while acquisition continues.
# -----------------------------------------------------------------------------
cpp = ENGINE.read_text(encoding='utf-8')

cpp = regex_once(
    cpp,
    r'''\n    // Breath/absence is positive evidence.*?\n    if \(!experimentalMinimalTransport\n        && state\.targetValid && \(confirmedBreath \|\| confirmedAbsence\)\)\n    \{.*?\n        return;\n    \}\n''',
    '''\n    // ONE_VOICE_BREATH_GLIDE_V11\n    // Breath/absence is part of the same voice. Evidence may describe the\n    // frame, but it cannot request release, unity or a weaker destination.\n    // If pitch is still valid we continue below; if pitch is missing, the\n    // existing musical destination is preserved by the acquisition branch.\n''',
    'remove breath/absence release-to-unity branch')

cpp = regex_once(
    cpp,
    r'''\n        if \(experimentalMinimalTransport && state\.targetValid\)\n        \{.*?\n            return;\n        \}\n\n        if \(state\.targetValid && std::abs\(state\.currentCents\) > 0\.001\)\n        \{\n            setState\(TrackingState::release\);\n            state\.desiredCents = 0\.0;\n            state\.responseMs = std::max\(5\.5,\n                responseTimeMs\(parameters, true, state\.lastTargetJumpCents\)\);\n        \}\n        else\n        \{\n            setState\(TrackingState::unvoiced\);\n        \}\n        return;\n''',
    '''\n        if (state.targetValid)\n        {\n            // ONE_VOICE_BREATH_GLIDE_V11: no periodicity is not permission to\n            // return toward dry/unity. Hold the exact acquired destination and\n            // keep searching; the next real target change will be a glide.\n            setState(TrackingState::acquire);\n            return;\n        }\n\n        setState(TrackingState::unvoiced);\n        return;\n''',
    'replace missing-F0 release with hold/acquire')

cpp = regex_once(
    cpp,
    r'''\n    // A strong breath/absence before any body latch.*?\n    if \(!state\.noteBodyLatched && richEvidence\n        && \(confirmedBreathFrame \|\| confirmedAbsenceFrame\)\)\n    \{\n        setState\(TrackingState::unvoiced\);\n        state\.desiredCents = 0\.0;\n        return;\n    \}\n''',
    '''\n    // ONE_VOICE_BREATH_GLIDE_V11: a breath/noise label cannot veto a valid F0.\n    // The waveform is one voice; valid pitch proceeds through the same target\n    // and correction law as every other frame.\n''',
    'remove pre-latch breath veto')

cpp = replace_once(
    cpp,
    '''double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept\n{\n    if (!state.targetValid)\n        return 0.0;\n''',
    '''double ModernPitchEngine::advanceCorrection(CorrectionState& state) noexcept\n{\n    if (!state.targetValid)\n        return state.currentCents; // ONE_VOICE_BREATH_GLIDE_V11: never force unity.\n''',
    'preserve current correction without target')

cpp = regex_once(
    cpp,
    r'''    const auto settleTrajectory = \[&state\]\(\) noexcept\n    \{\n        if \(state\.trackingState == TrackingState::release\n            && std::abs\(state\.currentCents\) < 0\.001\)\n        \{.*?\n        \}\n        else if \(\(state\.trackingState == TrackingState::attack\n                  \|\| state\.trackingState == TrackingState::transition\)\n                 && state\.noteBodyLatched\)''',
    '''    const auto settleTrajectory = [&state]() noexcept\n    {\n        if ((state.trackingState == TrackingState::attack\n             || state.trackingState == TrackingState::transition)\n            && state.noteBodyLatched)''',
    'remove release-to-unvoiced settle path')

if 'state.desiredCents = 0.0' in cpp:
    raise SystemExit('forbidden residual: state.desiredCents = 0.0')
if 'setState(TrackingState::release)' in cpp:
    raise SystemExit('forbidden residual: setState(TrackingState::release)')

ENGINE.write_text(cpp, encoding='utf-8')

# -----------------------------------------------------------------------------
# Renderer: no semantic split between voice and air/noise, and no F0-driven
# harmonic reconstruction. Formant/envelope code is intentionally untouched.
# -----------------------------------------------------------------------------
r = RENDERER.read_text(encoding='utf-8')

old_peak = '''    // TIMBRE_PHASE_LOCK_V1: only genuine local spectral peaks may become phase\n    // anchors. The threshold deliberately ignores diffuse low-level air/noise;\n    // those bins retain their own propagated phase below.\n    const float threshold = maximumMagnitude * 0.018f;\n    for (int bin = 1; bin < positiveBins; ++bin)\n    {\n        const float centre = magnitudes_[static_cast<std::size_t>(bin)];\n        if (centre >= threshold\n            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]\n            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])'''
new_peak = '''    // ONE_VOICE_BREATH_GLIDE_V11: breath/air/noise is not another signal.\n    // Every measurable local maximum is only a geometric anchor for the same\n    // spectral transport law; no energy class is excluded as diffuse noise.\n    for (int bin = 1; bin < positiveBins; ++bin)\n    {\n        const float centre = magnitudes_[static_cast<std::size_t>(bin)];\n        if (centre > 1.0e-12f\n            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]\n            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])'''
r = replace_once(r, old_peak, new_peak, 'remove air/noise peak threshold')

r = replace_once(
    r,
    '''    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});\n\n    // Correction authority comes from the musical trajectory.''',
    '''    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});\n\n    // ONE_VOICE_BREATH_GLIDE_V11: F0 chooses the musical destination upstream.\n    // It has no authority to rebuild a second harmonic interpretation here.\n    (void) sourceFundamentalHz;\n\n    // Correction authority comes from the musical trajectory.''',
    'mark renderer F0 as non-authoritative')

r = regex_once(
    r,
    r'''            // LIVE_EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7.*?\n            synthesisPhase \+= expectedPhaseScale \* transportTargetBin;''',
    '''            // ONE_VOICE_BREATH_GLIDE_V11\n            // One measured local region, one translation. The nearest peak is\n            // geometry only: it provides the region displacement measured from\n            // the waveform. F0/harmonic number, breath, transient and confidence\n            // never choose another phase trajectory.\n            const double measuredSourceBin =\n                trueSourceBins_[static_cast<std::size_t>(sourceBin)];\n            double transportTargetBin = measuredSourceBin * safeRatio;\n            if (frameSize_ <= 256 && !nearestPeak_.empty())\n            {\n                const int velocityPeak =\n                    nearestPeak_[static_cast<std::size_t>(sourceBin)];\n                if (velocityPeak >= 0 && velocityPeak <= positiveBins)\n                {\n                    const float distance = static_cast<float>(\n                        std::abs(velocityPeak - sourceBin));\n                    const float coherenceCore = frameSize_ <= 128 ? 0.50f : 0.75f;\n                    const float coherenceFade = frameSize_ <= 128 ? 2.50f : 2.75f;\n                    const float baseCoherence = 1.0f\n                        - smoothStep(coherenceCore, coherenceFade, distance);\n                    const float coherence = frameSize_ <= 128\n                        ? clamp01(baseCoherence * 0.850000f)\n                        : baseCoherence;\n                    const double peakMeasuredBin =\n                        trueSourceBins_[static_cast<std::size_t>(velocityPeak)];\n                    const double coherentSourceBin = measuredSourceBin\n                        + static_cast<double>(coherence)\n                        * (peakMeasuredBin - measuredSourceBin);\n                    const double regionShiftBins =\n                        peakMeasuredBin * (safeRatio - 1.0);\n                    transportTargetBin = coherentSourceBin + regionShiftBins;\n                }\n            }\n            synthesisPhase += expectedPhaseScale * transportTargetBin;''',
    'replace F0 harmonic phase transport with measured-region transport')

r = regex_once(
    r,
    r'''        double targetPosition = static_cast<double>\(sourceBin\) \* safeRatio;\n        int sourceHarmonicForMagnitude = 0;.*?\n        if \(targetPosition < -1\.0''',
    '''        double targetPosition = static_cast<double>(sourceBin) * safeRatio;\n        if (frameSize_ <= 256 && peakValid)\n        {\n            // ONE_VOICE_BREATH_GLIDE_V11: move the measured local lobe by the\n            // displacement of its measured peak. This is translation, not an\n            // h*F0 reconstruction, and applies equally to breath/air regions.\n            const double truePeakBin =\n                trueSourceBins_[static_cast<std::size_t>(peak)];\n            const double regionShiftBins =\n                truePeakBin * (safeRatio - 1.0);\n            targetPosition = static_cast<double>(sourceBin) + regionShiftBins;\n        }\n        if (targetPosition < -1.0''',
    'replace F0 harmonic magnitude transport with measured-region translation')

r = regex_once(
    r,
    r'''        bool sameExperimentalHarmonicFamily = true;.*?\n        const float spatialLock = peakValid && sameExperimentalHarmonicFamily\n            \? 1\.0f - smoothStep\(coreRadiusBins, fadeRadiusBins, peakDistance\)\n            : 0\.0f;''',
    '''        const float spatialLock = peakValid\n            ? 1.0f - smoothStep(coreRadiusBins, fadeRadiusBins, peakDistance)\n            : 0.0f;''',
    'remove harmonic-family phase ownership')

for forbidden in (
    'harmonicGuideValid',
    'sourceHarmonicForMagnitude',
    'sameExperimentalHarmonicFamily',
    'harmonicShiftBins',
    'maximumMagnitude * 0.018f',
):
    if forbidden in r:
        raise SystemExit(f'forbidden renderer residual: {forbidden}')

# Explicitly guard the formant subsystem: this patch must not remove it.
for required in (
    'calculateEnvelope(positiveBins);',
    'lookupFormantGain(envelopeRatio, safeFormant)',
    'const float formantGain',
    'spectralEnvelope_',
):
    if required not in r:
        raise SystemExit(f'formant guard missing: {required}')

RENDERER.write_text(r, encoding='utf-8')
print('ONE_VOICE_BREATH_GLIDE_V11_PATCH=PASS')
