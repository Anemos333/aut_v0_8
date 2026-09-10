from pathlib import Path
import runpy


# Materialize the base V7 first; this extension broadens only its geometry
# contract to Live/256 and keeps Quality/512 outside the V7 condition.
runpy.run_path("Tests/ExperimentalHarmonicCoordinateTransportV7Patch.py", run_name="__main__")


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if new in text:
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


renderer = "Source/SingleWetSpectralRenderer.cpp"

replace_once(
    renderer,
    "// EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7",
    "// LIVE_EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7")

# V7 geometry guide is valid in the two low-latency lattices only.
replace_once(
    renderer,
    "const bool harmonicGuideValid = frameSize_ <= 128\n",
    "const bool harmonicGuideValid = frameSize_ <= 256\n")

replace_once(
    renderer,
    "        if (frameSize_ <= 128\n            && std::isfinite(sourceFundamentalHz)\n",
    "        if (frameSize_ <= 256\n            && std::isfinite(sourceFundamentalHz)\n")

replace_once(
    renderer,
    "        if (frameSize_ <= 128\n            && sourceHarmonicForMagnitude > 0\n",
    "        if (frameSize_ <= 256\n            && sourceHarmonicForMagnitude > 0\n")

# Inside a detected harmonic family, retain the proven short-lattice phase
# coherence. Crucially, the coherence anchor is the measured peak velocity,
# while the pitch displacement is h*F0*(ratio-1): this does not reconstruct
# the partial onto h*F0 and cannot alter correction authority.
old_phase_branch = '''            if (harmonicGuideValid && sourceHarmonic > 0)\n            {\n                // measuredSourceBin is intentionally retained: only the required\n                // translation is harmonic-referenced, so the observed partial is\n                // shifted rather than snapped/reconstructed onto the harmonic grid.\n                transportTargetBin = measuredSourceBin + harmonicShiftBins;\n            }\n'''
new_phase_branch = '''            if (harmonicGuideValid && sourceHarmonic > 0)\n            {\n                // measuredSourceBin is intentionally retained: only the required\n                // translation is harmonic-referenced, so the observed partial is\n                // shifted rather than snapped/reconstructed onto the harmonic grid.\n                double coherentMeasuredSourceBin = measuredSourceBin;\n                if (!nearestPeak_.empty())\n                {\n                    const int velocityPeak =\n                        nearestPeak_[static_cast<std::size_t>(sourceBin)];\n                    if (velocityPeak >= 0 && velocityPeak <= positiveBins)\n                    {\n                        const double peakMeasuredBin =\n                            trueSourceBins_[static_cast<std::size_t>(velocityPeak)];\n                        const int peakHarmonic = std::max(1, static_cast<int>(std::lround(\n                            std::max(fundamentalBin, peakMeasuredBin) / fundamentalBin)));\n                        if (peakHarmonic == sourceHarmonic)\n                        {\n                            const float distance = static_cast<float>(\n                                std::abs(velocityPeak - sourceBin));\n                            const float coherenceCore = frameSize_ <= 128 ? 0.50f : 0.75f;\n                            const float coherenceFade = frameSize_ <= 128 ? 2.50f : 2.75f;\n                            const float coherence = 1.0f\n                                - smoothStep(coherenceCore, coherenceFade, distance);\n                            coherentMeasuredSourceBin += static_cast<double>(coherence)\n                                * (peakMeasuredBin - coherentMeasuredSourceBin);\n                        }\n                    }\n                }\n                transportTargetBin = coherentMeasuredSourceBin + harmonicShiftBins;\n            }\n'''
replace_once(renderer, old_phase_branch, new_phase_branch)

# Generalise the deterministic harmonic-stack helper to both low-latency modes.
test = "Tests/SingleWetSpectralRendererTest.cpp"
replace_once(
    test,
    '''std::vector<float> renderHarmonicStack128(double correctionCents)\n{\n    SingleWetSpectralRenderer renderer;\n    renderer.prepare(sampleRate, 128);\n''',
    '''std::vector<float> renderHarmonicStack(int frameSize, double correctionCents)\n{\n    SingleWetSpectralRenderer renderer;\n    renderer.prepare(sampleRate, frameSize);\n''')

