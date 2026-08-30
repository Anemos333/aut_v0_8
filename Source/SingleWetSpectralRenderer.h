#pragma once
#include <algorithm>
#include <array>
#include <complex>
#include <cstdint>
#include <vector>

class SingleWetSpectralRenderer final
{
public:
    struct Context
    {
        float detectedPitchHz = 0.0f;
        float confidence = 0.0f;
        float voicing = 0.0f;
        float consensus = 0.0f;
        float onsetStrength = 0.0f;
        float breathReduction = 0.0f;
        float noteAgeSeconds = 0.0f;
        float noteBodyConfidence = 0.0f;
        bool noteBodyLatched = false;
        bool pitchAnchorFresh = false;
        bool stableMusicalBody = false;
        bool transitionBody = false;
    };
    void prepare(double sampleRate, int frameSize);
    void reset() noexcept;
    [[nodiscard]] float processSample(float inputSample, double correctionCents,
                                      float formantPreservation,
                                      const Context& context) noexcept;
    [[nodiscard]] float processBypassedSample(float inputSample) noexcept;
    [[nodiscard]] int getLatencySamples() const noexcept { return frameSize_; }
private:
    using Complex = std::complex<float>;
    static constexpr int sineTableSize = 4096;
    static constexpr int formantRatioTableSize = 256;
    static constexpr int formantLevelCount = 32;
    struct SynthesisLayer
    {
        std::vector<Complex> spectrum;
        std::vector<double> synthesisPhases;
        std::vector<float> outputAccumulationRing;
        bool phaseInitialised = false;
    };
    void processFrame(std::int64_t frameEndSample, double correctionCents,
                      float formantPreservation, const Context& context) noexcept;
    void synthesiseLayer(SynthesisLayer& layer, std::int64_t frameEndSample,
                         double correctionCents, float formantPreservation,
                         bool resetPhases, int positiveBins) noexcept;
    void clearLayerOutput(SynthesisLayer& layer) noexcept;
    [[nodiscard]] float consumeLayerOutput(SynthesisLayer& layer,
                                           std::int64_t sample) noexcept;
    void fft(std::vector<Complex>& data, bool inverse) noexcept;
    [[nodiscard]] static double wrapPhase(double phase) noexcept;
    void fastSinCos(double phase, float& sine, float& cosine) const noexcept;
    [[nodiscard]] float lookupFormantGain(float envelopeRatio,
                                          float formantAmount) const noexcept;
    [[nodiscard]] float readInputSample(std::int64_t absoluteSample) const noexcept;
    [[nodiscard]] float interpolateEnvelope(double binPosition) const noexcept;
    void calculateEnvelope(int positiveBins) noexcept;
    void calculatePeakRegions(int positiveBins) noexcept;
    void updateHarmonicNoiseAnalysis(int positiveBins, float spectralFlux,
                                     const Context& context) noexcept;
    [[nodiscard]] float binFrequency(int bin) const noexcept;
    [[nodiscard]] float calculateHighBandFlatness(int firstBin,
                                                  int lastBin) const noexcept;
    struct AnalysisProfile
    {
        float combWeight=0.46f, peakWeight=0.25f, phaseWeight=0.21f, periodicWeight=0.08f;
        float bodyFloorBase=0.10f, bodyFloorTracking=0.88f, bodyUpperHz=4600.0f;
        float maskAttackMs=9.0f, maskReleaseMs=38.0f, maskRisePerSecond=34.0f, maskFallPerSecond=13.0f;
        float breathAttackMs=24.0f, breathReleaseMs=140.0f;
        float metricAttackMs=18.0f, metricReleaseMs=95.0f;
        float polyphonyAttackMs=28.0f, polyphonyReleaseMs=180.0f;
        float reliabilityAttackMs=22.0f, reliabilityReleaseMs=120.0f;
        float breathPersistenceStartMs=25.0f, breathPersistenceFullMs=130.0f;
        float noiseDominanceStartMs=35.0f, noiseDominanceFullMs=180.0f;
        float noiseDominanceThreshold=0.80f, maximumNoiseReductionDb=12.0f;
        float unresolvedCombBlend=0.35f, breathMaskBodyReduction=0.10f;
        float breathMaskAirReduction=0.62f, polyphonyTrust=1.0f;
    };
    double sampleRate_=48000.0; int frameSize_=0, hopSize_=0;
    std::vector<float> inputRing_; int inputRingMask_=0, outputRingMask_=0;
    std::vector<float> window_; std::vector<int> fftBitReversal_;
    std::vector<Complex> fftTwiddles_; std::vector<float> sineTable_, formantGainTable_;
    std::vector<Complex> fftBuffer_; std::vector<float> magnitudes_, analysisPhases_, previousMagnitudes_, previousAnalysisPhases_;
    std::vector<double> trueSourceBins_, propagatedPhases_; std::vector<float> logMagnitudes_, rawSpectralEnvelope_, spectralEnvelope_;
    std::vector<float> rawHarmonicMask_, harmonicMask_, harmonicMaskScratch_;
    std::vector<double> prefixSum_; std::vector<int> nearestPeak_, peakBins_;
    SynthesisLayer layer_; std::int64_t inputSampleCounter_=0;
    bool analysisPhaseInitialised_=false, phaseResetPending_=false, envelopeInitialised_=false;
    int envelopeFrameCounter_=0, envelopeUpdateInterval_=2; float synthesisGain_=0.5f;
    float envelopeAttackCoefficient_=1.0f, envelopeReleaseCoefficient_=1.0f;
    float smoothedFormantPreservation_=0.0f, formantReductionCoefficient_=1.0f, formantRecoveryCoefficient_=1.0f;
    float smoothedBreathiness_=0.0f, smoothedHarmonicity_=1.0f, smoothedNoisePathAmount_=0.0f, smoothedNoiseGain_=1.0f;
    float currentNoiseReductionDb_=0.0f, smoothedPolyphony_=0.0f, smoothedSpectralReliability_=1.0f, smoothedMaskStability_=1.0f;
    float breathProtection_=0.0f, breathAttackCoefficient_=1.0f, breathReleaseCoefficient_=1.0f;
    float maskAttackCoefficient_=1.0f, maskReleaseCoefficient_=1.0f, metricAttackCoefficient_=1.0f, metricReleaseCoefficient_=1.0f;
    float noiseReductionAttackCoefficient_=1.0f, noiseReductionReleaseCoefficient_=1.0f, transientNoiseRestoreCoefficient_=1.0f;
    float polyphonyAttackCoefficient_=1.0f, polyphonyReleaseCoefficient_=1.0f, reliabilityAttackCoefficient_=1.0f, reliabilityReleaseCoefficient_=1.0f;
    float breathPersistenceMs_=0.0f, noiseDominanceMs_=0.0f, maskRiseLimitPerFrame_=1.0f, maskFallLimitPerFrame_=1.0f;
    AnalysisProfile profile_{};
};
