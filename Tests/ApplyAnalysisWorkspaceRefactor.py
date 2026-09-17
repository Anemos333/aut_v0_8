#!/usr/bin/env python3
from pathlib import Path
import difflib
import sys

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "Source" / "ModernPitchEngine.h"
CPP = ROOT / "Source" / "ModernPitchEngine.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def replace_n(text: str, old: str, new: str, expected: int, label: str) -> str:
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} matches, found {count}")
    return text.replace(old, new)


header_before = HEADER.read_text()
cpp_before = CPP.read_text()
header = header_before
cpp = cpp_before

workspace_struct = """        struct AnalysisWorkspace
        {
            std::array<float, maxAnalysisSize> frame {};
            // Detector-only inverse-filtered residual. The audible signal never
            // enters this buffer and the renderer never reads it.
            std::array<float, maxAnalysisSize> voiceResidualFrame {};
            std::array<float, maxAnalysisSize> difference {};
        };

"""

candidate_slot = """        struct CandidateSlot
        {
            PitchCandidate candidate;
            int ageInHops = 1000;
        };

"""
header = replace_once(
    header,
    candidate_slot,
    candidate_slot + workspace_struct,
    "insert AnalysisWorkspace",
)

old_decl = """        [[nodiscard]] PitchCandidate analyse(
            const std::array<float, ringSize>& ring,
            int writePosition,
            int availableSamples,
            double effectiveSampleRate,
            float minimumFrequency,
            float maximumFrequency,
            int analysisLength) noexcept;
"""
new_decl = """        [[nodiscard]] PitchCandidate analyse(
            const std::array<float, ringSize>& ring,
            int writePosition,
            int availableSamples,
            double effectiveSampleRate,
            float minimumFrequency,
            float maximumFrequency,
            int analysisLength,
            AnalysisWorkspace& workspace) noexcept;
"""
header = replace_once(header, old_decl, new_decl, "analyse declaration")

old_storage = """        std::array<float, maxAnalysisSize> frame_ {};
        // Detector-only inverse-filtered residual. The audible signal never
        // enters this buffer and the renderer never reads it.
        std::array<float, maxAnalysisSize> voiceResidualFrame_ {};
        std::array<float, maxAnalysisSize> difference_ {};
"""
new_storage = """        // Serial workspace today; explicit ownership makes analyse() re-entrant
        // without changing any detector arithmetic. Future workers must provide
        // their own private AnalysisWorkspace instance.
        AnalysisWorkspace analysisWorkspace_ {};
"""
header = replace_once(header, old_storage, new_storage, "workspace storage")

old_reset = """    frame_.fill(0.0f);
    voiceResidualFrame_.fill(0.0f);
    difference_.fill(1.0f);
"""
new_reset = """    analysisWorkspace_.frame.fill(0.0f);
    analysisWorkspace_.voiceResidualFrame.fill(0.0f);
    analysisWorkspace_.difference.fill(1.0f);
"""
cpp = replace_once(cpp, old_reset, new_reset, "workspace reset")

old_def = """ModernPitchEngine::MultiRatePitchTracker::PitchCandidate
ModernPitchEngine::MultiRatePitchTracker::analyse(
    const std::array<float, ringSize>& ring,
    int writePosition,
    int availableSamples,
    double effectiveSampleRate,
    float minimumFrequency,
    float maximumFrequency,
    int analysisLength) noexcept
{
    PitchCandidate result;
"""
new_def = """ModernPitchEngine::MultiRatePitchTracker::PitchCandidate
ModernPitchEngine::MultiRatePitchTracker::analyse(
    const std::array<float, ringSize>& ring,
    int writePosition,
    int availableSamples,
    double effectiveSampleRate,
    float minimumFrequency,
    float maximumFrequency,
    int analysisLength,
    AnalysisWorkspace& workspace) noexcept
{
    // ANALYSIS_WORKSPACE_REENTRANCY_V1: preserve the golden detector body and
    // its exact operation ordering. These local aliases deliberately keep the
    // original identifiers used by every arithmetic expression below.
    auto& frame_ = workspace.frame;
    auto& voiceResidualFrame_ = workspace.voiceResidualFrame;
    auto& difference_ = workspace.difference;

    PitchCandidate result;
"""
cpp = replace_once(cpp, old_def, new_def, "analyse definition")

