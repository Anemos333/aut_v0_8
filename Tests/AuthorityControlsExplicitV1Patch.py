#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MARKER = "AUTHORITY_CONTROLS_EXPLICIT_V1"
FILES = {
    "Source/ModernPitchEngine.h": ROOT / "Source/ModernPitchEngine.h",
    "Source/ModernPitchEngine.cpp": ROOT / "Source/ModernPitchEngine.cpp",
    "Source/LivePitchProcessor.h": ROOT / "Source/LivePitchProcessor.h",
    "Tests/SupervisorContinuityTest.cpp": ROOT / "Tests/SupervisorContinuityTest.cpp",
}


def read(path):
    return path.read_text(encoding="utf-8")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


current = {name: read(path) for name, path in FILES.items()}
markers = {name: MARKER in text for name, text in current.items()}
if all(markers.values()):
    print(f"{MARKER}=already_materialized")
    raise SystemExit(0)
if any(markers.values()):
    raise RuntimeError(f"partial {MARKER} materialization: {markers}")

# ---------------------------------------------------------------------------
# ModernPitchEngine.h: expose an analysis-only immediate-authority switch to the
# tracker and explicit authority predicates to the supervisor. No renderer API
# or audio topology changes are made here.
h = current["Source/ModernPitchEngine.h"]
h = replace_once(
    h,
    "        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }\n",
    "        void setRescueMode(bool enabled) noexcept { rescueMode_ = enabled; }\n"
    "        void setImmediateAuthority(bool enabled) noexcept { immediateAuthority_ = enabled; } // AUTHORITY_CONTROLS_EXPLICIT_V1\n",
    "tracker immediate-authority setter")
h = replace_once(
    h,
    "        bool presenceMode_ = false;\n        bool presenceSinceLastHop_ = false;\n",
    "        bool presenceMode_ = false;\n        bool presenceSinceLastHop_ = false;\n"
    "        bool immediateAuthority_ = false; // AUTHORITY_CONTROLS_EXPLICIT_V1\n",
    "tracker immediate-authority state")
h = replace_once(
    h,
    "    [[nodiscard]] static double wrapToNearestOctave(double cents) noexcept;\n    [[nodiscard]] static int latencyForMode(LatencyMode mode) noexcept;\n",
    "    [[nodiscard]] static double wrapToNearestOctave(double cents) noexcept;\n"
    "    [[nodiscard]] static bool exactScaleLockAuthority(const Parameters& parameters) noexcept; // AUTHORITY_CONTROLS_EXPLICIT_V1\n"
    "    [[nodiscard]] static bool zeroPrudenceAuthority(const Parameters& parameters) noexcept;\n"
    "    [[nodiscard]] static int latencyForMode(LatencyMode mode) noexcept;\n",
    "authority predicate declarations")

# ---------------------------------------------------------------------------
# LivePitchProcessor.h: Hold is Hold. It must not secretly create mode-specific
# strictness. Scale Lock remains active, but target-hold authority comes only
# from the visible Hold control.
live = current["Source/LivePitchProcessor.h"]
old_live = """        const int modeIndex = activeModeIndex_.load(std::memory_order_acquire);\n        const float liveBoost = modeIndex == static_cast<int>(LatencyMode::live)\n            ? 0.03f : 0.0f;\n        const float experimentalBoost = modeIndex == static_cast<int>(LatencyMode::ultraLive)\n            ? 0.12f : 0.0f;\n\n        parameters_.lockStrictness = scaleLock\n            ? std::clamp(hysteresisStrictness + liveBoost + experimentalBoost,\n                         0.0f, 1.0f)\n            : 0.0f;\n\n        parameters_.hardLockActive = scaleLock;\n"""
new_live = """        // AUTHORITY_CONTROLS_EXPLICIT_V1: Hold owns target-hold prudence.\n        // No latency mode may add hidden strictness when the visible Hold control\n        // is at zero. This does not alter the renderer or correction depth.\n        parameters_.lockStrictness = scaleLock ? hysteresisStrictness : 0.0f;\n        parameters_.hardLockActive = scaleLock;\n"""
live = replace_once(live, old_live, new_live, "remove hidden Hold strictness boosts")

