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
# autocorrelation peak. Keep it detector-only but stop publishing the measured
# formant-resonance region as a provisional F0. Strong multi-path candidates
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

# PATH_REJECTED_CANDIDATE_IS_NOT_PROVISIONAL_V1
# The last formant-noise diagnostics show that all remaining provisional
# coordinates come from candidates whose own analysis path has already set
# valid=false. Such a coordinate is useful as internal autocorrelation evidence,
# but it is not a plausible physical F0 and must not escape as measurementAvailable.
# This is stricter semantics, not a confidence threshold: a path-valid candidate
# can still be provisional when consensus/decoder evidence is insufficient.
provisional_start = cpp.find('    const auto chooseProvisionalMeasurement = [this]() noexcept\n')
provisional_end = cpp.find('    const PitchCandidate provisionalMeasurement = chooseProvisionalMeasurement();\n',
                           provisional_start)
if provisional_start < 0 or provisional_end < 0:
    raise RuntimeError('path-invalid provisional veto: provisional block not found')
provisional_block = cpp[provisional_start:provisional_end]
provisional_candidate_anchor = '            const auto& candidate = slot.candidate;\n'
if provisional_block.count(provisional_candidate_anchor) != 1:
    raise RuntimeError('path-invalid provisional veto: candidate anchor is not unique')
provisional_block = provisional_block.replace(
    provisional_candidate_anchor,
    provisional_candidate_anchor
    + '            // PATH_REJECTED_CANDIDATE_IS_NOT_PROVISIONAL_V1\n'
    + '            if (!candidate.valid)\n'
    + '                return;\n',
    1)
cpp = cpp[:provisional_start] + provisional_block + cpp[provisional_end:]

# DIRECT_HIGH_PATH_OWNS_OCTAVE_CONFLICT_V1
# Above the half-rate direct-F0 ceiling, a fresh qualified full-rate coordinate
# is the only detector path that can directly measure that F0. A half-rate
# candidate exactly one octave below is still valuable harmonic evidence, but
# it cannot steer the published coordinate down by an octave. This runs after
# consensus ranking, only for an explicit octave conflict, and changes analysis
# coordinates only; musical target ownership and rendering remain untouched.
cpp = one(cpp,
'''    // DETECTOR_VETO_NOT_PERMISSION_V1: once a current finite measurement has
    // survived the detector's falsification stages, low confidence/consensus
    // cannot make it invalid. Octave/subharmonic ambiguity is handled below by
    // confirmOctaveTransition() as an explicit, bounded veto.
    decision.valid = true;
    return decision;
''',
'''    // DIRECT_HIGH_PATH_OWNS_OCTAVE_CONFLICT_V1
    constexpr float halfRateDirectMaximumHz = 900.0f;
    const auto& freshFull = fullRateCandidate_.candidate;
    const bool freshQualifiedHighFull = fullRateCandidate_.ageInHops == 0
        && freshFull.valid
        && std::isfinite(freshFull.frequencyHz)
        && freshFull.frequencyHz > halfRateDirectMaximumHz
        && freshFull.harmonicFamily >= 0.68f
        && freshFull.tonalCleanliness >= 0.68f;
    if (freshQualifiedHighFull
        && decision.candidate.frequencyHz > 0.0f)
    {
        const float doubledDecision = 2.0f * decision.candidate.frequencyHz;
        const float octaveConflictCents = centsDistance(doubledDecision,
                                                         freshFull.frequencyHz);
        if (octaveConflictCents <= 55.0f)
        {
            // The lower-rate path verifies the harmonic family but does not
            // own the high-register coordinate. Publish the direct full-rate
            // observation with deliberately single-path consensus semantics.
            decision.candidate = freshFull;
            decision.candidate.valid = true;
            decision.consensus = 0.0f;
            decision.supportCount = 1;
            decision.directSupportCount = 1;
            decision.freshSupportMask = static_cast<std::uint8_t>(1u);
            decision.decoderOctaveIndex = octaveState_;
        }
    }

    // DETECTOR_VETO_NOT_PERMISSION_V1: once a current finite measurement has
    // survived the detector's falsification stages, low confidence/consensus
    // cannot make it invalid. Octave/subharmonic ambiguity is handled below by
    // confirmOctaveTransition() as an explicit, bounded veto.
    decision.valid = true;
    return decision;
''',
'high direct-path octave ownership')

