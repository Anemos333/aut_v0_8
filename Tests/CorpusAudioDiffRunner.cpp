#include <JuceHeader.h>
#include "../Source/ModernPitchEngine.h"
#include "../Source/ScaleDefinitions.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
struct Cli
{
    juce::File input;
    juce::File output;
    double thresholdPct = 85.0;
    double paramDeltaThresholdPct = 15.0;
    double pitchDiffThresholdCents = 8.0;
    double pitchParamDeltaThresholdCents = 8.0;
    int pitchMinOverlapFrames = 8;
    bool disablePitchCriterion = false;
    int blockSize = 256;
    double maxSeconds = 0.0;      // 0 = full files
    bool keepWet = false;
    bool stopOnFirstFail = false;
};

struct RootNote
{
    const char* name;
    double frequencyHz;
};

const std::array<RootNote, 19> rootNotes {{
    { "C",  261.6256 },
    { "C#", 277.1826 },
    { "D",  293.6648 },
    { "D#", 311.1270 },
    { "E",  329.6276 },
    { "F",  349.2282 },
    { "F#", 369.9944 },
    { "G",  391.9954 },
    { "G#", 415.3047 },
    { "A",  440.0000 },
    { "A#", 466.1638 },
    { "B",  493.8833 },
    { "Ni", 261.6256 },
    { "Pa", 261.6256 * 1.122462048309373 },
    { "Vu", 261.6256 * 1.235894465929289 },
    { "Ga", 261.6256 * 1.334839854170034 },
    { "Di", 261.6256 * 1.498307076876682 },
    { "Ke", 261.6256 * 1.681792830507429 },
    { "Zo", 261.6256 * 1.851749424574581 }
}};

struct ParamSet
{
    const char* name;
    float speedMs;
    float amountPct;
    float humanizePct;
};

const std::array<ParamSet, 2> paramSets {{
    { "speed0_amount100_humanize0",       0.0f, 100.0f,  0.0f },
    { "speed0p10_amount75_humanize20",    0.10f, 75.0f, 20.0f }
}};

struct Mode
{
    const char* name;
    ModernPitchEngine::LatencyMode mode;
};

const std::array<Mode, 3> modes {{
    { "quality",      ModernPitchEngine::LatencyMode::quality },
    { "live",         ModernPitchEngine::LatencyMode::live },
    { "experimental", ModernPitchEngine::LatencyMode::ultraLive }
}};

struct Config
{
    int index = 0;
    int baseIndex = 0; // Same mode/root/scale across parameter sets.
    int modeIndex = 0;
    int rootIndex = 0;
    int scaleIndex = 0;
    int paramIndex = 0;
    std::string label;
};

struct WetEntry
{
    int configIndex = -1;
    int baseIndex = -1;
    int modeIndex = -1;
    int rootIndex = -1;
    int scaleIndex = -1;
    int paramIndex = -1;
    std::string label;
    juce::File rawFile;
    juce::File pitchFile;
};

struct DiffMetrics
{
    double diffPct = 0.0;
    double rmsA = 0.0;
    double rmsB = 0.0;
    double rmsDiff = 0.0;
    double correlation = 0.0;
};

struct PitchDiffMetrics
{
    double rmsTargetDeltaCents = 0.0;
    double meanAbsTargetDeltaCents = 0.0;
    int overlapFrames = 0;
};

struct RenderResult
{
    juce::AudioBuffer<float> wet;
    std::vector<float> targetOffsetCents;
    int latencySamples = 0;
    int validPitchFrames = 0;
};

struct Accumulator
{
    long double sumA2 = 0.0L;
    long double sumB2 = 0.0L;
    long double sumD2 = 0.0L;
    long double sumAB = 0.0L;
    std::uint64_t count = 0;

    void add(float a, float b)
    {
        const long double aa = static_cast<long double>(a);
        const long double bb = static_cast<long double>(b);
        const long double d = aa - bb;
        sumA2 += aa * aa;
        sumB2 += bb * bb;
        sumD2 += d * d;
        sumAB += aa * bb;
        ++count;
    }

    DiffMetrics finish() const
    {
        DiffMetrics m;
        if (count == 0)
            return m;

        const long double invN = 1.0L / static_cast<long double>(count);
        const long double rmsA = std::sqrt(sumA2 * invN);
        const long double rmsB = std::sqrt(sumB2 * invN);
        const long double rmsD = std::sqrt(sumD2 * invN);
        const long double eps = 1.0e-18L;

        m.rmsA = static_cast<double>(rmsA);
        m.rmsB = static_cast<double>(rmsB);
        m.rmsDiff = static_cast<double>(rmsD);

        // 0% = identical. 100% = no practically shared waveform energy under this RMS metric.
        // It is clipped to 100 to keep PASS/FAIL thresholds intuitive.
        const long double denom = rmsA + rmsB + eps;
        m.diffPct = static_cast<double>(100.0L * std::min(1.0L, rmsD / denom));

        const long double corrDenom = std::sqrt(sumA2 * sumB2) + eps;
        m.correlation = static_cast<double>(sumAB / corrDenom);
        return m;
    }
};

std::string quote(const std::string& s)
{
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

std::string sanitizeForPath(std::string s)
{
    for (char& c : s)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                     || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok)
            c = '_';
    }
    while (s.find("__") != std::string::npos)
        s = s.replace(s.find("__"), 2, "_");
    if (s.size() > 140)
        s.resize(140);
    if (s.empty())
        s = "file";
    return s;
}

std::string toStdString(const juce::String& s)
{
    return s.toRawUTF8();
}