# ---------------------------------------------------------------------------
# ModernPitchEngine.cpp
cpp = current["Source/ModernPitchEngine.cpp"]
cpp = replace_once(
    cpp,
    "    presenceMode_ = false;\n    presenceSinceLastHop_ = false;\n\n    octaveState_ = 0;\n",
    "    presenceMode_ = false;\n    presenceSinceLastHop_ = false;\n"
    "    immediateAuthority_ = false; // AUTHORITY_CONTROLS_EXPLICIT_V1\n\n    octaveState_ = 0;\n",
    "reset immediate-authority state")

old_history = """        float bestTransitionScore = -1000.0f;\n        int bestOctaveIndex = octaveState_;\n        bool foundPrevious = false;\n\n        for (const auto& previous : decoderBeam_)\n        {\n            if (!previous.valid)\n                continue;\n\n            foundPrevious = true;\n            const float deltaCents = static_cast<float>(1200.0\n                * (proposal.logFrequency - previous.logFrequency));\n            const float absoluteCents = std::abs(deltaCents);\n            const float continuityBonus = 0.30f * std::exp(-absoluteCents / 85.0f);\n            const float transitionPenalty = onsetPending\n                ? 0.10f * std::min(1.0f, absoluteCents / 1800.0f)\n                : 0.19f * std::min(2.0f, absoluteCents / 650.0f);\n\n            int octaveDelta = 0;\n            float residualCents = 0.0f;\n            const bool octaveLike = isOctaveLikeTransition(\n                static_cast<float>(std::exp2(previous.logFrequency)),\n                hypothesis.frequencyHz,\n                octaveDelta,\n                residualCents);\n            const float octavePenalty = octaveLike\n                ? 0.24f * static_cast<float>(std::abs(octaveDelta))\n                    * (1.0f - 0.70f * hypothesis.consensus)\n                : 0.0f;\n\n            const float historyWeight = onsetPending ? 0.24f : 0.72f;\n            const float transitionScore = historyWeight * previous.score\n                                        + proposal.score\n                                        + continuityBonus\n                                        - transitionPenalty\n                                        - octavePenalty;\n            if (transitionScore > bestTransitionScore)\n            {\n                bestTransitionScore = transitionScore;\n                bestOctaveIndex = previous.octaveIndex\n                    + (octaveLike ? octaveDelta : 0);\n            }\n        }\n\n        if (foundPrevious)\n            proposal.score = bestTransitionScore;\n        proposal.octaveIndex = bestOctaveIndex;\n"""
new_history = """        float bestTransitionScore = -1000.0f;\n        int bestOctaveIndex = octaveState_;\n        bool foundPrevious = false;\n\n        // AUTHORITY_CONTROLS_EXPLICIT_V1: detector fusion remains active, but\n        // the zero-prudence endpoint removes temporal/history preference. Current\n        // evidence is measured; it is not required to defeat a stale beam first.\n        if (!immediateAuthority_)\n        {\n            for (const auto& previous : decoderBeam_)\n            {\n                if (!previous.valid)\n                    continue;\n\n                foundPrevious = true;\n                const float deltaCents = static_cast<float>(1200.0\n                    * (proposal.logFrequency - previous.logFrequency));\n                const float absoluteCents = std::abs(deltaCents);\n                const float continuityBonus = 0.30f * std::exp(-absoluteCents / 85.0f);\n                const float transitionPenalty = onsetPending\n                    ? 0.10f * std::min(1.0f, absoluteCents / 1800.0f)\n                    : 0.19f * std::min(2.0f, absoluteCents / 650.0f);\n\n                int octaveDelta = 0;\n                float residualCents = 0.0f;\n                const bool octaveLike = isOctaveLikeTransition(\n                    static_cast<float>(std::exp2(previous.logFrequency)),\n                    hypothesis.frequencyHz,\n                    octaveDelta,\n                    residualCents);\n                const float octavePenalty = octaveLike\n                    ? 0.24f * static_cast<float>(std::abs(octaveDelta))\n                        * (1.0f - 0.70f * hypothesis.consensus)\n                    : 0.0f;\n\n                const float historyWeight = onsetPending ? 0.24f : 0.72f;\n                const float transitionScore = historyWeight * previous.score\n                                            + proposal.score\n                                            + continuityBonus\n                                            - transitionPenalty\n                                            - octavePenalty;\n                if (transitionScore > bestTransitionScore)\n                {\n                    bestTransitionScore = transitionScore;\n                    bestOctaveIndex = previous.octaveIndex\n                        + (octaveLike ? octaveDelta : 0);\n                }\n            }\n        }\n\n        if (foundPrevious)\n            proposal.score = bestTransitionScore;\n        proposal.octaveIndex = bestOctaveIndex;\n"""
cpp = replace_once(cpp, old_history, new_history, "remove decoder history at zero prudence")