# Positive control for the one place where this stronger solitary rule matters:
# a genuine high F0 above the half-rate direct band. It must still acquire from
# the full-rate path quickly and accurately in broadband noise. Use the normal
# broad production range so the half-rate 550-Hz subharmonic is present and the
# path-ownership rule is genuinely exercised.
anchor = '''    // LOW_RATE_RESONANCE_VETO_V1 positive control: true low F0 remains\n'''
if test.count(anchor) != 1:
    raise RuntimeError(f'high-F0 positive control anchor: expected one, found {test.count(anchor)}')

high_test = r'''    // SOLITARY_VOICE_STRUCTURE_V3 positive control: at 1100 Hz the full-rate
    // detector is the only direct F0 authority. The half-rate path can observe
    // the 550-Hz octave family but may not drag a real 1100-Hz source downward.
    auto highVoiceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    highVoiceTracker->prepare(48000.0);
    highVoiceTracker->setRange(45.0f, 1500.0f);
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

    // DIRECT_HIGH_PATH_OWNS_OCTAVE_CONFLICT_V1 negative control: the resolver
    // must not double a genuine 550-Hz source merely because 1100 Hz is a strong
    // second harmonic. If full-rate correctly reports 550 there is no conflict
    // to override, and the lower octave remains authoritative.
    auto midHighVoiceTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    midHighVoiceTracker->prepare(48000.0);
    midHighVoiceTracker->setRange(45.0f, 1500.0f);
    std::uint32_t midHighRng = 0x27182818u;
    float midHighNoiseLp = 0.0f;
    int midHighValid = 0;
    int midHighNear = 0;
    constexpr float midHighHz = 550.0f;
    for (int sample = 0; sample < 24000; ++sample)
    {
        midHighRng ^= midHighRng << 13;
        midHighRng ^= midHighRng >> 17;
        midHighRng ^= midHighRng << 5;
        const float white = static_cast<float>(midHighRng & 0xffffu) / 32767.5f - 1.0f;
        midHighNoiseLp = 0.86f * midHighNoiseLp + 0.14f * white;
        const float noise = 0.008f * (0.72f * white + 0.28f * midHighNoiseLp);
        const float phase = static_cast<float>(2.0 * 3.14159265358979323846
            * static_cast<double>(midHighHz) * static_cast<double>(sample) / 48000.0);
        const float voice = 0.060f * std::sin(phase)
                          + 0.026f * std::sin(2.0f * phase + 0.23f)
                          + 0.011f * std::sin(3.0f * phase + 0.37f);
        ModernPitchEngine::PitchObservation observed;
        if (midHighVoiceTracker->processSample(voice + noise, observed)
            && sample > 6000 && observed.valid)
        {
            ++midHighValid;
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / midHighHz));
            if (cents < 45.0f)
                ++midHighNear;
        }
    }
    std::cerr << "mid_high_voice_valid=" << midHighValid
              << " near_550=" << midHighNear << '\n';
    success &= check(midHighValid > 20
                     && midHighNear * 4 >= midHighValid * 3,
                     "high_path_octave_resolver_does_not_double_real_550_hz_voice");

'''
test = test.replace(anchor, high_test + anchor, 1)

for marker in [
    'SOLITARY_VOICE_STRUCTURE_V3',
    'PATH_REJECTED_CANDIDATE_IS_NOT_PROVISIONAL_V1',
    'DIRECT_HIGH_PATH_OWNS_OCTAVE_CONFLICT_V1'
]:
    if marker not in cpp:
        raise RuntimeError(f'missing marker {marker}')

cpp_path.write_text(cpp)
test_path.write_text(test)
print('SOLITARY_VOICE_STRUCTURE_V3 materialized')
