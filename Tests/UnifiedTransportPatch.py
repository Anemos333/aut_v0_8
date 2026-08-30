from pathlib import Path
import re


def require_once(text, pattern, replacement, label, flags=0):
    updated, count = re.subn(pattern, replacement, text, count=1, flags=flags)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return updated


renderer_path = Path("Source/SingleWetSpectralRenderer.cpp")
engine_path = Path("Source/ModernPitchEngine.cpp")
supervisor_path = Path("Tests/SupervisorContinuityTest.cpp")

renderer = renderer_path.read_text(encoding="utf-8")
engine = engine_path.read_text(encoding="utf-8")
supervisor = supervisor_path.read_text(encoding="utf-8")

sentinel = "FULL_SPECTRUM_SINGLE_TRANSPORT_V1"
if sentinel in renderer:
    print("unified transport already applied")
    raise SystemExit(0)

renderer = require_once(
    renderer,
    r"    // Wind Fix V6: each latency mode receives its own analysis profile\..*?    const double envelopeUpdateSeconds",
    """    // FULL_SPECTRUM_SINGLE_TRANSPORT_V1\n"
    "    // Quality, Live and Experimental share one reconstruction law. Frame\n"
    "    // size may change latency/resolution, but classification is never a\n"
    "    // licence to route spectral energy through a different reconstruction.\n"
    "    profile_ = AnalysisProfile {};\n\n"
    "    const double envelopeUpdateSeconds""",
    "replace mode-specific reconstruction profiles",
    flags=re.S,
)

renderer = require_once(
    renderer,
    r"    noiseReductionAttackCoefficient_ = frameCoefficient\(\n        frameSize_ <= 128 \? 42\.0f : frameSize_ <= 256 \? 34\.0f : 28\.0f\);\n    noiseReductionReleaseCoefficient_ = frameCoefficient\(\n        frameSize_ <= 128 \? 260\.0f : frameSize_ <= 256 \? 220\.0f : 180\.0f\);",
    """    noiseReductionAttackCoefficient_ = frameCoefficient(28.0f);\n"
    "    noiseReductionReleaseCoefficient_ = frameCoefficient(180.0f);""",
    "unify de-breath time constants",
)

renderer = require_once(
    renderer,
    r"    const int smoothingRadius = frameSize_ <= 128 \? 2 : 1;",
    "    const int smoothingRadius = 1;",
    "unify mask smoothing law",
)

renderer = require_once(
    renderer,
    r"    const bool reliableF0 = f0 >= 42\.0f\n                         && f0 <= static_cast<float>\(sampleRate_ \* 0\.22\)\n                         && context\.confidence >= 0\.20f;",
    """    const bool reliableF0 = context.pitchAnchorFresh\n"
    "                         && f0 >= 42.0f\n"
    "                         && f0 <= static_cast<float>(sampleRate_ * 0.22)\n"
    "                         && context.confidence >= 0.20f;""",
    "require fresh pitch anchor for F0-guided reconstruction",
)

renderer = renderer.replace(
    """        // Low-confidence frames must not redraw the complete mask.  Retain the\n        // previous spectral classification and allow only bounded, mode-aware\n        // movement per frame.  Falling mask values expose more residual, so\n        // they deliberately move more slowly in Live/Experimental.\n""",
    """        // Low-confidence frames must not redraw the complete guidance map.\n        // This map controls reconstruction care/phase locking only. It never\n        // decides which spectral energy is transported to the target pitch.\n""",
    1,
)

synthesis_pattern = re.compile(
    r"        const float harmonicWeight = clamp01\(harmonicMask_\[sourceIndex\]\);\n"
    r"        const float noiseWeight = 1\.0f - harmonicWeight;\n.*?"
    r"        const float outputMagnitude = harmonicMagnitude\n"
    r"                                    \* formantGain\n"
    r"                                    \* energyScale;",
    re.S,
)

synthesis_replacement = """        // The classifier is advisory only. Every spectral bin has exactly one\n        // transported coordinate. Tonal/aperiodic evidence changes how strongly\n        // the phase is locked and how de-breath gain is shaped, never whether a\n        // fraction of the bin remains at its source pitch.\n        const float phaseGuidance = clamp01(harmonicMask_[sourceIndex]);\n        const float aperiodicEvidence = 1.0f - phaseGuidance;\n\n        // Use the instantaneous-frequency estimate rather than the integer FFT\n        // bin centre. This is essential in Live/Experimental, where a short FFT\n        // otherwise quantises the reconstructed pitch into very coarse bins.\n        const double sourcePosition = std::clamp(\n            trueSourceBins_[sourceIndex],\n            0.0,\n            static_cast<double>(positiveBins));\n        const double targetPosition = sourcePosition * safeRatio;\n        if (targetPosition > static_cast<double>(positiveBins) + 1.0)\n            continue;\n\n        const int peak = nearestPeak_[sourceIndex];\n        const double relativeAnalysisPhase = wrapPhase(\n            static_cast<double>(analysisPhases_[sourceIndex])\n            - static_cast<double>(analysisPhases_[static_cast<std::size_t>(peak)]));\n        const double ownTransportPhase = initialiseLayer\n            ? static_cast<double>(analysisPhases_[sourceIndex])\n            : propagatedPhases_[sourceIndex];\n        const double peakLockedPhase = initialiseLayer\n            ? static_cast<double>(analysisPhases_[sourceIndex])\n            : propagatedPhases_[static_cast<std::size_t>(peak)]\n                + relativeAnalysisPhase;\n        const double outputPhase = ownTransportPhase\n            + static_cast<double>(phaseGuidance)\n                * wrapPhase(peakLockedPhase - ownTransportPhase);\n\n        const float sourceEnvelope = std::max(\n            1.0e-8f,\n            spectralEnvelope_[sourceIndex]);\n        const float targetEnvelope = std::max(\n            1.0e-8f,\n            interpolateEnvelope(targetPosition));\n        const float envelopeRatio = std::clamp(\n            targetEnvelope / sourceEnvelope,\n            0.56f,\n            1.78f);\n        const float formantGain = lookupFormantGain(envelopeRatio, safeFormant);\n\n        const float frequencyHz = binFrequency(sourceBin);\n        const float deBreathBandStrength = 0.16f\n            + 0.84f * smoothStep(850.0f, 6200.0f, frequencyHz);\n        const float reconstructionGain = 1.0f\n            - aperiodicEvidence * deBreathBandStrength\n                * (1.0f - smoothedNoiseGain_);\n        const float outputMagnitude = magnitude\n                                    * reconstructionGain\n                                    * formantGain\n                                    * energyScale;"""
renderer, count = synthesis_pattern.subn(synthesis_replacement, renderer, count=1)
if count != 1:
    raise RuntimeError(f"single-coordinate synthesis: expected exactly one match, found {count}")