old_hold = """    // A short hold branch prevents a single weak hop from forcing a jump.  It\n    // decays quickly, so genuine new notes still win after fresh evidence.\n    for (const auto& previous : decoderBeam_)\n    {\n        if (!previous.valid || proposalCount >= static_cast<int>(proposals.size()))\n            continue;\n\n        DecoderState held = previous;\n        held.score = previous.score * (onsetPending ? 0.22f : 0.76f)\n                   - (onsetPending ? 0.10f : 0.055f);\n        ++held.ageInHops;\n        if (held.ageInHops <= 4)\n            proposals[static_cast<std::size_t>(proposalCount++)] = held;\n    }\n"""
new_hold = """    // A short hold branch prevents a single weak hop from forcing a jump in\n    // normal operation. AUTHORITY_CONTROLS_EXPLICIT_V1 removes that hidden hold\n    // when all six visible prudence controls request the rigid endpoint.\n    if (!immediateAuthority_)\n    {\n        for (const auto& previous : decoderBeam_)\n        {\n            if (!previous.valid || proposalCount >= static_cast<int>(proposals.size()))\n                continue;\n\n            DecoderState held = previous;\n            held.score = previous.score * (onsetPending ? 0.22f : 0.76f)\n                       - (onsetPending ? 0.10f : 0.055f);\n            ++held.ageInHops;\n            if (held.ageInHops <= 4)\n                proposals[static_cast<std::size_t>(proposalCount++)] = held;\n        }\n    }\n"""
cpp = replace_once(cpp, old_hold, new_hold, "disable decoder held-state branch at zero prudence")

old_helpers = """double ModernPitchEngine::wrapToNearestOctave(double cents) noexcept\n{\n    if (!std::isfinite(cents))\n        return 0.0;\n    return cents - 1200.0 * std::nearbyint(cents / 1200.0);\n}\n\nint ModernPitchEngine::latencyForMode(LatencyMode mode) noexcept\n"""
new_helpers = """double ModernPitchEngine::wrapToNearestOctave(double cents) noexcept\n{\n    if (!std::isfinite(cents))\n        return 0.0;\n    return cents - 1200.0 * std::nearbyint(cents / 1200.0);\n}\n\n// AUTHORITY_CONTROLS_EXPLICIT_V1: only visible user controls define musical\n// softness. Internal confidence, mode and strictness may improve measurement or\n// identity safety, but they are not permission to weaken the requested lock.\nbool ModernPitchEngine::exactScaleLockAuthority(const Parameters& parameters) noexcept\n{\n    return parameters.scaleLock\n        && clamp01(parameters.amount) >= 0.99999f\n        && clamp01(parameters.humanize) <= 0.00001f\n        && clamp01(parameters.vibratoPreserve) <= 0.00001f;\n}\n\nbool ModernPitchEngine::zeroPrudenceAuthority(const Parameters& parameters) noexcept\n{\n    return exactScaleLockAuthority(parameters)\n        && std::clamp(static_cast<double>(finiteOr(parameters.retuneTimeMs, 50.0f)),\n                      0.0, 500.0) <= 0.00001\n        && std::clamp(static_cast<double>(finiteOr(parameters.lockHysteresis, 24.0f)),\n                      0.0, 80.0) <= 0.00001;\n}\n\nint ModernPitchEngine::latencyForMode(LatencyMode mode) noexcept\n"""
cpp = replace_once(cpp, old_helpers, new_helpers, "explicit authority helper definitions")

