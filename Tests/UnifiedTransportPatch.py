from pathlib import Path
import re

renderer_path = Path("Source/SingleWetSpectralRenderer.cpp")
engine_path = Path("Source/ModernPitchEngine.cpp")
renderer = renderer_path.read_text(encoding="utf-8")
engine = engine_path.read_text(encoding="utf-8")

if "PURE_SINGLE_TRANSPORT_V4" not in renderer:
    raise RuntimeError("V4 pure transport must exist before V5 cleanup")
if "MINIMAL_RENDERER_V5" in renderer:
    print("V5 minimal renderer already applied")
    raise SystemExit(0)


def sub_once(text, pattern, replacement, label):
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{label}: expected one block, found {count}")
    return updated


# Classifier storage disappears; prefixSum remains because it belongs to the
# user-controlled formant envelope, not to voice/noise classification.
renderer = sub_once(
    renderer,
    r"    rawHarmonicMask_\.assign\(.*?    peakBins_\.reserve\(.*?\);\n",
    "    prefixSum_.assign(static_cast<std::size_t>(positiveBinCount + 1), 0.0);\n",
    "classifier storage",
)

# Prepare only coefficients that directly belong to envelope/formant rendering.
renderer = sub_once(
    renderer,
    r"    // FULL_SPECTRUM_SINGLE_TRANSPORT_V1\n.*?    reset\(\);",
    """    // FULL_SPECTRUM_SINGLE_TRANSPORT_V1
    // STABLE_SINGLE_LATTICE_TRANSPORT_V3
    // PURE_SINGLE_TRANSPORT_V4
    // MINIMAL_RENDERER_V5
    // One analysis FFT -> one spectral transport -> one IFFT/OLA. Voice-state
    // analysis is upstream and has no runtime hook into this renderer.
    const double envelopeUpdateSeconds = static_cast<double>(
        hopSize_ * envelopeUpdateInterval_) / sampleRate_;
    envelopeAttackCoefficient_ = static_cast<float>(
        1.0 - std::exp(-envelopeUpdateSeconds / 0.008));
    envelopeReleaseCoefficient_ = static_cast<float>(
        1.0 - std::exp(-envelopeUpdateSeconds / 0.035));
    formantReductionCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.004 * sampleRate_)));
    formantRecoveryCoefficient_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (0.028 * sampleRate_)));
    reset();""",
    "protective prepare block",
)

renderer = sub_once(
    renderer,
    r"void SingleWetSpectralRenderer::reset\(\) noexcept\s*\{.*?\n\}\n\ndouble SingleWetSpectralRenderer::wrapPhase",
    """void SingleWetSpectralRenderer::reset() noexcept
{
    std::fill(inputRing_.begin(), inputRing_.end(), 0.0f);
    std::fill(fftBuffer_.begin(), fftBuffer_.end(), Complex {});
    std::fill(magnitudes_.begin(), magnitudes_.end(), 0.0f);
    std::fill(analysisPhases_.begin(), analysisPhases_.end(), 0.0f);
    std::fill(previousMagnitudes_.begin(), previousMagnitudes_.end(), 0.0f);
    std::fill(previousAnalysisPhases_.begin(), previousAnalysisPhases_.end(), 0.0f);
    std::fill(trueSourceBins_.begin(), trueSourceBins_.end(), 0.0);
    std::fill(propagatedPhases_.begin(), propagatedPhases_.end(), 0.0);
    std::fill(logMagnitudes_.begin(), logMagnitudes_.end(), 0.0f);
    std::fill(rawSpectralEnvelope_.begin(), rawSpectralEnvelope_.end(), 1.0f);
    std::fill(spectralEnvelope_.begin(), spectralEnvelope_.end(), 1.0f);
    std::fill(prefixSum_.begin(), prefixSum_.end(), 0.0);
    std::fill(layer_.spectrum.begin(), layer_.spectrum.end(), Complex {});
    std::fill(layer_.synthesisPhases.begin(), layer_.synthesisPhases.end(), 0.0);
    clearLayerOutput(layer_);
    layer_.phaseInitialised = false;
    inputSampleCounter_ = 0;
    analysisPhaseInitialised_ = false;
    phaseResetPending_ = false;
    envelopeInitialised_ = false;
    envelopeFrameCounter_ = 0;
    smoothedFormantPreservation_ = 0.0f;
}

double SingleWetSpectralRenderer::wrapPhase""",
    "reset cleanup",
)

