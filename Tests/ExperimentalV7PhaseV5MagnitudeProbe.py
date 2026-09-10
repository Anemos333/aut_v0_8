from pathlib import Path

p = Path('Source/SingleWetSpectralRenderer.cpp')
s = p.read_text()

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
        // EXPERIMENTAL_V7_PHASE_V5_MAGNITUDE_PROBE
        // The 128-sample lattice cannot assign every coefficient to a unique
        // harmonic. Keep the V5 lobe translation for magnitude while allowing
        // the F0-referenced V7 law to guide phase velocity. Live/256 keeps V7.
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
p.write_text(s.replace(old, new, 1))
print('EXPERIMENTAL_V7_PHASE_V5_MAGNITUDE_PROBE=PASS')