old_adaptive_start = """{\n    if (!parameters.scaleLock)\n    {\n"""
new_adaptive_start = """{\n    if (zeroPrudenceAuthority(parameters))\n        return 0.0f; // AUTHORITY_CONTROLS_EXPLICIT_V1: Hold=0 means exactly no target hold.\n\n    if (!parameters.scaleLock)\n    {\n"""
# Scope this replacement to the adaptiveHysteresis function only.
adaptive_anchor = "float ModernPitchEngine::adaptiveHysteresis(\n    const Parameters& parameters,\n    const ScaleQuantizer& quantizer,\n    const PitchObservation& observation) const noexcept\n"
pos = cpp.find(adaptive_anchor)
if pos < 0:
    raise RuntimeError("adaptiveHysteresis anchor missing")
prefix, rest = cpp[:pos], cpp[pos:]
rest = replace_once(rest, old_adaptive_start, new_adaptive_start, "zero Hold hysteresis endpoint")
cpp = prefix + rest

old_response_intro = """    const double requested = std::clamp(\n        static_cast<double>(finiteOr(parameters.retuneTimeMs, 50.0f)),\n        0.0, 500.0);\n    double response = std::max(0.35, requested);\n"""
new_response_intro = """    const double requested = std::clamp(\n        static_cast<double>(finiteOr(parameters.retuneTimeMs, 50.0f)),\n        0.0, 500.0);\n    if (zeroPrudenceAuthority(parameters))\n        return 0.0; // AUTHORITY_CONTROLS_EXPLICIT_V1: Response=0 is literal.\n    double response = std::max(0.35, requested);\n"""
cpp = replace_once(cpp, old_response_intro, new_response_intro, "literal zero response endpoint")

old_update_intro = """    const int hopSamples = MultiRatePitchTracker::hopSize();\n    const double hopSeconds = static_cast<double>(hopSamples) / sampleRate_;\n    const float humanize = clamp01(parameters.humanize);\n"""
new_update_intro = """    const int hopSamples = MultiRatePitchTracker::hopSize();\n    const double hopSeconds = static_cast<double>(hopSamples) / sampleRate_;\n    const float humanize = clamp01(parameters.humanize);\n    const bool exactAuthority = exactScaleLockAuthority(parameters);\n    const bool zeroPrudence = zeroPrudenceAuthority(parameters); // AUTHORITY_CONTROLS_EXPLICIT_V1\n"""
cpp = replace_once(cpp, old_update_intro, new_update_intro, "authority state in correction supervisor")

