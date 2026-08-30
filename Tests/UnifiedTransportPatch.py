from pathlib import Path

renderer_path = Path("Source/SingleWetSpectralRenderer.cpp")
renderer = renderer_path.read_text(encoding="utf-8")

if "FULL_SPECTRUM_SINGLE_TRANSPORT_V1" not in renderer:
    raise RuntimeError("V1 full-spectrum transport must exist before V2 phase unification")

sentinel = "UNIFIED_VOICED_PHASE_GUIDANCE_V2"
if sentinel in renderer:
    print("V2 voiced phase guidance already applied")
    raise SystemExit(0)

old = """        const float phaseGuidance = clamp01(harmonicMask_[sourceIndex]);
        const float aperiodicEvidence = 1.0f - phaseGuidance;
"""
new = """        // UNIFIED_VOICED_PHASE_GUIDANCE_V2
        // One voiced/reconstruction confidence controls phase coherence for the
        // complete spectrum. The harmonic map may still describe local noise
        // for de-breath treatment, but it no longer partitions phase behaviour.
        const float phaseGuidance = clamp01(smoothedSpectralReliability_);
        const float aperiodicEvidence = 1.0f
            - clamp01(harmonicMask_[sourceIndex]);
"""
if renderer.count(old) != 1:
    raise RuntimeError(f"expected exactly one per-bin phase guidance block, found {renderer.count(old)}")
renderer = renderer.replace(old, new, 1)

if "phaseGuidance = clamp01(harmonicMask_[sourceIndex])" in renderer:
    raise RuntimeError("per-bin phase policy still present")
if "layer.spectrum[sourceIndex] += fftBuffer_[sourceIndex]" in renderer:
    raise RuntimeError("source-coordinate residual write returned")

renderer_path.write_text(renderer, encoding="utf-8")
print("applied one global voiced phase policy across the transported spectrum")
