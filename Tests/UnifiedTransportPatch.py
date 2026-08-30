from pathlib import Path

renderer_path = Path("Source/SingleWetSpectralRenderer.cpp")
renderer = renderer_path.read_text(encoding="utf-8")

if "STABLE_SINGLE_LATTICE_TRANSPORT_V3" not in renderer:
    raise RuntimeError("V3 stable single-lattice transport must exist before V4 cleanup")

sentinel = "PURE_SINGLE_TRANSPORT_V4"
if sentinel in renderer:
    print("V4 pure single transport already applied")
    raise SystemExit(0)

old_signature = """void SingleWetSpectralRenderer::synthesiseLayer(
    SynthesisLayer& layer,
    std::int64_t frameEndSample,
    double correctionCents,
    float formantPreservation,
    bool resetPhases,
    float phaseAnchor,
    int positiveBins) noexcept
"""
new_signature = """void SingleWetSpectralRenderer::synthesiseLayer(
    SynthesisLayer& layer,
    std::int64_t frameEndSample,
    double correctionCents,
    float formantPreservation,
    bool resetPhases,
    int positiveBins) noexcept
"""
if renderer.count(old_signature) != 1:
    raise RuntimeError(f"synthesiseLayer signature: expected one match, found {renderer.count(old_signature)}")
renderer = renderer.replace(old_signature, new_signature, 1)

phase_anchor_block = """            if (phaseAnchor > 0.0f)
            {
                const double phaseError = wrapPhase(
                    analysisPhase - synthesisPhase);
                synthesisPhase += static_cast<double>(phaseAnchor) * phaseError;
                synthesisPhase -= twoPi * std::nearbyint(synthesisPhase / twoPi);
            }
"""
if renderer.count(phase_anchor_block) != 1:
    raise RuntimeError(f"phase anchor block: expected one match, found {renderer.count(phase_anchor_block)}")
renderer = renderer.replace(phase_anchor_block, "", 1)

old_phase_policy = """        // The classifier is advisory only. Every spectral bin has exactly one
        // transported coordinate. Tonal/aperiodic evidence changes how strongly
        // the phase is locked and how de-breath gain is shaped, never whether a
        // fraction of the bin remains at its source pitch.
        // UNIFIED_VOICED_PHASE_GUIDANCE_V2
        // One voiced/reconstruction confidence controls phase coherence for the
        // complete spectrum. The harmonic map may still describe local noise
        // for de-breath treatment, but it no longer partitions phase behaviour.
        const float phaseGuidance = clamp01(smoothedSpectralReliability_);
        const float aperiodicEvidence = 1.0f
            - clamp01(harmonicMask_[sourceIndex]);

"""
new_phase_policy = """        // PURE_SINGLE_TRANSPORT_V4
        // Detector, voiced, breath, harmonicity and reliability state are
        // observers/supervisors only. They cannot select a second phase law,
        // attenuate spectral pieces or pull reconstruction toward dry analysis.
        // Every bin follows the one propagated transport phase.

"""
if renderer.count(old_phase_policy) != 1:
    raise RuntimeError(f"protective phase policy: expected one match, found {renderer.count(old_phase_policy)}")
renderer = renderer.replace(old_phase_policy, new_phase_policy, 1)

old_phase_mix = """        const int peak = nearestPeak_[sourceIndex];
        const double relativeAnalysisPhase = wrapPhase(
            static_cast<double>(analysisPhases_[sourceIndex])
            - static_cast<double>(analysisPhases_[static_cast<std::size_t>(peak)]));
        const double ownTransportPhase = initialiseLayer
            ? static_cast<double>(analysisPhases_[sourceIndex])
            : propagatedPhases_[sourceIndex];
        const double peakLockedPhase = initialiseLayer
            ? static_cast<double>(analysisPhases_[sourceIndex])
            : propagatedPhases_[static_cast<std::size_t>(peak)]
                + relativeAnalysisPhase;
        const double outputPhase = ownTransportPhase
            + static_cast<double>(phaseGuidance)
                * wrapPhase(peakLockedPhase - ownTransportPhase);

"""
new_phase_mix = """        const double outputPhase = propagatedPhases_[sourceIndex];

"""
if renderer.count(old_phase_mix) != 1:
    raise RuntimeError(f"phase morph: expected one match, found {renderer.count(old_phase_mix)}")
renderer = renderer.replace(old_phase_mix, new_phase_mix, 1)

old_gain = """        const float frequencyHz = binFrequency(sourceBin);
        const float deBreathBandStrength = 0.16f
            + 0.84f * smoothStep(850.0f, 6200.0f, frequencyHz);
        const float reconstructionGain = 1.0f
            - aperiodicEvidence * deBreathBandStrength
                * (1.0f - smoothedNoiseGain_);
        const float outputMagnitude = magnitude
                                    * reconstructionGain
                                    * formantGain
                                    * energyScale;
"""
new_gain = """        // Formant is an explicit user control. No detector/classifier state
        // is allowed to scale the reconstructed spectrum.
        const float outputMagnitude = magnitude
                                    * formantGain
                                    * energyScale;
"""
if renderer.count(old_gain) != 1:
    raise RuntimeError(f"protective reconstruction gain: expected one match, found {renderer.count(old_gain)}")
renderer = renderer.replace(old_gain, new_gain, 1)

old_frame_anchor = """    const float phaseAnchor = resetAnalysis ? 0.0f
        : 0.32f * smoothStep(0.24f, 0.72f, spectralFlux);
"""
if renderer.count(old_frame_anchor) != 1:
    raise RuntimeError(f"frame phase anchor: expected one match, found {renderer.count(old_frame_anchor)}")
renderer = renderer.replace(old_frame_anchor, "", 1)

old_call = """    synthesiseLayer(layer_, frameEndSample, correctionCents, formantPreservation,
                    resetAnalysis, phaseAnchor, positiveBins);
"""
new_call = """    synthesiseLayer(layer_, frameEndSample, correctionCents, formantPreservation,
                    resetAnalysis, positiveBins);
"""
if renderer.count(old_call) != 1:
    raise RuntimeError(f"synthesiseLayer call: expected one match, found {renderer.count(old_call)}")
renderer = renderer.replace(old_call, new_call, 1)

for forbidden in (
    "phaseAnchor",
    "phaseGuidance",
    "peakLockedPhase",
    "reconstructionGain",
    "aperiodicEvidence",
):
    if forbidden in renderer:
        raise RuntimeError(f"protective audio symbol remains: {forbidden}")

required = (
    "PURE_SINGLE_TRANSPORT_V4",
    "const double targetPosition = static_cast<double>(sourceBin) * safeRatio",
    "const double outputPhase = propagatedPhases_[sourceIndex]",
    "const float outputMagnitude = magnitude",
    "* formantGain",
    "* energyScale",
)
for token in required:
    if token not in renderer:
        raise RuntimeError(f"V4 invariant missing: {token}")

renderer_path.write_text(renderer, encoding="utf-8")
print("applied V4: sensors are read-only to the single spectral transport")