old_target = """    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);\n    int pending = 0;\n    double newTarget = quantizer.chooseTargetLog2(\n        state.pitchCentreLog2,\n        hysteresis,\n        parameters.lockStrictness,\n        observation.confidence,\n        parameters.scaleLock && parameters.hardLockActive,\n        musicalOnset || liveIdentityBreak,\n        pending);\n\n    // SOUND_EQUALS_CORRECTION_V2: target register follows the current live F0,\n    // never a stale centre. This keeps an octave/register mistake from becoming\n    // a mathematically zero correction.\n    newTarget += std::round(observedLog2 - newTarget);\n"""
new_target = """    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);\n    int pending = 0;\n    const double targetSelectionLog2 = zeroPrudence\n        ? correctionObservedLog2 : state.pitchCentreLog2;\n    const float targetStrictness = zeroPrudence\n        ? 0.0f : parameters.lockStrictness;\n    const float targetConfidence = zeroPrudence\n        ? 1.0f : observation.confidence;\n    double newTarget = quantizer.chooseTargetLog2(\n        targetSelectionLog2,\n        hysteresis,\n        targetStrictness,\n        targetConfidence,\n        parameters.scaleLock && parameters.hardLockActive,\n        musicalOnset || liveIdentityBreak,\n        pending);\n\n    // SOUND_EQUALS_CORRECTION_V2: target register follows the current live F0,\n    // never a stale centre. AUTHORITY_CONTROLS_EXPLICIT_V1 extends that rule to\n    // the zero-prudence target selector itself: the live correction coordinate\n    // chooses the degree and its register instead of a continuity-delayed centre.\n    const double targetRegisterReference = zeroPrudence\n        ? correctionObservedLog2 : observedLog2;\n    newTarget += std::round(targetRegisterReference - newTarget);\n"""
cpp = replace_once(cpp, old_target, new_target, "live coordinate target selection at zero prudence")

old_absolute = """    const bool absoluteScaleLock = parameters.scaleLock\n        && parameters.hardLockActive\n        && clamp01(parameters.lockStrictness) >= 0.99999f\n        && clamp01(parameters.amount) >= 0.99999f\n        && humanize <= 0.00001f\n        && clamp01(parameters.vibratoPreserve) <= 0.00001f;\n"""
new_absolute = """    // AUTHORITY_CONTROLS_EXPLICIT_V1: exact centering is controlled only by\n    // visible Amount / Scale Lock / Humanize / Vibrato. Hold chooses WHICH degree\n    // and Response chooses HOW FAST; neither may create steady-state residual.\n    const bool absoluteScaleLock = exactAuthority;\n"""
cpp = replace_once(cpp, old_absolute, new_absolute, "remove hidden exact-lock gates")

old_advance = """    if (state.stateAgeSamples < std::numeric_limits<int>::max())\n        ++state.stateAgeSamples;\n\n    // Transition describes a note boundary, never convergence of a second-order\n"""
new_advance = """    if (state.stateAgeSamples < std::numeric_limits<int>::max())\n        ++state.stateAgeSamples;\n\n    // AUTHORITY_CONTROLS_EXPLICIT_V1: a literal zero Response contains no hidden\n    // 0.35 ms controller floor. The same single wet renderer receives the new\n    // ratio immediately; there is no dry transition or secondary synthesis path.\n    if (state.responseMs <= 0.00001)\n    {\n        state.currentCents = state.desiredCents;\n        state.velocityCentsPerSecond = 0.0;\n        return state.currentCents;\n    }\n\n    // Transition describes a note boundary, never convergence of a second-order\n"""
cpp = replace_once(cpp, old_advance, new_advance, "literal zero response in controller")

old_safe_mode = """    safe.maximumPitchHz = std::clamp(finiteOr(safe.maximumPitchHz, 1600.0f),\n                                     safe.minimumPitchHz + 20.0f, 3000.0f);\n    safe.latencyMode = static_cast<int>(latencyMode_);\n\n    const int channels = std::min({buffer.getNumChannels(), channelCount_, maxSupportedChannels});\n"""
new_safe_mode = """    safe.maximumPitchHz = std::clamp(finiteOr(safe.maximumPitchHz, 1600.0f),\n                                     safe.minimumPitchHz + 20.0f, 3000.0f);\n    safe.latencyMode = static_cast<int>(latencyMode_);\n    const bool immediateAuthority = zeroPrudenceAuthority(safe); // AUTHORITY_CONTROLS_EXPLICIT_V1\n\n    const int channels = std::min({buffer.getNumChannels(), channelCount_, maxSupportedChannels});\n"""
cpp = replace_once(cpp, old_safe_mode, new_safe_mode, "compute block immediate authority")

