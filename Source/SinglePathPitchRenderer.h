#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace neumaton::render
{

// One and only one audible path.
//
// Pitch is produced by a single fractional read head over one input history.
// There is no dry branch, no wet/dry blend, no OLA, no grains and no band
// reconstruction. When the read head must be recentered, candidate positions
// are inspected only as measurements; exactly one position is selected and
// exactly one interpolated sample reaches the output.
class SinglePathPitchRenderer final
{
public:
    static constexpr int maxChannels = 2;

    void prepare(double sampleRate, int channels, int latencySamples);
    void reset() noexcept;

    void processFrame(const float* input,
                      float* output,
                      int channels,
                      double correctionCents) noexcept;

    [[nodiscard]] int latencySamples() const noexcept { return latencySamples_; }
    [[nodiscard]] std::uint64_t spliceCount() const noexcept { return spliceCount_; }
    [[nodiscard]] float lastSpliceMismatch() const noexcept { return lastSpliceMismatch_; }

private:
    // Splice selection is measurement-only, but it still needs enough waveform
    // context to distinguish neighbouring vocal cycles. Eight samples (~0.17 ms
    // at 48 kHz) could match a local tangent while choosing the wrong cycle.
    // 64 samples (~1.33 ms) remain short and cheap while making the match depend
    // on meaningful waveform shape rather than a handful of adjacent samples.
    static constexpr int historySize = 64;

    double sampleRate_ = 48000.0;
    int channels_ = 1;
    int latencySamples_ = 256;
    int ringSize_ = 4096;
    int ringMask_ = 4095;

    std::array<std::vector<float>, maxChannels> ring_;
    std::array<std::array<float, historySize>, maxChannels> outputHistory_ {};

    int historyWrite_ = 0;
    int historyAvailable_ = 0;
    std::int64_t writeSample_ = 0;
    double readPosition_ = 0.0;
    bool primed_ = false;

    std::uint64_t spliceCount_ = 0;
    float lastSpliceMismatch_ = 0.0f;

    [[nodiscard]] float readInterpolated(int channel,
                                         double absolutePosition) const noexcept;
    [[nodiscard]] float readInteger(int channel,
                                    std::int64_t absolutePosition) const noexcept;
    [[nodiscard]] double spliceScore(double candidateReadPosition) const noexcept;
    void recenterReadHead(double ratio) noexcept;
};

} // namespace neumaton::render
