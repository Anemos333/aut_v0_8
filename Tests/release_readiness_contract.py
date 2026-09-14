#!/usr/bin/env python3
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FAILURES: list[str] = []


def check(condition: bool, label: str) -> None:
    status = "PASS" if condition else "FAIL"
    print(f"{label}={status}")
    if not condition:
        FAILURES.append(label)


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def git_blob(path: str) -> str:
    result = subprocess.run(
        ["git", "hash-object", str(ROOT / path)],
        check=True,
        text=True,
        capture_output=True,
    )
    return result.stdout.strip()


# ---------------------------------------------------------------------------
# 1. Freeze the accepted audio baseline for release-readiness work.
#    Deliberate pitch/timbre work in later phases must update this manifest
#    explicitly after an approved audio test; release-only changes must not.
AUDIO_BASELINE = {
    "Source/SingleWetSpectralRenderer.cpp": "051721f1d740e95e23f740353ad5df9dc6fefb3c",
    "Source/SingleWetSpectralRenderer.h": "2b68b3b3d5dc0579cdab94f4969203ef876ef058",
    "Source/ModernPitchEngine.cpp": "59eb1598d1c79bdf2e74beb844258deb14d300b3",
    "Source/ModernPitchEngine.h": "e069a3283e5e144f2d540145b3b4c13889c9c532",
    "Source/LivePitchProcessor.h": "6e7a050978d8fefd187f3649d198ec6efd2abc6d",
    "Source/PluginProcessor.cpp": "e2ba6b826f8e3cd9b292a4f98748240d6a581a12",
    "Source/PluginProcessor.h": "ddf47fe649734c7f0e9729e063d207a7c0fbe009",
}

for path, expected in AUDIO_BASELINE.items():
    check((ROOT / path).is_file(), f"audio_baseline_file_exists:{path}")
    if (ROOT / path).is_file():
        actual = git_blob(path)
        print(f"audio_baseline_blob:{path}={actual}")
        check(actual == expected, f"audio_baseline_unchanged:{path}")


# ---------------------------------------------------------------------------
# 2. DAW state contract. This is intentionally source-level: it guards that
#    every non-APVTS piece of session state is both written and restored, while
#    the APVTS itself is copied/restored through JUCE's ValueTree mechanism.
processor = text("Source/PluginProcessor.cpp")

check("auto state = apvts.copyState();" in processor,
      "state_apvts_saved")
check("apvts.replaceState (tree);" in processor,
      "state_apvts_restored")

state_properties = (
    "scaleIndex",
    "customPresetIndex",
    "rootNoteIndex",
    "processingMode",
    "factoryPresetIndex",
)

for prop in state_properties:
    saved = f'setProperty ("{prop}"' in processor
    restored = f'hasProperty ("{prop}")' in processor
    check(saved, f"state_saved:{prop}")
    check(restored, f"state_restored:{prop}")

check("customPresets.toValueTree()" in processor,
      "custom_scales_saved")
check('getChildWithName ("CustomScales")' in processor,
      "custom_scales_restored")
check("refreshScaleSnapshot();" in processor,
      "scale_snapshot_refreshed_after_restore")
check('hasProperty ("liveModeEnabled")' in processor,
      "legacy_session_mode_compatibility")


# ---------------------------------------------------------------------------
# 3. Parameter identity contract: VST hosts persist automation by parameter ID.
#    Duplicate IDs are release-blocking even if the GUI appears to work.
ids = re.findall(r'ParameterID\s*\{\s*"([^"]+)"\s*,', processor)
check(bool(ids), "parameter_ids_found")
check(len(ids) == len(set(ids)), "parameter_ids_unique")

required_ids = {
    "speed",
    "amount",
    "humanize",
    "tempoMode",
    "tempoDivision",
    "tempoGlidePercent",
    "tempoLockStrength",
    "tempoSmartOnset",
    "scaleLock",
    "lockHysteresis",
    "vibratoPreserve",
    "analogMode",
    "outVolume",
}
check(required_ids.issubset(set(ids)), "required_parameter_ids_present")


# ---------------------------------------------------------------------------
# 4. Custom scale persistence invariants and project-resource hygiene.
custom_header = text("Source/CustomScalePresets.h")
custom_cpp = text("Source/CustomScalePresets.cpp")
check("static constexpr int maxPresets = 7;" in custom_header,
      "custom_scale_limit_is_seven")
check('ValueTree tree ("CustomScales")' in custom_cpp,
      "custom_scale_tree_type_stable")
check('ValueTree scaleTree ("Scale")' in custom_cpp,
      "custom_scale_child_type_stable")
check('setProperty ("name"' in custom_cpp and 'setProperty ("ratios"' in custom_cpp,
      "custom_scale_payload_serialized")

for resource in (
    "Resources/sfondo1.jpeg",
    "Resources/sfondo2.jpg",
    "Resources/sfondo3.jpeg",
):
    check((ROOT / resource).is_file(), f"resource_exists:{resource}")

# Absolute local development paths must never leak into release sources.
source_text = "\n".join(
    p.read_text(encoding="utf-8", errors="ignore")
    for p in (ROOT / "Source").glob("*.*")
    if p.is_file()
)
check("C:\\Users\\" not in source_text,
      "no_windows_absolute_paths_in_source")


# ---------------------------------------------------------------------------
# 5. Packaging contract: the release target is VST3-only. Other formats may
#    not silently re-enter the product definition during release hardening.
cmake = text("CMakeLists.txt")
check(re.search(r"\bFORMATS\s+VST3\s*(?:\n|\r\n)", cmake) is not None,
      "cmake_declares_vst3_only")
check("FORMATS VST3 AU" not in cmake,
      "au_format_absent")
check('PRODUCT_NAME "Neumaton"' in cmake,
      "product_name_stable")
check("PLUGIN_MANUFACTURER_CODE Mtal" in cmake,
      "manufacturer_code_stable")
check("PLUGIN_CODE Matv" in cmake,
      "plugin_code_stable")


# ---------------------------------------------------------------------------
# 6. Release architecture contract. The accepted renderer remains exactly one
#    wet path; release hardening is not allowed to reintroduce old routing.
engine = text("Source/ModernPitchEngine.cpp")
renderer_h = text("Source/SingleWetSpectralRenderer.h")
renderer_cpp = text("Source/SingleWetSpectralRenderer.cpp")

check("MINIMAL_RENDERER_V5" in renderer_cpp,
      "single_wet_renderer_v5_marker")
check(renderer_h.count("SynthesisLayer layer_") == 1,
      "single_synthesis_layer")
check("array<SynthesisLayer" not in renderer_h,
      "no_parallel_synthesis_layer_array")
check("wetRenderers_[static_cast<std::size_t>(channel)].processSample(" in engine,
      "production_routes_through_single_wet_renderer")

for forbidden in (
    "TransportClock",
    "ChannelPath::",
    "updateLpcTarget",
    "rendererContext",
):
    check(forbidden not in engine, f"forbidden_old_routing_absent:{forbidden}")


if FAILURES:
    print("\nRelease readiness contract failed:")
    for failure in FAILURES:
        print(f" - {failure}")
    sys.exit(1)

print("\nrelease_readiness_contract=PASS")
