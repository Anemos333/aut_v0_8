#!/usr/bin/env python3
from pathlib import Path
import re

path = Path('Source/SingleWetSpectralRenderer.cpp')
src = path.read_text(encoding='utf-8')

# 1) Air/breath is not a separate phase class: every measurable local maximum
# can own the same local geometric transport law.
old = '''    const float threshold = maximumMagnitude * 0.018f;\n    for (int bin = 1; bin < positiveBins; ++bin)\n    {\n        const float centre = magnitudes_[static_cast<std::size_t>(bin)];\n        if (centre >= threshold\n            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]\n            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])'''
new = '''    // ONE_VOICE_PEAK_TRANSLATION_V10: no harmonic-vs-air/noise threshold.\n    // Every measurable local maximum is only a geometric anchor; no class of\n    // spectrum is left on a different phase/transport law.\n    for (int bin = 1; bin < positiveBins; ++bin)\n    {\n        const float centre = magnitudes_[static_cast<std::size_t>(bin)];\n        if (centre > 1.0e-12f\n            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]\n            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])'''
if old not in src:
    raise SystemExit('peak threshold anchor not found')
src = src.replace(old, new, 1)

# 2) F0 is upstream musical information only. Live/Experimental use the same
# measured local peak translation for instantaneous phase velocity. Quality
# keeps its proven measured-bin * ratio law.
phase_pattern = re.compile(
    r'''            const double measuredSourceBin =\n                trueSourceBins_\[static_cast<std::size_t>\(sourceBin\)\];\n            const bool harmonicGuideValid =.*?\n            synthesisPhase \+= expectedPhaseScale \* transportTargetBin;''',
    re.S)
phase_replacement = '''            const double measuredSourceBin =\n                trueSourceBins_[static_cast<std::size_t>(sourceBin)];\n\n            // ONE_VOICE_PEAK_TRANSLATION_V10\n            // Short modes do not reconstruct h*F0. Phase and magnitude share\n            // one translation derived from the measured local spectral peak.\n            // Quality retains its proven full-resolution measured-bin scaling.\n            double transportTargetBin = measuredSourceBin * safeRatio;\n            if (frameSize_ <= 256 && !nearestPeak_.empty())\n            {\n                const int transportPeak =\n                    nearestPeak_[static_cast<std::size_t>(sourceBin)];\n                if (transportPeak >= 0 && transportPeak <= positiveBins)\n                {\n                    const double truePeakBin =\n                        trueSourceBins_[static_cast<std::size_t>(transportPeak)];\n                    const double peakShiftBins =\n                        truePeakBin * (safeRatio - 1.0);\n                    transportTargetBin = measuredSourceBin + peakShiftBins;\n                }\n            }\n            synthesisPhase += expectedPhaseScale * transportTargetBin;'''
src, count = phase_pattern.subn(phase_replacement, src, count=1)
if count != 1:
    raise SystemExit(f'phase transport replacement count={count}')

# 3) The same local translation owns magnitude placement in Live/Experimental.
# No source-harmonic/F0 branch remains.
mag_pattern = re.compile(
    r'''        double targetPosition = static_cast<double>\(sourceBin\) \* safeRatio;\n        int sourceHarmonicForMagnitude = 0;\n\n        // EXPERIMENTAL_ONE_VOICE_TRANSLATION_V7_1.*?\n        if \(targetPosition < -1\.0''',
    re.S)
mag_replacement = '''        double targetPosition = static_cast<double>(sourceBin) * safeRatio;\n        if (frameSize_ <= 256 && peakValid)\n        {\n            // ONE_VOICE_PEAK_TRANSLATION_V10: exactly the same displacement\n            // used by phase transport. No h*F0 reconstruction and no separate\n            // treatment for breath/noise/transients.\n            const double truePeakBin =\n                trueSourceBins_[static_cast<std::size_t>(peak)];\n            const double peakShiftBins = truePeakBin * (safeRatio - 1.0);\n            targetPosition = static_cast<double>(sourceBin) + peakShiftBins;\n        }\n        if (targetPosition < -1.0'''
src, count = mag_pattern.subn(mag_replacement, src, count=1)
if count != 1:
    raise SystemExit(f'magnitude transport replacement count={count}')

# Remove the F0-family condition from identity phase locking. The lock is purely
# local geometry; it may never classify a bin as harmonic vs noise.
lock_pattern = re.compile(
    r'''        const float peakDistance = peakValid\n            \? static_cast<float>\(std::abs\(peak - sourceBin\)\)\n            : std::numeric_limits<float>::infinity\(\);\n        bool sameExperimentalHarmonicFamily = true;.*?\n        const float coreRadiusBins =''',
    re.S)
lock_replacement = '''        const float peakDistance = peakValid\n            ? static_cast<float>(std::abs(peak - sourceBin))\n            : std::numeric_limits<float>::infinity();\n        const float coreRadiusBins ='''
src, count = lock_pattern.subn(lock_replacement, src, count=1)
if count != 1:
    raise SystemExit(f'phase-family removal count={count}')

src = src.replace(
    '        const float spatialLock = peakValid && sameExperimentalHarmonicFamily\n            ? 1.0f - smoothStep(coreRadiusBins, fadeRadiusBins, peakDistance)\n            : 0.0f;',
    '        const float spatialLock = peakValid\n            ? 1.0f - smoothStep(coreRadiusBins, fadeRadiusBins, peakDistance)\n            : 0.0f;',
    1)

# F0 must now be unused by the renderer. Keep the API temporarily for minimal
# surface-area change, but make its lack of authority explicit.
needle = '    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});\n\n    // Correction authority comes from the musical trajectory.'
replacement = '    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});\n    (void) sourceFundamentalHz; // upstream target information only\n\n    // Correction authority comes from the musical trajectory.'
if needle not in src:
    raise SystemExit('sourceFundamentalHz insertion anchor not found')
src = src.replace(needle, replacement, 1)

for forbidden in (
    'harmonicGuideValid',
    'sourceHarmonicForMagnitude',
    'sameExperimentalHarmonicFamily',
    'harmonicShiftBins',
    'maximumMagnitude * 0.018f',
):
    if forbidden in src:
        raise SystemExit(f'forbidden residual: {forbidden}')

# Formant code must remain present and untouched by this patch.
for required in (
    'lookupFormantGain',
    'spectralEnvelope_',
    'formantPreservation',
    'const float safeFormant = frameSize_ <= 128',
):
    if required not in src:
        raise SystemExit(f'formant invariant missing: {required}')

path.write_text(src, encoding='utf-8')
print('PEAK_TRANSLATION_UNIFIED_V10_PROBE=PASS')