# Peak maps and all harmonic/noise helpers are physically deleted.
renderer = sub_once(
    renderer,
    r"void SingleWetSpectralRenderer::calculatePeakRegions\(\s*int positiveBins\) noexcept.*?float SingleWetSpectralRenderer::interpolateEnvelope",
    "float SingleWetSpectralRenderer::interpolateEnvelope",
    "peak-region removal",
)
renderer = sub_once(
    renderer,
    r"float SingleWetSpectralRenderer::binFrequency\(int bin\) const noexcept.*?void SingleWetSpectralRenderer::synthesiseLayer",
    "void SingleWetSpectralRenderer::synthesiseLayer",
    "harmonic/noise analyzer removal",
)

minimal_frame = r'''void SingleWetSpectralRenderer::processFrame(
    std::int64_t frameEndSample,
    double correctionCents,
    float formantPreservation) noexcept
{
    const std::int64_t frameStartSample = frameEndSample - frameSize_ + 1;
    for (int index = 0; index < frameSize_; ++index)
    {
        const float input = readInputSample(frameStartSample + index);
        fftBuffer_[static_cast<std::size_t>(index)] = Complex(
            input * window_[static_cast<std::size_t>(index)], 0.0f);
    }
    fft(fftBuffer_, false);

    const int positiveBins = frameSize_ / 2;
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        const Complex value = fftBuffer_[static_cast<std::size_t>(bin)];
        magnitudes_[static_cast<std::size_t>(bin)] = std::abs(value);
        analysisPhases_[static_cast<std::size_t>(bin)] =
            std::atan2(value.imag(), value.real());
    }

    if (!envelopeInitialised_
        || ++envelopeFrameCounter_ >= envelopeUpdateInterval_)
    {
        envelopeFrameCounter_ = 0;
        calculateEnvelope(positiveBins);
    }

    const bool resetAnalysis = phaseResetPending_ || !analysisPhaseInitialised_;
    phaseResetPending_ = false;
    const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)
                                    / static_cast<double>(frameSize_);
    const double binFromPhaseScale = static_cast<double>(frameSize_)
                                   / (twoPi * static_cast<double>(hopSize_));
    for (int sourceBin = 0; sourceBin <= positiveBins; ++sourceBin)
    {
        const double analysisPhase =
            analysisPhases_[static_cast<std::size_t>(sourceBin)];
        double trueSourceBin = static_cast<double>(sourceBin);
        if (!resetAnalysis)
        {
            const double expectedAdvance = expectedPhaseScale
                                         * static_cast<double>(sourceBin);
            const double phaseDeviation = wrapPhase(
                analysisPhase
                - static_cast<double>(previousAnalysisPhases_[
                    static_cast<std::size_t>(sourceBin)])
                - expectedAdvance);
            trueSourceBin += phaseDeviation * binFromPhaseScale;
        }
        trueSourceBins_[static_cast<std::size_t>(sourceBin)] = trueSourceBin;
    }

    synthesiseLayer(layer_, frameEndSample, correctionCents,
                    formantPreservation, resetAnalysis, positiveBins);
    for (int bin = 0; bin <= positiveBins; ++bin)
    {
        previousMagnitudes_[static_cast<std::size_t>(bin)] =
            magnitudes_[static_cast<std::size_t>(bin)];
        previousAnalysisPhases_[static_cast<std::size_t>(bin)] =
            analysisPhases_[static_cast<std::size_t>(bin)];
    }
    analysisPhaseInitialised_ = true;
}

float SingleWetSpectralRenderer::consumeLayerOutput'''
renderer = sub_once(
    renderer,
    r"void SingleWetSpectralRenderer::processFrame\(.*?float SingleWetSpectralRenderer::consumeLayerOutput",
    minimal_frame,
    "minimal processFrame",
)

