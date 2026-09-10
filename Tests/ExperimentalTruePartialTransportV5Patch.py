from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    if new in text:
        return
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


renderer_old = r'''        // STABLE_SINGLE_LATTICE_TRANSPORT_V3
        // Magnitudes keep one stable FFT geometry. trueSourceBins_ is an
        // instantaneous-frequency / phase-velocity estimate and belongs in the
        // synthesis phase integrator above; using it again as magnitude geometry
        // makes neighbouring leakage bins jump independently and fragments the
        // reconstructed voice. Every bin is transported once from the common
        // analysis lattice through the exact same correction ratio.
        const double targetPosition = static_cast<double>(sourceBin) * safeRatio;
        if (targetPosition > static_cast<double>(positiveBins) + 1.0)
            continue;

        // CONTINUOUS_PHASE_FIELD_V2
        // The first phase-lock pass used a binary decision: bins close to a
        // peak followed the peak, every other bin followed its own propagated
        // phase. That is still one audio path, but the moving binary boundary
        // can behave like two phase populations when correction is gentle or
        // changing. Use one continuous phase equation instead.
        //
        // At large corrections the core region is exactly the V1 identity
        // phase lock that preserves the good rigid timbre. Near zero correction
        // the phase field continuously returns to the bin's own propagation,
        // so soft settings do not manufacture chorus/comb motion. Correction
        // magnitude, targetPosition and the commanded pitch ratio are untouched.
        const int peak = nearestPeak_.empty()
            ? sourceBin
            : nearestPeak_[sourceIndex];
        const bool peakValid = peak >= 0 && peak <= positiveBins;
'''
renderer_new = r'''        // EXPERIMENTAL_TRUE_PARTIAL_TRANSPORT_V5
        // Quality/Live keep the proven common-lattice magnitude transport.
        // At 128 samples, however, one FFT bin is 375 Hz at 48 kHz: scaling the
        // nominal sourceBin geometry while phase follows trueSourceBins_ makes
        // energy and phase describe different partial locations. That mismatch
        // is heard as hollow/comb/wind motion on real vocals.
        //
        // Experimental therefore translates each analysed peak region by the
        // actual instantaneous-frequency displacement of its owning peak. The
        // spectral lobe is preserved and shifted as one object; it is not rebuilt
        // from a nominal integer-bin harmonic grid. This is still one FFT, one
        // spectrum, one IFFT and one wet path. safeRatio is unchanged.
        const int peak = nearestPeak_.empty()
            ? sourceBin
            : nearestPeak_[sourceIndex];
        const bool peakValid = peak >= 0 && peak <= positiveBins;

        double targetPosition = static_cast<double>(sourceBin) * safeRatio;
        if (frameSize_ <= 128 && peakValid)
        {
            const double truePeakBin =
                trueSourceBins_[static_cast<std::size_t>(peak)];
            const double peakShiftBins = truePeakBin * safeRatio - truePeakBin;
            targetPosition = static_cast<double>(sourceBin) + peakShiftBins;
        }
        if (targetPosition < -1.0
            || targetPosition > static_cast<double>(positiveBins) + 1.0)
            continue;

        // CONTINUOUS_PHASE_FIELD_V2
        // The first phase-lock pass used a binary decision: bins close to a
        // peak followed the peak, every other bin followed its own propagated
        // phase. That is still one audio path, but the moving binary boundary
        // can behave like two phase populations when correction is gentle or
        // changing. Use one continuous phase equation instead.
        //
        // At large corrections the core region is exactly the V1 identity
        // phase lock that preserves the good rigid timbre. Near zero correction
        // the phase field continuously returns to the bin's own propagation,
        // so soft settings do not manufacture chorus/comb motion. Correction
        // magnitude, targetPosition and the commanded pitch ratio are untouched.
'''
replace_once("Source/SingleWetSpectralRenderer.cpp", renderer_old, renderer_new)

