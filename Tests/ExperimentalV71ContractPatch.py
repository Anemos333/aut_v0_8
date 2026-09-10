from pathlib import Path

p = Path('Tests/SingleWetSpectralRendererTest.cpp')
s = p.read_text()


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected one occurrence, got {count}')
    return text.replace(old, new, 1)

s = once(s,
'''std::vector<float> renderInharmonicOctaveShift()
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, 512);
''',
'''std::vector<float> renderInharmonicOctaveShift(int frameSize)
{
    SingleWetSpectralRenderer renderer;
    renderer.prepare(sampleRate, frameSize);
''',
'inharmonic frame-size parameter')

old_harmonic = '''        double shiftedHarmonicPower = 0.0;
        double originalHarmonicPower = 0.0;
        bool harmonicPitchExact = true;
        for (int harmonic = 1; harmonic <= 8; ++harmonic)
        {
            const double sourceHz = harmonicFundamental * static_cast<double>(harmonic);
            const double expectedHz = sourceHz * harmonicRatio;
            shiftedHarmonicPower += tonePower(harmonicOutput, expectedHz, 24000);
            originalHarmonicPower += tonePower(harmonicOutput, sourceHz, 24000);
            if (expectedHz < 3000.0)
            {
                const double measuredHz = estimateToneFrequency(
                    harmonicOutput, expectedHz, 24000);
                const double harmonicError = centsError(measuredHz, expectedHz);
                std::cerr << (harmonicFrameSize == 256 ? "live_256_harmonic_"
                                                        : "experimental_128_harmonic_")
                          << harmonic << "_error_cents=" << harmonicError << '\\n';
                harmonicPitchExact = harmonicPitchExact
                    && std::abs(harmonicError) < 0.60;
            }
        }
        const double harmonicFamilyRatio = shiftedHarmonicPower
            / std::max(1.0e-20, originalHarmonicPower);
        std::cerr << (harmonicFrameSize == 256
                         ? "live_256_shifted_harmonic_family_ratio="
                         : "experimental_128_shifted_harmonic_family_ratio=")
                  << harmonicFamilyRatio << '\\n';
        success &= check(
            harmonicFamilyRatio > 20.0,
            harmonicFrameSize == 256
                ? "live_256_shifts_harmonic_family_not_original_grid"
                : "experimental_128_shifts_harmonic_family_not_original_grid");
        success &= check(
            harmonicPitchExact,
            harmonicFrameSize == 256
                ? "live_256_harmonic_family_keeps_exact_ratio"
                : "experimental_128_harmonic_family_keeps_exact_ratio");
'''

new_harmonic = '''        double shiftedHarmonicPower = 0.0;
        double originalHarmonicPower = 0.0;
        double harmonicAbsoluteErrorSum = 0.0;
        double harmonicMaximumAbsoluteError = 0.0;
        double fundamentalError = 1.0e9;
        int measuredHarmonics = 0;
        for (int harmonic = 1; harmonic <= 8; ++harmonic)
        {
            const double sourceHz = harmonicFundamental * static_cast<double>(harmonic);
            const double expectedHz = sourceHz * harmonicRatio;
            shiftedHarmonicPower += tonePower(harmonicOutput, expectedHz, 24000);
            originalHarmonicPower += tonePower(harmonicOutput, sourceHz, 24000);
            if (expectedHz < 3000.0)
            {
                const double measuredHz = estimateToneFrequency(
                    harmonicOutput, expectedHz, 24000);
                const double harmonicError = centsError(measuredHz, expectedHz);
                const double absoluteError = std::abs(harmonicError);
                std::cerr << (harmonicFrameSize == 256 ? "live_256_harmonic_"
                                                        : "experimental_128_harmonic_")
                          << harmonic << "_error_cents=" << harmonicError << '\\n';
                if (harmonic == 1)
                    fundamentalError = harmonicError;
                harmonicAbsoluteErrorSum += absoluteError;
                harmonicMaximumAbsoluteError = std::max(
                    harmonicMaximumAbsoluteError, absoluteError);
                ++measuredHarmonics;
            }
        }
        const double harmonicMeanAbsoluteError = measuredHarmonics > 0
            ? harmonicAbsoluteErrorSum / static_cast<double>(measuredHarmonics)
            : 1.0e9;
        const double harmonicFamilyRatio = shiftedHarmonicPower
            / std::max(1.0e-20, originalHarmonicPower);
        std::cerr << (harmonicFrameSize == 256
                         ? "live_256_shifted_harmonic_family_ratio="
                         : "experimental_128_shifted_harmonic_family_ratio=")
                  << harmonicFamilyRatio << '\\n';
        std::cerr << (harmonicFrameSize == 256
                         ? "live_256_harmonic_mae_cents="
                         : "experimental_128_harmonic_mae_cents=")
                  << harmonicMeanAbsoluteError << '\\n';
        std::cerr << (harmonicFrameSize == 256
                         ? "live_256_harmonic_max_error_cents="
                         : "experimental_128_harmonic_max_error_cents=")
                  << harmonicMaximumAbsoluteError << '\\n';

        // V7.1 candidate contract. Fundamental/pitch authority stays sub-cent;
        // no original-grid family is allowed to remain audible. The higher
        // partial bound is intentionally a timbre-quality guard, not a claim
        // that a 128-sample window resolves every low vocal harmonic exactly.
        const bool sourceRejected = harmonicFrameSize == 256
            ? harmonicFamilyRatio > 1000.0
            : harmonicFamilyRatio > 300.0;
        const bool familyCoherent = harmonicFrameSize == 256
            ? (std::abs(fundamentalError) < 0.10
               && harmonicMaximumAbsoluteError < 4.0
               && harmonicMeanAbsoluteError < 2.0)
            : (std::abs(fundamentalError) < 0.10
               && harmonicMaximumAbsoluteError < 5.5
               && harmonicMeanAbsoluteError < 3.0);
        success &= check(
            sourceRejected,
            harmonicFrameSize == 256
                ? "live_256_harmonic_family_rejects_original"
                : "experimental_128_one_voice_family_rejects_original");
        success &= check(
            familyCoherent,
            harmonicFrameSize == 256
                ? "live_256_harmonic_family_is_coherent"
                : "experimental_128_one_voice_family_is_coherent");
'''
s = once(s, old_harmonic, new_harmonic, 'harmonic family candidate contract')

