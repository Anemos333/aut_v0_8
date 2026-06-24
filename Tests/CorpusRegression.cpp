#include "../Source/ModernPitchEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
struct WavData
{
    int channels = 0;
    int sampleRate = 0;
    std::vector<std::vector<float>> samples;
};

std::uint16_t readU16(std::istream& stream)
{
    std::array<unsigned char, 2> b {};
    stream.read(reinterpret_cast<char*>(b.data()), 2);
    return static_cast<std::uint16_t>(b[0] | (static_cast<unsigned>(b[1]) << 8));
}

std::uint32_t readU32(std::istream& stream)
{
    std::array<unsigned char, 4> b {};
    stream.read(reinterpret_cast<char*>(b.data()), 4);
    return static_cast<std::uint32_t>(b[0] | (static_cast<unsigned>(b[1]) << 8)
        | (static_cast<unsigned>(b[2]) << 16) | (static_cast<unsigned>(b[3]) << 24));
}

bool readWav(const fs::path& path, WavData& result, double maximumSeconds)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return false;
    char riff[4] {};
    stream.read(riff, 4);
    if (std::memcmp(riff, "RIFF", 4) != 0 && std::memcmp(riff, "RF64", 4) != 0)
        return false;
    static_cast<void>(readU32(stream));
    char wave[4] {};
    stream.read(wave, 4);
    if (std::memcmp(wave, "WAVE", 4) != 0)
        return false;

    std::uint16_t format = 0, channels = 0, bits = 0;
    std::uint32_t sampleRate = 0;
    std::vector<unsigned char> data;
    while (stream && (format == 0 || data.empty()))
    {
        char id[4] {};
        stream.read(id, 4);
        if (stream.gcount() != 4)
            break;
        const std::uint32_t size = readU32(stream);
        const std::streampos chunkStart = stream.tellg();
        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16)
        {
            format = readU16(stream);
            channels = readU16(stream);
            sampleRate = readU32(stream);
            static_cast<void>(readU32(stream));
            static_cast<void>(readU16(stream));
            bits = readU16(stream);
            if (format == 0xfffe && size >= 40)
            {
                static_cast<void>(readU16(stream));
                static_cast<void>(readU16(stream));
                static_cast<void>(readU32(stream));
                format = readU16(stream); // first word of sub-format GUID
            }
        }
        else if (std::memcmp(id, "data", 4) == 0)
        {
            const std::uint64_t bytesPerSample = std::max<std::uint64_t>(1, bits / 8);
            const std::uint64_t maxBytes = sampleRate > 0 && channels > 0
                ? static_cast<std::uint64_t>(maximumSeconds * sampleRate)
                    * channels * bytesPerSample
                : size;
            const std::size_t bytes = static_cast<std::size_t>(
                std::min<std::uint64_t>(size, maxBytes));
            data.resize(bytes);
            stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(bytes));
        }
        stream.clear();
        stream.seekg(chunkStart + static_cast<std::streamoff>(size + (size & 1u)));
    }

    if (channels == 0 || channels > 2 || sampleRate == 0 || data.empty())
        return false;
    const int bytesPerSample = static_cast<int>(bits / 8);
    if (bytesPerSample <= 0)
        return false;
    const std::size_t frames = data.size() /
        (static_cast<std::size_t>(channels) * static_cast<std::size_t>(bytesPerSample));
    if (frames == 0)
        return false;

    result.channels = static_cast<int>(channels);
    result.sampleRate = static_cast<int>(sampleRate);
    result.samples.assign(channels, std::vector<float>(frames, 0.0f));
    const unsigned char* cursor = data.data();
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        for (int channel = 0; channel < static_cast<int>(channels); ++channel)
        {
            float value = 0.0f;
            if (format == 1 && bits == 16)
            {
                const std::int16_t x = static_cast<std::int16_t>(cursor[0] | (cursor[1] << 8));
                value = static_cast<float>(x) / 32768.0f;
            }
            else if (format == 1 && bits == 24)
            {
                std::int32_t x = static_cast<std::int32_t>(cursor[0]
                    | (static_cast<unsigned>(cursor[1]) << 8)
                    | (static_cast<unsigned>(cursor[2]) << 16));
                if ((x & 0x00800000) != 0)
                    x |= static_cast<std::int32_t>(0xff000000);
                value = static_cast<float>(x) / 8388608.0f;
            }
            else if (format == 1 && bits == 32)
            {
                std::int32_t x = static_cast<std::int32_t>(cursor[0]
                    | (static_cast<unsigned>(cursor[1]) << 8)
                    | (static_cast<unsigned>(cursor[2]) << 16)
                    | (static_cast<unsigned>(cursor[3]) << 24));
                value = static_cast<float>(static_cast<double>(x) / 2147483648.0);
            }
            else if (format == 3 && bits == 32)
            {
                std::memcpy(&value, cursor, sizeof(float));
            }
            else
            {
                return false;
            }
            result.samples[static_cast<std::size_t>(channel)][frame] =
                std::isfinite(value) ? value : 0.0f;
            cursor += bytesPerSample;
        }
    }
    return true;
}

