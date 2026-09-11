#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'Source/SingleWetSpectralRenderer.h'
CPP = ROOT / 'Source/SingleWetSpectralRenderer.cpp'
MARKER = 'UNIFIED_CONTINUOUS_SHIFT_FIELD_V13_1'


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {count}')
    return text.replace(old, new, 1)


def replace_section(text, start, end, replacement, label):
    first = text.find(start)
    if first < 0:
        raise RuntimeError(f'{label}: start marker not found')
    last = text.find(end, first + len(start))
    if last < 0:
        raise RuntimeError(f'{label}: end marker not found')
    return text[:first] + replacement + text[last:]


h = HEADER.read_text(encoding='utf-8')
cpp = CPP.read_text(encoding='utf-8')
if MARKER in h and MARKER in cpp:
    print(f'{MARKER}=already_materialized')
    raise SystemExit(0)

# No peak/harmonic ownership survives in rendering. Formant envelope is the
# only intentionally reconstructed spectral structure.
h = replace_once(
    h,
    '    void calculateEnvelope(int positiveBins) noexcept;\n    void calculatePeakRegions(int positiveBins) noexcept;\n',
    '    void calculateEnvelope(int positiveBins) noexcept;\n    void calculateContinuousShiftField(int positiveBins, double shiftScale) noexcept;\n',
    'replace peak-region declaration')
h = replace_once(
    h,
    '    std::vector<double> trueSourceBins_;\n    std::vector<double> propagatedPhases_;\n',
    '    std::vector<double> trueSourceBins_;\n    // UNIFIED_CONTINUOUS_SHIFT_FIELD_V13_1: continuous geometry displacement.\n    // It never replaces the measured instantaneous-frequency phase velocity.\n    std::vector<double> transportShiftBins_;\n',
    'replace propagated phase storage')
h = replace_once(
    h,
    '    std::vector<float> spectralEnvelope_;\n    std::vector<double> prefixSum_;\n    std::vector<int> nearestPeak_;\n    std::vector<int> peakBins_;\n',
    '    std::vector<float> spectralEnvelope_;\n    std::vector<double> prefixSum_;\n',
    'remove peak ownership storage')

cpp = replace_once(
    cpp,
    '    trueSourceBins_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);\n    propagatedPhases_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);\n',
    '    trueSourceBins_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);\n    transportShiftBins_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);\n',
    'prepare shift storage')
cpp = replace_once(
    cpp,
    '    prefixSum_.assign(static_cast<std::size_t>(positiveBinCount + 1), 0.0);\n    nearestPeak_.assign(static_cast<std::size_t>(positiveBinCount), 0);\n    peakBins_.clear();\n    peakBins_.reserve(static_cast<std::size_t>(positiveBinCount));\n',
    '    prefixSum_.assign(static_cast<std::size_t>(positiveBinCount + 1), 0.0);\n',
    'remove peak prepare storage')
cpp = replace_once(
    cpp,
    '    std::fill(trueSourceBins_.begin(), trueSourceBins_.end(), 0.0);\n    std::fill(propagatedPhases_.begin(), propagatedPhases_.end(), 0.0);\n',
    '    std::fill(trueSourceBins_.begin(), trueSourceBins_.end(), 0.0);\n    std::fill(transportShiftBins_.begin(), transportShiftBins_.end(), 0.0);\n',
    'reset shift storage')
cpp = replace_once(
    cpp,
    '    std::fill(prefixSum_.begin(), prefixSum_.end(), 0.0);\n    std::fill(nearestPeak_.begin(), nearestPeak_.end(), 0);\n    peakBins_.clear();\n',
    '    std::fill(prefixSum_.begin(), prefixSum_.end(), 0.0);\n',
    'remove peak reset storage')

