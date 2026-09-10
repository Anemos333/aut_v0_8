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


phase_old = r'''            // SHORT_LATTICE_COHERENT_PHASE_V3
            // On 128/256-sample lattices, neighbouring leakage bins belonging
            // to one partial have noisy independent instantaneous-frequency
            // estimates. Let only their phase velocity converge continuously
            // toward the nearest peak's velocity. This is still one phase
            // field and one spectral transport: no residual/noise path, no dry
            // contribution, no detector gate, and no change to targetPosition
            // or safeRatio. Quality (512+) executes the previous equation.
            double transportSourceBin =
                trueSourceBins_[static_cast<std::size_t>(sourceBin)];
            if (frameSize_ <= 256 && !nearestPeak_.empty())
            {
                const int velocityPeak = nearestPeak_[static_cast<std::size_t>(sourceBin)];
                if (velocityPeak >= 0 && velocityPeak <= positiveBins)
                {
                    const float distance = static_cast<float>(
                        std::abs(velocityPeak - sourceBin));
                    const float coherenceCore = frameSize_ <= 128 ? 0.50f : 0.75f;
                    const float coherenceFade = frameSize_ <= 128 ? 2.50f : 2.75f;
                    const float coherence = 1.0f
                        - smoothStep(coherenceCore, coherenceFade, distance);
                    const double peakVelocityBin =
                        trueSourceBins_[static_cast<std::size_t>(velocityPeak)];
                    transportSourceBin += static_cast<double>(coherence)
                        * (peakVelocityBin - transportSourceBin);
                }
            }
'''
phase_new = r'''            // EXPERIMENTAL_INDEPENDENT_PARTIAL_PHASE_V6
            // A 128-sample bin spans 375 Hz at 48 kHz. Neighbouring bins are
            // therefore not safely classifiable as leakage from one partial:
            // forcing them toward nearestPeak velocity can merge distinct vocal
            // harmonics into one reconstructed phase family. Experimental keeps
            // every measured instantaneous-frequency coordinate independent and
            // shifts it directly. Live/256 retains the proven leakage-coherence
            // stabiliser; Quality/512 was already independent and is untouched.
            double transportSourceBin =
                trueSourceBins_[static_cast<std::size_t>(sourceBin)];
            if (frameSize_ == 256 && !nearestPeak_.empty())
            {
                const int velocityPeak = nearestPeak_[static_cast<std::size_t>(sourceBin)];
                if (velocityPeak >= 0 && velocityPeak <= positiveBins)
                {
                    const float distance = static_cast<float>(
                        std::abs(velocityPeak - sourceBin));
                    constexpr float coherenceCore = 0.75f;
                    constexpr float coherenceFade = 2.75f;
                    const float coherence = 1.0f
                        - smoothStep(coherenceCore, coherenceFade, distance);
                    const double peakVelocityBin =
                        trueSourceBins_[static_cast<std::size_t>(velocityPeak)];
                    transportSourceBin += static_cast<double>(coherence)
                        * (peakVelocityBin - transportSourceBin);
                }
            }
'''
replace_once("Source/SingleWetSpectralRenderer.cpp", phase_old, phase_new)

contract_old = r'''    success &= check(has(renderer, "EXPERIMENTAL_TRUE_PARTIAL_TRANSPORT_V5")
                         && has(renderer, "if (frameSize_ <= 128 && peakValid)")
                         && has(renderer, "const double truePeakBin")
                         && has(renderer, "const double peakShiftBins = truePeakBin * safeRatio - truePeakBin")
                         && has(renderer, "targetPosition = static_cast<double>(sourceBin) + peakShiftBins")
                         && has(renderer, "double targetPosition = static_cast<double>(sourceBin) * safeRatio"),
                     "experimental_128_translates_true_partial_regions");
'''
contract_new = r'''    success &= check(has(renderer, "EXPERIMENTAL_TRUE_PARTIAL_TRANSPORT_V5")
                         && has(renderer, "if (frameSize_ <= 128 && peakValid)")
                         && has(renderer, "const double truePeakBin")
                         && has(renderer, "const double peakShiftBins = truePeakBin * safeRatio - truePeakBin")
                         && has(renderer, "targetPosition = static_cast<double>(sourceBin) + peakShiftBins")
                         && has(renderer, "double targetPosition = static_cast<double>(sourceBin) * safeRatio"),
                     "experimental_128_translates_true_partial_regions");

    success &= check(has(renderer, "EXPERIMENTAL_INDEPENDENT_PARTIAL_PHASE_V6")
                         && has(renderer, "if (frameSize_ == 256 && !nearestPeak_.empty())")
                         && !has(renderer, "if (frameSize_ <= 256 && !nearestPeak_.empty())"),
                     "experimental_128_keeps_independent_partial_phase_velocity");
'''
replace_once("Tests/GuiAudioControlContractTest.cpp", contract_old, contract_new)

test_old = r'''        if (expectedHz < 3000.0)
        {
            const double measuredHz = estimateToneFrequency(harmonic128, expectedHz, 24000);
            harmonicPitchExact = harmonicPitchExact
                && std::abs(centsError(measuredHz, expectedHz)) < 0.60;
        }
'''
test_new = r'''        if (expectedHz < 3000.0)
        {
            const double measuredHz = estimateToneFrequency(harmonic128, expectedHz, 24000);
            const double harmonicError = centsError(measuredHz, expectedHz);
            std::cerr << "experimental_128_harmonic_" << harmonic
                      << "_error_cents=" << harmonicError << '\n';
            harmonicPitchExact = harmonicPitchExact
                && std::abs(harmonicError) < 0.60;
        }
'''
replace_once("Tests/SingleWetSpectralRendererTest.cpp", test_old, test_new)

print("EXPERIMENTAL_INDEPENDENT_PARTIAL_PHASE_V6_PATCH=PASS")