std::array<double, 12> chromatic()
{
    std::array<double, 12> result {};
    for (int i = 0; i < 12; ++i)
        result[static_cast<std::size_t>(i)] = std::exp2(static_cast<double>(i) / 12.0);
    return result;
}

struct Metrics
{
    bool finite = true;
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
    double inputRms = 0.0;
    double outputRms = 0.0;
    float pitchMedian = 0.0f;
    float confidenceMedian = 0.0f;
    float lowPitchFraction = 0.0f;
    float widthCorrelationIn = 0.0f;
    float widthCorrelationOut = 0.0f;
};

double channelRms(const std::vector<std::vector<float>>& audio, int start, int end = -1)
{
    long double sum = 0.0;
    std::uint64_t count = 0;
    for (const auto& channel : audio)
    {
        const int stop = end < 0 ? static_cast<int>(channel.size())
                                 : std::min(end, static_cast<int>(channel.size()));
        for (int i = std::max(0, start); i < stop; ++i)
        {
            sum += static_cast<long double>(channel[static_cast<std::size_t>(i)])
                 * channel[static_cast<std::size_t>(i)];
            ++count;
        }
    }
    return count > 0 ? std::sqrt(static_cast<double>(sum / count)) : 0.0;
}

float stereoCorrelation(const std::vector<std::vector<float>>& audio, int start, int end = -1)
{
    if (audio.size() < 2)
        return 1.0f;
    long double aa = 0.0, bb = 0.0, ab = 0.0;
    const int stop = end < 0 ? static_cast<int>(audio[0].size())
                             : std::min(end, static_cast<int>(audio[0].size()));
    for (int i = std::max(0, start); i < stop; ++i)
    {
        const auto index = static_cast<std::size_t>(i);
        aa += static_cast<long double>(audio[0][index]) * audio[0][index];
        bb += static_cast<long double>(audio[1][index]) * audio[1][index];
        ab += static_cast<long double>(audio[0][index]) * audio[1][index];
    }
    return static_cast<float>(ab / std::sqrt(std::max<long double>(1.0e-30L, aa * bb)));
}

