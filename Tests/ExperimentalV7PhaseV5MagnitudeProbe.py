from pathlib import Path

p = Path('Source/SingleWetSpectralRenderer.cpp')
s = p.read_text()

# Experimental/128: disable harmonic-coordinate phase ownership. The 128-bin
# lattice cannot prove a unique harmonic identity for every coefficient. This
# restores the V6 independent instantaneous-frequency phase law only at 128;
# Live/256 keeps V7 harmonic-coordinate phase transport.
old_phase_guide = 'const bool harmonicGuideValid = frameSize_ <= 256'
new_phase_guide = 'const bool harmonicGuideValid = frameSize_ == 256'
if s.count(old_phase_guide) != 1:
    raise SystemExit(f'phase guide condition count={s.count(old_phase_guide)}')
s = s.replace(old_phase_guide, new_phase_guide, 1)

old_fallback = 'else if (frameSize_ <= 256 && !nearestPeak_.empty())'
new_fallback = 'else if (frameSize_ == 256 && !nearestPeak_.empty())'
if s.count(old_fallback) != 1:
    raise SystemExit(f'phase fallback condition count={s.count(old_fallback)}')
s = s.replace(old_fallback, new_fallback, 1)

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
        // EXPERIMENTAL_V6_PHASE_V5_MAGNITUDE_PROBE
        // At 128 samples, preserve the analysed lobe and translate it by its
        // measured peak displacement. No harmonic reconstruction is introduced.
        // Live/256 retains V7 harmonic-coordinate magnitude transport.
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
print('EXPERIMENTAL_V6_PHASE_V5_MAGNITUDE_PROBE=PASS')