void usage()
{
    std::cout
        << "AutotuneCorpusDiff\n\n"
        << "Usage:\n"
        << "  AutotuneCorpusDiff --input <voices_dir> --output <results_dir> [options]\n\n"
        << "Options:\n"
        << "  --threshold <pct>       Within-set difference PASS threshold, default 85\n"
        << "  --param-delta-threshold <pct>  Cross-parameter similarity PASS ceiling, default 15\n"
        << "  --pitch-diff-threshold-cents <cents>  Minimum target-intonation delta for different root/scale pairs, default 8\n"
        << "  --pitch-param-delta-threshold-cents <cents>  Maximum target-intonation delta for matching cross-param pairs, default 8\n"
        << "  --pitch-min-overlap-frames <frames>  Minimum shared voiced metering frames for a pitch comparison, default 8\n"
        << "  --disable-pitch-criterion  Log pitch metrics but do not include them in PASS/FAIL\n"
        << "  --block-size <samples>  Processing block size, default 256\n"
        << "  --max-seconds <sec>     Shakedown limit per file. 0 = full file, default 0\n"
        << "  --keep-wet              Also write rendered wet WAV files. Very large.\n"
        << "  --stop-on-first-fail    Stop a voice as soon as any comparison fails.\n"
        << "  --help                  Show this help\n\n"
        << "The diff metric is clipped normalized RMS distance:\n"
        << "  100 * min(1, rms(a-b) / (rms(a) + rms(b) + eps))\n\n"
        << "PASS logic per voice:\n"
        << "  param set 0: dry-vs-wet and unique wet-vs-wet pairs must be >= threshold\n"
        << "  param set 1: dry-vs-wet and unique wet-vs-wet pairs must be >= threshold\n"
        << "  cross-param: same mode/root/scale across the two parameter sets must be <= param-delta-threshold\n"
        << "  pitch: different root/scale pairs in the same mode and parameter set must diverge by >= pitch-diff-threshold-cents;\n"
        << "         matching cross-param pairs must stay within pitch-param-delta-threshold-cents.\n";
}

bool parseArgs(int argc, char* argv[], Cli& cli)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto requireValue = [&](const char* option) -> const char*
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after " << option << "\n";
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h")
        {
            usage();
            std::exit(0);
        }
        else if (arg == "--input")
            cli.input = juce::File(requireValue("--input"));
        else if (arg == "--output")
            cli.output = juce::File(requireValue("--output"));
        else if (arg == "--threshold")
            cli.thresholdPct = std::stod(requireValue("--threshold"));
        else if (arg == "--param-delta-threshold")
            cli.paramDeltaThresholdPct = std::stod(requireValue("--param-delta-threshold"));
        else if (arg == "--pitch-diff-threshold-cents")
            cli.pitchDiffThresholdCents = std::stod(requireValue("--pitch-diff-threshold-cents"));
        else if (arg == "--pitch-param-delta-threshold-cents")
            cli.pitchParamDeltaThresholdCents = std::stod(requireValue("--pitch-param-delta-threshold-cents"));
        else if (arg == "--pitch-min-overlap-frames")
            cli.pitchMinOverlapFrames = std::max(1, std::stoi(requireValue("--pitch-min-overlap-frames")));
        else if (arg == "--disable-pitch-criterion")
            cli.disablePitchCriterion = true;
        else if (arg == "--block-size")
            cli.blockSize = std::max(1, std::stoi(requireValue("--block-size")));
        else if (arg == "--max-seconds")
            cli.maxSeconds = std::max(0.0, std::stod(requireValue("--max-seconds")));
        else if (arg == "--keep-wet")
            cli.keepWet = true;
        else if (arg == "--stop-on-first-fail")
            cli.stopOnFirstFail = true;
        else
        {
            std::cerr << "Unknown argument: " << arg << "\n";
            return false;
        }
    }

    if (! cli.input.exists())
    {
        std::cerr << "--input does not exist: " << toStdString(cli.input.getFullPathName()) << "\n";
        return false;
    }
    if (cli.output.getFullPathName().isEmpty())
    {
        std::cerr << "--output is required\n";
        return false;
    }
    return true;
}

bool hasAudioExtension(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff" || ext == ".flac";
}

std::vector<juce::File> findAudioFiles(const juce::File& root)
{
    std::vector<juce::File> files;

    if (root.isDirectory())
    {
        juce::Array<juce::File> found;
        root.findChildFiles(found, juce::File::findFiles, true);
        for (const auto& file : found)
        {
            if (file.getFileName().startsWith("._"))
                continue;
            if (hasAudioExtension(file))
                files.push_back(file);
        }
    }
    else if (hasAudioExtension(root) && ! root.getFileName().startsWith("._"))
    {
        files.push_back(root);
    }

    std::sort(files.begin(), files.end(),
              [](const juce::File& a, const juce::File& b)
              {
                  return a.getFullPathName() < b.getFullPathName();
              });
    return files;
}

std::vector<double> normaliseScaleLikePlugin(const std::vector<double>& source)
{
    std::vector<double> ratios;
    ratios.reserve(std::min<std::size_t>(source.size() + 1,
                                         ModernPitchEngine::maxScaleRatios));

    ratios.push_back(1.0);

    for (double ratio : source)
    {
        if (ratios.size() >= static_cast<std::size_t>(ModernPitchEngine::maxScaleRatios))
            break;
        if (! std::isfinite(ratio) || ratio <= 0.0)
            continue;

        const double l = std::log2(ratio);
        double folded = std::exp2(l - std::floor(l));
        if (folded >= 2.0)
            folded = 1.0;
        ratios.push_back(folded);
    }

    std::sort(ratios.begin(), ratios.end());
    ratios.erase(std::unique(ratios.begin(), ratios.end(),
                             [](double a, double b)
                             {
                                 return std::abs(a - b) <= 1.0e-8;
                             }),
                 ratios.end());

    if (ratios.empty())
        ratios.push_back(1.0);
    return ratios;
}

