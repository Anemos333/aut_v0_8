from pathlib import Path

path = Path("Tests/MaterializeScaleOwnedAuthorityV3.py")
text = path.read_text()

old = '''if m.count(old_guard) != 2:
    raise SystemExit(f"decoder immediate-authority guards: expected 2, found {m.count(old_guard)}")'''
new = '''if m.count(old_guard) < 1:
    raise SystemExit("decoder immediate-authority guard not found")'''
if text.count(old) != 1:
    raise SystemExit("authority guard matcher patch did not match exactly once")
text = text.replace(old, new, 1)

anchor = '''engine_path = Path("Source/ModernPitchEngine.cpp")
m = engine_path.read_text()

# Detector fusion always uses its own evidence/continuity model.'''
insert = '''engine_path = Path("Source/ModernPitchEngine.cpp")
m = engine_path.read_text()

# Presence is telemetry, never permission to lower detector standards.
m = replace_once(
    m,
    "    immediateAuthority_ = false; // AUTHORITY_CONTROLS_EXPLICIT_V1\\n",
    "",
    "remove detector authority reset")
m = replace_once(
    m,
    "    // RAP_VOICING_V1_AUDIO_PRESENCE: detector confidence may be poor, but a\\n"
    "    // numerically non-silent input is never allowed to erase the voice.  In\\n"
    "    // presence mode analyse the best available period instead of returning no\\n"
    "    // path merely because YIN confidence is low.\\n"
    "    const float rmsFloor = presenceMode_ ? numericalPresenceRms : minimumDetectorRms;\\n"
    "    if (rms < rmsFloor)\\n",
    "    // DETECTOR_IS_OBSERVER_V1: audio presence does not lower pitch-analysis\\n"
    "    // standards. Aperiodic material may correctly yield no F0 while the\\n"
    "    // downstream scale target remains fully authoritative.\\n"
    "    if (rms < minimumDetectorRms)\\n",
    "presence cannot lower detector RMS gate")
m = replace_once(
    m,
    "    if (!presenceMode_ && thresholdTau < 0 && globalValue > fallbackThreshold)\\n"
    "        return result;\\n",
    "    if (thresholdTau < 0 && globalValue > fallbackThreshold)\\n"
    "        return result;\\n",
    "presence cannot force YIN fallback")
m = replace_once(
    m,
    "    if (!immediateAuthority_)\\n"
    "    {\\n",
    "    {\\n",
    "remove detector authority hold guard")

# Detector fusion always uses its own evidence/continuity model.'''
if text.count(anchor) != 1:
    raise SystemExit("engine authority insertion anchor did not match exactly once")
text = text.replace(anchor, insert, 1)
path.write_text(text)
print("authority materializer patched")
