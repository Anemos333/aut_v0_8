#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / 'Source/SingleWetSpectralRenderer.h'
CPP = ROOT / 'Source/SingleWetSpectralRenderer.cpp'
MARKER = 'UNIFIED_CONTINUOUS_SPECTRAL_FIELD_V13'


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

# Header: peak ownership and propagated peak-lock phases are removed entirely.
# The only retained local reconstruction model is the spectral/formant envelope.
h = replace_once(
    h,
    '    void calculateEnvelope(int positiveBins) noexcept;\n    void calculatePeakRegions(int positiveBins) noexcept;\n',
    '    void calculateEnvelope(int positiveBins) noexcept;\n    void calculateContinuousTransportField(int positiveBins) noexcept;\n',
    'replace peak-region declaration')

h = replace_once(
    h,
    '    std::vector<double> trueSourceBins_;\n    std::vector<double> propagatedPhases_;\n',
    '    std::vector<double> trueSourceBins_;\n    // UNIFIED_CONTINUOUS_SPECTRAL_FIELD_V13: one smooth instantaneous-frequency\n    // field for the whole spectrum. It has no peak/harmonic/region ownership.\n    std::vector<double> transportSourceBins_;\n',
    'replace propagated phase storage')

h = replace_once(
    h,
    '    std::vector<float> spectralEnvelope_;\n    std::vector<double> prefixSum_;\n    std::vector<int> nearestPeak_;\n    std::vector<int> peakBins_;\n',
    '    std::vector<float> spectralEnvelope_;\n    std::vector<double> prefixSum_;\n',
    'remove peak-region storage')

# Prepare/reset storage.
cpp = replace_once(
    cpp,
    '    trueSourceBins_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);\n    propagatedPhases_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);\n',
    '    trueSourceBins_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);\n    transportSourceBins_.assign(static_cast<std::size_t>(positiveBinCount), 0.0);\n',
    'prepare continuous transport storage')

cpp = replace_once(
    cpp,
    '    prefixSum_.assign(static_cast<std::size_t>(positiveBinCount + 1), 0.0);\n    nearestPeak_.assign(static_cast<std::size_t>(positiveBinCount), 0);\n    peakBins_.clear();\n    peakBins_.reserve(static_cast<std::size_t>(positiveBinCount));\n',
    '    prefixSum_.assign(static_cast<std::size_t>(positiveBinCount + 1), 0.0);\n',
    'remove peak prepare storage')

cpp = replace_once(
    cpp,
    '    std::fill(trueSourceBins_.begin(), trueSourceBins_.end(), 0.0);\n    std::fill(propagatedPhases_.begin(), propagatedPhases_.end(), 0.0);\n',
    '    std::fill(trueSourceBins_.begin(), trueSourceBins_.end(), 0.0);\n    std::fill(transportSourceBins_.begin(), transportSourceBins_.end(), 0.0);\n',
    'reset continuous transport storage')

cpp = replace_once(
    cpp,
    '    std::fill(prefixSum_.begin(), prefixSum_.end(), 0.0);\n    std::fill(nearestPeak_.begin(), nearestPeak_.end(), 0);\n    peakBins_.clear();\n',
    '    std::fill(prefixSum_.begin(), prefixSum_.end(), 0.0);\n',
    'remove peak reset storage')

