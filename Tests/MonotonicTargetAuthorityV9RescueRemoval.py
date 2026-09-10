import re
from pathlib import Path

CPP = Path('Source/ModernPitchEngine.cpp')
text = CPP.read_text(encoding='utf-8')


def regex_once(src: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(pattern, replacement, src, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one match, got {count}')
    return out


# MONOTONIC_TARGET_AUTHORITY_V9: decode only CURRENT detector evidence. There is
# no rescue mode, anchor window, confidence permission, or stale-register veto.
# Spatial consensus may rank simultaneous hypotheses but cannot withhold the
# winning finite F0. Presence fallback accepts the strongest raw current path.
text = regex_once(
    text,
    r'ModernPitchEngine::MultiRatePitchTracker::DecoderDecision\nModernPitchEngine::MultiRatePitchTracker::decodeCandidate\(bool onsetPending\) noexcept\n\{.*?\n\}\n\nbool ModernPitchEngine::MultiRatePitchTracker::confirmOctaveTransition',
    '''ModernPitchEngine::MultiRatePitchTracker::DecoderDecision
ModernPitchEngine::MultiRatePitchTracker::decodeCandidate(bool onsetPending) noexcept
{
    std::array<PitchCandidate, detectorPathCount> candidates {};
    const int candidateCount = collectFreshCandidates(candidates);
    if (candidateCount <= 0)
        return {};

    const auto makeCurrentFallback = [&]() noexcept
    {
        DecoderDecision fallback;
        float bestScore = -1000.0f;
        for (int index = 0; index < candidateCount; ++index)
        {
            const auto& raw = candidates[static_cast<std::size_t>(index)];
            if (!raw.valid || !std::isfinite(raw.frequencyHz) || raw.frequencyHz <= 0.0f)
                continue;

            const float score = candidateBaseScore(raw)
                * (0.65f + 0.35f * pathReliability(raw.pathIndex, raw.frequencyHz));
            if (score <= bestScore)
                continue;

            bestScore = score;
            fallback.candidate = raw;
            fallback.candidate.valid = true;
            fallback.consensus = 0.0f;
            fallback.supportCount = 1;
            fallback.directSupportCount = 1;
            fallback.freshSupportMask = raw.ageInHops == 0 && raw.pathIndex >= 0
                ? static_cast<std::uint8_t>(1u << raw.pathIndex) : 0;
            fallback.decoderOctaveIndex = octaveState_;
            fallback.valid = true;
        }
        return fallback;
    };

    std::array<ConsensusHypothesis, maxConsensusHypotheses> hypotheses {};
    const int hypothesisCount = buildConsensusHypotheses(
        candidates, candidateCount, hypotheses);
    if (hypothesisCount <= 0)
        return makeCurrentFallback();

    updateDecoderBeam(hypotheses, hypothesisCount, onsetPending);
    if (!decoderBeam_[0].valid)
        return makeCurrentFallback();

    const float decodedFrequency = static_cast<float>(
        std::exp2(decoderBeam_[0].logFrequency));
    int matchedHypothesis = -1;
    float matchedDistance = std::numeric_limits<float>::infinity();
    for (int index = 0; index < hypothesisCount; ++index)
    {
        const auto& hypothesis = hypotheses[static_cast<std::size_t>(index)];
        if (!hypothesis.valid)
            continue;
        const float distance = centsDistance(hypothesis.frequencyHz, decodedFrequency);
        if (distance < matchedDistance)
        {
            matchedDistance = distance;
            matchedHypothesis = index;
        }
    }

    if (matchedHypothesis < 0)
        return makeCurrentFallback();

    const auto& hypothesis = hypotheses[static_cast<std::size_t>(matchedHypothesis)];
    DecoderDecision decision;
    decision.candidate.frequencyHz = hypothesis.frequencyHz;
    decision.candidate.confidence = hypothesis.confidence;
    decision.candidate.periodicity = hypothesis.periodicity;
    decision.candidate.valid = std::isfinite(hypothesis.frequencyHz)
        && hypothesis.frequencyHz > 0.0f;
    decision.consensus = hypothesis.consensus;
    decision.supportCount = hypothesis.supportCount;
    decision.directSupportCount = hypothesis.directSupportCount;
    decision.freshSupportMask = hypothesis.freshSupportMask;
    decision.decoderOctaveIndex = decoderBeam_[0].octaveIndex;
    decision.valid = decision.candidate.valid;
    return decision.valid ? decision : makeCurrentFallback();
}

bool ModernPitchEngine::MultiRatePitchTracker::confirmOctaveTransition''',
    'replace rescue decoder with current evidence only')

# Remove the 60 ms delayed second detector regime in dual-mono processing.
text = regex_once(
    text,
    r'''                if \(correction\.noteBodyLatched && correction\.transportPeriodHz > 0\.0\)\n                    tracker\.setReacquisitionAnchor\(static_cast<float>\(correction\.transportPeriodHz\)\);\n                else\n                    tracker\.clearReacquisitionAnchor\(\);\n                const bool rescueSearch = correction\.noteBodyLatched\n                    && correction\.pitchStaleSamples >= static_cast<int>\(0\.060 \* sampleRate_\);\n                tracker\.setRange\(rescueSearch \? std::min\(safe\.minimumPitchHz, 28\.0f\) : safe\.minimumPitchHz,\n                                 safe\.maximumPitchHz\);\n                tracker\.setSensitivity\(rescueSearch \? std::max\(safe\.detectorSensitivity, 0\.98f\)\n                                                    : safe\.detectorSensitivity\);\n                tracker\.setRescueMode\(rescueSearch\); // PITCH_RESCUE_V1\n''',
    '''                // MONOTONIC_TARGET_AUTHORITY_V9: one detector regime only.
                // No stale timer, no anchor-owned register, no rescue sensitivity.
                tracker.setRange(safe.minimumPitchHz, safe.maximumPitchHz);
                tracker.setSensitivity(safe.detectorSensitivity);
''',
    'remove dual-mono delayed rescue regime')

# Remove the same delayed second detector regime in linked processing.
text = regex_once(
    text,
    r'''            if \(linkedCorrection_\.noteBodyLatched && linkedCorrection_\.transportPeriodHz > 0\.0\)\n                linkedTracker_\.setReacquisitionAnchor\(\n                    static_cast<float>\(linkedCorrection_\.transportPeriodHz\)\);\n            else\n                linkedTracker_\.clearReacquisitionAnchor\(\);\n            const bool rescueSearch = linkedCorrection_\.noteBodyLatched\n                && linkedCorrection_\.pitchStaleSamples >= static_cast<int>\(0\.060 \* sampleRate_\);\n            linkedTracker_\.setRange\(rescueSearch \? std::min\(safe\.minimumPitchHz, 28\.0f\) : safe\.minimumPitchHz,\n                                    safe\.maximumPitchHz\);\n            linkedTracker_\.setSensitivity\(rescueSearch \? std::max\(safe\.detectorSensitivity, 0\.98f\)\n                                                       : safe\.detectorSensitivity\);\n            linkedTracker_\.setRescueMode\(rescueSearch\); // PITCH_RESCUE_V1\n''',
    '''            // MONOTONIC_TARGET_AUTHORITY_V9: one detector regime only.
            linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);
            linkedTracker_.setSensitivity(safe.detectorSensitivity);
''',
    'remove linked delayed rescue regime')

for needle in (
    'rescueSearch',
    'sameNoteRescueCents',
    'wideRescueCents',
    'wideRescueChallenger',
    'rescueEvidence',
    'PITCH_RESCUE_V3_REGISTER_GUARD',
    '0.060 * sampleRate_',
    'std::max(safe.detectorSensitivity, 0.98f)',
):
    if needle in text:
        raise SystemExit(f'forbidden delayed rescue token remains: {needle}')

CPP.write_text(text, encoding='utf-8')
print('MONOTONIC_TARGET_AUTHORITY_V9_RESCUE_REMOVAL=PASS')
