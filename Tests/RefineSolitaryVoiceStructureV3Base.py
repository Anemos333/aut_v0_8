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


# DIRECT_HIGH_YIN_FIRST_MINIMUM_V1
# At high F0 the full-rate path is the only path that can directly observe the
# real fundamental. Diagnostics on a clean 1100-Hz source show that YIN's first
# threshold minimum is normally the correct ~1100-Hz period, while a slightly
# higher raw score can occasionally promote its 1/2 subharmonic (~550 Hz).
# Prefer the first threshold basin only when it is itself a strong, clean vocal
# candidate above the half-rate direct-F0 ceiling. The preference affects
# candidate selection only: confidence keeps the unboosted physical score.
analyse_start = cpp.find('ModernPitchEngine::MultiRatePitchTracker::analyse(')
analyse_end = cpp.find('float ModernPitchEngine::MultiRatePitchTracker::centsDistance(',
                       analyse_start)
if analyse_start < 0 or analyse_end < 0:
    raise RuntimeError('high-F0 YIN refinement: analyse block not found')
analyse = cpp[analyse_start:analyse_end]

if analyse.count('    float bestScore = -1.0f;\n') != 1:
    raise RuntimeError('high-F0 YIN refinement: best score anchor not unique')
analyse = analyse.replace(
    '    float bestScore = -1.0f;\n',
    '    float bestScore = -1.0f;\n'
    '    float bestSelectionScore = -1.0f;\n',
    1)

selection_anchor = '''        if (score > bestScore)
        {
            bestScore = score;
            bestTau = tau;
'''
if analyse.count(selection_anchor) != 1:
    raise RuntimeError('high-F0 YIN refinement: selection anchor not unique')
analyse = analyse.replace(
    selection_anchor,
'''        // DIRECT_HIGH_YIN_FIRST_MINIMUM_V1: selection-only preference.
        // Never inflate the published confidence/evidence score.
        const bool directHighThresholdCandidate = thresholdTau >= 0
            && candidateIndex == 0
            && effectiveSampleRate >= sampleRate_ * 0.75
            && effectiveSampleRate / static_cast<double>(std::max(1, tau)) > 900.0
            && harmonicFamily >= 0.60f
            && tonalCleanliness >= 0.68f;
        const float selectionScore = score
            * (directHighThresholdCandidate ? 1.35f : 1.0f);

        if (selectionScore > bestSelectionScore)
        {
            bestSelectionScore = selectionScore;
            bestScore = score;
            bestTau = tau;
''',
    1)
cpp = cpp[:analyse_start] + analyse + cpp[analyse_end:]

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

# DIRECT_HIGH_PATH_OWNS_RATIONAL_ALIAS_V2
# Above the half-rate direct-F0 ceiling, only full rate can directly measure the
# coordinate. Half/quarter-rate paths can nevertheless line up exactly at 1/2
# and 1/3 of that source because decimation preserves the harmonic family. They
# are corroboration, not competing F0 coordinates. This is applied only when a
# fresh, independently voice-clean full-rate measurement exists.
cpp = one(cpp,
'''    // DETECTOR_VETO_NOT_PERMISSION_V1: once a current finite measurement has
    // survived the detector's falsification stages, low confidence/consensus
    // cannot make it invalid. Octave/subharmonic ambiguity is handled below by
    // confirmOctaveTransition() as an explicit, bounded veto.
    decision.valid = true;
    return decision;
''',
'''    // DIRECT_HIGH_PATH_OWNS_RATIONAL_ALIAS_V2
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
        int aliasDivisor = 0;
        for (int divisor = 2; divisor <= 3; ++divisor)
        {
            const float expanded = static_cast<float>(divisor)
                * decision.candidate.frequencyHz;
            if (centsDistance(expanded, freshFull.frequencyHz) <= 55.0f)
            {
                aliasDivisor = divisor;
                break;
            }
        }

        if (aliasDivisor != 0)
        {
            // Lower-rate rational aliases verify periodic family membership but
            // cannot own a coordinate outside their direct measurement band.
            // Single-path consensus semantics make the authority explicit.
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
'high direct-path rational alias ownership')

# Positive control for the one place where this stronger solitary rule matters:
# a genuine high F0 above the half-rate direct band. It must still acquire from
# the full-rate path quickly and accurately in broadband noise. Use the normal
# broad production range so the 1/2 and 1/3 subharmonics are present and the
# path-ownership rule is genuinely exercised.
anchor = '''    // LOW_RATE_RESONANCE_VETO_V1 positive control: true low F0 remains\n'''
if test.count(anchor) != 1:
    raise RuntimeError(f'high-F0 positive control anchor: expected one, found {test.count(anchor)}')

high_test = r'''    // SOLITARY_VOICE_STRUCTURE_V3 positive control: at 1100 Hz the full-rate
    // detector is the only direct F0 authority. Lower-rate paths may observe
    // exact 1/2 and 1/3 aliases but may not drag a real 1100-Hz source downward.
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

    // DIRECT_HIGH_PATH_OWNS_RATIONAL_ALIAS_V2 negative control: the resolver
    // must not double a genuine 550-Hz source merely because 1100 Hz is a strong
    // harmonic. A real 550-Hz first YIN basin remains at 550 and is unaffected.
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
    'DIRECT_HIGH_YIN_FIRST_MINIMUM_V1',
    'SOLITARY_VOICE_STRUCTURE_V3',
    'PATH_REJECTED_CANDIDATE_IS_NOT_PROVISIONAL_V1',
    'DIRECT_HIGH_PATH_OWNS_RATIONAL_ALIAS_V2'
]:
    if marker not in cpp:
        raise RuntimeError(f'missing marker {marker}')

cpp_path.write_text(cpp)
test_path.write_text(test)
print('SOLITARY_VOICE_STRUCTURE_V3 materialized')