# Replace hard nearest-peak territories with a smooth, overlapping frequency
# field. Every bin contributes continuously to its neighbours; there are no
# ownership boundaries and no harmonic identities. Magnitude is only a soft
# analysis weight, never a permission or a separate rendered component.
new_field = r'''void SingleWetSpectralRenderer::calculateContinuousTransportField(
    int positiveBins) noexcept
{
    if (positiveBins < 0
        || transportSourceBins_.size() < static_cast<std::size_t>(positiveBins + 1))
        return;

    // UNIFIED_CONTINUOUS_SPECTRAL_FIELD_V13
    // Five overlapping bins are sufficient to make neighbouring instantaneous
    // frequency estimates coherent without partitioning the spectrum into
    // peaks, harmonics or regions. The same law is used at every frame size.
    constexpr int radius = 2;

    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        if (sourceBin == 0 || sourceBin == positiveBins)
        {
            transportSourceBins_[static_cast<std::size_t>(sourceBin)] =
                static_cast<double>(sourceBin);
            continue;
        }

        const int first = std::max(0, sourceBin - radius);
        const int last = std::min(positiveBins, sourceBin + radius);
        float localMaximum = 0.0f;
        for (int neighbour = first; neighbour <= last; ++neighbour)
        {
            localMaximum = std::max(
                localMaximum,
                magnitudes_[static_cast<std::size_t>(neighbour)]);
        }

        const double safeMaximum = std::max(1.0e-12,
                                             static_cast<double>(localMaximum));
        double weightedCoordinate = 0.0;
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
            const double analysisWeight = spatialWeight
                * (0.04 + 0.96 * std::sqrt(magnitudeRatio));

            const double measured = std::clamp(
                trueSourceBins_[static_cast<std::size_t>(neighbour)],
                0.0,
                static_cast<double>(positiveBins));
            weightedCoordinate += analysisWeight * measured;
            totalWeight += analysisWeight;
        }

        const double ownMeasured = std::clamp(
            trueSourceBins_[static_cast<std::size_t>(sourceBin)],
            0.0,
            static_cast<double>(positiveBins));
        const double smoothCoordinate = totalWeight > 1.0e-12
            ? weightedCoordinate / totalWeight
            : ownMeasured;

        // Keep the field local and monotonic enough for stable interpolation.
        // This is a continuous safety bound around the bin coordinate, not a
        // region assignment and not a detector-dependent correction limit.
        transportSourceBins_[static_cast<std::size_t>(sourceBin)] = std::clamp(
            smoothCoordinate,
            std::max(0.0, static_cast<double>(sourceBin) - 2.0),
            std::min(static_cast<double>(positiveBins),
                     static_cast<double>(sourceBin) + 2.0));
    }
}

'''
cpp = replace_section(
    cpp,
    'void SingleWetSpectralRenderer::calculatePeakRegions(',
    'float SingleWetSpectralRenderer::interpolateEnvelope(',
    new_field,
    'replace peak regions with continuous field')