contract_old = r'''    success &= check(has(renderer, "CONTINUOUS_PHASE_FIELD_V2")
                         && !has(renderer, "const bool usePeakPhase")
                         && has(renderer, "const double phaseDelta = wrapPhase(lockedPhase - ownPhase)")
                         && has(renderer, "spatialLock * correctionPhaseNeed"),
                     "renderer_phase_field_is_continuous_not_binary");
'''
contract_new = r'''    success &= check(has(renderer, "CONTINUOUS_PHASE_FIELD_V2")
                         && !has(renderer, "const bool usePeakPhase")
                         && has(renderer, "const double phaseDelta = wrapPhase(lockedPhase - ownPhase)")
                         && has(renderer, "spatialLock * correctionPhaseNeed"),
                     "renderer_phase_field_is_continuous_not_binary");

    success &= check(has(renderer, "EXPERIMENTAL_TRUE_PARTIAL_TRANSPORT_V5")
                         && has(renderer, "if (frameSize_ <= 128 && peakValid)")
                         && has(renderer, "const double truePeakBin")
                         && has(renderer, "const double peakShiftBins = truePeakBin * safeRatio - truePeakBin")
                         && has(renderer, "targetPosition = static_cast<double>(sourceBin) + peakShiftBins")
                         && has(renderer, "double targetPosition = static_cast<double>(sourceBin) * safeRatio"),
                     "experimental_128_translates_true_partial_regions");
'''
replace_once("Tests/GuiAudioControlContractTest.cpp", contract_old, contract_new)

test_old = r'''std::vector<float> renderInharmonicOctaveShift()
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 512);
'''
test_new = r'''std::vector<float> renderHarmonicStack128(double correctionCents)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 128);

    constexpr double fundamental = 173.70;
    std::vector<float> output(72000);
    for (int sample = 0; sample < static_cast<int>(output.size()); ++sample)
    {
        double input = 0.0;
        for (int harmonic = 1; harmonic <= 8; ++harmonic)
        {
            const double amplitude = 0.12 / static_cast<double>(harmonic);
            input += amplitude * std::sin(
                2.0 * pi * fundamental * static_cast<double>(harmonic)
                * static_cast<double>(sample) / sampleRate
                + 0.17 * static_cast<double>(harmonic));
        }
        output[static_cast<std::size_t>(sample)] = renderer.processSample(
            static_cast<float>(input), correctionCents, 0.0f);
    }
    return output;
}

std::vector<float> renderInharmonicOctaveShift()
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 512);
'''
replace_once("Tests/SingleWetSpectralRendererTest.cpp", test_old, test_new)

main_old = r'''    const auto unity = renderTone(512, 0.0);
'''
main_new = r'''    // EXPERIMENTAL_TRUE_PARTIAL_TRANSPORT_V5: a vocal-like harmonic
    // stack must move as one harmonic family at 128 samples. This complements
    // the single-sine ratio tests by catching a return to nominal-bin spectral
    // reconstruction that can sound hollow even when one isolated sine is exact.
    constexpr double harmonicFundamental = 173.70;
    constexpr double harmonicShiftCents = 137.60;
    const double harmonicRatio = std::exp2(harmonicShiftCents / 1200.0);
    const auto harmonic128 = renderHarmonicStack128(harmonicShiftCents);
    double shiftedHarmonicPower = 0.0;
    double originalHarmonicPower = 0.0;
    bool harmonicPitchExact = true;
    for (int harmonic = 1; harmonic <= 8; ++harmonic)
    {
        const double sourceHz = harmonicFundamental * static_cast<double>(harmonic);
        const double expectedHz = sourceHz * harmonicRatio;
        shiftedHarmonicPower += tonePower(harmonic128, expectedHz, 24000);
        originalHarmonicPower += tonePower(harmonic128, sourceHz, 24000);
        if (expectedHz < 3000.0)
        {
            const double measuredHz = estimateToneFrequency(harmonic128, expectedHz, 24000);
            harmonicPitchExact = harmonicPitchExact
                && std::abs(centsError(measuredHz, expectedHz)) < 0.60;
        }
    }
    const double harmonicFamilyRatio = shiftedHarmonicPower
        / std::max(1.0e-20, originalHarmonicPower);
    std::cerr << "experimental_128_shifted_harmonic_family_ratio="
              << harmonicFamilyRatio << '\n';
    success &= check(harmonicFamilyRatio > 20.0,
                     "experimental_128_shifts_harmonic_family_not_original_grid");
    success &= check(harmonicPitchExact,
                     "experimental_128_harmonic_family_keeps_exact_ratio");

    const auto unity = renderTone(512, 0.0);
'''
replace_once("Tests/SingleWetSpectralRendererTest.cpp", main_old, main_new)

print("EXPERIMENTAL_TRUE_PARTIAL_TRANSPORT_V5_PATCH=PASS")
