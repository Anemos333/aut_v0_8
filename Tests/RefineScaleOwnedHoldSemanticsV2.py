from pathlib import Path

# V2 keeps the literal-cent Hold contract from V1, but restores the crucial
# distinction between a scale-boundary excursion and a musically qualified new
# note. Hold may suppress fast/deep promotion while the voice remains inside the
# user-selected radius; it must never manufacture a new-note candidate by itself.
script_path = Path(__file__).resolve().parent / 'RefineScaleOwnedHoldSemanticsV1.py'
source = script_path.read_text()

old = """new_deep = '''            const bool outsideUserHold = observedDistanceFromOwnedTarget
                > static_cast<double>(holdRadiusCents) + 1.0e-6;
            const bool deepCandidate = octaveLikeTarget || outsideUserHold;
'''
"""
new = """new_deep = '''            // HOLD_DOES_NOT_CREATE_NOTE_IDENTITY_V2: crossing the Hold radius
            // is not, by itself, evidence of a new musical note. Preserve the
            // existing scale-relative deep geometry so vibrato and continuous
            // trajectories cannot chatter between degrees. A wider Hold may
            // suppress this fast/deep promotion, while the independent
            // same-side persistence path below remains free to qualify a real
            // sustained/legato note and then bypass Hold.
            const bool outsideUserHold = observedDistanceFromOwnedTarget
                > static_cast<double>(holdRadiusCents) + 1.0e-6;
            const bool deepCandidate = octaveLikeTarget
                || (outsideUserHold
                    && observedDistanceFromOwnedTarget >= deepExitRatio * scaleStep);
'''
"""
if source.count(old) != 1:
    raise RuntimeError(
        f'Hold V2 deep-candidate anchor: expected one, found {source.count(old)}')
source = source.replace(old, new, 1)

namespace = {'__file__': str(script_path), '__name__': '__main__'}
exec(compile(source, str(script_path), 'exec'), namespace)