minimal_sample = r'''float SingleWetSpectralRenderer::processSample(
    float inputSample,
    double correctionCents,
    float formantPreservation) noexcept
{
    inputSample = sanitiseAudioSample(inputSample);
    if (frameSize_ <= 0 || inputRing_.empty())
        return inputSample;
    const std::int64_t currentSample = inputSampleCounter_;
    inputRing_[static_cast<std::size_t>(currentSample & inputRingMask_)] = inputSample;
    const float target = clamp01(formantPreservation);
    const float coefficient = target < smoothedFormantPreservation_
        ? formantReductionCoefficient_ : formantRecoveryCoefficient_;
    smoothedFormantPreservation_ += coefficient
        * (target - smoothedFormantPreservation_);
    if (((currentSample + 1) % hopSize_) == 0)
        processFrame(currentSample, correctionCents, smoothedFormantPreservation_);
    const float shifted = consumeLayerOutput(layer_, currentSample);
    ++inputSampleCounter_;
    return sanitiseAudioSample(shifted);
}
float SingleWetSpectralRenderer::processBypassedSample'''
renderer = sub_once(
    renderer,
    r"float SingleWetSpectralRenderer::processSample\(float inputSample,double correctionCents,float formantPreservation,const Context& context\) noexcept.*?float SingleWetSpectralRenderer::processBypassedSample",
    minimal_sample,
    "sensor-free processSample",
)

# Voice evidence remains in ModernPitchEngine supervisor/detector state only.
engine = sub_once(
    engine,
    r"    const auto rendererContext = \[this, &safe\].*?    \};\n    for \(int sample",
    "    for (int sample",
    "rendererContext removal",
)
engine = sub_once(
    engine,
    r"                const double audible = decision\.controllerCents;\n                const auto context = rendererContext\(.*?safe\.formantPreservation, context\);",
    """                const double audible = decision.controllerCents;
                data[static_cast<std::size_t>(channel)][sample] =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audible,
                        safe.formantPreservation);""",
    "dual-mono sensor adapter removal",
)
engine = sub_once(
    engine,
    r"            audibleCorrectionCents_ = decision\.controllerCents;\n            const auto context = rendererContext\(latestObservation_, linkedCorrection_\);\n            for \(int channel = 0; channel < channels; \+\+channel\)\n                data\[static_cast<std::size_t>\(channel\)\]\[sample\] =\n                    wetRenderers_\[static_cast<std::size_t>\(channel\)\]\.processSample\(\n                        data\[static_cast<std::size_t>\(channel\)\]\[sample\], audibleCorrectionCents_,\n                        safe\.formantPreservation, context\);",
    """            audibleCorrectionCents_ = decision.controllerCents;
            for (int channel = 0; channel < channels; ++channel)
                data[static_cast<std::size_t>(channel)][sample] =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audibleCorrectionCents_,
                        safe.formantPreservation);""",
    "linked sensor adapter removal",
)

for term in (
    "updateHarmonicNoiseAnalysis", "harmonicMask_", "noiseDominanceMs_",
    "breathProtection_", "smoothedSpectralReliability_"
):
    if term in renderer:
        raise RuntimeError(f"protective renderer symbol remains: {term}")
if "rendererContext" in engine:
    raise RuntimeError("rendererContext remains in ModernPitchEngine")

renderer_path.write_text(renderer, encoding="utf-8")
engine_path.write_text(engine, encoding="utf-8")
print("applied V5: protective analysis physically removed from renderer runtime")