std::vector<Config> buildConfigs()
{
    std::vector<Config> configs;
    const int scaleCount = ScaleDefinitions::getScaleCount();

    int index = 0;
    int baseIndex = 0;
    for (int m = 0; m < static_cast<int>(modes.size()); ++m)
    {
        for (int r = 0; r < static_cast<int>(rootNotes.size()); ++r)
        {
            for (int s = 0; s < scaleCount; ++s)
            {
                const auto& scale = ScaleDefinitions::getScale(s);
                for (int p = 0; p < static_cast<int>(paramSets.size()); ++p)
                {
                    std::ostringstream label;
                    label << std::setw(5) << std::setfill('0') << index
                          << "_base" << std::setw(5) << std::setfill('0') << baseIndex
                          << "_mode-" << modes[static_cast<std::size_t>(m)].name
                          << "_root-" << rootNotes[static_cast<std::size_t>(r)].name
                          << "_scale" << s << "-" << sanitizeForPath(scale.name)
                          << "_" << paramSets[static_cast<std::size_t>(p)].name;

                    configs.push_back({ index, baseIndex, m, r, s, p, label.str() });
                    ++index;
                }
                ++baseIndex;
            }
        }
    }

    return configs;
}

ModernPitchEngine::Parameters makeParameters(const ParamSet& set)
{
    ModernPitchEngine::Parameters p;
    p.retuneTimeMs = set.speedMs;
    p.amount = juce::jlimit(0.0f, 1.0f, set.amountPct / 100.0f);
    p.transitionTimeMs = 35.0f;
    p.preserveVibrato = 0.70f;
    p.humanize = juce::jlimit(0.0f, 1.0f, set.humanizePct / 100.0f);
    p.formantPreservation = 0.90f;
    p.transientProtection = 0.85f;
    p.detectorSensitivity = 0.70f;
    p.maximumCorrectionSemitones = 12.0f;
    p.minimumPitchHz = 45.0f;
    p.maximumPitchHz = 1600.0f;
    p.stereoMode = ModernPitchEngine::StereoMode::linkedMidSide;
    p.breathReduction = 0.50f;
    return p;
}

RenderResult renderWet(const juce::AudioBuffer<float>& input,
                       double sampleRate,
                       int blockSize,
                       const Config& cfg)
{
    const auto& mode = modes[static_cast<std::size_t>(cfg.modeIndex)];
    const auto& root = rootNotes[static_cast<std::size_t>(cfg.rootIndex)];
    const auto& paramSet = paramSets[static_cast<std::size_t>(cfg.paramIndex)];
    const auto& scale = ScaleDefinitions::getScale(cfg.scaleIndex);
    const auto ratios = normaliseScaleLikePlugin(scale.ratios);

    ModernPitchEngine engine;
    engine.prepare(sampleRate, blockSize, input.getNumChannels(), mode.mode);
    auto parameters = makeParameters(paramSet);

    RenderResult result;
    result.latencySamples = engine.getLatencySamples();

    const int originalSamples = input.getNumSamples();
    const int tailSamples = std::max(blockSize * 8, result.latencySamples + blockSize * 4);
    juce::AudioBuffer<float> work(input.getNumChannels(), originalSamples + tailSamples);
    work.clear();

    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        work.copyFrom(ch, 0, input, ch, 0, originalSamples);

    result.targetOffsetCents.reserve(static_cast<std::size_t>((originalSamples + blockSize - 1) / blockSize));

    for (int offset = 0; offset < work.getNumSamples(); offset += blockSize)
    {
        const int count = std::min(blockSize, work.getNumSamples() - offset);
        juce::AudioBuffer<float> view(work.getArrayOfWritePointers(),
                                      work.getNumChannels(),
                                      offset,
                                      count);
        engine.process(view,
                       ratios.empty() ? nullptr : ratios.data(),
                       static_cast<int>(ratios.size()),
                       root.frequencyHz,
                       parameters);

        if (offset < originalSamples)
        {
            const auto meter = engine.getMetering();
            const bool valid = std::isfinite(meter.detectedPitchHz)
                            && std::isfinite(meter.targetPitchHz)
                            && meter.detectedPitchHz > 0.0f
                            && meter.targetPitchHz > 0.0f
                            && meter.voicing >= 0.08f
                            && meter.confidence >= 0.12f
                            && meter.detectorSupport > 0;
            if (valid)
            {
                const double cents = 1200.0 * std::log2(
                    static_cast<double>(meter.targetPitchHz)
                    / static_cast<double>(meter.detectedPitchHz));
                if (std::isfinite(cents) && std::abs(cents) <= 2400.0)
                {
                    result.targetOffsetCents.push_back(static_cast<float>(cents));
                    ++result.validPitchFrames;
                }
                else
                {
                    result.targetOffsetCents.push_back(std::numeric_limits<float>::quiet_NaN());
                }
            }
            else
            {
                result.targetOffsetCents.push_back(std::numeric_limits<float>::quiet_NaN());
            }
        }
    }

    result.wet.setSize(input.getNumChannels(), originalSamples);
    result.wet.clear();

    const int available = std::max(0, std::min(originalSamples,
                                               work.getNumSamples() - result.latencySamples));
    for (int ch = 0; ch < input.getNumChannels(); ++ch)
        result.wet.copyFrom(ch, 0, work, ch, result.latencySamples, available);

    return result;
}

DiffMetrics compareBuffers(const juce::AudioBuffer<float>& a,
                           const juce::AudioBuffer<float>& b)
{
    Accumulator acc;
    const int channels = std::min(a.getNumChannels(), b.getNumChannels());
    const int samples = std::min(a.getNumSamples(), b.getNumSamples());
    for (int sample = 0; sample < samples; ++sample)
        for (int ch = 0; ch < channels; ++ch)
            acc.add(a.getSample(ch, sample), b.getSample(ch, sample));
    return acc.finish();
}

void writeRawInterleaved(const juce::AudioBuffer<float>& buffer, const juce::File& rawFile)
{
    rawFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(rawFile.createOutputStream());
    if (stream == nullptr || ! stream->openedOk())
        throw std::runtime_error("Cannot create raw temp file: " + toStdString(rawFile.getFullPathName()));

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const float value = buffer.getSample(ch, sample);
            stream->write(&value, sizeof(value));
        }
    }
}

