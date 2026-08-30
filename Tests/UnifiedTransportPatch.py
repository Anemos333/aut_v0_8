from pathlib import Path
import re

renderer_path = Path("Source/SingleWetSpectralRenderer.cpp")
engine_path = Path("Source/ModernPitchEngine.cpp")
renderer = renderer_path.read_text(encoding="utf-8")
engine = engine_path.read_text(encoding="utf-8")

if "PURE_SINGLE_TRANSPORT_V4" not in renderer:
    raise RuntimeError("V4 pure transport must exist before V5 physical cleanup")

sentinel = "MINIMAL_RENDERER_V5"
if sentinel in renderer:
    print("V5 minimal renderer already applied")
    raise SystemExit(0)

# Remove all harmonic/noise classifier storage from prepare().
renderer, count = re.subn(
    r"    rawHarmonicMask_\.assign\(.*?    peakBins_\.reserve\(.*?\);\n",
    "",
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError(f"prepare classifier storage: expected one block, found {count}")

# Keep only the envelope/formant coefficients used by the actual renderer.
renderer, count = re.subn(
    r"    // FULL_SPECTRUM_SINGLE_TRANSPORT_V1\n.*?    reset\(\);",
    """    // FULL_SPECTRUM_SINGLE_TRANSPORT_V1
    // STABLE_SINGLE_LATTICE_TRANSPORT_V3
    // PURE_SINGLE_TRANSPORT_V4
    // MINIMAL_RENDERER_V5
    // The renderer owns only analysis FFT, one spectral transport, formant
    // shaping, one inverse FFT and one OLA. Voice-state classifiers live
    // upstream and have no runtime hook into this object.
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
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError(f"prepare protective coefficient block: expected one, found {count}")

# Replace reset() so no dead classifier state remains.
renderer, count = re.subn(
    r"void SingleWetSpectralRenderer::reset\(\) noexcept\n\{.*?\n\}\n\ndouble SingleWetSpectralRenderer::wrapPhase",
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
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError(f"reset cleanup: expected one block, found {count}")

# Peak-region tracking existed only to support the removed phase/classifier policy.
renderer, count = re.subn(
    r"void SingleWetSpectralRenderer::calculatePeakRegions\(int positiveBins\) noexcept.*?float SingleWetSpectralRenderer::interpolateEnvelope",
    "float SingleWetSpectralRenderer::interpolateEnvelope",
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError(f"peak-region removal: expected one block, found {count}")

# Remove all remaining protective analysis helpers in one contiguous block.
renderer, count = re.subn(
    r"float SingleWetSpectralRenderer::binFrequency\(int bin\) const noexcept.*?void SingleWetSpectralRenderer::synthesiseLayer",
    "void SingleWetSpectralRenderer::synthesiseLayer",
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError(f"harmonic/noise analyzer removal: expected one block, found {count}")

# Rebuild processFrame as the minimal audio path. No Context enters this method.
minimal_process_frame = r'''void SingleWetSpectralRenderer::processFrame(
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
renderer, count = re.subn(
    r"void SingleWetSpectralRenderer::processFrame\(.*?float SingleWetSpectralRenderer::consumeLayerOutput",
    minimal_process_frame,
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError(f"minimal processFrame: expected one block, found {count}")

# Remove Context from the sample API as well.
renderer, count = re.subn(
    r"float SingleWetSpectralRenderer::processSample\(float inputSample,double correctionCents,float formantPreservation,const Context& context\) noexcept\n\{.*?\n\}\nfloat SingleWetSpectralRenderer::processBypassedSample",
    """float SingleWetSpectralRenderer::processSample(
    float inputSample,
    double correctionCents,
    float formantPreservation) noexcept
{
    inputSample = sanitiseAudioSample(inputSample);
    if (frameSize_ <= 0 || inputRing_.empty())
        return inputSample;

    const std::int64_t currentSample = inputSampleCounter_;
    inputRing_[static_cast<std::size_t>(currentSample & inputRingMask_)] = inputSample;
    const float formantTarget = clamp01(formantPreservation);
    const float coefficient = formantTarget < smoothedFormantPreservation_
        ? formantReductionCoefficient_ : formantRecoveryCoefficient_;
    smoothedFormantPreservation_ += coefficient
        * (formantTarget - smoothedFormantPreservation_);

    if (((currentSample + 1) % hopSize_) == 0)
        processFrame(currentSample, correctionCents, smoothedFormantPreservation_);

    const float shifted = consumeLayerOutput(layer_, currentSample);
    ++inputSampleCounter_;
    return sanitiseAudioSample(shifted);
}
float SingleWetSpectralRenderer::processBypassedSample""",
    renderer,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError(f"processSample Context removal: expected one block, found {count}")

# Remove the renderer-context adapter from ModernPitchEngine. Voice evidence still
# supervises note state/target upstream; it is no longer passed to the renderer.
engine, count = re.subn(
    r"    const auto rendererContext = \[this, &safe\].*?    \};\n    for \(int sample",
    "    for (int sample",
    engine,
    count=1,
    flags=re.S,
)
if count != 1:
    raise RuntimeError(f"rendererContext removal: expected one block, found {count}")

old_dual = """                const double audible = decision.controllerCents;
                const auto context = rendererContext(
                    latestChannelObservation_[static_cast<std::size_t>(channel)], correction);
                data[static_cast<std::size_t>(channel)][sample] =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audible,
                        safe.formantPreservation, context);
"""
new_dual = """                const double audible = decision.controllerCents;
                data[static_cast<std::size_t>(channel)][sample] =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audible,
                        safe.formantPreservation);
"""
if engine.count(old_dual) != 1:
    raise RuntimeError(f"dual-mono renderer call: expected one match, found {engine.count(old_dual)}")
engine = engine.replace(old_dual, new_dual, 1)

old_linked = """            audibleCorrectionCents_ = decision.controllerCents;
            const auto context = rendererContext(latestObservation_, linkedCorrection_);
            for (int channel = 0; channel < channels; ++channel)
                data[static_cast<std::size_t>(channel)][sample] =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audibleCorrectionCents_,
                        safe.formantPreservation, context);
"""
new_linked = """            audibleCorrectionCents_ = decision.controllerCents;
            for (int channel = 0; channel < channels; ++channel)
                data[static_cast<std::size_t>(channel)][sample] =
                    wetRenderers_[static_cast<std::size_t>(channel)].processSample(
                        data[static_cast<std::size_t>(channel)][sample], audibleCorrectionCents_,
                        safe.formantPreservation);
"""
if engine.count(old_linked) != 1:
    raise RuntimeError(f"linked renderer call: expected one match, found {engine.count(old_linked)}")
engine = engine.replace(old_linked, new_linked, 1)

for forbidden in (
    "updateHarmonicNoiseAnalysis",
    "harmonicMask_",
    "noiseDominanceMs_",
    "breathProtection_",
    "smoothedSpectralReliability_",
    "rendererContext",
):
    if forbidden in renderer or (forbidden == "rendererContext" and forbidden in engine):
        raise RuntimeError(f"protective runtime symbol remains: {forbidden}")

renderer_path.write_text(renderer, encoding="utf-8")
engine_path.write_text(engine, encoding="utf-8")
print("applied V5: protective analysis physically removed from renderer runtime")
