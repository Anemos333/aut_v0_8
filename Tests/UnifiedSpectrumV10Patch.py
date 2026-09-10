#!/usr/bin/env python3
from pathlib import Path
import re

path = Path('Source/SingleWetSpectralRenderer.cpp')
src = path.read_text()

new_peak = r'''void SingleWetSpectralRenderer::calculatePeakRegions(
    int positiveBins) noexcept
{
    peakBins_.clear();
    if (positiveBins <= 1 || magnitudes_.empty() || nearestPeak_.empty())
        return;

    float maximumMagnitude = 0.0f;
    int maximumBin = 1;
    for (int bin = 1; bin < positiveBins; ++bin)
    {
        const float magnitude = magnitudes_[static_cast<std::size_t>(bin)];
        if (magnitude > maximumMagnitude)
        {
            maximumMagnitude = magnitude;
            maximumBin = bin;
        }
    }

    // ONE_VOICE_UNIFIED_SPECTRUM_V10
    // There is no harmonic-vs-air/noise threshold. Every measurable local
    // maximum is simply a geometric anchor for its neighbouring FFT samples.
    // A diffuse/noisy region therefore follows exactly the same transport law
    // as a tonal region instead of being left as a second phase population.
    for (int bin = 1; bin < positiveBins; ++bin)
    {
        const float centre = magnitudes_[static_cast<std::size_t>(bin)];
        if (centre > 1.0e-12f
            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]
            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])
        {
            peakBins_.push_back(bin);
        }
    }

    if (peakBins_.empty())
        peakBins_.push_back(maximumBin);

    int peakIndex = 0;
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        while (peakIndex + 1 < static_cast<int>(peakBins_.size()))
        {
            const int currentPeak = peakBins_[static_cast<std::size_t>(peakIndex)];
            const int nextPeak = peakBins_[static_cast<std::size_t>(peakIndex + 1)];
            if (bin <= (currentPeak + nextPeak) / 2)
                break;
            ++peakIndex;
        }
        nearestPeak_[static_cast<std::size_t>(bin)] =
            peakBins_[static_cast<std::size_t>(peakIndex)];
    }
}

'''

pattern_peak = re.compile(
    r'void SingleWetSpectralRenderer::calculatePeakRegions\(\s*int positiveBins\) noexcept\s*\{.*?\n\}\n\n(?=float SingleWetSpectralRenderer::interpolateEnvelope)',
    re.S)