# One synthesis equation for Quality, Live and Experimental. No frame-size
# branches, no peak ownership, no harmonic family rebuild and no identity lock.
# Formant preservation is the only explicit spectral-envelope reconstruction.
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

    // UNIFIED_CONTINUOUS_SPECTRAL_FIELD_V13
    // F0 chooses the musical destination upstream. Rendering has one continuous
    // spectrum-wide transport field and never rebuilds harmonic/peak regions.
    (void) sourceFundamentalHz;

    const double safeCents = sanitiseCorrectionCents(correctionCents);
    const double safeRatio = std::exp2(safeCents / 1200.0);
    const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)
                                    / static_cast<double>(frameSize_);
    const float safeFormant = clamp01(formantPreservation);
    const float energyScale = static_cast<float>(1.0 / std::sqrt(safeRatio));
    const bool initialiseLayer = resetPhases || !layer.phaseInitialised;

    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const std::size_t sourceIndex = static_cast<std::size_t>(sourceBin);
        const double analysisPhase = analysisPhases_[sourceIndex];
        const double transportCoordinate = std::clamp(
            transportSourceBins_[sourceIndex],
            0.0,
            static_cast<double>(positiveBins));

        double& synthesisPhase = layer.synthesisPhases[sourceIndex];
        if (initialiseLayer)
        {
            synthesisPhase = analysisPhase;
        }
        else
        {
            // One coherent phase velocity field for every mode. No peak may
            // own another bin and no latency mode selects another phase law.
            synthesisPhase += expectedPhaseScale
                            * transportCoordinate * safeRatio;
            synthesisPhase -= twoPi
                            * std::nearbyint(synthesisPhase / twoPi);
        }

        const float magnitude = magnitudes_[sourceIndex];
        if (magnitude <= 1.0e-12f)
            continue;

        // Shift the existing spectral geometry by one smooth displacement
        // field. Neighbouring bins overlap continuously instead of moving as
        // separately reconstructed peak/harmonic territories.
        const double targetPosition = static_cast<double>(sourceBin)
            + transportCoordinate * (safeRatio - 1.0);
        if (targetPosition < -1.0
            || targetPosition > static_cast<double>(positiveBins) + 1.0)
            continue;

        const float sourceEnvelope = std::max(
            1.0e-8f,
            spectralEnvelope_[sourceIndex]);
        const float targetEnvelope = std::max(
            1.0e-8f,
            interpolateEnvelope(targetPosition));
        const float envelopeRatio = std::clamp(
            targetEnvelope / sourceEnvelope,
            0.56f,
            1.78f);
        const float formantGain = lookupFormantGain(envelopeRatio, safeFormant);

        // Formant is the only permitted reconstruction layer. It acts on the
        // one transported spectrum and never creates another audio component.
        const float outputMagnitude = magnitude
                                    * formantGain
                                    * energyScale;
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
        const float weightNormalisation = weightPower > 1.0e-12f
            ? 1.0f / std::sqrt(weightPower)
            : 1.0f;
        lowerWeight *= weightNormalisation;
        upperWeight *= weightNormalisation;

        if (targetBin0 >= 0 && targetBin0 <= positiveBins)
        {
            layer.spectrum[static_cast<std::size_t>(targetBin0)] +=
                polar * lowerWeight;
        }

        const int targetBin1 = targetBin0 + 1;
        if (targetBin1 >= 0 && targetBin1 <= positiveBins)
            layer.spectrum[static_cast<std::size_t>(targetBin1)] +=
                polar * upperWeight;
    }

    layer.phaseInitialised = true;
    layer.spectrum[0] = Complex(layer.spectrum[0].real(), 0.0f);
    layer.spectrum[static_cast<std::size_t>(positiveBins)] =
        Complex(layer.spectrum[static_cast<std::size_t>(positiveBins)].real(),
                0.0f);

    for (int bin = 1; bin < positiveBins; ++bin)
    {
        layer.spectrum[static_cast<std::size_t>(frameSize_ - bin)] =
            std::conj(layer.spectrum[static_cast<std::size_t>(bin)]);
    }

    fft(layer.spectrum, true);

    const std::int64_t outputStartSample = frameEndSample + 1;
    for (int index = 0; index < frameSize_; ++index)
    {
        const float synthesisWindow = window_[static_cast<std::size_t>(index)];
        const float output = layer.spectrum[static_cast<std::size_t>(index)].real()
                           * synthesisWindow;
        const int outputIndex = static_cast<int>((outputStartSample + index)
                                                  & outputRingMask_);
        layer.outputAccumulationRing[static_cast<std::size_t>(outputIndex)] +=
            output;
    }
}

'''
cpp = replace_section(
    cpp,
    'void SingleWetSpectralRenderer::synthesiseLayer(',
    'void SingleWetSpectralRenderer::processFrame(',
    new_synth,
    'unified synthesis law')

cpp = replace_once(
    cpp,
    '    calculatePeakRegions(positiveBins);\n\n',
    '',
    'remove peak-region analysis call')

cpp = replace_once(
    cpp,
    '        trueSourceBins_[static_cast<std::size_t>(sourceBin)] = trueSourceBin;\n    }\n\n    synthesiseLayer(layer_, frameEndSample, correctionCents,\n',
    '        trueSourceBins_[static_cast<std::size_t>(sourceBin)] = trueSourceBin;\n    }\n\n    calculateContinuousTransportField(positiveBins);\n\n    synthesiseLayer(layer_, frameEndSample, correctionCents,\n',
    'insert continuous transport field')

HEADER.write_text(h, encoding='utf-8')
CPP.write_text(cpp, encoding='utf-8')
print(f'{MARKER}=materialized')
