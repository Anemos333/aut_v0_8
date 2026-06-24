#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>
#include <type_traits>
#include <vector>

namespace juce {

template <typename SampleType>
class AudioBuffer {
public:
    AudioBuffer() = default;

    AudioBuffer(int channels, int samples) { setSize(channels, samples); }

    AudioBuffer(SampleType* const* dataToReferTo, int channels, int samples)
        : numChannels_(std::max(0, channels)), numSamples_(std::max(0, samples)), external_(true)
    {
        pointers_.resize(static_cast<std::size_t>(numChannels_));
        for (int ch = 0; ch < numChannels_; ++ch)
            pointers_[static_cast<std::size_t>(ch)] = dataToReferTo[ch];
    }

    AudioBuffer(SampleType* const* dataToReferTo, int channels, int startSample, int samples)
        : numChannels_(std::max(0, channels)), numSamples_(std::max(0, samples)), external_(true)
    {
        pointers_.resize(static_cast<std::size_t>(numChannels_));
        for (int ch = 0; ch < numChannels_; ++ch)
            pointers_[static_cast<std::size_t>(ch)] = dataToReferTo[ch] + startSample;
    }

    void setSize(int channels, int samples)
    {
        numChannels_ = std::max(0, channels);
        numSamples_ = std::max(0, samples);
        external_ = false;
        owned_.assign(static_cast<std::size_t>(numChannels_),
                      std::vector<SampleType>(static_cast<std::size_t>(numSamples_), SampleType{}));
        refreshPointers();
    }

    int getNumChannels() const noexcept { return numChannels_; }
    int getNumSamples() const noexcept { return numSamples_; }

    SampleType* getWritePointer(int channel) noexcept { return pointers_[static_cast<std::size_t>(channel)]; }
    const SampleType* getReadPointer(int channel) const noexcept { return pointers_[static_cast<std::size_t>(channel)]; }

    SampleType** getArrayOfWritePointers() noexcept { return pointers_.data(); }
    SampleType* const* getArrayOfWritePointers() const noexcept { return pointers_.data(); }

    void clear()
    {
        for (int ch = 0; ch < numChannels_; ++ch)
            std::fill_n(getWritePointer(ch), numSamples_, SampleType{});
    }

    void clear(int channel, int startSample, int numSamples)
    {
        std::fill_n(getWritePointer(channel) + startSample, numSamples, SampleType{});
    }

    void copyFrom(int destChannel, int destStartSample,
                  const AudioBuffer& source, int sourceChannel, int sourceStartSample,
                  int numSamples)
    {
        std::copy_n(source.getReadPointer(sourceChannel) + sourceStartSample,
                    numSamples,
                    getWritePointer(destChannel) + destStartSample);
    }

    void makeCopyOf(const AudioBuffer& source)
    {
        setSize(source.getNumChannels(), source.getNumSamples());
        for (int ch = 0; ch < numChannels_; ++ch)
            std::copy_n(source.getReadPointer(ch), numSamples_, getWritePointer(ch));
    }

private:
    void refreshPointers()
    {
        pointers_.resize(static_cast<std::size_t>(numChannels_));
        for (int ch = 0; ch < numChannels_; ++ch)
            pointers_[static_cast<std::size_t>(ch)] = owned_[static_cast<std::size_t>(ch)].data();
    }

    int numChannels_ = 0;
    int numSamples_ = 0;
    bool external_ = false;
    std::vector<std::vector<SampleType>> owned_;
    std::vector<SampleType*> pointers_;
};

} // namespace juce
