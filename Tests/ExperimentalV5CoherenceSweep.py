from pathlib import Path
import os

coherence_scale = float(os.environ.get('NEUMATON_COHERENCE_SCALE', '1.0'))
lock_scale = float(os.environ.get('NEUMATON_LOCK_SCALE', '1.0'))
if not (0.0 <= coherence_scale <= 1.25 and 0.0 <= lock_scale <= 1.25):
    raise SystemExit('invalid sweep parameters')

p = Path('Source/SingleWetSpectralRenderer.cpp')
s = p.read_text()

# Experimental uses the pre-V7 measured-peak coherence law. Live/256 keeps V7.
old = 'const bool harmonicGuideValid = frameSize_ <= 256'
new = 'const bool harmonicGuideValid = frameSize_ == 256'
if s.count(old) != 1:
    raise SystemExit(f'phase guide count={s.count(old)}')
s = s.replace(old, new, 1)

# Restore V5 magnitude translation at 128 while retaining V7 magnitude at 256.
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
if s.count(old_mag) != 1:
    raise SystemExit(f'magnitude block count={s.count(old_mag)}')
s = s.replace(old_mag, new_mag, 1)

# Scale only Experimental's velocity coherence inside the fallback branch.
fallback = '''            else if (frameSize_ <= 256 && !nearestPeak_.empty())
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
                    const double coherentSourceBin = measuredSourceBin
                        + static_cast<double>(coherence)
                        * (peakVelocityBin - measuredSourceBin);
                    transportTargetBin = coherentSourceBin * safeRatio;
                }
            }
'''
fallback_new = fallback.replace(
'''                    const float coherence = 1.0f
                        - smoothStep(coherenceCore, coherenceFade, distance);''',
'''                    const float baseCoherence = 1.0f
                        - smoothStep(coherenceCore, coherenceFade, distance);
                    const float coherence = frameSize_ <= 128
                        ? clamp01(baseCoherence * ''' + repr(coherence_scale) + '''f)
                        : baseCoherence;''')
if s.count(fallback) != 1:
    raise SystemExit(f'fallback block count={s.count(fallback)}')
s = s.replace(fallback, fallback_new, 1)

lock_line = '''        const float lockStrength = clamp01(spatialLock * correctionPhaseNeed);'''
lock_new = '''        const float lockStrength = clamp01(spatialLock * correctionPhaseNeed
            * (frameSize_ <= 128 ? ''' + repr(lock_scale) + '''f : 1.0f));'''
if s.count(lock_line) != 1:
    raise SystemExit(f'lock strength count={s.count(lock_line)}')
s = s.replace(lock_line, lock_new, 1)

p.write_text(s)
print(f'V5_COHERENCE_SWEEP coherence={coherence_scale} lock={lock_scale}')
