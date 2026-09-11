from pathlib import Path
import re
import runpy

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'Tests' / 'UnifiedContinuousSpectralTransportV13.py'
CPP = ROOT / 'Source' / 'SingleWetSpectralRenderer.cpp'
HEADER = ROOT / 'Source' / 'SingleWetSpectralRenderer.h'
MARKER = 'UNIFIED_CONTINUOUS_CORRECTION_FIELD_V14'

# Start from the already-audited removal of peak/region reconstruction.
runpy.run_path(str(BASE), run_name='__main__')

cpp = CPP.read_text(encoding='utf-8')
header = HEADER.read_text(encoding='utf-8')

start = cpp.index('void SingleWetSpectralRenderer::calculateContinuousCorrectionField(')
end = cpp.index('float SingleWetSpectralRenderer::interpolateEnvelope(', start)

new_field = r'''void SingleWetSpectralRenderer::calculateContinuousCorrectionField(
    int positiveBins,
    double shiftScale) noexcept
{
    if (positiveBins < 0
        || correctionShiftBins_.size() < static_cast<std::size_t>(positiveBins + 1))
        return;

    // UNIFIED_CONTINUOUS_CORRECTION_FIELD_V14
    // One soft bilateral carrier field over adjacent spectral coordinates.
    // No peaks, regions, harmonic identities, masks or ownership boundaries.
    // The result controls one continuous energy geometry for the whole spectrum.
    const int radius = frameSize_ <= 128 ? 5
                     : frameSize_ <= 256 ? 3
                                         : 2;
    const double affinitySigmaBins = frameSize_ <= 128 ? 0.72
                                   : frameSize_ <= 256 ? 0.56
                                                       : 0.44;
    const double coherenceBlend = frameSize_ <= 128 ? 0.92
                                : frameSize_ <= 256 ? 0.68
                                                    : 0.46;

    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const std::size_t sourceIndex = static_cast<std::size_t>(sourceBin);
        const double rawSourceBin = std::isfinite(trueSourceBins_[sourceIndex])
            ? trueSourceBins_[sourceIndex]
            : static_cast<double>(sourceBin);
        const float selfMagnitude = std::max(magnitudes_[sourceIndex], 1.0e-8f);
        double weightedCarrier = 0.0;
        double weightSum = 0.0;

        const int first = std::max(0, sourceBin - radius);
        const int last = std::min(positiveBins, sourceBin + radius);
        for (int neighbourBin = first; neighbourBin <= last; ++neighbourBin)
        {
            const std::size_t neighbourIndex = static_cast<std::size_t>(neighbourBin);
            const double rawNeighbourBin = std::isfinite(trueSourceBins_[neighbourIndex])
                ? trueSourceBins_[neighbourIndex]
                : static_cast<double>(neighbourBin);
            const double spatialDistance = static_cast<double>(
                std::abs(neighbourBin - sourceBin));
            const double spatialWeight = std::max(
                0.0,
                1.0 - spatialDistance / static_cast<double>(radius + 1));
            const double frequencyDistance =
                (rawNeighbourBin - rawSourceBin) / affinitySigmaBins;
            const double frequencyWeight = std::exp(
                -0.5 * frequencyDistance * frequencyDistance);
            const double magnitudeRatio = std::clamp(
                static_cast<double>(magnitudes_[neighbourIndex])
                    / static_cast<double>(selfMagnitude),
                0.0,
                4.0);
            const double magnitudeWeight = 0.18 + 0.82 * std::sqrt(magnitudeRatio);
            const double weight = spatialWeight * frequencyWeight * magnitudeWeight;
            weightedCarrier += weight * rawNeighbourBin;
            weightSum += weight;
        }

        const double neighbourCarrier = weightSum > 1.0e-12
            ? weightedCarrier / weightSum
            : rawSourceBin;
        const double continuousCarrier = rawSourceBin
            + coherenceBlend * (neighbourCarrier - rawSourceBin);
        correctionShiftBins_[sourceIndex] = continuousCarrier * shiftScale;
    }
}

'''
cpp = cpp[:start] + new_field + cpp[end:]

phase_loop_anchor = '''    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const std::size_t sourceIndex = static_cast<std::size_t>(sourceBin);
        const double analysisPhase = analysisPhases_[sourceIndex];'''
if phase_loop_anchor not in cpp:
    raise SystemExit('V14 phase loop anchor not found')

phase_reference = '''    float phaseReferenceMagnitude = 1.0e-8f;
    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
        phaseReferenceMagnitude = std::max(
            phaseReferenceMagnitude,
            magnitudes_[static_cast<std::size_t>(sourceBin)]);

'''
cpp = cpp.replace(phase_loop_anchor, phase_reference + phase_loop_anchor, 1)

phase_pattern = re.compile(
    r'const double correctedPhaseVelocityBin\s*=\s*measuredSourceBin\s*\n\s*\+\s*correctionShiftBins_\[sourceIndex\];')
if not phase_pattern.search(cpp):
    raise SystemExit('V14 phase velocity anchor not found')

phase_code = r'''const double shiftScale = safeRatio - 1.0;
            const double continuousCarrier = std::abs(shiftScale) > 1.0e-12
                ? correctionShiftBins_[sourceIndex] / shiftScale
                : measuredSourceBin;
            const double relativeEnergy = std::clamp(
                static_cast<double>(magnitudes_[sourceIndex])
                    / static_cast<double>(phaseReferenceMagnitude),
                0.0,
                1.0);
            const double dominantAuthority = relativeEnergy * relativeEnergy
                                           * relativeEnergy * relativeEnergy;
            const double phaseRegularisation = frameSize_ <= 128 ? 0.96
                                               : frameSize_ <= 256 ? 0.72
                                                                   : 0.50;
            const double phaseCarrier = measuredSourceBin
                + phaseRegularisation * (1.0 - dominantAuthority)
                    * (continuousCarrier - measuredSourceBin);
            const double correctedPhaseVelocityBin = phaseCarrier * safeRatio;'''
cpp = phase_pattern.sub(phase_code, cpp, count=1)

cpp = cpp.replace('UNIFIED_CONTINUOUS_CORRECTION_FIELD_V13_3', MARKER)
header = header.replace('UNIFIED_CONTINUOUS_CORRECTION_FIELD_V13_3', MARKER)

CPP.write_text(cpp, encoding='utf-8')
HEADER.write_text(header, encoding='utf-8')
print(f'{MARKER}=materialized')