void writePitchSeries(const std::vector<float>& values, const juce::File& pitchFile)
{
    pitchFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> stream(pitchFile.createOutputStream());
    if (stream == nullptr || ! stream->openedOk())
        throw std::runtime_error("Cannot create pitch temp file: " + toStdString(pitchFile.getFullPathName()));
    if (! values.empty())
        stream->write(values.data(), static_cast<int>(values.size() * sizeof(float)));
}

PitchDiffMetrics comparePitchSeries(const juce::File& aFile,
                                    const juce::File& bFile)
{
    juce::FileInputStream aStream(aFile);
    juce::FileInputStream bStream(bFile);
    if (! aStream.openedOk())
        throw std::runtime_error("Cannot read pitch temp file: " + toStdString(aFile.getFullPathName()));
    if (! bStream.openedOk())
        throw std::runtime_error("Cannot read pitch temp file: " + toStdString(bFile.getFullPathName()));

    const auto aBytes = aFile.getSize();
    const auto bBytes = bFile.getSize();
    const int frames = static_cast<int>(std::min(aBytes, bBytes) / static_cast<juce::int64>(sizeof(float)));
    constexpr int chunkFrames = 8192;
    std::vector<float> a(static_cast<std::size_t>(chunkFrames));
    std::vector<float> b(static_cast<std::size_t>(chunkFrames));

    long double sumD2 = 0.0L;
    long double sumAbsD = 0.0L;
    int overlap = 0;
    int frameBase = 0;
    while (frameBase < frames)
    {
        const int count = std::min(chunkFrames, frames - frameBase);
        const int bytes = count * static_cast<int>(sizeof(float));
        if (aStream.read(a.data(), bytes) != bytes || bStream.read(b.data(), bytes) != bytes)
            throw std::runtime_error("Unexpected EOF while reading pitch series");

        for (int i = 0; i < count; ++i)
        {
            const float av = a[static_cast<std::size_t>(i)];
            const float bv = b[static_cast<std::size_t>(i)];
            if (! std::isfinite(av) || ! std::isfinite(bv))
                continue;
            const long double d = static_cast<long double>(av) - static_cast<long double>(bv);
            sumD2 += d * d;
            sumAbsD += std::abs(d);
            ++overlap;
        }
        frameBase += count;
    }

    PitchDiffMetrics m;
    m.overlapFrames = overlap;
    if (overlap > 0)
    {
        const long double inv = 1.0L / static_cast<long double>(overlap);
        m.rmsTargetDeltaCents = static_cast<double>(std::sqrt(sumD2 * inv));
        m.meanAbsTargetDeltaCents = static_cast<double>(sumAbsD * inv);
    }
    return m;
}

void logPitchComparison(std::ofstream& log,
                        const std::string& type,
                        const std::string& left,
                        const std::string& right,
                        const PitchDiffMetrics& m,
                        double thresholdCents,
                        int minOverlapFrames,
                        bool passWhenGreaterOrEqual)
{
    const bool enough = m.overlapFrames >= minOverlapFrames;
    const bool ok = enough && (passWhenGreaterOrEqual ? (m.rmsTargetDeltaCents >= thresholdCents)
                                                      : (m.rmsTargetDeltaCents <= thresholdCents));
    log << type << '\t'
        << quote(left) << '\t'
        << quote(right) << '\t'
        << std::fixed << std::setprecision(6)
        << m.overlapFrames << '\t'
        << m.rmsTargetDeltaCents << '\t'
        << m.meanAbsTargetDeltaCents << '\t'
        << (passWhenGreaterOrEqual ? ">=" : "<=") << thresholdCents << '\t'
        << "min_overlap=" << minOverlapFrames << '\t'
        << (enough ? (ok ? "PASS" : "FAIL") : "SKIP_INSUFFICIENT_OVERLAP") << '\n';
}


DiffMetrics compareRawToBuffer(const juce::File& rawFile,
                               const juce::AudioBuffer<float>& b)
{
    juce::FileInputStream stream(rawFile);
    if (! stream.openedOk())
        throw std::runtime_error("Cannot read raw temp file: " + toStdString(rawFile.getFullPathName()));

    Accumulator acc;
    const int channels = b.getNumChannels();
    const int totalSamples = b.getNumSamples();
    constexpr int framesPerChunk = 8192;
    std::vector<float> chunk(static_cast<std::size_t>(framesPerChunk * channels));

    int frameBase = 0;
    while (frameBase < totalSamples)
    {
        const int frames = std::min(framesPerChunk, totalSamples - frameBase);
        const int values = frames * channels;
        const int bytes = values * static_cast<int>(sizeof(float));
        const int read = stream.read(chunk.data(), bytes);
        if (read != bytes)
            throw std::runtime_error("Unexpected EOF in raw temp file: " + toStdString(rawFile.getFullPathName()));

        int k = 0;
        for (int frame = 0; frame < frames; ++frame)
            for (int ch = 0; ch < channels; ++ch)
                acc.add(chunk[static_cast<std::size_t>(k++)],
                        b.getSample(ch, frameBase + frame));

        frameBase += frames;
    }

    return acc.finish();
}

void writeWetWav(const juce::AudioBuffer<float>& wet,
                 double sampleRate,
                 const juce::File& outFile)
{
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream(outFile.createOutputStream());
    if (stream == nullptr || ! stream->openedOk())
        throw std::runtime_error("Cannot create wet WAV: " + toStdString(outFile.getFullPathName()));

    std::unique_ptr<juce::AudioFormatWriter> writer(
        wav.createWriterFor(stream.get(), sampleRate,
                            static_cast<unsigned int>(wet.getNumChannels()),
                            24, {}, 0));
    if (writer == nullptr)
        throw std::runtime_error("Cannot create WAV writer: " + toStdString(outFile.getFullPathName()));

    stream.release();
    writer->writeFromAudioSampleBuffer(wet, 0, wet.getNumSamples());
}