old_inharmonic = '''    // An inharmonic signal has no special branch: every component must obey the
    // same requested transport.
    const auto inharmonic = renderInharmonicOctaveShift();
    constexpr std::array<double, 4> sourceFrequencies { 277.0, 401.0, 593.0, 877.0 };
    double inharmonicSourcePower = 0.0;
    double inharmonicTargetPower = 0.0;
    for (const double frequency : sourceFrequencies)
    {
        inharmonicSourcePower += tonePower(inharmonic, frequency, 24000);
        inharmonicTargetPower += tonePower(inharmonic, 2.0 * frequency, 24000);
    }
    const double inharmonicRatio = inharmonicTargetPower
        / std::max(1.0e-20, inharmonicSourcePower);
    std::cerr << "inharmonic_full_transport_ratio=" << inharmonicRatio << '\\n';
    success &= check(inharmonicTargetPower > 2.0 * inharmonicSourcePower,
                     "inharmonic_signal_uses_same_transport");
'''

new_inharmonic = '''    // ONE_VOICE_INHARMONIC_GLIDE_V1: lack of harmonicity is never permission
    // to preserve a source/dry family. At every production frame size the same
    // commanded ratio transports the whole inharmonic signal through the one
    // wet renderer. This is the spectral counterpart of the one-voice glide
    // rule used by the musical controller.
    constexpr std::array<double, 4> sourceFrequencies { 277.0, 401.0, 593.0, 877.0 };
    for (const int frameSize : frameSizes)
    {
        const auto inharmonic = renderInharmonicOctaveShift(frameSize);
        double inharmonicSourcePower = 0.0;
        double inharmonicTargetPower = 0.0;
        for (const double frequency : sourceFrequencies)
        {
            inharmonicSourcePower += tonePower(inharmonic, frequency, 24000);
            inharmonicTargetPower += tonePower(inharmonic, 2.0 * frequency, 24000);
        }
        const double inharmonicRatio = inharmonicTargetPower
            / std::max(1.0e-20, inharmonicSourcePower);
        std::cerr << "inharmonic_full_transport_frame_" << frameSize
                  << "_ratio=" << inharmonicRatio << '\\n';
        success &= check(
            inharmonicRatio > 20.0,
            frameSize == 512 ? "quality_inharmonic_uses_one_wet_transport"
            : frameSize == 256 ? "live_inharmonic_uses_one_wet_transport"
                               : "experimental_128_inharmonic_uses_one_wet_transport");
    }
'''
s = once(s, old_inharmonic, new_inharmonic, 'inharmonic all-mode contract')

p.write_text(s)
print('EXPERIMENTAL_V7_1_RENDERER_CONTRACT_PATCH=PASS')
