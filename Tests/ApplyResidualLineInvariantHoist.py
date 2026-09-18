from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
source = path.read_text()

old = r'''    // RESIDUAL_HARMONIC_CONTRAST_V1
    // A real voiced source produces narrow coherent lines at F0 multiples after
    // inverse filtering. Broadband breath/background produces comparable energy
    // between those lines. The half-harmonic subtraction also suppresses the
    // common 2F0 alias without requiring detector history to own the register.
    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
    {
        if (!std::isfinite(cyclesPerSample)
            || cyclesPerSample <= 0.0 || cyclesPerSample >= 0.48)
        {
            return 0.0f;
        }
        double real = 0.0;
        double imag = 0.0;
        double signalEnergy = 0.0;
        double windowEnergy = 0.0;
        const double denominatorN = static_cast<double>(std::max(1, analysisLength - 1));
        for (int index = 0; index < analysisLength; ++index)
        {
            const double window = 0.5 - 0.5 * std::cos(
                twoPi * static_cast<double>(index) / denominatorN);
            const double sample = static_cast<double>(
                voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
            const double phase = twoPi * cyclesPerSample * static_cast<double>(index);
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
            signalEnergy += sample * sample;
            windowEnergy += window * window;
        }
        const double normaliser = std::max(1.0e-20, signalEnergy * windowEnergy);
        return clamp01(static_cast<float>(std::sqrt(
            2.0 * (real * real + imag * imag) / normaliser)));
    };
'''

new = r'''    // RESIDUAL_HARMONIC_CONTRAST_V1
    // A real voiced source produces narrow coherent lines at F0 multiples after
    // inverse filtering. Broadband breath/background produces comparable energy
    // between those lines. The half-harmonic subtraction also suppresses the
    // common 2F0 alias without requiring detector history to own the register.
    //
    // RESIDUAL_LINE_INVARIANT_HOIST_V1
    // The Hann-windowed residual and its two normalisation energies are invariant
    // across every spectral line inspected during this analyse() call. Compute
    // them once, preserving the golden expression and accumulation order exactly;
    // only the frequency-dependent projections remain inside the line evaluator.
    std::array<double, maxAnalysisSize> residualWindowedFrame {};
    double residualSignalEnergy = 0.0;
    double residualWindowEnergy = 0.0;
    const double residualWindowDenominatorN =
        static_cast<double>(std::max(1, analysisLength - 1));
    for (int index = 0; index < analysisLength; ++index)
    {
        const double window = 0.5 - 0.5 * std::cos(
            twoPi * static_cast<double>(index) / residualWindowDenominatorN);
        const double sample = static_cast<double>(
            voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
        residualWindowedFrame[static_cast<std::size_t>(index)] = sample;
        residualSignalEnergy += sample * sample;
        residualWindowEnergy += window * window;
    }
    const double residualLineNormaliser = std::max(
        1.0e-20, residualSignalEnergy * residualWindowEnergy);

    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
    {
        if (!std::isfinite(cyclesPerSample)
            || cyclesPerSample <= 0.0 || cyclesPerSample >= 0.48)
        {
            return 0.0f;
        }
        double real = 0.0;
        double imag = 0.0;
        for (int index = 0; index < analysisLength; ++index)
        {
            const double sample =
                residualWindowedFrame[static_cast<std::size_t>(index)];
            const double phase = twoPi * cyclesPerSample * static_cast<double>(index);
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
        }
        return clamp01(static_cast<float>(std::sqrt(
            2.0 * (real * real + imag * imag) / residualLineNormaliser)));
    };
'''

if source.count(old) != 1:
    raise SystemExit(f"expected exactly one golden residual-line block, found {source.count(old)}")

patched = source.replace(old, new, 1)

required = [
    "ANALYSIS_WORKSPACE_REENTRANCY_V1",
    "VOICE_BODY_LIVE_PERMISSION_V6_7_1",
    "RESIDUAL_HARMONIC_CONTRAST_V1",
    "RESIDUAL_LINE_INVARIANT_HOIST_V1",
    "real += sample * std::cos(phase);",
    "imag -= sample * std::sin(phase);",
    "const auto residualHarmonicContrast = [&](int tau) noexcept",
]
for marker in required:
    if marker not in patched:
        raise SystemExit(f"missing required golden marker/expression: {marker}")

if "RESIDUAL_LINE_COHERENCE_CPU_V1" in patched:
    raise SystemExit("refusing to build on the previously rejected residual-line optimization")

if patched.count("RESIDUAL_LINE_INVARIANT_HOIST_V1") != 1:
    raise SystemExit("unexpected invariant-hoist marker count")

path.write_text(patched)
print("RESIDUAL_LINE_INVARIANT_HOIST_PATCH=APPLIED")