void logComparison(std::ofstream& log,
                   const std::string& type,
                   const std::string& left,
                   const std::string& right,
                   const DiffMetrics& m,
                   double threshold,
                   bool passWhenGreaterOrEqual)
{
    const bool ok = passWhenGreaterOrEqual ? (m.diffPct >= threshold)
                                           : (m.diffPct <= threshold);
    log << type << '\t'
        << quote(left) << '\t'
        << quote(right) << '\t'
        << std::fixed << std::setprecision(6)
        << m.diffPct << '\t'
        << m.rmsA << '\t'
        << m.rmsB << '\t'
        << m.rmsDiff << '\t'
        << m.correlation << '\t'
        << (passWhenGreaterOrEqual ? ">=" : "<=") << threshold << '\t'
        << (ok ? "PASS" : "FAIL") << '\n';
}


struct VoiceResult
{
    bool pass = true;
    std::array<bool, 2> paramSetPass {{ true, true }};
    bool crossParamPass = true;
    bool pitchDiffPass = true;
    bool pitchCrossParamPass = true;

    std::array<double, 2> minWithinSetDiffPct {{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()
    }};
    double maxCrossParamDiffPct = 0.0;
    double minDifferentIntonationRmsCents = std::numeric_limits<double>::infinity();
    double maxCrossParamPitchRmsCents = 0.0;

    std::array<std::uint64_t, 2> withinSetComparisons {{ 0, 0 }};
    std::array<std::uint64_t, 2> withinSetFailedComparisons {{ 0, 0 }};
    std::uint64_t crossParamComparisons = 0;
    std::uint64_t crossParamFailedComparisons = 0;
    std::uint64_t pitchDifferentComparisons = 0;
    std::uint64_t pitchDifferentFailedComparisons = 0;
    std::uint64_t pitchDifferentSkippedComparisons = 0;
    std::uint64_t pitchCrossParamComparisons = 0;
    std::uint64_t pitchCrossParamFailedComparisons = 0;
    std::uint64_t pitchCrossParamSkippedComparisons = 0;

    [[nodiscard]] std::uint64_t comparisons() const noexcept
    {
        return withinSetComparisons[0] + withinSetComparisons[1] + crossParamComparisons
             + pitchDifferentComparisons + pitchCrossParamComparisons;
    }

    [[nodiscard]] std::uint64_t failedComparisons() const noexcept
    {
        return withinSetFailedComparisons[0] + withinSetFailedComparisons[1]
             + crossParamFailedComparisons + pitchDifferentFailedComparisons
             + pitchCrossParamFailedComparisons;
    }

    void refreshPass(bool includePitchCriterion = true) noexcept
    {
        pass = paramSetPass[0] && paramSetPass[1] && crossParamPass
            && (! includePitchCriterion || (pitchDiffPass && pitchCrossParamPass));
    }
};


