#pragma once

#include "PitchCore.h"
#include "PitchCorrectionTrajectory.h"
#include "SinglePathPitchRenderer.h"

#include <cstdint>

namespace neumaton
{

// Minimal serial V1 engine:
// input -> PitchCore measurement -> scale target/trajectory -> one renderer -> output.
// Analysis may inspect audio, but there is one and only one audible path.
class PitchEngineV1 final
{
public:
    struct Metering
    {
        pitch::PitchResult pitch;
        double targetPitchHz = 0.0;
        double correctionCents = 0.0;
        std::uint64_t rendererSplices = 0;
    };

    void prepare(double sampleRate,
                 int channels,
                 pitch::LatencyMode mode,
                 float minimumPitchHz = 45.0f,
                 float maximumPitchHz = 1600.0f) noexcept;

    void reset() noexcept;

    bool setScale(const double* ratios,
                  int count,
                  double referenceHz,
                  double equaveRatio = 2.0) noexcept;

    void processFrame(const float* input,
                      float* output,
                      int channels,
                      float speedMs,
                      float amount,
                      float humanize) noexcept;

    [[nodiscard]] int latencySamples() const noexcept
    {
        return pitch::declaredLatencySamples(mode_);
    }

    [[nodiscard]] pitch::LatencyMode latencyMode() const noexcept
    {
        return mode_;
    }

    [[nodiscard]] Metering metering() const noexcept
    {
        return metering_;
    }

private:
    double sampleRate_ = 48000.0;
    int channels_ = 1;
    pitch::LatencyMode mode_ = pitch::LatencyMode::live256;

    pitch::PitchCore pitchCore_;
    pitch::ScaleQuantizer quantizer_;
    render::PitchCorrectionTrajectory trajectory_;
    render::SinglePathPitchRenderer renderer_;
    Metering metering_ {};
};

} // namespace neumaton