new_field = r'''void SingleWetSpectralRenderer::calculateContinuousShiftField(
    int positiveBins,
    double shiftScale) noexcept
{
    if (positiveBins < 0
        || transportShiftBins_.size() < static_cast<std::size_t>(positiveBins + 1))
        return;

    // UNIFIED_CONTINUOUS_SHIFT_FIELD_V13_1
    // We smooth only the requested geometrical displacement. The measured
    // instantaneous frequency itself remains untouched in the phase integrator.
    // Therefore correction == 0 produces exactly zero displacement everywhere.
    constexpr int radius = 2;
    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const int first = std::max(0, sourceBin - radius);
        const int last = std::min(positiveBins, sourceBin + radius);
        float localMaximum = 0.0f;
        for (int neighbour = first; neighbour <= last; ++neighbour)
            localMaximum = std::max(localMaximum,
                magnitudes_[static_cast<std::size_t>(neighbour)]);

        const double safeMaximum = std::max(1.0e-12,
                                             static_cast<double>(localMaximum));
        double weightedShift = 0.0;
        double totalWeight = 0.0;
        for (int neighbour = first; neighbour <= last; ++neighbour)
        {
            const int distance = std::abs(neighbour - sourceBin);
            const double spatialWeight = 1.0
                - static_cast<double>(distance)
                    / static_cast<double>(radius + 1);
            const double magnitudeRatio = std::clamp(
                static_cast<double>(magnitudes_[static_cast<std::size_t>(neighbour)])
                    / safeMaximum,
                0.0,
                1.0);
            const double weight = spatialWeight
                * (0.02 + 0.98 * magnitudeRatio * magnitudeRatio);
            const double measured = std::clamp(
                trueSourceBins_[static_cast<std::size_t>(neighbour)],
                0.0,
                static_cast<double>(positiveBins));
            weightedShift += weight * measured * shiftScale;
            totalWeight += weight;
        }

        const double ownShift = std::clamp(
            trueSourceBins_[static_cast<std::size_t>(sourceBin)],
            0.0,
            static_cast<double>(positiveBins)) * shiftScale;
        transportShiftBins_[static_cast<std::size_t>(sourceBin)] =
            totalWeight > 1.0e-12 ? weightedShift / totalWeight : ownShift;
    }
}

'''
cpp = replace_section(
    cpp,
    'void SingleWetSpectralRenderer::calculatePeakRegions(',
    'float SingleWetSpectralRenderer::interpolateEnvelope(',
    new_field,
    'replace peak regions with continuous shift field')