juce::AudioBuffer<float> readAudioFile(juce::AudioFormatManager& fm,
                                       const juce::File& file,
                                       double maxSeconds,
                                       double& sampleRate)
{
    std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
    if (reader == nullptr)
        throw std::runtime_error("Unsupported or unreadable audio file: " + toStdString(file.getFullPathName()));

    sampleRate = reader->sampleRate;
    const int channels = std::clamp(static_cast<int>(reader->numChannels),
                                    1, ModernPitchEngine::maxSupportedChannels);

    juce::int64 samplesToRead = reader->lengthInSamples;
    if (maxSeconds > 0.0)
        samplesToRead = std::min(samplesToRead,
                                 static_cast<juce::int64>(std::llround(maxSeconds * sampleRate)));

    if (samplesToRead <= 0 || samplesToRead > static_cast<juce::int64>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Invalid or too large file length: " + toStdString(file.getFullPathName()));

    juce::AudioBuffer<float> input(channels, static_cast<int>(samplesToRead));
    input.clear();
    reader->read(&input, 0, static_cast<int>(samplesToRead), 0, true, true);
    return input;
}

VoiceResult processVoice(juce::AudioFormatManager& fm,
                         const juce::File& file,
                         const Cli& cli,
                         const std::vector<Config>& configs)
{
    const auto voiceStem = sanitizeForPath(file.getFileNameWithoutExtension().toStdString());
    const auto voiceDir = cli.output.getChildFile(voiceStem);
    const auto tmpDir = voiceDir.getChildFile("_tmp_raw");
    const auto wetDir = voiceDir.getChildFile("wet_wavs");

    voiceDir.createDirectory();
    tmpDir.createDirectory();
    tmpDir.deleteRecursively();
    tmpDir.createDirectory();
    if (cli.keepWet)
        wetDir.createDirectory();

    double sampleRate = 0.0;
    auto input = readAudioFile(fm, file, cli.maxSeconds, sampleRate);

    const auto logFile = voiceDir.getChildFile(voiceStem + "_diffs.txt");
    std::ofstream log(logFile.getFullPathName().toStdString());
    if (! log)
        throw std::runtime_error("Cannot create log file: " + toStdString(logFile.getFullPathName()));

    log << "voice\t" << quote(file.getFileName().toStdString()) << "\n";
    log << "sample_rate\t" << sampleRate << "\n";
    log << "channels\t" << input.getNumChannels() << "\n";
    log << "samples\t" << input.getNumSamples() << "\n";
    log << "within_set_threshold_pct\t" << cli.thresholdPct << "\n";
    log << "cross_param_delta_threshold_pct\t" << cli.paramDeltaThresholdPct << "\n";
    log << "pitch_diff_threshold_cents\t" << cli.pitchDiffThresholdCents << "\n";
    log << "pitch_param_delta_threshold_cents\t" << cli.pitchParamDeltaThresholdCents << "\n";
    log << "pitch_min_overlap_frames\t" << cli.pitchMinOverlapFrames << "\n";
    log << "pitch_criterion_enabled\t" << (cli.disablePitchCriterion ? "0" : "1") << "\n";
    log << "metric\tclipped_normalized_rms_distance_pct\n";
    log << "formula\t100 * min(1, rms(a-b) / (rms(a) + rms(b) + eps))\n";
    log << "configs\t" << configs.size() << "\n";
    log << "pass_logic\tparam set 0 and param set 1 each require dry-vs-wet and unique within-set wet-vs-wet pairs >= within_set_threshold_pct; cross-param same mode/root/scale pairs require diff <= cross_param_delta_threshold_pct; pitch different-intonation pairs require RMS target-offset delta >= pitch_diff_threshold_cents; cross-param same-mode/root/scale pitch pairs require RMS target-offset delta <= pitch_param_delta_threshold_cents\n";
    log << "AUDIO_COMPARISONS_BEGIN\n";
    log << "type\tleft\tright\tdiff_pct\trms_left\trms_right\trms_diff\tcorrelation\tcriterion\tresult\n";

    VoiceResult result;
    std::array<std::vector<WetEntry>, 2> previousByParam;
    previousByParam[0].reserve(configs.size() / 2 + 1);
    previousByParam[1].reserve(configs.size() / 2 + 1);
    log << "PITCH_COMPARISONS_BEGIN\n";
    log << "pitch_type	left	right	overlap_frames	rms_target_offset_delta_cents	mean_abs_target_offset_delta_cents	criterion	min_overlap	result\n";

    std::vector<WetEntry> param0ByBase(configs.size() / paramSets.size() + 1);
    std::vector<bool> hasParam0ByBase(param0ByBase.size(), false);

    for (const auto& cfg : configs)
    {
        const int p = cfg.paramIndex;
        if (p < 0 || p >= static_cast<int>(paramSets.size()))
            throw std::runtime_error("Invalid parameter-set index");

        const auto rendered = renderWet(input, sampleRate, cli.blockSize, cfg);
        const auto& wet = rendered.wet;

        const auto dryWet = compareBuffers(input, wet);
        result.minWithinSetDiffPct[static_cast<std::size_t>(p)] =
            std::min(result.minWithinSetDiffPct[static_cast<std::size_t>(p)], dryWet.diffPct);
        ++result.withinSetComparisons[static_cast<std::size_t>(p)];
        if (dryWet.diffPct < cli.thresholdPct)
        {
            result.paramSetPass[static_cast<std::size_t>(p)] = false;
            ++result.withinSetFailedComparisons[static_cast<std::size_t>(p)];
        }

        logComparison(log, p == 0 ? "param0_dry_vs_wet" : "param1_dry_vs_wet",
                      "ORIGINAL", cfg.label, dryWet, cli.thresholdPct, true);

        if (cli.keepWet)
        {
            const auto wetFile = wetDir.getChildFile(sanitizeForPath(cfg.label) + ".wav");
            writeWetWav(wet, sampleRate, wetFile);
        }

        const auto pitchFile = tmpDir.getChildFile(juce::String::formatted("%05d.pitchf32", cfg.index));
        writePitchSeries(rendered.targetOffsetCents, pitchFile);

        // Unique within-set wet/wet comparisons only: previous entries in the same
        // parameter set. This deliberately avoids A,A and avoids mirrored B,A pairs.
        for (const auto& prev : previousByParam[static_cast<std::size_t>(p)])
        {
            if (prev.configIndex == cfg.index)
                throw std::runtime_error("Internal error: attempted self comparison");
            if (prev.paramIndex != p)
                throw std::runtime_error("Internal error: crossed parameter sets in within-set comparison");

            const auto wetWet = compareRawToBuffer(prev.rawFile, wet);
            result.minWithinSetDiffPct[static_cast<std::size_t>(p)] =
                std::min(result.minWithinSetDiffPct[static_cast<std::size_t>(p)], wetWet.diffPct);
            ++result.withinSetComparisons[static_cast<std::size_t>(p)];
            if (wetWet.diffPct < cli.thresholdPct)
            {
                result.paramSetPass[static_cast<std::size_t>(p)] = false;
                ++result.withinSetFailedComparisons[static_cast<std::size_t>(p)];
            }

            logComparison(log, p == 0 ? "param0_wet_vs_wet" : "param1_wet_vs_wet",
                          prev.label, cfg.label, wetWet, cli.thresholdPct, true);

            const bool differentIntonation = prev.modeIndex == cfg.modeIndex
                                          && (prev.rootIndex != cfg.rootIndex
                                              || prev.scaleIndex != cfg.scaleIndex);
            if (differentIntonation)
            {
                const auto pitch = comparePitchSeries(prev.pitchFile, pitchFile);
                const bool enough = pitch.overlapFrames >= cli.pitchMinOverlapFrames;
                if (enough)
                {
                    ++result.pitchDifferentComparisons;
                    result.minDifferentIntonationRmsCents = std::min(
                        result.minDifferentIntonationRmsCents, pitch.rmsTargetDeltaCents);
                    if (pitch.rmsTargetDeltaCents < cli.pitchDiffThresholdCents)
                    {
                        result.pitchDiffPass = false;
                        ++result.pitchDifferentFailedComparisons;
                    }
                }
                else
                {
                    ++result.pitchDifferentSkippedComparisons;
                }

                logPitchComparison(log,
                                   p == 0 ? "pitch_param0_different_root_or_scale"
                                          : "pitch_param1_different_root_or_scale",
                                   prev.label, cfg.label, pitch,
                                   cli.pitchDiffThresholdCents,
                                   cli.pitchMinOverlapFrames,
                                   true);
            }

            result.refreshPass(! cli.disablePitchCriterion);
            if (cli.stopOnFirstFail && ! result.pass)
                break;
        }

        const auto rawFile = tmpDir.getChildFile(juce::String::formatted("%05d.rawf32", cfg.index));
        writeRawInterleaved(wet, rawFile);
        WetEntry current { cfg.index, cfg.baseIndex, cfg.modeIndex, cfg.rootIndex,
                           cfg.scaleIndex, p, cfg.label, rawFile, pitchFile };

        // Cross-parameter stability check: compare only the corresponding renders
        // with the same mode/root/scale and different parameter set.
        if (p == 0)
        {
            if (cfg.baseIndex >= static_cast<int>(param0ByBase.size()))
                throw std::runtime_error("Internal error: base index out of range");
            param0ByBase[static_cast<std::size_t>(cfg.baseIndex)] = current;
            hasParam0ByBase[static_cast<std::size_t>(cfg.baseIndex)] = true;
        }
        else if (p == 1)
        {
            if (cfg.baseIndex >= static_cast<int>(param0ByBase.size())
                || ! hasParam0ByBase[static_cast<std::size_t>(cfg.baseIndex)])
                throw std::runtime_error("Internal error: missing param0 counterpart for cross-param comparison");

            const auto& counterpart = param0ByBase[static_cast<std::size_t>(cfg.baseIndex)];
            if (counterpart.configIndex == cfg.index || counterpart.paramIndex == p
                || counterpart.baseIndex != cfg.baseIndex)
                throw std::runtime_error("Internal error: invalid cross-param comparison pair");

            const auto cross = compareRawToBuffer(counterpart.rawFile, wet);
            result.maxCrossParamDiffPct = std::max(result.maxCrossParamDiffPct, cross.diffPct);
            ++result.crossParamComparisons;
            if (cross.diffPct > cli.paramDeltaThresholdPct)
            {
                result.crossParamPass = false;
                ++result.crossParamFailedComparisons;
            }

            logComparison(log, "cross_param_same_mode_root_scale",
                          counterpart.label, cfg.label, cross,
                          cli.paramDeltaThresholdPct, false);

            const auto pitchCross = comparePitchSeries(counterpart.pitchFile, pitchFile);
            const bool enoughPitchCross = pitchCross.overlapFrames >= cli.pitchMinOverlapFrames;
            if (enoughPitchCross)
            {
                ++result.pitchCrossParamComparisons;
                result.maxCrossParamPitchRmsCents = std::max(
                    result.maxCrossParamPitchRmsCents, pitchCross.rmsTargetDeltaCents);
                if (pitchCross.rmsTargetDeltaCents > cli.pitchParamDeltaThresholdCents)
                {
                    result.pitchCrossParamPass = false;
                    ++result.pitchCrossParamFailedComparisons;
                }
            }
            else
            {
                ++result.pitchCrossParamSkippedComparisons;
            }

            logPitchComparison(log, "pitch_cross_param_same_mode_root_scale",
                               counterpart.label, cfg.label, pitchCross,
                               cli.pitchParamDeltaThresholdCents,
                               cli.pitchMinOverlapFrames,
                               false);
        }
        else
        {
            throw std::runtime_error("This runner currently expects exactly two parameter sets");
        }

        previousByParam[static_cast<std::size_t>(p)].push_back(current);
        result.refreshPass(! cli.disablePitchCriterion);

        if (cli.stopOnFirstFail && ! result.pass)
            break;

        if (cfg.index % 100 == 0)
        {
            std::cout << "  " << file.getFileName() << ": rendered "
                      << (cfg.index + 1) << "/" << configs.size()
                      << ", comparisons=" << result.comparisons() << "\n";
        }
    }

    if (! cli.disablePitchCriterion)
    {
        if (result.pitchDifferentComparisons == 0)
            result.pitchDiffPass = false;
        if (result.pitchCrossParamComparisons == 0)
            result.pitchCrossParamPass = false;
    }

    result.refreshPass(! cli.disablePitchCriterion);
    log << "pass_primo_set_parametri\t" << (result.paramSetPass[0] ? "PASS" : "FAIL") << "\n";
    log << "pass_secondo_set_parametri\t" << (result.paramSetPass[1] ? "PASS" : "FAIL") << "\n";
    log << "pass_cross_parametri\t" << (result.crossParamPass ? "PASS" : "FAIL") << "\n";
    log << "pass_pitch_different_intonation\t" << (result.pitchDiffPass ? "PASS" : "FAIL") << "\n";
    log << "pass_pitch_cross_parametri\t" << (result.pitchCrossParamPass ? "PASS" : "FAIL") << "\n";
    log << "RESULT\t" << (result.pass ? "PASS" : "FAIL") << "\n";
    log << "min_diff_primo_set_pct\t" << std::fixed << std::setprecision(6) << result.minWithinSetDiffPct[0] << "\n";
    log << "min_diff_secondo_set_pct\t" << std::fixed << std::setprecision(6) << result.minWithinSetDiffPct[1] << "\n";
    log << "max_cross_param_diff_pct\t" << std::fixed << std::setprecision(6) << result.maxCrossParamDiffPct << "\n";
    log << "min_pitch_different_intonation_rms_cents\t" << std::fixed << std::setprecision(6) << result.minDifferentIntonationRmsCents << "\n";
    log << "max_pitch_cross_param_rms_cents\t" << std::fixed << std::setprecision(6) << result.maxCrossParamPitchRmsCents << "\n";
    log << "comparisons_primo_set\t" << result.withinSetComparisons[0] << "\n";
    log << "comparisons_secondo_set\t" << result.withinSetComparisons[1] << "\n";
    log << "comparisons_cross_parametri\t" << result.crossParamComparisons << "\n";
    log << "failed_primo_set\t" << result.withinSetFailedComparisons[0] << "\n";
    log << "failed_secondo_set\t" << result.withinSetFailedComparisons[1] << "\n";
    log << "failed_cross_parametri\t" << result.crossParamFailedComparisons << "\n";
    log << "pitch_different_comparisons\t" << result.pitchDifferentComparisons << "\n";
    log << "pitch_different_failed\t" << result.pitchDifferentFailedComparisons << "\n";
    log << "pitch_different_skipped\t" << result.pitchDifferentSkippedComparisons << "\n";
    log << "pitch_cross_param_comparisons\t" << result.pitchCrossParamComparisons << "\n";
    log << "pitch_cross_param_failed\t" << result.pitchCrossParamFailedComparisons << "\n";
    log << "pitch_cross_param_skipped\t" << result.pitchCrossParamSkippedComparisons << "\n";

    tmpDir.deleteRecursively();

    std::cout << file.getFileName() << ": "
              << (result.pass ? "PASS" : "FAIL")
              << " set0=" << (result.paramSetPass[0] ? "PASS" : "FAIL")
              << " set1=" << (result.paramSetPass[1] ? "PASS" : "FAIL")
              << " cross=" << (result.crossParamPass ? "PASS" : "FAIL")
              << " pitchDiff=" << (result.pitchDiffPass ? "PASS" : "FAIL")
              << " pitchCross=" << (result.pitchCrossParamPass ? "PASS" : "FAIL")
              << " min0=" << std::fixed << std::setprecision(3) << result.minWithinSetDiffPct[0]
              << "% min1=" << result.minWithinSetDiffPct[1]
              << "% maxCross=" << result.maxCrossParamDiffPct
              << "% minPitchDiff=" << result.minDifferentIntonationRmsCents
              << "c maxPitchCross=" << result.maxCrossParamPitchRmsCents
              << "c comparisons=" << result.comparisons()
              << " failed=" << result.failedComparisons()
              << "\n";
    return result;
}

} // namespace

