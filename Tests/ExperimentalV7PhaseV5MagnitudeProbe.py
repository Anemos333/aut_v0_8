from pathlib import Path

p = Path('Source/SingleWetSpectralRenderer.cpp')
s = p.read_text()

# Experimental/128 cannot safely assign each coarse coefficient to a unique
# harmonic. Live/256 keeps V7 harmonic-coordinate transport; Experimental uses
# the same measured-peak displacement for both phase velocity and magnitude.
old_phase_guide = 'const bool harmonicGuideValid = frameSize_ <= 256'
new_phase_guide = 'const bool harmonicGuideValid = frameSize_ == 256'
if s.count(old_phase_guide) != 1:
    raise SystemExit(f'phase guide condition count={s.count(old_phase_guide)}')
s = s.replace(old_phase_guide, new_phase_guide, 1)

old_transport = '''            double transportTargetBin = measuredSourceBin * safeRatio;
            if (harmonicGuideValid && sourceHarmonic > 0)
            {
'''
new_transport = '''            double transportTargetBin = measuredSourceBin * safeRatio;
            if (frameSize_ <= 128 && !nearestPeak_.empty())
            {
                const int translationPeak =
                    nearestPeak_[static_cast<std::size_t>(sourceBin)];
                if (translationPeak >= 0 && translationPeak <= positiveBins)
                {
                    const double peakMeasuredBin =
                        trueSourceBins_[static_cast<std::size_t>(translationPeak)];
                    const double peakShiftBins =
                        peakMeasuredBin * (safeRatio - 1.0);
                    transportTargetBin = measuredSourceBin + peakShiftBins;
                }
            }
            else if (harmonicGuideValid && sourceHarmonic > 0)
            {
'''
if s.count(old_transport) != 1:
    raise SystemExit(f'phase transport insertion count={s.count(old_transport)}')
s = s.replace(old_transport, new_transport, 1)

old = '''        double targetPosition = static_cast<double>(sourceBin) * safeRatio;
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

new = '''        double targetPosition = static_cast<double>(sourceBin) * safeRatio;
        int sourceHarmonicForMagnitude = 0;
        // EXPERIMENTAL_PEAK_COORDINATE_TRANSLATION_PROBE
        // At 128, phase and magnitude obey one identical additive displacement:
        // delta = measuredPeak * (ratio - 1). The analysed lobe is translated,
        // never snapped to an F0 harmonic grid and never mixed with source audio.
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

if s.count(old) != 1:
    raise SystemExit(f'expected one V7 magnitude block, got {s.count(old)}')
s = s.replace(old, new, 1)

p.write_text(s)
print('EXPERIMENTAL_PEAK_COORDINATE_TRANSLATION_PROBE=PASS')
