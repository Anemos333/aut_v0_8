# Validation trigger: source and contract are already materialized.
from pathlib import Path

renderer_path = Path('Source/SingleWetSpectralRenderer.cpp')
contract_path = Path('Tests/GuiAudioControlContractTest.cpp')

renderer = renderer_path.read_text(encoding='utf-8')
replacement = '''        // CONTINUOUS_PHASE_FIELD_V2\n        // The first phase-lock pass used a binary decision: bins close to a\n        // peak followed the peak, every other bin followed its own propagated\n        // phase. That is still one audio path, but the moving binary boundary\n        // can behave like two phase populations when correction is gentle or\n        // changing. Use one continuous phase equation instead.\n        //\n        // At large corrections the core region is exactly the V1 identity\n        // phase lock that preserves the good rigid timbre. Near zero correction\n        // the phase field continuously returns to the bin's own propagation,\n        // so soft settings do not manufacture chorus/comb motion. Correction\n        // magnitude, targetPosition and the commanded pitch ratio are untouched.\n        const int peak = nearestPeak_.empty()\n            ? sourceBin\n            : nearestPeak_[sourceIndex];\n        const bool peakValid = peak >= 0 && peak <= positiveBins;\n        const float peakDistance = peakValid\n            ? static_cast<float>(std::abs(peak - sourceBin))\n            : std::numeric_limits<float>::infinity();\n        const float coreRadiusBins = frameSize_ >= 512 ? 2.0f : 1.0f;\n        const float fadeRadiusBins = frameSize_ >= 512 ? 3.0f : 2.0f;\n        const float spatialLock = peakValid\n            ? 1.0f - smoothStep(coreRadiusBins, fadeRadiusBins, peakDistance)\n            : 0.0f;\n        const float correctionPhaseNeed = smoothStep(6.0f, 42.0f,\n            static_cast<float>(std::abs(safeCents)));\n        const float lockStrength = clamp01(spatialLock * correctionPhaseNeed);\n\n        const double ownPhase = propagatedPhases_[sourceIndex];\n        const double lockedPhase = peakValid\n            ? propagatedPhases_[static_cast<std::size_t>(peak)]\n                + wrapPhase(static_cast<double>(analysisPhases_[sourceIndex])\n                    - static_cast<double>(analysisPhases_[static_cast<std::size_t>(peak)]))\n            : ownPhase;\n        const double phaseDelta = wrapPhase(lockedPhase - ownPhase);\n        const double outputPhase = ownPhase\n            + static_cast<double>(lockStrength) * phaseDelta;\n\n'''

if 'CONTINUOUS_PHASE_FIELD_V2' not in renderer:
    start_marker = '        // TIMBRE_PHASE_LOCK_V1: local identity phase locking reduces\n'
    end_marker = '        const float sourceEnvelope = std::max(\n'
    start = renderer.find(start_marker)
    end = renderer.find(end_marker, start)
    if start < 0 or end < 0:
        raise SystemExit('renderer phase-lock block not found')
    renderer = renderer[:start] + replacement + renderer[end:]
    renderer_path.write_text(renderer, encoding='utf-8')
elif 'const bool usePeakPhase' in renderer:
    raise SystemExit('mixed V1/V2 renderer state')

contract = contract_path.read_text(encoding='utf-8')
old = '''                         && has(renderer, "TIMBRE_PHASE_LOCK_V1")\n                         && has(renderer,\n                                "const double targetPosition = static_cast<double>(sourceBin) * safeRatio")\n                         && has(renderer,\n                                "synthesisPhase += expectedPhaseScale")\n                         && has(renderer,\n                                "* trueSourceBins_[static_cast<std::size_t>(sourceBin)]")\n                         && has(renderer,\n                                "const double outputPhase = usePeakPhase")\n                         && has(renderer,\n                                "propagatedPhases_[static_cast<std::size_t>(peak)] + relativeAnalysisPhase")\n                         && has(renderer,\n                                ": propagatedPhases_[sourceIndex]")\n                         && has(renderer,\n                                "const float outputMagnitude = magnitude"),\n                     "renderer_uses_one_minimal_audio_transport");\n'''
new = '''                         && has(renderer, "TIMBRE_PHASE_LOCK_V1")\n                         && has(renderer, "CONTINUOUS_PHASE_FIELD_V2")\n                         && has(renderer,\n                                "const double targetPosition = static_cast<double>(sourceBin) * safeRatio")\n                         && has(renderer,\n                                "synthesisPhase += expectedPhaseScale")\n                         && has(renderer,\n                                "* trueSourceBins_[static_cast<std::size_t>(sourceBin)]")\n                         && has(renderer,\n                                "const float correctionPhaseNeed = smoothStep(6.0f, 42.0f")\n                         && has(renderer,\n                                "const double outputPhase = ownPhase")\n                         && has(renderer,\n                                "+ static_cast<double>(lockStrength) * phaseDelta")\n                         && !has(renderer, "const bool usePeakPhase")\n                         && has(renderer,\n                                "const float outputMagnitude = magnitude"),\n                     "renderer_uses_one_minimal_audio_transport");\n\n    success &= check(has(renderer, "CONTINUOUS_PHASE_FIELD_V2")\n                         && !has(renderer, "const bool usePeakPhase")\n                         && has(renderer, "const double phaseDelta = wrapPhase(lockedPhase - ownPhase)")\n                         && has(renderer, "spatialLock * correctionPhaseNeed"),\n                     "renderer_phase_field_is_continuous_not_binary");\n'''
if 'renderer_phase_field_is_continuous_not_binary' not in contract:
    if old not in contract:
        raise SystemExit('contract phase-lock block not found')
    contract = contract.replace(old, new, 1)
    contract_path.write_text(contract, encoding='utf-8')
elif 'const double outputPhase = usePeakPhase' in contract:
    raise SystemExit('mixed V1/V2 contract state')
