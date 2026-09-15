from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
test_path = root / 'Tests' / 'SupervisorContinuityTest.cpp'
cpp = cpp_path.read_text()
test = test_path.read_text()


def one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


# SOLITARY_VOICE_STRUCTURE_V3
# Diagnostics show the remaining false positives are isolated full-rate
# formant resonances with no direct support from any other rate and cleanliness
# <= ~0.65.  A lone detector path therefore needs stronger source structure.
# Multi-path evidence keeps the existing sensitive thresholds, so this is not a
# detector-wide confidence gate and cannot become musical hold/prudence.
cpp = one(cpp,
'''            const float solitaryCleanFloor = candidate.pathIndex <= 1 ? 0.52f
                : (candidate.pathIndex == 2 ? 0.62f : 0.70f);
''',
'''            // SOLITARY_VOICE_STRUCTURE_V3: isolated full-band formant peaks
            // measured in regression top out below ~0.65 cleanliness. A real
            // lone F0 must show source structure beyond that measured region.
            const float solitaryCleanFloor = candidate.pathIndex == 0 ? 0.68f
                : (candidate.pathIndex == 1 ? 0.66f
                   : (candidate.pathIndex == 2 ? 0.62f : 0.70f));
''',
'raw solitary source floor')

cpp = one(cpp,
'''        const float solitaryHypothesisFloor = rescueMode_ ? 0.56f : 0.60f;
''',
'''        const float solitaryHypothesisFloor = rescueMode_ ? 0.64f : 0.68f;
''',
'consensus solitary source floor')

# Provisional means a plausible physical pitch coordinate, not merely an
# autocorrelation peak.  Keep it detector-only but stop publishing the measured
# formant-resonance region as a provisional F0.  Strong multi-path candidates
# still become valid through consensus independently of this fallback.
cpp = one(cpp,
'''            const float provisionalPathFloor = candidate.pathIndex <= 0 ? 0.22f
                : (candidate.pathIndex == 1 ? 0.24f
                   : (candidate.pathIndex == 2 ? 0.46f : 0.54f));
''',
'''            const float provisionalPathFloor = candidate.pathIndex == 0 ? 0.68f
                : (candidate.pathIndex == 1 ? 0.66f
                   : (candidate.pathIndex == 2 ? 0.62f : 0.70f));
''',
'provisional solitary source floor')

# Positive control for the one place where this stronger solitary rule matters:
# a genuine high F0 above the half-rate direct band. It must still acquire from
# the full-rate path quickly and accurately in broadband noise.
anchor = '''    // LOW_RATE_RESONANCE_VETO_V1 positive control: true low F0 remains\n'''
if test.count(anchor) != 1:
    raise RuntimeError(f'high-F0 positive control anchor: expected one, found {test.count(anchor)}')

high_test = r'''    // SOLITARY_VOICE_STRUCTURE_V3 positive control: at 1100 Hz the full-rate
    // detector is the only direct F0 authority. A real harmonic source must not
    // be lost merely because solitary resonances are now rejected more strictly.
    auto highVoiceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    highVoiceTracker->prepare(48000.0);
    highVoiceTracker->setRange(700.0f, 1500.0f);
    std::uint32_t highVoiceRng = 0x31415926u;
    float highNoiseLp = 0.0f;
    int highVoiceValid = 0;
    int highVoiceNear = 0;
    constexpr float highVoiceHz = 1100.0f;
    for (int sample = 0; sample < 24000; ++sample)
    {
        highVoiceRng ^= highVoiceRng << 13;
        highVoiceRng ^= highVoiceRng >> 17;
        highVoiceRng ^= highVoiceRng << 5;
        const float white = static_cast<float>(highVoiceRng & 0xffffu) / 32767.5f - 1.0f;
        highNoiseLp = 0.86f * highNoiseLp + 0.14f * white;
        const float noise = 0.008f * (0.72f * white + 0.28f * highNoiseLp);
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * static_cast<double>(highVoiceHz) * static_cast<double>(sample) / 48000.0);
        const float voice = 0.065f * std::sin(phase)
                          + 0.020f * std::sin(2.0f * phase + 0.17f)
                          + 0.010f * std::sin(3.0f * phase + 0.41f);
        ModernPitchEngine::PitchObservation observed;
        if (highVoiceTracker->processSample(voice + noise, observed)
            && sample > 6000 && observed.valid)
        {
            ++highVoiceValid;
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / highVoiceHz));
            if (cents < 45.0f)
                ++highVoiceNear;
        }
    }
    std::cerr << "solitary_high_voice_valid=" << highVoiceValid
              << " near_1100=" << highVoiceNear << '\n';
    success &= check(highVoiceValid > 20
                     && highVoiceNear * 4 >= highVoiceValid * 3,
                     "solitary_full_rate_real_voice_survives_structure_veto");

'''
test = test.replace(anchor, high_test + anchor, 1)

cpp_path.write_text(cpp)
test_path.write_text(test)
print('SOLITARY_VOICE_STRUCTURE_V3 materialized')