float median(std::vector<float> values)
{
    if (values.empty())
        return 0.0f;
    const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

Metrics process(WavData input, int mode, float amount)
{
    Metrics metrics;
    for (const auto& channel : input.samples)
        for (float value : channel)
            metrics.inputPeak = std::max(metrics.inputPeak, std::abs(value));

    ModernPitchEngine engine;
    engine.prepare(input.sampleRate, 4096, input.channels,
                   static_cast<ModernPitchEngine::LatencyMode>(mode));
    const int latency = engine.getLatencySamples();
    const int totalSamples = static_cast<int>(input.samples[0].size());
    const int comparisonStart = std::min(1024, std::max(0, totalSamples / 4));
    const int inputEnd = std::max(comparisonStart, totalSamples - latency);
    metrics.inputRms = channelRms(input.samples, comparisonStart, inputEnd);
    metrics.widthCorrelationIn = stereoCorrelation(input.samples, comparisonStart, inputEnd);
    ModernPitchEngine::Parameters parameters;
    parameters.amount = amount;
    parameters.minimumPitchHz = 35.0f;
    parameters.maximumPitchHz = 1600.0f;
    parameters.retuneTimeMs = 8.0f;
    const auto scale = chromatic();
    std::array<float*, 2> pointers {};
    std::vector<float> pitches;
    std::vector<float> confidences;
    int lowPitchFrames = 0;
    int voicedFrames = 0;
    std::mt19937 random(0x731u + static_cast<unsigned>(mode));
    std::uniform_int_distribution<int> blockDistribution(1, 4096);
    int offset = 0;
    while (offset < totalSamples)
    {
        const int count = std::min(blockDistribution(random), totalSamples - offset);
        for (int channel = 0; channel < input.channels; ++channel)
            pointers[static_cast<std::size_t>(channel)] =
                input.samples[static_cast<std::size_t>(channel)].data() + offset;
        juce::AudioBuffer<float> block(pointers.data(), input.channels, count);
        engine.process(block, scale.data(), static_cast<int>(scale.size()), 261.625565, parameters);
        const auto meter = engine.getMetering();
        if (meter.detectedPitchHz > 0.0f && meter.confidence > 0.25f)
        {
            pitches.push_back(meter.detectedPitchHz);
            confidences.push_back(meter.confidence);
            ++voicedFrames;
            if (meter.detectedPitchHz < 130.813f)
                ++lowPitchFrames;
        }
        offset += count;
    }

    metrics.outputRms = channelRms(input.samples,
                                   comparisonStart + latency,
                                   totalSamples);
    metrics.widthCorrelationOut = stereoCorrelation(input.samples,
                                                     comparisonStart + latency,
                                                     totalSamples);
    for (const auto& channel : input.samples)
        for (float value : channel)
        {
            metrics.finite = metrics.finite && std::isfinite(value);
            metrics.outputPeak = std::max(metrics.outputPeak, std::abs(value));
        }
    metrics.pitchMedian = median(std::move(pitches));
    metrics.confidenceMedian = median(std::move(confidences));
    metrics.lowPitchFraction = voicedFrames > 0
        ? static_cast<float>(lowPitchFrames) / voicedFrames : 0.0f;
    return metrics;
}

std::string csvEscape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value)
    {
        if (c == '"') escaped.push_back('"');
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "Usage: CorpusRegression output.csv wav...\n";
        return 2;
    }
    std::ofstream csv(argv[1]);
    csv << "file,mode,amount,sampleRate,channels,duration,finite,inputPeak,outputPeak,inputRms,outputRms,deltaDb,pitchMedian,confidenceMedian,lowPitchFraction,widthIn,widthOut,widthDelta\n";
    bool failure = false;
    int processed = 0;
    for (int arg = 2; arg < argc; ++arg)
    {
        WavData wav;
        if (!readWav(argv[arg], wav, 1.5))
        {
            std::cerr << "SKIP " << argv[arg] << '\n';
            continue;
        }
        for (int mode = 0; mode < 3; ++mode)
        {
            const Metrics metrics = process(wav, mode, 1.0f);
            const double deltaDb = 20.0 * std::log10(std::max(1.0e-12,
                metrics.outputRms / std::max(1.0e-12, metrics.inputRms)));
            csv << csvEscape(fs::path(argv[arg]).filename().string()) << ',' << mode
                << ",1," << wav.sampleRate << ',' << wav.channels << ','
                << static_cast<double>(wav.samples[0].size()) / wav.sampleRate << ','
                << metrics.finite << ',' << metrics.inputPeak << ',' << metrics.outputPeak
                << ',' << metrics.inputRms << ',' << metrics.outputRms << ',' << deltaDb
                << ',' << metrics.pitchMedian << ',' << metrics.confidenceMedian
                << ',' << metrics.lowPitchFraction << ',' << metrics.widthCorrelationIn
                << ',' << metrics.widthCorrelationOut << ','
                << std::abs(metrics.widthCorrelationOut - metrics.widthCorrelationIn) << '\n';
            failure = failure || !metrics.finite || metrics.outputPeak > 8.0f;
            ++processed;
        }

        const Metrics dry = process(wav, 1, 0.0f);
        const double dryDeltaDb = 20.0 * std::log10(std::max(1.0e-12,
            dry.outputRms / std::max(1.0e-12, dry.inputRms)));
        csv << csvEscape(fs::path(argv[arg]).filename().string())
            << ",1,0," << wav.sampleRate << ',' << wav.channels << ','
            << static_cast<double>(wav.samples[0].size()) / wav.sampleRate << ','
            << dry.finite << ',' << dry.inputPeak << ',' << dry.outputPeak
            << ',' << dry.inputRms << ',' << dry.outputRms << ',' << dryDeltaDb
            << ',' << dry.pitchMedian << ',' << dry.confidenceMedian
            << ',' << dry.lowPitchFraction << ',' << dry.widthCorrelationIn
            << ',' << dry.widthCorrelationOut << ','
            << std::abs(dry.widthCorrelationOut - dry.widthCorrelationIn) << '\n';
        failure = failure || !dry.finite
            || std::abs(dry.widthCorrelationOut - dry.widthCorrelationIn) > 0.003f;
        ++processed;
        std::cout << fs::path(argv[arg]).filename().string() << " complete\n";
    }
    std::cout << "rows=" << processed << '\n';
    return failure ? 1 : 0;
}
