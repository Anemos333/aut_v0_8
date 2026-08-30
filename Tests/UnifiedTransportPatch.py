from pathlib import Path

renderer_path = Path("Source/SingleWetSpectralRenderer.cpp")
renderer = renderer_path.read_text(encoding="utf-8")

if "FULL_SPECTRUM_SINGLE_TRANSPORT_V1" not in renderer:
    raise RuntimeError("full-spectrum transport must exist before V3 geometry cleanup")
if "UNIFIED_VOICED_PHASE_GUIDANCE_V2" not in renderer:
    raise RuntimeError("V2 unified phase policy must exist before V3 geometry cleanup")

sentinel = "STABLE_SINGLE_LATTICE_TRANSPORT_V3"
if sentinel in renderer:
    print("V3 stable single-lattice transport already applied")
    raise SystemExit(0)

old = """        // Use the instantaneous-frequency estimate rather than the integer FFT
        // bin centre. This is essential in Live/Experimental, where a short FFT
        // otherwise quantises the reconstructed pitch into very coarse bins.
        const double sourcePosition = std::clamp(
            trueSourceBins_[sourceIndex],
            0.0,
            static_cast<double>(positiveBins));
        const double targetPosition = sourcePosition * safeRatio;
"""
new = """        // STABLE_SINGLE_LATTICE_TRANSPORT_V3
        // Magnitudes keep one stable FFT geometry. trueSourceBins_ is an
        // instantaneous-frequency / phase-velocity estimate and belongs in the
        // synthesis phase integrator above; using it again as magnitude geometry
        // makes neighbouring leakage bins jump independently and fragments the
        // reconstructed voice. Every bin is transported once from the common
        // analysis lattice through the exact same correction ratio.
        const double targetPosition = static_cast<double>(sourceBin) * safeRatio;
"""
if renderer.count(old) != 1:
    raise RuntimeError(f"expected exactly one instantaneous-frequency magnitude mapping, found {renderer.count(old)}")
renderer = renderer.replace(old, new, 1)

if "const double sourcePosition = std::clamp(" in renderer:
    raise RuntimeError("instantaneous-frequency magnitude remapping still present")
if "layer.spectrum[sourceIndex] += fftBuffer_[sourceIndex]" in renderer:
    raise RuntimeError("source-coordinate residual write returned")
if "const float harmonicMagnitude" in renderer:
    raise RuntimeError("harmonic-only transport branch returned")

renderer_path.write_text(renderer, encoding="utf-8")
print("applied stable one-lattice magnitude transport; true frequency remains phase-only")