old_tracker_setup = """    linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);\n    linkedTracker_.setSensitivity(safe.detectorSensitivity);\n    for (int channel = 0; channel < channels; ++channel)\n    {\n        channelTrackers_[static_cast<std::size_t>(channel)].setRange(\n            safe.minimumPitchHz, safe.maximumPitchHz);\n        channelTrackers_[static_cast<std::size_t>(channel)].setSensitivity(\n            safe.detectorSensitivity);\n    }\n"""
new_tracker_setup = """    linkedTracker_.setRange(safe.minimumPitchHz, safe.maximumPitchHz);\n    linkedTracker_.setSensitivity(safe.detectorSensitivity);\n    linkedTracker_.setImmediateAuthority(immediateAuthority);\n    for (int channel = 0; channel < channels; ++channel)\n    {\n        channelTrackers_[static_cast<std::size_t>(channel)].setRange(\n            safe.minimumPitchHz, safe.maximumPitchHz);\n        channelTrackers_[static_cast<std::size_t>(channel)].setSensitivity(\n            safe.detectorSensitivity);\n        channelTrackers_[static_cast<std::size_t>(channel)].setImmediateAuthority(\n            immediateAuthority);\n    }\n"""
cpp = replace_once(cpp, old_tracker_setup, new_tracker_setup, "publish immediate authority to trackers")