if "layer.spectrum[sourceIndex] += fftBuffer_[sourceIndex]" in renderer:
    raise RuntimeError("source-coordinate residual write still present")
if "const float harmonicMagnitude" in renderer:
    raise RuntimeError("harmonic-only magnitude branch still present")
if "targetPosition = static_cast<double>(sourceBin) * safeRatio" in renderer:
    raise RuntimeError("integer-bin transport still present")

engine = require_once(
    engine,
    r"        context\.noteBodyLatched = correction\.noteBodyLatched;\n",
    """        context.noteBodyLatched = correction.noteBodyLatched;\n"
    "        context.pitchAnchorFresh = observation.valid\n"
    "            && correction.pitchStaleSamples == 0;\n""",
    "publish pitch freshness to renderer",
)

supervisor = require_once(
    supervisor,
    r"    setBodyEvidence\(parameters\);\n    const auto syncObservation = strongPitch\(\);.*?"
    r"    // A long vibrato around one quantized note is stable musical content, not\n",
    """    // A long vibrato around one quantized note is stable musical content, not\n""",
    "remove obsolete TransportClock invariants",
    flags=re.S,
)

supervisor = require_once(
    supervisor,
    r"    // PARCOR envelope memory should not be replaced by a transient/noisy frame\..*?\n    return success \? 0 : 1;",
    "    return success ? 0 : 1;",
    "remove obsolete LPC invariants",
    flags=re.S,
)

supervisor = require_once(
    supervisor,
    r"    bool leftMusicalBody = false;\n    for \(int hop = 0; hop < 1800; \+\+hop\)",
    """    bool leftMusicalBody = false;\n"
    "    bool vibratoTargetCaptured = false;\n"
    "    bool vibratoChangedTargetIdentity = false;\n"
    "    double vibratoTargetReference = 0.0;\n"
    "    for (int hop = 0; hop < 1800; ++hop)""",
    "add vibrato target identity tracking",
)

supervisor = require_once(
    supervisor,
    r"        if \(hop > 100\n            && \(vibratoState\.trackingState == ModernPitchEngine::TrackingState::unvoiced\n                \|\| vibratoState\.trackingState == ModernPitchEngine::TrackingState::release\)\)\n        \{\n            leftMusicalBody = true;\n        \}\n",
    """        if (hop > 100\n            && (vibratoState.trackingState == ModernPitchEngine::TrackingState::unvoiced\n                || vibratoState.trackingState == ModernPitchEngine::TrackingState::release))\n        {\n            leftMusicalBody = true;\n        }\n        if (hop > 100 && vibratoState.targetValid)\n        {\n            if (!vibratoTargetCaptured)\n            {\n                vibratoTargetReference = vibratoState.targetLog2;\n                vibratoTargetCaptured = true;\n            }\n            else if (std::abs(vibratoState.targetLog2 - vibratoTargetReference) * 1200.0 > 0.5)\n            {\n                vibratoChangedTargetIdentity = true;\n            }\n        }\n""",
    "track vibrato target identity",
)

supervisor = require_once(
    supervisor,
    r"    success &= check\(!leftMusicalBody\n                     && vibratoState\.noteBodyLatched\n                     && vibratoState\.trackingState == ModernPitchEngine::TrackingState::stable,\n                     \"long_vibrato_is_classified_as_stable_note_body\"\);",
    """    success &= check(!leftMusicalBody\n                     && vibratoState.noteBodyLatched\n                     && vibratoState.trackingState == ModernPitchEngine::TrackingState::stable,\n                     \"long_vibrato_is_classified_as_stable_note_body\");\n    success &= check(vibratoTargetCaptured && !vibratoChangedTargetIdentity,\n                     \"vibrato_does_not_become_a_note_identity_change\");""",
    "assert vibrato identity stability",
)

renderer_path.write_text(renderer, encoding="utf-8")
engine_path.write_text(engine, encoding="utf-8")
supervisor_path.write_text(supervisor, encoding="utf-8")
print("applied full-spectrum single transport, pitch freshness and supervisor cleanup")
