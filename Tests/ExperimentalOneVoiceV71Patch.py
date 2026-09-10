from pathlib import Path

p = Path('Source/SingleWetSpectralRenderer.cpp')
s = p.read_text()


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one occurrence, got {count}')
    return text.replace(old, new, 1)

# LIVE keeps the V7 F0-referenced harmonic-coordinate guide. Experimental/128
# deliberately does not: at 48 kHz its 375 Hz lattice cannot assign one unique
# harmonic identity to every spectral coefficient. The F0 still owns correction
# upstream; the renderer only translates the measured one-voice spectrum.
s = replace_once(
    s,
    'const bool harmonicGuideValid = frameSize_ <= 256',
    'const bool harmonicGuideValid = frameSize_ == 256',
    'Live-only harmonic phase guide')

old_mag = '''        double targetPosition = static_cast<double>(sourceBin) * safeRatio;
        int sourceHarmonicForMagnitude = 0;
        if (frameSize_ <= 256
            && std::isfinite(sourceFundamentalHz)
            && sourceFundamentalHz >= 25.0
            && sourceFundamentalHz <= 3000.0
            && sourceBin > 0)
        {
            const double fundamentalBin = sourceFundamentalHz
                * static_cast<double>(frameSize_) / sampleRate_;
            if (fundamentalBin > 1.0e-6)
            {
                const double measuredSourceBin = trueSourceBins_[sourceIndex];
                sourceHarmonicForMagnitude = std::max(1, static_cast<int>(std::lround(
                    std::max(fundamentalBin, measuredSourceBin) / fundamentalBin)));
                const double harmonicSourceBin =
                    static_cast<double>(sourceHarmonicForMagnitude) * fundamentalBin;
                const double harmonicShiftBins =
                    harmonicSourceBin * (safeRatio - 1.0);
                targetPosition = static_cast<double>(sourceBin) + harmonicShiftBins;
            }
        }
        else if (frameSize_ <= 128 && peakValid)
        {
            const double truePeakBin =
                trueSourceBins_[static_cast<std::size_t>(peak)];
            const double peakShiftBins = truePeakBin * safeRatio - truePeakBin;
            targetPosition = static_cast<double>(sourceBin) + peakShiftBins;
        }
'''

new_mag = '''        double targetPosition = static_cast<double>(sourceBin) * safeRatio;
        int sourceHarmonicForMagnitude = 0;

        // EXPERIMENTAL_ONE_VOICE_TRANSLATION_V7_1
        // A 128-sample frame at 48 kHz is a 375 Hz lattice. Low vocal partials
        // therefore overlap inside a nominal FFT-bin width, so snapping each bin
        // to h*F0 is not a measurement and was the source of the hollow/metallic
        // V7 regression. Experimental instead preserves each analysed peak lobe
        // and translates it by the measured instantaneous peak displacement.
        // This is still one spectrum, one IFFT, one wet path and the exact same
        // safeRatio. Nothing is copied from dry/source audio. Live/256 retains
        // the V7 harmonic-coordinate law, where the lattice supports it.
        if (frameSize_ <= 128 && peakValid)
        {
            const double truePeakBin =
                trueSourceBins_[static_cast<std::size_t>(peak)];
            const double peakShiftBins = truePeakBin * safeRatio - truePeakBin;
            targetPosition = static_cast<double>(sourceBin) + peakShiftBins;
        }
        else if (frameSize_ <= 256
            && std::isfinite(sourceFundamentalHz)
            && sourceFundamentalHz >= 25.0
            && sourceFundamentalHz <= 3000.0
            && sourceBin > 0)
        {
            const double fundamentalBin = sourceFundamentalHz
                * static_cast<double>(frameSize_) / sampleRate_;
            if (fundamentalBin > 1.0e-6)
            {
                const double measuredSourceBin = trueSourceBins_[sourceIndex];
                sourceHarmonicForMagnitude = std::max(1, static_cast<int>(std::lround(
                    std::max(fundamentalBin, measuredSourceBin) / fundamentalBin)));
                const double harmonicSourceBin =
                    static_cast<double>(sourceHarmonicForMagnitude) * fundamentalBin;
                const double harmonicShiftBins =
                    harmonicSourceBin * (safeRatio - 1.0);
                targetPosition = static_cast<double>(sourceBin) + harmonicShiftBins;
            }
        }
'''
s = replace_once(s, old_mag, new_mag, 'Experimental magnitude ownership')

old_coherence = '''                    const float coherence = 1.0f
                        - smoothStep(coherenceCore, coherenceFade, distance);
                    const double peakVelocityBin =
                        trueSourceBins_[static_cast<std::size_t>(velocityPeak)];
                    const double coherentSourceBin = measuredSourceBin
                        + static_cast<double>(coherence)
                        * (peakVelocityBin - measuredSourceBin);
'''
new_coherence = '''                    const float baseCoherence = 1.0f
                        - smoothStep(coherenceCore, coherenceFade, distance);
                    // V7.1: at 128, retain 85% of measured-peak velocity
                    // coherence. This was the minimum-error one-voice point in
                    // the guarded H1-H8 sweep; it changes no timing or ratio.
                    const float coherence = frameSize_ <= 128
                        ? clamp01(baseCoherence * 0.85f)
                        : baseCoherence;
                    const double peakVelocityBin =
                        trueSourceBins_[static_cast<std::size_t>(velocityPeak)];
                    const double coherentSourceBin = measuredSourceBin
                        + static_cast<double>(coherence)
                        * (peakVelocityBin - measuredSourceBin);
'''
# Only the fallback branch must be changed. The same source fragment also
# exists inside the Live harmonic branch, so anchor it to the preceding else-if.
old_fallback = '''            else if (frameSize_ <= 256 && !nearestPeak_.empty())
            {
                const int velocityPeak = nearestPeak_[static_cast<std::size_t>(sourceBin)];
                if (velocityPeak >= 0 && velocityPeak <= positiveBins)
                {
                    const float distance = static_cast<float>(
                        std::abs(velocityPeak - sourceBin));
                    const float coherenceCore = frameSize_ <= 128 ? 0.50f : 0.75f;
                    const float coherenceFade = frameSize_ <= 128 ? 2.50f : 2.75f;
''' + old_coherence + '''                    transportTargetBin = coherentSourceBin * safeRatio;
                }
            }
'''
new_fallback = '''            else if (frameSize_ <= 256 && !nearestPeak_.empty())
            {
                const int velocityPeak = nearestPeak_[static_cast<std::size_t>(sourceBin)];
                if (velocityPeak >= 0 && velocityPeak <= positiveBins)
                {
                    const float distance = static_cast<float>(
                        std::abs(velocityPeak - sourceBin));
                    const float coherenceCore = frameSize_ <= 128 ? 0.50f : 0.75f;
                    const float coherenceFade = frameSize_ <= 128 ? 2.50f : 2.75f;
''' + new_coherence + '''                    transportTargetBin = coherentSourceBin * safeRatio;
                }
            }
'''
s = replace_once(s, old_fallback, new_fallback, 'Experimental phase velocity coherence')

s = replace_once(
    s,
    'const float lockStrength = clamp01(spatialLock * correctionPhaseNeed);',
    '''const float lockStrength = clamp01(spatialLock * correctionPhaseNeed
            * (frameSize_ <= 128 ? 0.90f : 1.0f));''',
    'Experimental spatial phase lock')

p.write_text(s)
print('EXPERIMENTAL_ONE_VOICE_TRANSLATION_V7_1_PATCH=PASS')