int main(int argc, char* argv[])
{
    Cli cli;
    if (! parseArgs(argc, argv, cli))
    {
        usage();
        return 2;
    }

    cli.output.createDirectory();

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    const auto files = findAudioFiles(cli.input);
    if (files.empty())
    {
        std::cerr << "No supported audio files found under "
                  << toStdString(cli.input.getFullPathName()) << "\n";
        return 2;
    }

    const auto configs = buildConfigs();

    std::cout << "Audio files: " << files.size() << "\n";
    std::cout << "Scales: " << ScaleDefinitions::getScaleCount() << "\n";
    std::cout << "Roots: " << rootNotes.size() << "\n";
    std::cout << "Modes: " << modes.size() << "\n";
    std::cout << "Parameter sets: " << paramSets.size() << "\n";
    std::cout << "Wet renders per voice: " << configs.size() << "\n";
    const auto configsPerParam = static_cast<std::uint64_t>(configs.size() / paramSets.size());
    const auto withinPerParam = configsPerParam + (configsPerParam * (configsPerParam - 1)) / 2;
    const auto crossParamPairs = configsPerParam;
    std::cout << "Expected comparisons per full voice: "
              << (withinPerParam * paramSets.size() + crossParamPairs)
              << " (within each param set + same-mode/root/scale cross-param pairs)\n";

    bool corpusPass = true;
    std::uint64_t totalComparisons = 0;
    std::uint64_t totalFailed = 0;

    const auto summaryFile = cli.output.getChildFile("corpus_summary.txt");
    std::ofstream summary(summaryFile.getFullPathName().toStdString());
    summary << "file\tresult\tpass_primo_set\tpass_secondo_set\tpass_cross_parametri\tpass_pitch_different_intonation\tpass_pitch_cross_parametri\tmin_diff_primo_set_pct\tmin_diff_secondo_set_pct\tmax_cross_param_diff_pct\tmin_pitch_different_intonation_rms_cents\tmax_pitch_cross_param_rms_cents\tcomparisons\tfailed_comparisons\n";

    for (const auto& file : files)
    {
        try
        {
            auto result = processVoice(formatManager, file, cli, configs);
            corpusPass = corpusPass && result.pass;
            totalComparisons += result.comparisons();
            totalFailed += result.failedComparisons();

            summary << quote(file.getFileName().toStdString()) << '\t'
                    << (result.pass ? "PASS" : "FAIL") << '\t'
                    << (result.paramSetPass[0] ? "PASS" : "FAIL") << '\t'
                    << (result.paramSetPass[1] ? "PASS" : "FAIL") << '\t'
                    << (result.crossParamPass ? "PASS" : "FAIL") << '\t'
                    << (result.pitchDiffPass ? "PASS" : "FAIL") << '\t'
                    << (result.pitchCrossParamPass ? "PASS" : "FAIL") << '\t'
                    << std::fixed << std::setprecision(6) << result.minWithinSetDiffPct[0] << '\t'
                    << result.minWithinSetDiffPct[1] << '\t'
                    << result.maxCrossParamDiffPct << '\t'
                    << result.minDifferentIntonationRmsCents << '\t'
                    << result.maxCrossParamPitchRmsCents << '\t'
                    << result.comparisons() << '\t'
                    << result.failedComparisons() << '\n';
        }
        catch (const std::exception& e)
        {
            corpusPass = false;
            ++totalFailed;
            std::cerr << file.getFileName() << ": FAIL exception: " << e.what() << "\n";
            std::cout << file.getFileName() << ": FAIL\n";
            summary << quote(file.getFileName().toStdString()) << "\tFAIL\tFAIL\tFAIL\tFAIL\tFAIL\tFAIL\tnan\tnan\tnan\tnan\tnan\t0\t1\n";
        }
    }

    summary << "CORPUS\t" << (corpusPass ? "PASS" : "FAIL")
            << "\t\t\t\t\t\t\t"
            << totalComparisons << '\t' << totalFailed << '\n';

    std::cout << "CORPUS " << (corpusPass ? "PASS" : "FAIL")
              << " total_comparisons=" << totalComparisons
              << " failed=" << totalFailed << "\n";

    return corpusPass ? 0 : 1;
}