old_test_block = '''    const auto harmonic128 = renderHarmonicStack128(harmonicShiftCents);\n    double shiftedHarmonicPower = 0.0;\n    double originalHarmonicPower = 0.0;\n    bool harmonicPitchExact = true;\n    for (int harmonic = 1; harmonic <= 8; ++harmonic)\n    {\n        const double sourceHz = harmonicFundamental * static_cast<double>(harmonic);\n        const double expectedHz = sourceHz * harmonicRatio;\n        shiftedHarmonicPower += tonePower(harmonic128, expectedHz, 24000);\n        originalHarmonicPower += tonePower(harmonic128, sourceHz, 24000);\n        if (expectedHz < 3000.0)\n        {\n            const double measuredHz = estimateToneFrequency(harmonic128, expectedHz, 24000);\n            const double harmonicError = centsError(measuredHz, expectedHz);\n            std::cerr << "experimental_128_harmonic_" << harmonic\n                      << "_error_cents=" << harmonicError << '\\n';\n            harmonicPitchExact = harmonicPitchExact\n                && std::abs(harmonicError) < 0.60;\n        }\n    }\n    const double harmonicFamilyRatio = shiftedHarmonicPower\n        / std::max(1.0e-20, originalHarmonicPower);\n    std::cerr << "experimental_128_shifted_harmonic_family_ratio="\n              << harmonicFamilyRatio << '\\n';\n    success &= check(harmonicFamilyRatio > 20.0,\n                     "experimental_128_shifts_harmonic_family_not_original_grid");\n    success &= check(harmonicPitchExact,\n                     "experimental_128_harmonic_family_keeps_exact_ratio");\n'''
new_test_block = '''    for (const int harmonicFrameSize : std::array<int, 2> { 256, 128 })\n    {\n        const auto harmonicOutput = renderHarmonicStack(\n            harmonicFrameSize, harmonicShiftCents);\n        double shiftedHarmonicPower = 0.0;\n        double originalHarmonicPower = 0.0;\n        bool harmonicPitchExact = true;\n        for (int harmonic = 1; harmonic <= 8; ++harmonic)\n        {\n            const double sourceHz = harmonicFundamental * static_cast<double>(harmonic);\n            const double expectedHz = sourceHz * harmonicRatio;\n            shiftedHarmonicPower += tonePower(harmonicOutput, expectedHz, 24000);\n            originalHarmonicPower += tonePower(harmonicOutput, sourceHz, 24000);\n            if (expectedHz < 3000.0)\n            {\n                const double measuredHz = estimateToneFrequency(\n                    harmonicOutput, expectedHz, 24000);\n                const double harmonicError = centsError(measuredHz, expectedHz);\n                std::cerr << (harmonicFrameSize == 256 ? "live_256_harmonic_"\n                                                        : "experimental_128_harmonic_")\n                          << harmonic << "_error_cents=" << harmonicError << '\\n';\n                harmonicPitchExact = harmonicPitchExact\n                    && std::abs(harmonicError) < 0.60;\n            }\n        }\n        const double harmonicFamilyRatio = shiftedHarmonicPower\n            / std::max(1.0e-20, originalHarmonicPower);\n        std::cerr << (harmonicFrameSize == 256\n                         ? "live_256_shifted_harmonic_family_ratio="\n                         : "experimental_128_shifted_harmonic_family_ratio=")\n                  << harmonicFamilyRatio << '\\n';\n        success &= check(\n            harmonicFamilyRatio > 20.0,\n            harmonicFrameSize == 256\n                ? "live_256_shifts_harmonic_family_not_original_grid"\n                : "experimental_128_shifts_harmonic_family_not_original_grid");\n        success &= check(\n            harmonicPitchExact,\n            harmonicFrameSize == 256\n                ? "live_256_harmonic_family_keeps_exact_ratio"\n                : "experimental_128_harmonic_family_keeps_exact_ratio");\n    }\n'''
replace_once(test, old_test_block, new_test_block)

# Update static contract wording: both low-latency modes use V7, Quality does not.
contract = "Tests/GuiAudioControlContractTest.cpp"
replace_once(
    contract,
    '''    success &= check(has(renderer, "EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7")\n                         && has(renderer, "transportTargetBin = measuredSourceBin + harmonicShiftBins")\n                         && has(renderer, "harmonicSourceBin * (safeRatio - 1.0)")\n                         && has(rendererHeader, "double sourceFundamentalHz = 0.0")\n                         && !has(rendererHeader, "confidence")\n                         && !has(rendererHeader, "voicing"),\n                     "experimental_128_uses_geometry_only_harmonic_coordinate_transport");\n''',
    '''    success &= check(has(renderer, "LIVE_EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7")\n                         && has(renderer, "const bool harmonicGuideValid = frameSize_ <= 256")\n                         && has(renderer, "transportTargetBin = coherentMeasuredSourceBin + harmonicShiftBins")\n                         && has(renderer, "harmonicSourceBin * (safeRatio - 1.0)")\n                         && has(rendererHeader, "double sourceFundamentalHz = 0.0")\n                         && !has(rendererHeader, "confidence")\n                         && !has(rendererHeader, "voicing"),\n                     "live_and_experimental_use_geometry_only_harmonic_coordinate_transport");\n''')

print("LIVE_EXPERIMENTAL_HARMONIC_COORDINATE_TRANSPORT_V7_PATCH=PASS")
