from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
h_path = root / 'Source' / 'ModernPitchEngine.h'
live_path = root / 'Source' / 'LivePitchProcessor.h'
voice_path = root / 'Source' / 'VoiceEvidenceAnalyzer.h'

cpp = cpp_path.read_text()
h = h_path.read_text()
live = live_path.read_text()
voice = voice_path.read_text()


def one(text, anchor, replacement, label):
    count = text.count(anchor)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(anchor, replacement, 1)


marker = 'VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7'
if marker not in cpp:
    # V6.7 does not create another confidence voter. It separates two things:
    #   1) coordinate authority, still owned by current path geometry; and
    #   2) permission to inspect the octave below, granted only by a concrete
    #      current vocal-body/subharmonic signature or independent direct paths.
    #
    # VoiceEvidenceAnalyzer already computes the half-frequency probe used for
    # polyphony diagnostics. V6.7 publishes that physical observation as a
    # dedicated lowerFamilyEvidence value, body-gated with metrics that already
    # exist. No new FFT, YIN pass, lag scan, renderer path or scale logic is added.

    # ------------------------------------------------------------------
    # VoiceEvidenceAnalyzer: expose the already-computed F/2 body evidence.
    voice = one(
        voice,
        '''        float voicedBodyEnergy = 0.0f;\n        float polyphonyRisk = 0.0f;\n''',
        '''        float voicedBodyEnergy = 0.0f;\n        float polyphonyRisk = 0.0f;\n        // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7: physical permission to\n        // inspect a longer period below the currently observed family. This is\n        // not an F0 vote and never attenuates an upper coordinate by itself.\n        float lowerFamilyEvidence = 0.0f;\n''',
        'voice evidence field')

    voice = one(
        voice,
        '''        const float voicedBodyRaw = clamp01(\n            std::sqrt(std::max(0.0f, lowRatio + midRatio))\n            * smoothStep(0.0008f, 0.010f, rms));\n\n        Evidence target;\n''',
        '''        const float voicedBodyRaw = clamp01(\n            std::sqrt(std::max(0.0f, lowRatio + midRatio))\n            * smoothStep(0.0008f, 0.010f, rms));\n\n        // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7\n        // subharmonicConflict already measures the F/2 probe against the\n        // current harmonic family. Convert it into permission to LOOK below\n        // only when the same block has coherent vocal body. A breath/formant\n        // cannot earn lower-register authority merely by containing low energy.\n        const float lowerFamilyEvidenceRaw = clamp01(\n            smoothStep(0.08f, 0.34f, subharmonicConflict)\n            * std::sqrt(std::max(0.0f, harmonicityRaw * voicedBodyRaw))\n            * (0.65f + 0.35f * formantStability)\n            * (1.0f - 0.35f * breathRaw));\n\n        Evidence target;\n''',
        'lower family physical evidence')

    voice = one(
        voice,
        '''        target.voicedBodyEnergy = voicedBodyRaw;\n        target.polyphonyRisk = polyphonyRaw;\n''',
        '''        target.voicedBodyEnergy = voicedBodyRaw;\n        target.polyphonyRisk = polyphonyRaw;\n        target.lowerFamilyEvidence = lowerFamilyEvidenceRaw;\n''',
        'lower family target')

    voice = one(
        voice,
        '''            result.voicedBodyEnergy = voicedBodyEnergy_.load(std::memory_order_relaxed);\n            result.polyphonyRisk = polyphonyRisk_.load(std::memory_order_relaxed);\n''',
        '''            result.voicedBodyEnergy = voicedBodyEnergy_.load(std::memory_order_relaxed);\n            result.polyphonyRisk = polyphonyRisk_.load(std::memory_order_relaxed);\n            result.lowerFamilyEvidence = lowerFamilyEvidence_.load(std::memory_order_relaxed);\n''',
        'lower family latest')

    voice = one(
        voice,
        '''        smoothMetric(smoothed_.polyphonyRisk, target.polyphonyRisk,\n                     mediumAttack, slowRelease);\n''',
        '''        smoothMetric(smoothed_.polyphonyRisk, target.polyphonyRisk,\n                     mediumAttack, slowRelease);\n        // Permission must appear quickly on a real downward note change, but\n        // must also disappear faster than broad polyphony suspicion.\n        smoothMetric(smoothed_.lowerFamilyEvidence, target.lowerFamilyEvidence,\n                     fastAttack, coefficient(45.0f));\n''',
        'lower family smoothing')

    voice = one(
        voice,
        '''        smoothed_.polyphonyRisk += release * (0.0f - smoothed_.polyphonyRisk);\n''',
        '''        smoothed_.polyphonyRisk += release * (0.0f - smoothed_.polyphonyRisk);\n        smoothed_.lowerFamilyEvidence += release * (0.0f - smoothed_.lowerFamilyEvidence);\n''',
        'lower family silence decay')

    voice = one(
        voice,
        '''        polyphonyRisk_.store(evidence.polyphonyRisk, std::memory_order_relaxed);\n        sequence_.fetch_add(1u, std::memory_order_release);\n''',
        '''        polyphonyRisk_.store(evidence.polyphonyRisk, std::memory_order_relaxed);\n        lowerFamilyEvidence_.store(evidence.lowerFamilyEvidence, std::memory_order_relaxed);\n        sequence_.fetch_add(1u, std::memory_order_release);\n''',
        'lower family publish')

    voice = one(
        voice,
        '''    std::atomic<float> polyphonyRisk_ { 0.0f };\n};\n''',
        '''    std::atomic<float> polyphonyRisk_ { 0.0f };\n    std::atomic<float> lowerFamilyEvidence_ { 0.0f };\n};\n''',
        'lower family atomic')

    # ------------------------------------------------------------------
    # LivePitchProcessor -> ModernPitchEngine analysis-only plumbing.
    live = one(
        live,
        '''        conditioned.voiceFormantStability = std::clamp(\n            evidence.formantStability, 0.0f, 1.0f);\n        return conditioned;\n''',
        '''        conditioned.voiceFormantStability = std::clamp(\n            evidence.formantStability, 0.0f, 1.0f);\n        conditioned.voiceLowerFamilyEvidence = std::clamp(\n            evidence.lowerFamilyEvidence, 0.0f, 1.0f);\n        return conditioned;\n''',
        'live lower family plumbing')

    # ------------------------------------------------------------------
    # ModernPitchEngine public analysis parameters and tracker context.
    h = one(
        h,
        '''        float voiceEventStrength = 0.0f;\n        float voiceFormantStability = 0.0f;\n''',
        '''        float voiceEventStrength = 0.0f;\n        float voiceFormantStability = 0.0f;\n        // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7: analysis-only. This value\n        // permits lower-family inspection; it never reduces correction depth.\n        float voiceLowerFamilyEvidence = 0.0f;\n''',
        'engine lower family parameter')

    h = one(
        h,
        '''        void setSensitivity(float sensitivity) noexcept;\n        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }\n''',
        '''        void setSensitivity(float sensitivity) noexcept;\n        void setVoiceAuthorityContext(bool valid,\n                                      float harmonicity,\n                                      float breathiness,\n                                      float bodyEnergy,\n                                      float spectralReliability,\n                                      float eventStrength,\n                                      float formantStability,\n                                      float lowerFamilyEvidence) noexcept;\n        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }\n''',
        'tracker voice context setter')

    h = one(
        h,
        '''        [[nodiscard]] float candidateBaseScore(const PitchCandidate& candidate) const noexcept;\n        [[nodiscard]] static float centsDistance(float frequencyA,\n''',
        '''        [[nodiscard]] float candidateBaseScore(const PitchCandidate& candidate) const noexcept;\n        [[nodiscard]] float voiceBodyAuthorityV67() const noexcept;\n        [[nodiscard]] bool voiceAllowsLowerFamilyV67() const noexcept;\n        [[nodiscard]] static float centsDistance(float frequencyA,\n''',
        'voice authority helpers')

    h = one(
        h,
        '''        bool observationContinuityBroken_ = false;\n\n        std::array<float, ringSize> fullRateRing_ {};\n''',
        '''        bool observationContinuityBroken_ = false;\n\n        // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7: previous-block causal\n        // supervision only. Coordinate measurements still come exclusively\n        // from the detector paths below.\n        bool voiceAuthorityContextValid_ = false;\n        float voiceAuthorityHarmonicity_ = 0.0f;\n        float voiceAuthorityBreathiness_ = 0.0f;\n        float voiceAuthorityBodyEnergy_ = 0.0f;\n        float voiceAuthoritySpectralReliability_ = 0.0f;\n        float voiceAuthorityEventStrength_ = 0.0f;\n        float voiceAuthorityFormantStability_ = 0.0f;\n        float voiceAuthorityLowerFamilyEvidence_ = 0.0f;\n\n        std::array<float, ringSize> fullRateRing_ {};\n''',
        'tracker voice context storage')

    # ------------------------------------------------------------------
    # ModernPitchEngine implementation.
    cpp = one(
        cpp,
        '''    transitionWake_ = false;\n    observationContinuityBroken_ = false;\n\n    octaveState_ = 0;\n''',
        '''    transitionWake_ = false;\n    observationContinuityBroken_ = false;\n    voiceAuthorityContextValid_ = false;\n    voiceAuthorityHarmonicity_ = 0.0f;\n    voiceAuthorityBreathiness_ = 0.0f;\n    voiceAuthorityBodyEnergy_ = 0.0f;\n    voiceAuthoritySpectralReliability_ = 0.0f;\n    voiceAuthorityEventStrength_ = 0.0f;\n    voiceAuthorityFormantStability_ = 0.0f;\n    voiceAuthorityLowerFamilyEvidence_ = 0.0f;\n\n    octaveState_ = 0;\n''',
        'reset voice context')

    cpp = one(
        cpp,
        '''void ModernPitchEngine::MultiRatePitchTracker::setSensitivity(float sensitivity) noexcept\n{\n    sensitivity_ = clamp01(sensitivity);\n}\n\n''',
        '''void ModernPitchEngine::MultiRatePitchTracker::setSensitivity(float sensitivity) noexcept\n{\n    sensitivity_ = clamp01(sensitivity);\n}\n\nvoid ModernPitchEngine::MultiRatePitchTracker::setVoiceAuthorityContext(\n    bool valid,\n    float harmonicity,\n    float breathiness,\n    float bodyEnergy,\n    float spectralReliability,\n    float eventStrength,\n    float formantStability,\n    float lowerFamilyEvidence) noexcept\n{\n    voiceAuthorityContextValid_ = valid;\n    voiceAuthorityHarmonicity_ = clamp01(harmonicity);\n    voiceAuthorityBreathiness_ = clamp01(breathiness);\n    voiceAuthorityBodyEnergy_ = clamp01(bodyEnergy);\n    voiceAuthoritySpectralReliability_ = clamp01(spectralReliability);\n    voiceAuthorityEventStrength_ = clamp01(eventStrength);\n    voiceAuthorityFormantStability_ = clamp01(formantStability);\n    voiceAuthorityLowerFamilyEvidence_ = clamp01(lowerFamilyEvidence);\n}\n\n''',
        'voice context setter implementation')

    cpp = one(
        cpp,
        '''float ModernPitchEngine::MultiRatePitchTracker::pathPitchAuthority(\n    int pathIndex,\n    float frequencyHz) const noexcept\n''',
        '''float ModernPitchEngine::MultiRatePitchTracker::voiceBodyAuthorityV67() const noexcept\n{\n    if (!voiceAuthorityContextValid_)\n        return 0.0f;\n\n    return clamp01(0.30f * voiceAuthorityBodyEnergy_\n                 + 0.24f * voiceAuthorityHarmonicity_\n                 + 0.22f * voiceAuthoritySpectralReliability_\n                 + 0.14f * voiceAuthorityFormantStability_\n                 + 0.10f * (1.0f - voiceAuthorityBreathiness_));\n}\n\nbool ModernPitchEngine::MultiRatePitchTracker::voiceAllowsLowerFamilyV67() const noexcept\n{\n    if (!voiceAuthorityContextValid_)\n        return false;\n\n    // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7\n    // This is permission to inspect below, not permission to choose below. The\n    // actual lower coordinate must still be measured by one or more F0 paths.\n    return voiceBodyAuthorityV67() >= 0.40f\n        && voiceAuthoritySpectralReliability_ >= 0.24f\n        && voiceAuthorityBreathiness_ <= 0.76f\n        && voiceAuthorityLowerFamilyEvidence_ >= 0.18f;\n}\n\nfloat ModernPitchEngine::MultiRatePitchTracker::pathPitchAuthority(\n    int pathIndex,\n    float frequencyHz) const noexcept\n''',
        'voice authority helper implementation')

    # V6.1 direct path exists before this materializer is applied.
    cpp = one(
        cpp,
        '''    int authoritativeIndexV6 = -1;\n    float authoritativeScoreV6 = -1.0f;\n''',
        '''    // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7\n    // A lower-rate octave member remains family evidence by default. The vocal\n    // body can OPEN the lower-family question, but cannot answer it.\n    const bool lowerFamilyPermissionV67 = voiceAllowsLowerFamilyV67();\n    const auto isOneOctaveBelowV67 = [this](float lowerHz, float upperHz) noexcept\n    {\n        return lowerHz > 0.0f && upperHz > 0.0f\n            && centsDistance(2.0f * lowerHz, upperHz) <= 85.0f;\n    };\n    const auto challengesCommittedUpperV67 = [&](const PitchCandidate& c) noexcept\n    {\n        const float reference = trackedPitchHz_ > 0.0f\n            ? trackedPitchHz_ : committedOctaveFrequencyHz_;\n        return reference > 0.0f\n            && isOneOctaveBelowV67(c.frequencyHz, reference);\n    };\n\n    int authoritativeIndexV6 = -1;\n    float authoritativeScoreV6 = -1.0f;\n''',
        'v67 direct context')

    cpp = one(
        cpp,
        '''        if (candidate.ageInHops != 0\n            || !structurallyAuthoritativeV6(candidate)\n            || lowerAliasShadowedV6(index))\n''',
        '''        if (candidate.ageInHops != 0\n            || !structurallyAuthoritativeV6(candidate)\n            || (lowerAliasShadowedV6(index) && !lowerFamilyPermissionV67)\n            || (challengesCommittedUpperV67(candidate)\n                && voiceAuthorityContextValid_\n                && !lowerFamilyPermissionV67))\n''',
        'v67 authoritative candidate eligibility')

    cpp = one(
        cpp,
        '''            if (other.ageInHops != 0\n                || !structurallyAuthoritativeV6(other)\n                || lowerAliasShadowedV6(index)\n                || centsDistance(other.frequencyHz, best.frequencyHz) <= 70.0f)\n''',
        '''            if (other.ageInHops != 0\n                || !structurallyAuthoritativeV6(other)\n                || (lowerAliasShadowedV6(index) && !lowerFamilyPermissionV67)\n                || centsDistance(other.frequencyHz, best.frequencyHz) <= 70.0f)\n''',
        'v67 ambiguity eligibility')

    cpp = one(
        cpp,
        '''            if (directStructureScoreV6(other) >= authoritativeScoreV6 - 0.10f)\n            {\n                genuinelyAmbiguous = true;\n                break;\n            }\n''',
        '''            const bool octaveFamilyPair = isOneOctaveBelowV67(\n                    other.frequencyHz, best.frequencyHz)\n                || isOneOctaveBelowV67(best.frequencyHz, other.frequencyHz);\n            // If the vocal body has physically justified looking below, an\n            // octave-related pair is a real ambiguity regardless of a small\n            // score advantage. We consult the old resolver; we do NOT select\n            // the lower member here.\n            if ((octaveFamilyPair && lowerFamilyPermissionV67)\n                || directStructureScoreV6(other) >= authoritativeScoreV6 - 0.10f)\n            {\n                genuinelyAmbiguous = true;\n                break;\n            }\n''',
        'v67 ambiguity opening')

    cpp = one(
        cpp,
        '''                if (!structurallyAuthoritativeV6(other)\n                    || lowerAliasShadowedV6(index)\n                    || centsDistance(other.frequencyHz, best.frequencyHz) > 70.0f)\n''',
        '''                if (!structurallyAuthoritativeV6(other)\n                    || (lowerAliasShadowedV6(index) && !lowerFamilyPermissionV67)\n                    || centsDistance(other.frequencyHz, best.frequencyHz) > 70.0f)\n''',
        'v67 native support')

    cpp = one(
        cpp,
        '''            directDecision.decoderOctaveIndex = octaveState_;\n            directDecision.authoritativeDirect = true;\n            directDecision.valid = true;\n''',
        '''            directDecision.decoderOctaveIndex = octaveState_;\n            // A single lower-family witness may nominate a downward octave, but\n            // body permission is only permission to inspect it. Require either\n            // two native direct paths or the historical octave confirmer before\n            // the lower coordinate may bypass continuity.\n            const bool downwardFamilyChallengeV67 =\n                challengesCommittedUpperV67(best);\n            directDecision.authoritativeDirect = !downwardFamilyChallengeV67\n                || nativeSupport >= 2;\n            directDecision.valid = true;\n''',
        'v67 direct downward semantics')

    # In the historical confirmer, persistence alone may no longer turn one low
    # path into a downward register change when current vocal body explicitly
    # says no lower period is present. Independent direct paths remain enough.
    confirm_gate_anchor = '''    const int requiredObservations = directHighFamilyFastConfirm\n        ? 3\n        : (multiPathDirect\n            ? (octaveDelta < 0 ? 12 : 10)\n            : (octaveDelta < 0 ? 28 : 24));\n    if (pendingOctaveCount_ < requiredObservations)\n'''
    confirm_gate_replacement = '''    const int requiredObservations = directHighFamilyFastConfirm\n        ? 3\n        : (multiPathDirect\n            ? (octaveDelta < 0 ? 12 : 10)\n            : (octaveDelta < 0 ? 28 : 24));\n\n    // VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7\n    // Negative evidence is extremely narrow: only a DOWNWARD octave family\n    // conflict, only with valid coherent body context, and only when one path\n    // is trying to win by persistence alone. This is not global prudence. A\n    // real lower period measured by two native paths remains immediately legal.\n    const bool lowerFamilyPhysicallyPermittedV67 = octaveDelta >= 0\n        || !voiceAuthorityContextValid_\n        || voiceAllowsLowerFamilyV67()\n        || multiPathDirect;\n    if (!lowerFamilyPhysicallyPermittedV67)\n    {\n        pendingOctaveCount_ = std::min(pendingOctaveCount_,\n                                      std::max(0, requiredObservations - 1));\n        decision.candidate.frequencyHz = trackedPitchHz_;\n        decision.candidate.confidence = trackedConfidence_ * 0.97f;\n        decision.candidate.periodicity = trackedPeriodicity_;\n        decision.consensus = trackedConsensus_;\n        decision.supportCount = trackedSupportCount_;\n        decision.decoderOctaveIndex = octaveState_;\n        decision.valid = trackedPitchHz_ > 0.0f;\n        return false;\n    }\n\n    if (pendingOctaveCount_ < requiredObservations)\n'''
    cpp = one(cpp, confirm_gate_anchor, confirm_gate_replacement,
              'v67 downward confirmation permission')

    # Rescue confirmation has a separate historical branch. Apply the same
    # narrow physical rule before persistence can finally own a lower octave.
    rescue_gate_anchor = '''        if (pendingOctaveCount_ < requiredObservations)\n        {\n            decision.valid = false;\n            return false;\n        }\n\n        octaveState_ = std::clamp(octaveState_ + rescueOctaveDelta, -4, 4);\n'''
    rescue_gate_replacement = '''        const bool rescueLowerPermittedV67 = rescueOctaveDelta >= 0\n            || !voiceAuthorityContextValid_\n            || voiceAllowsLowerFamilyV67()\n            || multiPathDirect;\n        if (pendingOctaveCount_ < requiredObservations\n            || !rescueLowerPermittedV67)\n        {\n            decision.valid = false;\n            return false;\n        }\n\n        octaveState_ = std::clamp(octaveState_ + rescueOctaveDelta, -4, 4);\n'''
    cpp = one(cpp, rescue_gate_anchor, rescue_gate_replacement,
              'v67 rescue downward permission')

    cpp = one(
        cpp,
        '''    safe.voiceFormantStability = clamp01(finiteOr(safe.voiceFormantStability, 0.0f));\n    safe.lockHysteresis = std::clamp(finiteOr(safe.lockHysteresis, 24.0f), 0.0f, 80.0f);\n''',
        '''    safe.voiceFormantStability = clamp01(finiteOr(safe.voiceFormantStability, 0.0f));\n    safe.voiceLowerFamilyEvidence = clamp01(finiteOr(safe.voiceLowerFamilyEvidence, 0.0f));\n    safe.lockHysteresis = std::clamp(finiteOr(safe.lockHysteresis, 24.0f), 0.0f, 80.0f);\n''',
        'sanitize lower family evidence')

    cpp = one(
        cpp,
        '''    linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);\n    linkedTracker_.setSensitivity(safe.detectorSensitivity);\n    for (int channel = 0; channel < channels; ++channel)\n    {\n''',
        '''    linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);\n    linkedTracker_.setSensitivity(safe.detectorSensitivity);\n    linkedTracker_.setVoiceAuthorityContext(\n        safe.voiceEvidenceValid,\n        safe.voiceHarmonicity,\n        safe.voiceBreathiness,\n        safe.voiceBodyEnergy,\n        safe.voiceSpectralReliability,\n        safe.voiceEventStrength,\n        safe.voiceFormantStability,\n        safe.voiceLowerFamilyEvidence);\n    for (int channel = 0; channel < channels; ++channel)\n    {\n''',
        'linked tracker voice context')

    cpp = one(
        cpp,
        '''        channelTrackers_[static_cast<std::size_t>(channel)].setSensitivity(\n            safe.detectorSensitivity);\n    }\n''',
        '''        channelTrackers_[static_cast<std::size_t>(channel)].setSensitivity(\n            safe.detectorSensitivity);\n        channelTrackers_[static_cast<std::size_t>(channel)].setVoiceAuthorityContext(\n            safe.voiceEvidenceValid,\n            safe.voiceHarmonicity,\n            safe.voiceBreathiness,\n            safe.voiceBodyEnergy,\n            safe.voiceSpectralReliability,\n            safe.voiceEventStrength,\n            safe.voiceFormantStability,\n            safe.voiceLowerFamilyEvidence);\n    }\n''',
        'channel tracker voice context')

    cpp_path.write_text(cpp)
    h_path.write_text(h)
    live_path.write_text(live)
    voice_path.write_text(voice)

print('VOICE_BODY_ADAPTIVE_PATH_AUTHORITY_V6_7 materialized')
