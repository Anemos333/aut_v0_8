from pathlib import Path

header_path = Path('Source/SingleWetSpectralRenderer.h')
source_path = Path('Source/SingleWetSpectralRenderer.cpp')

header = header_path.read_text(encoding='utf-8')
source = source_path.read_text(encoding='utf-8')

HEADER_METHOD_OLD = """    [[nodiscard]] float interpolateEnvelope(double binPosition) const noexcept;\n    void calculateEnvelope(int positiveBins) noexcept;\n"""
HEADER_METHOD_NEW = """    [[nodiscard]] float interpolateEnvelope(double binPosition) const noexcept;\n    void calculateEnvelope(int positiveBins) noexcept;\n    void calculatePeakRegions(int positiveBins) noexcept;\n"""

HEADER_STATE_OLD = """    std::vector<float> spectralEnvelope_;\n    std::vector<double> prefixSum_;\n\n    SynthesisLayer layer_;\n"""
HEADER_STATE_NEW = """    std::vector<float> spectralEnvelope_;\n    std::vector<double> prefixSum_;\n    std::vector<int> nearestPeak_;\n    std::vector<int> peakBins_;\n\n    SynthesisLayer layer_;\n"""

SOURCE_PREP_OLD = """    spectralEnvelope_.assign(static_cast<std::size_t>(positiveBinCount), 1.0f);\n    prefixSum_.assign(static_cast<std::size_t>(positiveBinCount + 1), 0.0);\n\n    layer_.spectrum.assign(static_cast<std::size_t>(frameSize_), Complex {});\n"""
SOURCE_PREP_NEW = """    spectralEnvelope_.assign(static_cast<std::size_t>(positiveBinCount), 1.0f);\n    prefixSum_.assign(static_cast<std::size_t>(positiveBinCount + 1), 0.0);\n    nearestPeak_.assign(static_cast<std::size_t>(positiveBinCount), 0);\n    peakBins_.clear();\n    peakBins_.reserve(static_cast<std::size_t>(positiveBinCount));\n\n    layer_.spectrum.assign(static_cast<std::size_t>(frameSize_), Complex {});\n"""

SOURCE_RESET_OLD = """    std::fill(spectralEnvelope_.begin(), spectralEnvelope_.end(), 1.0f);\n    std::fill(prefixSum_.begin(), prefixSum_.end(), 0.0);\n    std::fill(layer_.spectrum.begin(), layer_.spectrum.end(), Complex {});\n"""
SOURCE_RESET_NEW = """    std::fill(spectralEnvelope_.begin(), spectralEnvelope_.end(), 1.0f);\n    std::fill(prefixSum_.begin(), prefixSum_.end(), 0.0);\n    std::fill(nearestPeak_.begin(), nearestPeak_.end(), 0);\n    peakBins_.clear();\n    std::fill(layer_.spectrum.begin(), layer_.spectrum.end(), Complex {});\n"""

SOURCE_FUNCTION_ANCHOR = """float SingleWetSpectralRenderer::interpolateEnvelope(\n    double binPosition) const noexcept\n"""
SOURCE_FUNCTION_INSERT = r'''void SingleWetSpectralRenderer::calculatePeakRegions(
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

    // TIMBRE_PHASE_LOCK_V1: only genuine local spectral peaks may become phase
    // anchors. The threshold deliberately ignores diffuse low-level air/noise;
    // those bins retain their own propagated phase below.
    const float threshold = maximumMagnitude * 0.018f;
    for (int bin = 1; bin < positiveBins; ++bin)
    {
        const float centre = magnitudes_[static_cast<std::size_t>(bin)];
        if (centre >= threshold
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

SOURCE_PHASE_OLD = """        const double outputPhase = propagatedPhases_[sourceIndex];\n\n        const float sourceEnvelope = std::max(\n"""
SOURCE_PHASE_NEW = """        // TIMBRE_PHASE_LOCK_V1: local identity phase locking reduces\n        // phase-vocoder metallicity without changing magnitude transport, target\n        // position, correction ratio, FFT count or the single wet path. Only\n        // leakage bins immediately adjacent to a strong local spectral peak are\n        // locked; diffuse/noise bins keep their independently propagated phase.\n        const int peak = nearestPeak_.empty()\n            ? sourceBin\n            : nearestPeak_[sourceIndex];\n        const int lockRadiusBins = frameSize_ >= 512 ? 2 : 1;\n        const bool usePeakPhase = peak >= 0 && peak <= positiveBins\n            && std::abs(peak - sourceBin) <= lockRadiusBins;\n        const double relativeAnalysisPhase = usePeakPhase\n            ? wrapPhase(static_cast<double>(analysisPhases_[sourceIndex])\n                - static_cast<double>(analysisPhases_[static_cast<std::size_t>(peak)]))\n            : 0.0;\n        const double outputPhase = usePeakPhase\n            ? propagatedPhases_[static_cast<std::size_t>(peak)] + relativeAnalysisPhase\n            : propagatedPhases_[sourceIndex];\n\n        const float sourceEnvelope = std::max(\n"""

SOURCE_FRAME_OLD = """    if (!envelopeInitialised_\n        || ++envelopeFrameCounter_ >= envelopeUpdateInterval_)\n    {\n        envelopeFrameCounter_ = 0;\n        calculateEnvelope(positiveBins);\n    }\n\n    const bool resetAnalysis = phaseResetPending_ || !analysisPhaseInitialised_;\n"""
SOURCE_FRAME_NEW = """    if (!envelopeInitialised_\n        || ++envelopeFrameCounter_ >= envelopeUpdateInterval_)\n    {\n        envelopeFrameCounter_ = 0;\n        calculateEnvelope(positiveBins);\n    }\n    calculatePeakRegions(positiveBins);\n\n    const bool resetAnalysis = phaseResetPending_ || !analysisPhaseInitialised_;\n"""

replacements = [
    ('header method', header_path, HEADER_METHOD_OLD, HEADER_METHOD_NEW),
    ('header state', header_path, HEADER_STATE_OLD, HEADER_STATE_NEW),
    ('source prepare', source_path, SOURCE_PREP_OLD, SOURCE_PREP_NEW),
    ('source reset', source_path, SOURCE_RESET_OLD, SOURCE_RESET_NEW),
    ('source phase', source_path, SOURCE_PHASE_OLD, SOURCE_PHASE_NEW),
    ('source frame', source_path, SOURCE_FRAME_OLD, SOURCE_FRAME_NEW),
]

texts = {header_path: header, source_path: source}
for name, path, old, new in replacements:
    text = texts[path]
    if new in text:
        continue
    if old not in text:
        raise RuntimeError(f'{name}: expected source anchor not found')
    texts[path] = text.replace(old, new, 1)

text = texts[source_path]
if SOURCE_FUNCTION_INSERT not in text:
    if SOURCE_FUNCTION_ANCHOR not in text:
        raise RuntimeError('calculatePeakRegions insertion anchor not found')
    text = text.replace(SOURCE_FUNCTION_ANCHOR,
                        SOURCE_FUNCTION_INSERT + SOURCE_FUNCTION_ANCHOR,
                        1)
    texts[source_path] = text

for path, text in texts.items():
    path.write_text(text, encoding='utf-8')

print('TIMBRE_PHASE_LOCK_V1 materialized')
