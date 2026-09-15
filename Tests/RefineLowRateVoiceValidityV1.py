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


# LOW_RATE_RESONANCE_VETO_V1
# Quarter/eighth-rate paths retain excellent period geometry but deliberately
# discard spectral detail.  A lone resonant pole therefore needs stronger
# source-cleanliness evidence before that path itself may call the estimate a
# trusted vocal F0.  The estimate can still exist provisionally and can still be
# corroborated by the other detector rates; no audio/rendering path is touched.
cpp = one(cpp,
'''    const float trustedFamilyFloor = rescueMode_ ? 0.27f : 0.32f;
    const float trustedCleanlinessFloor = rescueMode_ ? 0.22f : 0.26f;
    result.valid = structurallyTrusted
        && bestScore >= minimumCandidateScore
        && bestHarmonicFamily >= trustedFamilyFloor
        && bestTonalCleanliness >= trustedCleanlinessFloor;
''',
'''    const float trustedFamilyFloor = rescueMode_ ? 0.27f : 0.32f;
    // LOW_RATE_RESONANCE_VETO_V1: effectiveSampleRate identifies the analysis
    // rate inside analyse(). Low-rate paths may locate periods with little
    // remaining spectral evidence, so they need a cleaner source before they
    // independently assert "vocal F0". This is not a detector-wide threshold.
    const double analysisRateRatio = effectiveSampleRate / std::max(1.0, sampleRate_);
    const float trustedCleanlinessFloor = analysisRateRatio <= 0.14
        ? (rescueMode_ ? 0.40f : 0.46f)
        : (analysisRateRatio <= 0.30
            ? (rescueMode_ ? 0.36f : 0.40f)
            : (rescueMode_ ? 0.22f : 0.26f));
    result.valid = structurallyTrusted
        && bestScore >= minimumCandidateScore
        && bestHarmonicFamily >= trustedFamilyFloor
        && bestTonalCleanliness >= trustedCleanlinessFloor;
''',
'low-rate trusted cleanliness')

# Positive control: a real low vocal family whose fundamental lives below the
# half-rate direct-F0 band must remain detectable through quarter/eighth paths.
# This protects the change from becoming a blunt low-frequency rejection.
test = one(test,
'''    // OBSERVATION_MEMORY_IS_FALSIFIABLE_V1: emulate a stale wrong register
''',
'''    // LOW_RATE_RESONANCE_VETO_V1 positive control: true low F0 remains
    // measurable even though quarter/eighth paths use a stricter cleanliness
    // requirement than full/half rate.
    auto lowVoiceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    lowVoiceTracker->prepare(48000.0);
    lowVoiceTracker->setRange(45.0f, 320.0f);
    constexpr float lowVoiceHz = 60.0f;
    int lowVoiceValid = 0;
    int lowVoiceNearFundamental = 0;
    for (int sample = 0; sample < 36000; ++sample)
    {
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * static_cast<double>(lowVoiceHz) * static_cast<double>(sample) / 48000.0);
        const float voice = 0.075f * std::sin(phase)
                          + 0.024f * std::sin(2.0f * phase + 0.19f)
                          + 0.012f * std::sin(3.0f * phase + 0.43f)
                          + 0.006f * std::sin(4.0f * phase + 0.71f);
        ModernPitchEngine::PitchObservation observed;
        if (lowVoiceTracker->processSample(voice, observed)
            && sample > 12000 && observed.valid)
        {
            ++lowVoiceValid;
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / lowVoiceHz));
            if (cents < 45.0f)
                ++lowVoiceNearFundamental;
        }
    }
    std::cerr << "low_rate_voice_valid=" << lowVoiceValid
              << " near_60=" << lowVoiceNearFundamental << '\\n';
    success &= check(lowVoiceValid > 20
                     && lowVoiceNearFundamental * 4 >= lowVoiceValid * 3,
                     "low_rate_real_voice_survives_resonance_veto");

    // OBSERVATION_MEMORY_IS_FALSIFIABLE_V1: emulate a stale wrong register
''',
'low-rate real voice positive control')

cpp_path.write_text(cpp)
test_path.write_text(test)
print('LOW_RATE_RESONANCE_VETO_V1 materialized')
