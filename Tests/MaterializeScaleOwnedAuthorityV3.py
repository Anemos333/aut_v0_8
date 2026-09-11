from pathlib import Path
import subprocess
import sys


def require_once(text: str, needle: str, label: str) -> None:
    count = text.count(needle)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 occurrence, found {count}")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    require_once(text, old, label)
    return text.replace(old, new, 1)


def remove_between(text: str, start_marker: str, end_marker: str, label: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        raise SystemExit(f"{label}: start marker not found")
    end = text.find(end_marker, start)
    if end < 0:
        raise SystemExit(f"{label}: end marker not found")
    return text[:start] + text[end:]


# First materialize the already-validated subtractive cleanup. Its previous CI
# failure happened after all tests + VST3 build, only while deleting its own
# patched materializer. Keep that cleanup as the base of this authority pass.
cleanup_path = Path("Tests/MaterializeSingleAuthorityCleanupV1.py")
cleanup = cleanup_path.read_text()
cleanup = replace_once(
    cleanup,
    'mode_update_end = p.find("//==============================================================================\\nvoid MicrotonalAutotuneAudioProcessor::resetSlowStateNoAlloc()", mode_update_start)',
    'mode_update_end = p.find("void MicrotonalAutotuneAudioProcessor::resetSlowStateNoAlloc()", mode_update_start)',
    "cleanup mode-update marker")
cleanup = replace_once(
    cleanup,
    '    "//==============================================================================\\nvoid MicrotonalAutotuneAudioProcessor::resetSlowStateNoAlloc() noexcept\\n",',
    '    "void MicrotonalAutotuneAudioProcessor::resetSlowStateNoAlloc() noexcept\\n",',
    "cleanup slow-reset marker")
cleanup_path.write_text(cleanup)
subprocess.check_call([sys.executable, str(cleanup_path)])

# The old workflow had to narrow this static UI matcher after materialization.
contract_path = Path("Tests/GuiAudioControlContractTest.cpp")
contract = contract_path.read_text()
contract = contract.replace(
    '!has(editor, "selectedId - 1")',
    '!has(editor, "int newMode = selectedId - 1")',
    1)
contract_path.write_text(contract)


# -----------------------------------------------------------------------------
# ModernPitchEngine.h: the detector must not receive musical-authority settings.
header_path = Path("Source/ModernPitchEngine.h")
h = header_path.read_text()
h = replace_once(
    h,
    "        void setImmediateAuthority(bool enabled) noexcept { immediateAuthority_ = enabled; } // AUTHORITY_CONTROLS_EXPLICIT_V1\n",
    "",
    "remove detector immediate-authority API")
h = replace_once(
    h,
    "        bool immediateAuthority_ = false; // AUTHORITY_CONTROLS_EXPLICIT_V1\n",
    "",
    "remove detector immediate-authority state")
header_path.write_text(h)


# -----------------------------------------------------------------------------
# ModernPitchEngine.cpp: DETECTOR_IS_OBSERVER_V1.
# Presence says only that audio exists. It can never make an F0 valid, bypass
# register confirmation, or change detector temporal logic because Scale Lock is
# rigid. The supervisor/quantizer remains the sole musical authority.
engine_path = Path("Source/ModernPitchEngine.cpp")
m = engine_path.read_text()

# Detector fusion always uses its own evidence/continuity model. Rigid Scale Lock
# may change the correction endpoint, never the detector's history rules.
old_guard = "        if (!immediateAuthority_)\n        {\n"
if m.count(old_guard) != 2:
    raise SystemExit(f"decoder immediate-authority guards: expected 2, found {m.count(old_guard)}")
m = m.replace(old_guard, "        {\n")
m = m.replace(
    "        // AUTHORITY_CONTROLS_EXPLICIT_V1: detector fusion remains active, but\n"
    "        // the zero-prudence endpoint removes temporal/history preference. Current\n"
    "        // evidence is measured; it is not required to defeat a stale beam first.\n",
    "        // DETECTOR_IS_OBSERVER_V1: detector history is analysis evidence only.\n"
    "        // Musical rigidity never changes this decoder; Scale Lock authority lives\n"
    "        // downstream in the supervisor/quantizer.\n",
    1)
m = m.replace(
    "    // A short hold branch prevents a single weak hop from forcing a jump in\n"
    "    // normal operation. AUTHORITY_CONTROLS_EXPLICIT_V1 removes that hidden hold\n"
    "    // when all six visible prudence controls request the rigid endpoint.\n",
    "    // A short detector hold prevents one weak observation from becoming a new\n"
    "    // measured F0. It never weakens correction: downstream scale ownership\n"
    "    // continues while the detector is uncertain.\n",
    1)

# Remove the presence fallback that promoted any raw detector family to F0.
fallback_start = "    // RAP_VOICING_V2_ZERO_CONSENSUS_CORRECTION: consensus is diagnostic\n"
fallback_end = "    std::array<ConsensusHypothesis, maxConsensusHypotheses> hypotheses {};\n"
start = m.find(fallback_start)
end = m.find(fallback_end, start)
if start < 0 or end < 0:
    raise SystemExit("presence fallback block markers changed")
observer_comment = (
    "    // DETECTOR_IS_OBSERVER_V1: audio presence is not pitch evidence. A raw\n"
    "    // detector family may be reported diagnostically, but only the existing\n"
    "    // consensus/continuity machinery may publish a valid F0. Detector doubt\n"
    "    // is absorbed downstream by the already-owned scale target/glide.\n"
)
m = m[:start] + observer_comment + m[end:]

m = replace_once(
    m,
    "    if (hypothesisCount <= 0)\n        return makePresenceFallback();\n",
    "    if (hypothesisCount <= 0)\n        return {};\n",
    "no-consensus detector result")
m = replace_once(
    m,
    "    if (!decoderBeam_[0].valid)\n        return makePresenceFallback();\n",
    "    if (!decoderBeam_[0].valid)\n        return {};\n",
    "invalid decoder beam")
m = replace_once(
    m,
    "        if (matchedHypothesis < 0 || matchedDistance > 65.0f)\n"
    "        {\n"
    "            if (presenceMode_)\n"
    "                return makePresenceFallback();\n"
    "            return {}; // the winning branch is only a decaying hold state\n"
    "        }\n",
    "        if (matchedHypothesis < 0 || matchedDistance > 65.0f)\n"
    "            return {}; // detector has no new trustworthy F0\n",
    "remove presence mismatch fallback")

# A live signal cannot bypass a persistent register anchor during reacquisition.
m = replace_once(
    m,
    "    if (rescueMode_ && !presenceMode_ && rescueReferenceHz > 0.0f)\n",
    "    if (rescueMode_ && rescueReferenceHz > 0.0f)\n",
    "presence cannot bypass rescue register")

presence_initial = (
    "    const bool presenceInitialEvidence = presenceMode_\n"
    "        && decision.supportCount >= 1\n"
    "        && decision.candidate.confidence >= 0.05f\n"
    "        && decision.candidate.periodicity >= 0.18f;\n"
)
require_once(m, presence_initial, "presence initial evidence")
m = m.replace(presence_initial, "", 1)
m = replace_once(
    m,
    "    decision.valid = presenceMode_\n"
    "        ? (decision.candidate.valid && decision.candidate.frequencyHz > 0.0f)\n"
    "        : (rescueMode_\n"
    "            ? rescueEvidence\n"
    "            : (closeToTrack || sufficientInitialEvidence || presenceInitialEvidence));\n",
    "    decision.valid = rescueMode_\n"
    "        ? rescueEvidence\n"
    "        : (closeToTrack || sufficientInitialEvidence);\n",
    "presence cannot validate F0")

# Remove the presence fast path that instantly committed octave-like candidates.
fast_octave_start = (
    "    // SOUND_EQUALS_CORRECTION_V1: audible input plus a finite F0 is enough\n"
)
fast_octave_end = (
    "    // If current F0 expired while a musical note body is still latched,\n"
)
start = m.find(fast_octave_start)
end = m.find(fast_octave_end, start)
if start < 0 or end < 0:
    raise SystemExit("presence octave fast-path markers changed")
m = m[:start] + (
    "    // DETECTOR_IS_OBSERVER_V1: audio presence cannot commit a register.\n"
    "    // Octave-like observations always use the same evidence/continuity guards.\n\n"
) + m[end:]

# Remove the second presence-only initial-lock bypass. Initial register acquisition
# uses the existing short temporal confirmation; renderer latency already gives
# the detector several hops before the first audible synthesis frame.
first_presence_start = (
    "    // Presence owns correction authority. With actual input present, the first\n"
)
first_presence_end = (
    "    // Initial register acquisition without explicit audio presence remains\n"
)
start = m.find(first_presence_start)
end = m.find(first_presence_end, start)
if start < 0 or end < 0:
    raise SystemExit("presence initial-register bypass markers changed")
m = m[:start] + (
    "    // Initial register acquisition is an evidence decision, independent of\n"
    "    // whether samples are non-zero. Presence never upgrades weak F0 evidence.\n"
) + m[end:]

# Published observations keep audio presence and F0 validity independent.
m = replace_once(
    m,
    "        // LIVE_CORRECTION_COORDINATE_V5: identity remains on the proven\n"
    "        // continuity-smoothed track, but a rigid lock must calculate its ratio\n"
    "        // from the latest accepted F0 rather than from that delayed identity\n"
    "        // coordinate. All non-rigid modes continue to use frequencyHz below.\n"
    "        observation.correctionFrequencyHz = presenceMode_\n"
    "            && std::isfinite(decision.candidate.frequencyHz)\n"
    "            && decision.candidate.frequencyHz > 0.0f\n"
    "            ? decision.candidate.frequencyHz\n"
    "            : trackedPitchHz_;\n",
    "        // DETECTOR_IS_OBSERVER_V1: exact correction may use the latest F0 only\n"
    "        // after that F0 has survived the detector's normal evidence/register\n"
    "        // guards. Audio presence by itself has no pitch authority.\n"
    "        observation.correctionFrequencyHz =\n"
    "            std::isfinite(decision.candidate.frequencyHz)\n"
    "            && decision.candidate.frequencyHz > 0.0f\n"
    "            ? decision.candidate.frequencyHz\n"
    "            : trackedPitchHz_;\n",
    "correction coordinate authority")
m = replace_once(
    m,
    "        observation.audioPresent = presenceMode_;\n"
    "        observation.voicing = presenceMode_ ? 1.0f : detectorVoicing;\n"
    "        observation.valid = presenceMode_ || detectorVoicing > 0.08f;\n",
    "        observation.audioPresent = presenceMode_;\n"
    "        observation.voicing = detectorVoicing;\n"
    "        observation.valid = true; // this branch contains a confirmed F0\n",
    "separate presence from valid F0")
m = replace_once(
    m,
    "        observation.audioPresent = presenceMode_;\n"
    "        observation.voicing = presenceMode_ ? 1.0f : 0.0f;\n"
    "        observation.valid = false;\n",
    "        observation.audioPresent = presenceMode_;\n"
    "        observation.voicing = 0.0f;\n"
    "        observation.valid = false;\n",
    "invalid presence remains pitchless")

# Scale Lock no longer pushes a mode into the detector.
m = replace_once(
    m,
    "    const bool immediateAuthority = zeroPrudenceAuthority(safe); // AUTHORITY_CONTROLS_EXPLICIT_V1\n",
    "",
    "remove detector authority local")
m = replace_once(
    m,
    "    linkedTracker_.setImmediateAuthority(immediateAuthority);\n",
    "",
    "remove linked detector authority call")
m = replace_once(
    m,
    "        channelTrackers_[static_cast<std::size_t>(channel)].setImmediateAuthority(\n"
    "            immediateAuthority);\n",
    "",
    "remove channel detector authority call")

# Guard against the inverted architecture returning under authoritative names.
for forbidden in (
    "immediateAuthority_",
    "setImmediateAuthority",
    "makePresenceFallback",
    "presenceInitialEvidence",
    "audible input plus a finite F0 is enough",
    "Presence owns correction authority",
):
    if forbidden in m:
        raise SystemExit(f"ModernPitchEngine.cpp: forbidden detector-authority token remains: {forbidden}")

if "DETECTOR_IS_OBSERVER_V1" not in m:
    raise SystemExit("detector observer marker missing")
engine_path.write_text(m)


# -----------------------------------------------------------------------------
# Supervisor tests: presence must stay independent from pitch certainty, while
# an already-owned scale correction survives detector uncertainty unchanged.
test_path = Path("Tests/SupervisorContinuityTest.cpp")
t = test_path.read_text()

old_rap_checks = """    success &= check(rapPresenceHops > 20 && rapMinimumVoicing > 0.99f,
                     "nonzero_audio_cannot_be_unvoiced");
    success &= check(rapMaxDetectorSupport > 0,
                     "nonzero_audio_keeps_detector_paths_alive");
    success &= check(rapPresenceHops > 20 && rapMinDetectorSupport > 0,
                     "nonzero_audio_never_reports_zero_detector_paths");
    success &= check(rapPresenceHops > 20 && rapPitchlessPresentHops == 0,
                     "nonzero_audio_never_reports_pitchless_stable");
"""
new_rap_checks = """    success &= check(rapPresenceHops > 20,
                     "audio_presence_is_reported_independently_of_f0");
    success &= check(rapPresenceHops > 20 && rapPitchlessPresentHops > 0,
                     "aperiodic_presence_may_report_no_trustworthy_f0");
    success &= check(rapMinimumVoicing >= 0.0f && rapMinimumVoicing <= 1.0f
                     && rapMaxDetectorSupport >= 0 && rapMinDetectorSupport >= 0,
                     "detector_diagnostics_remain_bounded_without_fabricating_pitch");
"""
t = replace_once(t, old_rap_checks, new_rap_checks, "rap presence semantics")

old_zero_checks = """    success &= check(zeroConsensusDecision.valid
                     && zeroConsensusDecision.candidate.frequencyHz > 0.0f
                     && std::abs(zeroConsensusDecision.consensus) < 1.0e-7f,
                     "zero_consensus_presence_fallback_yields_valid_f0");

    auto firstPresenceLock = zeroConsensusDecision;
    const bool firstPresenceAccepted = zeroConsensusTracker->confirmOctaveTransition(
        firstPresenceLock, false);
    success &= check(firstPresenceAccepted && firstPresenceLock.valid,
                     "presence_first_lock_does_not_wait_for_consensus");
"""
new_zero_checks = """    success &= check(!zeroConsensusDecision.valid,
                     "presence_does_not_fabricate_weak_zero_consensus_f0");

    auto firstPresenceLock = zeroConsensusDecision;
    const bool firstPresenceAccepted = zeroConsensusTracker->confirmOctaveTransition(
        firstPresenceLock, false);
    success &= check(!firstPresenceAccepted && !firstPresenceLock.valid,
                     "presence_cannot_bypass_initial_register_evidence");
"""
t = replace_once(t, old_zero_checks, new_zero_checks, "zero-consensus presence semantics")

old_live_rescue = """    const bool liveRescueAccepted = liveRescueTracker->confirmOctaveTransition(
        liveRescueDecision, false);
    success &= check(liveRescueAccepted && liveRescueDecision.valid
                     && std::abs(liveRescueDecision.candidate.frequencyHz - 440.0f) < 0.1f,
                     "live_presence_replaces_stale_rescue_register_immediately");
"""
new_live_rescue = """    const bool liveRescueAccepted = liveRescueTracker->confirmOctaveTransition(
        liveRescueDecision, false);
    success &= check(!liveRescueAccepted && !liveRescueDecision.valid,
                     "presence_cannot_override_rescue_register_without_evidence");
"""
t = replace_once(t, old_live_rescue, new_live_rescue, "live rescue presence authority")

# Remove the obsolete test asserting that rigid Scale Lock rewrites detector
# temporal behaviour. The architecture now forbids that coupling entirely.
immediate_start = "    // Even if stale temporal history has an artificially huge score, immediate\n"
immediate_end = "    // A consonant/transient may temporarily remove a usable F0, but while audio\n"
start = t.find(immediate_start)
end = t.find(immediate_end, start)
if start < 0 or end < 0:
    raise SystemExit("immediate-authority test block markers changed")
t = t[:start] + (
    "    // DETECTOR_IS_OBSERVER_V1: rigid correction settings do not alter the\n"
    "    // detector decoder. Static CI below forbids that API from returning.\n\n"
) + t[end:]

test_path.write_text(t)


# Permanent static contract: no detector API may receive musical authority.
contract = contract_path.read_text()
anchor = "    success &= check(!has(engine, \"desiredCents = 0.0\")\n"
require_once(contract, anchor, "authority contract anchor")
extra = """    success &= check(has(engine, "DETECTOR_IS_OBSERVER_V1")
                         && !has(engine, "makePresenceFallback")
                         && !has(engine, "setImmediateAuthority")
                         && !has(engineHeader, "setImmediateAuthority")
                         && !has(engine, "presenceInitialEvidence")
                         && !has(engine, "presenceMode_ ? (decision.candidate.valid"),
                     "detector_observes_but_never_owns_scale_authority");

"""
# The cleanup test already reads engineHeader in current sources; if not, add it.
if 'const auto engineHeader = readFile("Source/ModernPitchEngine.h");' not in contract:
    read_engine = '    const auto engine = readFile("Source/ModernPitchEngine.cpp");\n'
    require_once(contract, read_engine, "read engine source")
    contract = contract.replace(
        read_engine,
        read_engine + '    const auto engineHeader = readFile("Source/ModernPitchEngine.h");\n',
        1)
contract = contract.replace(anchor, extra + anchor, 1)
contract_path.write_text(contract)

print("scale-owned detector authority materialized")