# ---------------------------------------------------------------------------
# Supervisor regression tests: hidden flags/mode confidence may not weaken the
# visible six-control endpoint; transitions with live audio keep the correction.
test = current["Tests/SupervisorContinuityTest.cpp"]
insert_before = "    // Native API semantics: one semitone means 100 cents, with no adapter hack.\n"
new_tests = r'''    // AUTHORITY_CONTROLS_EXPLICIT_V1: the six visible controls define the
    // zero-prudence endpoint. Hidden hardLockActive/lockStrictness values are not
    // allowed to withhold exact centering or add a response floor.
    ModernPitchEngine::Parameters explicitAuthorityParameters;
    setBodyEvidence(explicitAuthorityParameters);
    explicitAuthorityParameters.scaleLock = true;
    explicitAuthorityParameters.hardLockActive = false;
    explicitAuthorityParameters.lockStrictness = 0.0f;
    explicitAuthorityParameters.lockHysteresis = 0.0f;
    explicitAuthorityParameters.amount = 1.0f;
    explicitAuthorityParameters.retuneTimeMs = 0.0f;
    explicitAuthorityParameters.humanize = 0.0f;
    explicitAuthorityParameters.vibratoPreserve = 0.0f;
    explicitAuthorityParameters.preserveVibrato = 0.0f;
    explicitAuthorityParameters.maximumCorrectionSemitones = 24.0f;

    ModernPitchEngine::ScaleQuantizer explicitAuthorityQuantizer;
    explicitAuthorityQuantizer.reset();
    explicitAuthorityQuantizer.setScale(&liveCoordinateUnison, 1, 440.0);
    ModernPitchEngine::CorrectionState explicitAuthorityState;
    auto explicitAuthorityObservation = strongPitch(445.0f);
    explicitAuthorityObservation.audioPresent = true;
    explicitAuthorityObservation.correctionFrequencyHz = 452.0f;
    explicitAuthorityObservation.confidence = 0.01f;
    explicitAuthorityObservation.periodicity = 0.05f;
    explicitAuthorityObservation.consensus = 0.0f;
    engine->updateCorrectionState(explicitAuthorityState,
                                  explicitAuthorityQuantizer,
                                  explicitAuthorityObservation,
                                  explicitAuthorityParameters);
    const double explicitTargetHz = std::exp2(explicitAuthorityState.targetLog2);
    const double explicitExpectedCents = 1200.0 * std::log2(
        explicitTargetHz / static_cast<double>(explicitAuthorityObservation.correctionFrequencyHz));
    success &= check(std::abs(explicitAuthorityState.desiredCents
                              - explicitExpectedCents) < 1.0e-6,
                     "visible_controls_own_exact_lock_without_hidden_flags");
    success &= check(std::abs(explicitAuthorityState.responseMs) < 1.0e-12,
                     "response_zero_has_no_hidden_mode_floor");
    const double immediateController = engine->advanceCorrection(explicitAuthorityState);
    success &= check(std::abs(immediateController - explicitAuthorityState.desiredCents) < 1.0e-9
                     && std::abs(explicitAuthorityState.velocityCentsPerSecond) < 1.0e-12,
                     "response_zero_reaches_destination_in_one_sample");
    success &= check(engine->adaptiveHysteresis(explicitAuthorityParameters,
                                                 explicitAuthorityQuantizer,
                                                 explicitAuthorityObservation) == 0.0f,
                     "hold_zero_means_exactly_zero_hysteresis");

    // Even if stale temporal history has an artificially huge score, immediate
    // authority keeps current detector fusion and removes only the history/hold
    // preference. The current hypothesis must own the decoder immediately.
    auto immediateTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    immediateTracker->prepare(48000.0);
    immediateTracker->setImmediateAuthority(true);
    immediateTracker->decoderBeam_[0].valid = true;
    immediateTracker->decoderBeam_[0].logFrequency = std::log2(440.0);
    immediateTracker->decoderBeam_[0].score = 100.0f;
    std::array<ModernPitchEngine::MultiRatePitchTracker::ConsensusHypothesis,
               ModernPitchEngine::MultiRatePitchTracker::maxConsensusHypotheses>
        currentHypotheses {};
    currentHypotheses[0].valid = true;
    currentHypotheses[0].frequencyHz = 466.1638f;
    currentHypotheses[0].confidence = 0.75f;
    currentHypotheses[0].periodicity = 0.75f;
    currentHypotheses[0].consensus = 0.25f;
    currentHypotheses[0].evidenceScore = 0.45f;
    currentHypotheses[0].supportCount = 1;
    currentHypotheses[0].directSupportCount = 1;
    immediateTracker->updateDecoderBeam(currentHypotheses, 1, false);
    const double immediateDecodedHz = std::exp2(immediateTracker->decoderBeam_[0].logFrequency);
    success &= check(std::abs(immediateDecodedHz - 466.1638) < 0.1
                     && immediateTracker->decoderBeam_[0].ageInHops == 0,
                     "zero_prudence_decoder_has_no_temporal_hold");

    // A consonant/transient may temporarily remove a usable F0, but while audio
    // is present the already-selected correction remains on the same wet path.
    // There is no permission to return to dry/unshifted audio between voiced hops.
    ModernPitchEngine::PitchObservation explicitDropout;
    explicitDropout.audioPresent = true;
    explicitDropout.voicing = 1.0f;
    const double heldExplicitCents = explicitAuthorityState.desiredCents;
    engine->updateCorrectionState(explicitAuthorityState,
                                  explicitAuthorityQuantizer,
                                  explicitDropout,
                                  explicitAuthorityParameters);
    const double dropoutController = engine->advanceCorrection(explicitAuthorityState);
    success &= check(explicitAuthorityState.trackingState
                         == ModernPitchEngine::TrackingState::acquire
                     && std::abs(explicitAuthorityState.desiredCents - heldExplicitCents) < 1.0e-9
                     && std::abs(dropoutController - heldExplicitCents) < 1.0e-9,
                     "transient_f0_hole_keeps_authoritative_wet_correction");

'''
test = replace_once(test, insert_before, new_tests + insert_before, "explicit authority regression tests")

# Ensure marker exists in all four materialized files.
if MARKER not in h or MARKER not in cpp or MARKER not in live or MARKER not in test:
    raise RuntimeError("marker insertion incomplete")

FILES["Source/ModernPitchEngine.h"].write_text(h, encoding="utf-8")
FILES["Source/ModernPitchEngine.cpp"].write_text(cpp, encoding="utf-8")
FILES["Source/LivePitchProcessor.h"].write_text(live, encoding="utf-8")
FILES["Tests/SupervisorContinuityTest.cpp"].write_text(test, encoding="utf-8")
print(f"{MARKER}=materialized")