# DETECTOR_EXTREME_DIAGNOSTIC_V1 is deliberately measurement-only. It does not
# modify ModernPitchEngine, detector thresholds, path authority, quantizer or
# renderer. The goal is to measure whether realistic-but-hostile signals get
# stuck in Acquire before considering any detector change.
test_path = script_path.parent / 'SupervisorContinuityTest.cpp'
test = test_path.read_text()
if 'DETECTOR_EXTREME_DIAGNOSTIC_V1' not in test:
    return_anchor = '    return success ? 0 : 1;\n}'
    if test.count(return_anchor) != 1:
        raise RuntimeError(
            f'extreme diagnostic return anchor: expected one, found {test.count(return_anchor)}')

    diagnostic = r'''
    // DETECTOR_EXTREME_DIAGNOSTIC_V1
    // Diagnostic-only SNR/level matrix. No PASS threshold is attached: these
    // numbers are evidence for the next detector decision, not permission to
    // weaken acquisition when measurement becomes difficult.
    struct StressMetrics
    {
        int firstValid = -1;
        int firstCorrect = -1;
        int stableCorrect = -1;
        int valid = 0;
        int correct = 0;
        int halfOctave = 0;
        int doubleOctave = 0;
        int other = 0;
        int maxInvalidAfterLock = 0;
        double centsSum = 0.0;
    };

    const auto stressRms = [](const std::vector<float>& x)
    {
        double e = 0.0;
        for (float v : x)
            e += static_cast<double>(v) * static_cast<double>(v);
        return x.empty() ? 0.0 : std::sqrt(e / static_cast<double>(x.size()));
    };

    const auto stressScaleToRms = [&stressRms](std::vector<float>& x, double target)
    {
        const double current = stressRms(x);
        if (!(current > 0.0))
            return;
        const double gain = target / current;
        for (float& v : x)
            v = static_cast<float>(static_cast<double>(v) * gain);
    };

    const auto stressVoice = [&stressScaleToRms](double hz,
                                                  int samples,
                                                  double dbfs,
                                                  bool missingFundamental,
                                                  bool breathy)
    {
        std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
        std::uint32_t rng = 0x31415926u;
        float hpMemory = 0.0f;
        float previousWhite = 0.0f;
        double phase = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            phase += 2.0 * 3.14159265358979323846 * hz / 48000.0;
            if (phase >= 2.0 * 3.14159265358979323846)
                phase -= 2.0 * 3.14159265358979323846;
            double value = (missingFundamental ? 0.045 : 1.0) * std::sin(phase)
                + 0.44 * std::sin(2.0 * phase + 0.17)
                + 0.23 * std::sin(3.0 * phase + 0.41)
                + 0.12 * std::sin(4.0 * phase + 0.73);
            if (breathy)
            {
                rng ^= rng << 13;
                rng ^= rng >> 17;
                rng ^= rng << 5;
                const float white = static_cast<float>(rng & 0xffffu) / 32767.5f - 1.0f;
                const float hp = white - previousWhite + 0.82f * hpMemory;
                previousWhite = white;
                hpMemory = hp;
                value += 0.70 * static_cast<double>(hp);
            }
            out[static_cast<std::size_t>(i)] = static_cast<float>(value);
        }
        stressScaleToRms(out, std::pow(10.0, dbfs / 20.0));
        return out;
    };

    const auto stressNoise = [](int samples, std::uint32_t seed)
    {
        std::vector<float> out(static_cast<std::size_t>(samples), 0.0f);
        float fast = 0.0f;
        float slow = 0.0f;
        for (int i = 0; i < samples; ++i)
        {
            seed ^= seed << 13;
            seed ^= seed >> 17;
            seed ^= seed << 5;
            const float white = static_cast<float>(seed & 0xffffu) / 32767.5f - 1.0f;
            fast = 0.92f * fast + 0.08f * white;
            slow = 0.992f * slow + 0.008f * white;
            out[static_cast<std::size_t>(i)] = 0.52f * white + 0.31f * fast + 0.17f * slow;
        }
        return out;
    };

    const auto stressNear = [](float measured, double expected)
    {
        return measured > 0.0f && expected > 0.0
            && std::abs(1200.0 * std::log2(static_cast<double>(measured) / expected)) <= 45.0;
    };

    const auto stressMeasure = [&stressNear](const std::vector<float>& signal, double expected)
    {
        auto detector = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
        detector->prepare(48000.0);
        detector->setRange(45.0f, 1600.0f);
        StressMetrics m;
        int correctRun = 0;
        int invalidRun = 0;
        for (int sample = 0; sample < static_cast<int>(signal.size()); ++sample)
        {
            ModernPitchEngine::PitchObservation o;
            if (!detector->processSample(signal[static_cast<std::size_t>(sample)], o))
                continue;
            if (!o.valid || !(o.correctionFrequencyHz > 0.0f))
            {
                correctRun = 0;
                if (m.stableCorrect >= 0)
                {
                    ++invalidRun;
                    m.maxInvalidAfterLock = std::max(m.maxInvalidAfterLock, invalidRun);
                }
                continue;
            }
            invalidRun = 0;
            if (m.firstValid < 0)
                m.firstValid = sample;
            ++m.valid;
            const double cents = std::abs(1200.0 * std::log2(
                static_cast<double>(o.correctionFrequencyHz) / expected));
            m.centsSum += cents;
            if (cents <= 45.0)
            {
                ++m.correct;
                if (m.firstCorrect < 0)
                    m.firstCorrect = sample;
                ++correctRun;
                if (m.stableCorrect < 0 && correctRun >= 4)
                    m.stableCorrect = sample - 96;
            }
            else
            {
                correctRun = 0;
                if (stressNear(o.correctionFrequencyHz, expected * 0.5)) ++m.halfOctave;
                else if (stressNear(o.correctionFrequencyHz, expected * 2.0)) ++m.doubleOctave;
                else ++m.other;
            }
        }
        return m;
    };

    const auto stressRun = [&](const char* label,
                               double hz,
                               double dbfs,
                               double snrDb,
                               bool missingFundamental = false,
                               bool breathy = false,
                               bool hum = false,
                               bool thunder = false)
    {
        constexpr int samples = 24000;
        auto voice = stressVoice(hz, samples, dbfs, missingFundamental, breathy);
        auto noise = stressNoise(samples,
            0x1234567u + static_cast<std::uint32_t>(hz * 17.0 + (snrDb + 20.0) * 101.0));
        const double voiceRms = stressRms(voice);
        stressScaleToRms(noise, voiceRms / std::pow(10.0, snrDb / 20.0));
        double humPhase = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            double extra = 0.0;
            if (hum)
            {
                humPhase += 2.0 * 3.14159265358979323846 * 50.0 / 48000.0;
                if (humPhase >= 2.0 * 3.14159265358979323846)
                    humPhase -= 2.0 * 3.14159265358979323846;
                const double n = voiceRms / std::pow(10.0, snrDb / 20.0);
                extra += 0.70 * n * std::sin(humPhase)
                       + 0.35 * n * std::sin(2.0 * humPhase + 0.2);
            }
            if (thunder)
            {
                const double t = static_cast<double>(i) / 48000.0;
                if (t >= 0.24 && t < 0.40)
                {
                    const double u = t - 0.24;
                    const double n = voiceRms / std::pow(10.0, snrDb / 20.0);
                    const double env = std::exp(-18.0 * u);
                    extra += 3.5 * n * env
                        * (0.75 * std::sin(2.0 * 3.14159265358979323846 * 43.0 * u)
                           + 0.25 * std::sin(2.0 * 3.14159265358979323846 * 87.0 * u + 0.7));
                }
            }
            voice[static_cast<std::size_t>(i)] += noise[static_cast<std::size_t>(i)]
                + static_cast<float>(extra);
        }
        const auto m = stressMeasure(voice, hz);
        const auto ms = [](int sample)
        {
            return sample < 0 ? -1.0 : 1000.0 * static_cast<double>(sample) / 48000.0;
        };
        std::cerr << "DETECTOR_STRESS case=" << label
                  << " hz=" << hz
                  << " voice_dbfs=" << dbfs
                  << " snr_db=" << snrDb
                  << " first_valid_ms=" << ms(m.firstValid)
                  << " first_correct_ms=" << ms(m.firstCorrect)
                  << " stable_lock_ms=" << ms(m.stableCorrect)
                  << " valid=" << m.valid
                  << " correct_fraction=" << (m.valid > 0
                        ? static_cast<double>(m.correct) / static_cast<double>(m.valid) : 0.0)
                  << " mean_abs_cents=" << (m.valid > 0 ? m.centsSum / m.valid : 0.0)
                  << " half_oct=" << m.halfOctave
                  << " double_oct=" << m.doubleOctave
                  << " other=" << m.other
                  << " max_invalid_after_lock_ms="
                  << 1000.0 * 32.0 * static_cast<double>(m.maxInvalidAfterLock) / 48000.0
                  << '\n';
    };

    for (double hz : {110.0, 220.0, 440.0})
        for (double snr : {12.0, 6.0, 3.0, 0.0, -3.0})
            stressRun("colored_noise", hz, -42.0, snr);

    for (double dbfs : {-24.0, -42.0, -54.0, -60.0})
        stressRun("quiet_voice", 220.0, dbfs, 6.0);

    stressRun("missing_fundamental", 220.0, -42.0, 3.0, true, false);
    stressRun("breathy_voice", 220.0, -42.0, 3.0, false, true);
    stressRun("mains_hum", 220.0, -42.0, 3.0, false, false, true, false);
    stressRun("thunder_transient", 220.0, -42.0, 6.0, false, false, false, true);

    {
        auto pureNoise = stressNoise(24000, 0xdeadbeefu);
        stressScaleToRms(pureNoise, std::pow(10.0, -24.0 / 20.0));
        const auto m = stressMeasure(pureNoise, 220.0);
        std::cerr << "DETECTOR_STRESS case=pure_colored_noise"
                  << " first_valid_ms=" << (m.firstValid < 0 ? -1.0
                        : 1000.0 * static_cast<double>(m.firstValid) / 48000.0)
                  << " valid=" << m.valid
                  << " half_oct=" << m.halfOctave
                  << " double_oct=" << m.doubleOctave
                  << " other=" << m.other << '\n';
    }

'''
    test = test.replace(return_anchor, diagnostic + return_anchor, 1)
    test_path.write_text(test)

print('SCALE_OWNED_HOLD_SEMANTICS_V2 materialized')