src, n = pattern_peak.subn(new_peak, src, count=1)
assert n == 1, f'calculatePeakRegions replacement count={n}'

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

    // ONE_VOICE_UNIFIED_SPECTRUM_V10
    // The renderer receives one musical ratio and transports the entire measured
    // spectrum with one law. sourceFundamentalHz is deliberately not consulted:
    // F0 chooses the musical target upstream, never a separate spectral family.
    (void) sourceFundamentalHz;
    const double safeCents = sanitiseCorrectionCents(correctionCents);
    const double safeRatio = std::exp2(safeCents / 1200.0);
    const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)
                                    / static_cast<double>(frameSize_);
    const float globalPhaseCoherence = smoothStep(
        6.0f, 42.0f, static_cast<float>(std::abs(safeCents)));

    const bool initialiseLayer = resetPhases || !layer.phaseInitialised;
    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const std::size_t sourceIndex = static_cast<std::size_t>(sourceBin);
        const double analysisPhase = analysisPhases_[sourceIndex];
        const int peak = nearestPeak_.empty()
            ? sourceBin : nearestPeak_[sourceIndex];
        const bool peakValid = peak >= 0 && peak <= positiveBins;
        const double truePeakBin = peakValid
            ? trueSourceBins_[static_cast<std::size_t>(peak)]
            : trueSourceBins_[sourceIndex];
        const double regionShiftBins = truePeakBin * (safeRatio - 1.0);
        const double transportedInstantaneousBin =
            trueSourceBins_[sourceIndex] + regionShiftBins;

        if (initialiseLayer)
        {
            layer.synthesisPhases[sourceIndex] = analysisPhase;
        }
        else
        {
            double& synthesisPhase = layer.synthesisPhases[sourceIndex];
            synthesisPhase += expectedPhaseScale * transportedInstantaneousBin;
            synthesisPhase -= twoPi * std::nearbyint(synthesisPhase / twoPi);
        }
        propagatedPhases_[sourceIndex] = layer.synthesisPhases[sourceIndex];
    }

    // Preserve the existing formant control for this isolated phase/topology
    // probe. It is deliberately not allowed to choose different spectral
    // populations. Experimental keeps the already-approved no-envelope policy.
    const float safeFormant = frameSize_ <= 128
        ? 0.0f : clamp01(formantPreservation);
    const float energyScale = static_cast<float>(1.0 / std::sqrt(safeRatio));

    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const std::size_t sourceIndex = static_cast<std::size_t>(sourceBin);
        const float magnitude = magnitudes_[sourceIndex];
        if (magnitude <= 1.0e-12f)
            continue;

        const int peak = nearestPeak_.empty()
            ? sourceBin : nearestPeak_[sourceIndex];
        const bool peakValid = peak >= 0 && peak <= positiveBins;
        const int phasePeak = peakValid ? peak : sourceBin;
        const double truePeakBin = peakValid
            ? trueSourceBins_[static_cast<std::size_t>(peak)]
            : trueSourceBins_[sourceIndex];
        const double regionShiftBins = truePeakBin * (safeRatio - 1.0);
        const double targetPosition = static_cast<double>(sourceBin)
                                    + regionShiftBins;
        if (targetPosition < -1.0
            || targetPosition > static_cast<double>(positiveBins) + 1.0)
            continue;

        // One global phase-coherence amount for the whole spectrum. There is no
        // distance, magnitude, harmonicity, breath or transient decision that
        // can put a subset of bins onto another phase law.
        const double ownPhase = propagatedPhases_[sourceIndex];
        const double anchorPhase =
            propagatedPhases_[static_cast<std::size_t>(phasePeak)]
            + wrapPhase(static_cast<double>(analysisPhases_[sourceIndex])
                - static_cast<double>(analysisPhases_[static_cast<std::size_t>(phasePeak)]));
        const double phaseDelta = wrapPhase(anchorPhase - ownPhase);
        const double outputPhase = ownPhase
            + static_cast<double>(globalPhaseCoherence) * phaseDelta;

        const float sourceEnvelope = std::max(
            1.0e-8f, spectralEnvelope_[sourceIndex]);
        const float targetEnvelope = std::max(
            1.0e-8f, interpolateEnvelope(targetPosition));
        const float envelopeRatio = std::clamp(
            targetEnvelope / sourceEnvelope, 0.56f, 1.78f);
        const float formantGain = lookupFormantGain(envelopeRatio, safeFormant);
        const float outputMagnitude = magnitude * formantGain * energyScale;

        float phaseSine = 0.0f;
        float phaseCosine = 1.0f;
        fastSinCos(outputPhase, phaseSine, phaseCosine);
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
            ? 1.0f / std::sqrt(weightPower) : 1.0f;
        lowerWeight *= weightNormalisation;
        upperWeight *= weightNormalisation;

        if (targetBin0 >= 0 && targetBin0 <= positiveBins)
            layer.spectrum[static_cast<std::size_t>(targetBin0)] +=
                polar * lowerWeight;
        const int targetBin1 = targetBin0 + 1;
        if (targetBin1 >= 0 && targetBin1 <= positiveBins)
            layer.spectrum[static_cast<std::size_t>(targetBin1)] +=
                polar * upperWeight;
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

pattern_synth = re.compile(
    r'void SingleWetSpectralRenderer::synthesiseLayer\(.*?\n\}\n\n(?=void SingleWetSpectralRenderer::processFrame)',
    re.S)
src, n = pattern_synth.subn(new_synth, src, count=1)
assert n == 1, f'synthesiseLayer replacement count={n}'

# Guard against the old split/reconstruction mechanisms surviving the replacement.
for forbidden in (
    'maximumMagnitude * 0.018f',
    'harmonicGuideValid',
    'sourceHarmonicForMagnitude',
    'sameExperimentalHarmonicFamily',
    'spatialLock',
    'lockStrength',
    'harmonicShiftBins',
):
    assert forbidden not in src, f'forbidden residual: {forbidden}'
assert 'ONE_VOICE_UNIFIED_SPECTRUM_V10' in src
assert 'const double targetPosition = static_cast<double>(sourceBin)\n                                    + regionShiftBins;' in src

path.write_text(src)
print('UNIFIED_SPECTRUM_V10_PATCH=PASS')
