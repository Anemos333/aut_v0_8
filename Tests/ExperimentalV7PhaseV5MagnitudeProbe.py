from pathlib import Path

p = Path('Source/SingleWetSpectralRenderer.cpp')
s = p.read_text()

old_transport = '''            double transportTargetBin = measuredSourceBin * safeRatio;
            if (harmonicGuideValid && sourceHarmonic > 0)
            {
'''
new_transport = '''            double transportTargetBin = measuredSourceBin * safeRatio;
            // EXPERIMENTAL_PEAK_OWNED_HARMONIC_TRANSPORT_PROBE
            // At 128, harmonic identity belongs to the measured peak/lobe, not
            // to every coarse FFT coefficient. One lobe receives one additive
            // F0-referenced displacement while its measured local phase shape
            // is retained. Live/256 keeps the existing per-coordinate V7 law.
            if (frameSize_ <= 128
                && harmonicGuideValid
                && !nearestPeak_.empty()
                && fundamentalBin > 1.0e-6)
            {
                const int velocityPeak =
                    nearestPeak_[static_cast<std::size_t>(sourceBin)];
                if (velocityPeak >= 0 && velocityPeak <= positiveBins)
                {
                    const double peakMeasuredBin =
                        trueSourceBins_[static_cast<std::size_t>(velocityPeak)];
                    const int peakHarmonic = std::max(1, static_cast<int>(std::lround(
                        std::max(fundamentalBin, peakMeasuredBin) / fundamentalBin)));
                    const double harmonicPeakBin =
                        static_cast<double>(peakHarmonic) * fundamentalBin;
                    const double harmonicShiftBins =
                        harmonicPeakBin * (safeRatio - 1.0);
                    const float distance = static_cast<float>(
                        std::abs(velocityPeak - sourceBin));
                    constexpr float coherenceCore = 0.50f;
                    constexpr float coherenceFade = 2.50f;
                    const float coherence = 1.0f
                        - smoothStep(coherenceCore, coherenceFade, distance);
                    const double coherentMeasuredSourceBin = measuredSourceBin
                        + static_cast<double>(coherence)
                        * (peakMeasuredBin - measuredSourceBin);
                    transportTargetBin = coherentMeasuredSourceBin
                        + harmonicShiftBins;
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
        if (frameSize_ <= 128
            && peakValid
            && std::isfinite(sourceFundamentalHz)
            && sourceFundamentalHz >= 25.0
            && sourceFundamentalHz <= 3000.0)
        {
            const double fundamentalBin = sourceFundamentalHz
                * static_cast<double>(frameSize_) / sampleRate_;
            if (fundamentalBin > 1.0e-6)
            {
                const double peakMeasuredBin =
                    trueSourceBins_[static_cast<std::size_t>(peak)];
                sourceHarmonicForMagnitude = std::max(1, static_cast<int>(std::lround(
                    std::max(fundamentalBin, peakMeasuredBin) / fundamentalBin)));
                const double harmonicPeakBin =
                    static_cast<double>(sourceHarmonicForMagnitude) * fundamentalBin;
                const double harmonicShiftBins =
                    harmonicPeakBin * (safeRatio - 1.0);
                targetPosition = static_cast<double>(sourceBin) + harmonicShiftBins;
            }
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
        else if (frameSize_ <= 128 && peakValid)
        {
            const double truePeakBin =
                trueSourceBins_[static_cast<std::size_t>(peak)];
            const double peakShiftBins = truePeakBin * safeRatio - truePeakBin;
            targetPosition = static_cast<double>(sourceBin) + peakShiftBins;
        }
'''

if s.count(old) != 1:
    raise SystemExit(f'expected one V7 magnitude block, got {s.count(old)}')
s = s.replace(old, new, 1)

p.write_text(s)
print('EXPERIMENTAL_PEAK_OWNED_HARMONIC_TRANSPORT_PROBE=PASS')
