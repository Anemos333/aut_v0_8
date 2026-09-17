#!/usr/bin/env python3
from pathlib import Path

path = Path('Source/ModernPitchEngine.cpp')
text = path.read_text()

start_marker = '    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept\n'
end_marker = '    const auto residualHarmonicContrast = [&](int tau) noexcept\n'
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit('residualLineCoherence block not found')

replacement = r'''    // RESIDUAL_LINE_COHERENCE_CPU_V1
    // The Hann-windowed residual and its normaliser do not depend on the
    // frequency being probed.  The previous implementation rebuilt both for
    // every harmonic/inter-harmonic line and called sin/cos for every sample.
    // Build that invariant frame once, then advance each probe with a complex
    // oscillator.  This changes no detector authority, thresholds or path
    // cadence; it only removes repeated transcendental work.
    std::array<double, maxAnalysisSize> harmonicWindowedResidual {};
    double harmonicWindowedSignalEnergy = 0.0;
    double harmonicWindowEnergy = 0.0;
    const double harmonicWindowDenominator =
        static_cast<double>(std::max(1, analysisLength - 1));
    for (int index = 0; index < analysisLength; ++index)
    {
        // Keep the exact Hann expression from the baseline, but evaluate it
        // once per analysis frame rather than once per spectral line.
        const double window = 0.5 - 0.5 * std::cos(
            twoPi * static_cast<double>(index) / harmonicWindowDenominator);
        const double sample = static_cast<double>(
            voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
        harmonicWindowedResidual[static_cast<std::size_t>(index)] = sample;
        harmonicWindowedSignalEnergy += sample * sample;
        harmonicWindowEnergy += window * window;
    }
    const double harmonicWindowNormaliser = std::max(
        1.0e-20, harmonicWindowedSignalEnergy * harmonicWindowEnergy);

    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
    {
        if (!std::isfinite(cyclesPerSample)
            || cyclesPerSample <= 0.0 || cyclesPerSample >= 0.48)
        {
            return 0.0f;
        }

        const double angle = twoPi * cyclesPerSample;
        const double stepCos = std::cos(angle);
        const double stepSin = std::sin(angle);
        double phaseCos = 1.0;
        double phaseSin = 0.0;
        double real = 0.0;
        double imag = 0.0;

        for (int index = 0; index < analysisLength; ++index)
        {
            const double sample =
                harmonicWindowedResidual[static_cast<std::size_t>(index)];
            real += sample * phaseCos;
            imag -= sample * phaseSin;

            const double nextCos = phaseCos * stepCos - phaseSin * stepSin;
            const double nextSin = phaseSin * stepCos + phaseCos * stepSin;
            phaseCos = nextCos;
            phaseSin = nextSin;

            // Bound oscillator drift without reintroducing per-sample trig.
            if ((index & 63) == 63)
            {
                const double norm = std::sqrt(std::max(
                    1.0e-30, phaseCos * phaseCos + phaseSin * phaseSin));
                phaseCos /= norm;
                phaseSin /= norm;
            }
        }

        return clamp01(static_cast<float>(std::sqrt(
            2.0 * (real * real + imag * imag) / harmonicWindowNormaliser)));
    };

'''

text = text[:start] + replacement + text[end:]
if text.count('RESIDUAL_LINE_COHERENCE_CPU_V1') != 1:
    raise SystemExit('unexpected CPU marker count')
path.write_text(text)
print('RESIDUAL_LINE_COHERENCE_CPU_V1=materialized')
