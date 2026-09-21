#pragma once

#include <complex>
#include <cstdint>
#include <vector>

class SingleWetSpectralRenderer final
{
public:
    void prepare(double sampleRate, int frameSize);
    void reset() noexcept;

    [[nodiscard]] float processSample(float inputSample,
                                      double correctionCents,
                                      float formantPreservation) noexcept;
    [[nodiscard]] float processBypassedSample(float inputSample) noexcept;
    [[nodiscard]] int getLatencySamples() const noexcept { return frameSize_; }

    struct Diagnostics
    {
        bool valid = false;
        int frameCount = 0;
        std::int64_t worstFrameEndSample = 0;
        float minimumPreIfftCoherence = 1.0f;
        float minimumStrongBinCoherence = 1.0f;
        float maximumDominantRidgeSpreadBins = 0.0f;

        // OLA_ACCUMULATION_DIAGNOSTIC_V2
        // Shadow-only measurements. No value here has synthesis authority.
        bool olaValid = false;
        std::int64_t worstOlaHopEndSample = 0;

        // Hop-energy ratio: E(sum of overlapping frames) / sum(E(each frame)).
        // Unlike the old sample-wise ratio this cannot false-trigger merely at
        // an ordinary zero crossing. Values below 1 mean net cancellation;
        // values above 1 mean net reinforcement.
        float minimumOlaEnergyRatio = 4.0f;

        float minimumOlaCoverageRatio = 1.0f;
        int minimumOlaContributionCount = 4;

        // Correlation of a newly synthesised frame with the OLA that was already
        // scheduled before that frame was added. -1 is fully antagonistic,
        // 0 unrelated, +1 aligned.
        bool frameOverlapCorrelationValid = false;
        float minimumFrameOverlapCorrelation = 1.0f;
        std::int64_t worstFrameCorrelationEndSample = 0;
    };

    // Diagnostic-only readback. Called by ModernPitchEngine on the audio thread
    // after a block; it has no authority over synthesis and resets only the
    // diagnostic block accumulator.
    [[nodiscard]] Diagnostics consumeDiagnostics() noexcept;

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

    void processFrame(std::int64_t frameEndSample,
                      double correctionCents,
                      float formantPreservation) noexcept;
    void synthesiseLayer(SynthesisLayer& layer,
                         std::int64_t frameEndSample,
                         double correctionCents,
                         float formantPreservation,
                         bool resetPhases,
                         int positiveBins) noexcept;
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

    double sampleRate_ = 48000.0;
    int frameSize_ = 0;
    int hopSize_ = 0;

    std::vector<float> inputRing_;
    int inputRingMask_ = 0;
    int outputRingMask_ = 0;

    std::vector<float> window_;
    std::vector<int> fftBitReversal_;
    std::vector<Complex> fftTwiddles_;
    std::vector<float> sineTable_;
    std::vector<float> formantGainTable_;

    std::vector<Complex> fftBuffer_;
    std::vector<float> magnitudes_;
    std::vector<float> analysisPhases_;
    std::vector<float> previousAnalysisPhases_;
    std::vector<double> trueSourceBins_;
    std::vector<double> propagatedPhases_;
    std::vector<float> logMagnitudes_;
    std::vector<float> rawSpectralEnvelope_;
    std::vector<float> spectralEnvelope_;
    std::vector<double> prefixSum_;

    SynthesisLayer layer_;
    std::int64_t inputSampleCounter_ = 0;
    bool analysisPhaseInitialised_ = false;
    bool phaseResetPending_ = false;
    bool envelopeInitialised_ = false;
    int envelopeFrameCounter_ = 0;
    int envelopeUpdateInterval_ = 2;
    float synthesisGain_ = 0.5f;
     float envelopeAttackCoefficient_ = 1.0f;
    float envelopeReleaseCoefficient_ = 1.0f;
    float smoothedFormantPreservation_ = 0.0f;
    float formantReductionCoefficient_ = 1.0f;
    float formantRecoveryCoefficient_ = 1.0f;

    Diagnostics diagnosticBlock_ {};
};