call_replacements = [
    (
        """                                               fullMinimum,
                                               fullMaximum,
                                               standardAnalysisSize);""",
        """                                               fullMinimum,
                                               fullMaximum,
                                               standardAnalysisSize,
                                               analysisWorkspace_);""",
        "full-rate analyse call",
    ),
    (
        """                                                   halfMinimum,
                                                   halfMaximum,
                                                   standardAnalysisSize);""",
        """                                                   halfMinimum,
                                                   halfMaximum,
                                                   standardAnalysisSize,
                                                   analysisWorkspace_);""",
        "half-rate analyse call",
    ),
    (
        """                                                      quarterMinimum,
                                                      quarterMaximum,
                                                      384);""",
        """                                                      quarterMinimum,
                                                      quarterMaximum,
                                                      384,
                                                      analysisWorkspace_);""",
        "quarter-rate analyse call",
    ),
    (
        """                                                     eighthMinimum,
                                                     eighthMaximum,
                                                     maxAnalysisSize);""",
        """                                                     eighthMinimum,
                                                     eighthMaximum,
                                                     maxAnalysisSize,
                                                     analysisWorkspace_);""",
        "eighth-rate analyse call",
    ),
]
for old, new, label in call_replacements:
    cpp = replace_once(cpp, old, new, label)

# Hard guard: golden detector math must remain byte-for-byte present. The refactor
# is allowed to alter only storage plumbing around analyse(), not these sensitive
# operations that previously caused audible regressions when optimized.
for needle in [
    "const double window = 0.5 - 0.5 * std::cos(",
    "real += sample * std::cos(phase);",
    "imag -= sample * std::sin(phase);",
    "const float yinThreshold = 0.12f + 0.16f * sensitivity_;",
    "const float fallbackThreshold = 0.26f + 0.20f * sensitivity_",
    "// PRIMITIVE_DIVISOR_GEOMETRY_V6_6",
    "// VOICE_BODY_LIVE_PERMISSION_V6_7_1",
]:
    if cpp_before.count(needle) != cpp.count(needle):
        raise RuntimeError(f"golden detector math guard changed: {needle}")

# No old shared scratch arrays may survive as tracker storage.
for needle in [
    "std::array<float, maxAnalysisSize> frame_ {}",
    "std::array<float, maxAnalysisSize> voiceResidualFrame_ {}",
    "std::array<float, maxAnalysisSize> difference_ {}",
]:
    if needle in header:
        raise RuntimeError(f"shared scratch storage still present: {needle}")

# The only new analyse parameter must be supplied by all four rate paths.
if cpp.count("analysisWorkspace_);") != 4:
    raise RuntimeError("expected exactly four serial workspace analyse call sites")

HEADER.write_text(header)
CPP.write_text(cpp)

print("ANALYSIS_WORKSPACE_REFACTOR=APPLIED")
print("HEADER_DIFF_BEGIN")
print("".join(difflib.unified_diff(
    header_before.splitlines(True), header.splitlines(True),
    fromfile="ModernPitchEngine.h.golden", tofile="ModernPitchEngine.h.workspace")))
print("HEADER_DIFF_END")
print("CPP_DIFF_BEGIN")
print("".join(difflib.unified_diff(
    cpp_before.splitlines(True), cpp.splitlines(True),
    fromfile="ModernPitchEngine.cpp.golden", tofile="ModernPitchEngine.cpp.workspace")))
print("CPP_DIFF_END")