new_synth = r'''void SingleWetSpectralRenderer::synthesiseLayer(
    SynthesisLayer& layer,
    std::int64_t frameEndSample,
    double correctionCents,
    float formantPreservation,
    double sourceFundamentalHz,
    bool resetPhases,
    int positiveBins) noexcept
{
    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});

    // UNIFIED_CONTINUOUS_SHIFT_FIELD_V13_1
    // F0 is upstream authority only. No harmonic/peak identity participates in
    // synthesis, and Quality/Live/Experimental use this same equation.
    (void) sourceFundamentalHz;

    const double safeCents = sanitiseCorrectionCents(correctionCents);
    const double safeRatio = std::exp2(safeCents / 1200.0);
    const double shiftScale = safeRatio - 1.0;
    const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)
                                    / static_cast<double>(frameSize_);
    const float safeFormant = clamp01(formantPreservation);
    const float energyScale = static_cast<float>(1.0 / std::sqrt(safeRatio));
    const bool initialiseLayer = resetPhases || !layer.phaseInitialised;

    calculateContinuousShiftField(positiveBins, shiftScale);

    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const std::size_t sourceIndex = static_cast<std::size_t>(sourceBin);
        const double analysisPhase = analysisPhases_[sourceIndex];
        double& synthesisPhase = layer.synthesisPhases[sourceIndex];

        if (initialiseLayer)
            synthesisPhase = analysisPhase;
        else
        {
            // Exact measured phase velocity remains authoritative. We do not
            // average, quantise, peak-lock or otherwise reconstruct it.
            const double measuredSourceBin = trueSourceBins_[sourceIndex];
            synthesisPhase += expectedPhaseScale * measuredSourceBin * safeRatio;
            synthesisPhase -= twoPi * std::nearbyint(synthesisPhase / twoPi);
        }

        const float magnitude = magnitudes_[sourceIndex];
        if (magnitude <= 1.0e-12f)
            continue;

        // Only geometry is regularised: a continuous overlapping shift field
        // transports the existing spectrum without assigning bins to pieces.
        const double targetPosition = static_cast<double>(sourceBin)
                                    + transportShiftBins_[sourceIndex];
        if (targetPosition < -1.0
            || targetPosition > static_cast<double>(positiveBins) + 1.0)
            continue;

        const float sourceEnvelope = std::max(1.0e-8f,
                                               spectralEnvelope_[sourceIndex]);
        const float targetEnvelope = std::max(1.0e-8f,
                                               interpolateEnvelope(targetPosition));
        const float envelopeRatio = std::clamp(targetEnvelope / sourceEnvelope,
                                                0.56f, 1.78f);
        const float formantGain = lookupFormantGain(envelopeRatio, safeFormant);
        const float outputMagnitude = magnitude * formantGain * energyScale;

        float phaseSine = 0.0f;
        float phaseCosine = 1.0f;
        fastSinCos(synthesisPhase, phaseSine, phaseCosine);
        const Complex polar(outputMagnitude * phaseCosine,
                            outputMagnitude * phaseSine);

        const int targetBin0 = static_cast<int>(std::floor(targetPosition));
        const float fraction = static_cast<float>(
            targetPosition - static_cast<double>(targetBin0));
        float lowerWeight = 1.0f - fraction;
        float upperWeight = fraction;
        const float weightPower = lowerWeight * lowerWeight
                                + upperWeight * upperWeight;
        const float normalisation = weightPower > 1.0e-12f
            ? 1.0f / std::sqrt(weightPower) : 1.0f;
        lowerWeight *= normalisation;
        upperWeight *= normalisation;

        if (targetBin0 >= 0 && targetBin0 <= positiveBins)
            layer.spectrum[static_cast<std::size_t>(targetBin0)] += polar * lowerWeight;
        const int targetBin1 = targetBin0 + 1;
        if (targetBin1 >= 0 && targetBin1 <= positiveBins)
            layer.spectrum[static_cast<std::size_t>(targetBin1)] += polar * upperWeight;
    }

    layer.phaseInitialised = true;
    layer.spectrum[0] = Complex(layer.spectrum[0].real(), 0.0f);
    layer.spectrum[static_cast<std::size_t>(positiveBins)] =
        Complex(layer.spectrum[static_cast<std::size_t>(positiveBins)].real(), 0.0f);
    for (int bin = 1; bin < positiveBins; ++bin)
        layer.spectrum[static_cast<std::size_t>(frameSize_ - bin)] =
            std::conj(layer.spectrum[static_cast<std::size_t>(bin)]);

    fft(layer.spectrum, true);
    const std::int64_t outputStartSample = frameEndSample + 1;
    for (int index = 0; index < frameSize_; ++index)
    {
        const float output = layer.spectrum[static_cast<std::size_t>(index)].real()
                           * window_[static_cast<std::size_t>(index)];
        const int outputIndex = static_cast<int>((outputStartSample + index)
                                                  & outputRingMask_);
        layer.outputAccumulationRing[static_cast<std::size_t>(outputIndex)] += output;
    }
}

'''
cpp = replace_section(
    cpp,
    'void SingleWetSpectralRenderer::synthesiseLayer(',
    'void SingleWetSpectralRenderer::processFrame(',
    new_synth,
    'unified synthesis law')
cpp = replace_once(cpp, '    calculatePeakRegions(positiveBins);\n\n', '',
                   'remove peak analysis call')

# smoothStep belonged only to peak ownership/phase locking.
cpp = replace_once(
    cpp,
    'float smoothStep(float a,float b,float v) noexcept { if(b<=a) return v>=b?1.0f:0.0f; const float x=std::clamp((v-a)/(b-a),0.0f,1.0f); return x*x*(3.0f-2.0f*x); }\n',
    '',
    'remove obsolete peak helper')

HEADER.write_text(h, encoding='utf-8')
CPP.write_text(cpp, encoding='utf-8')
print(f'{MARKER}=materialized')
