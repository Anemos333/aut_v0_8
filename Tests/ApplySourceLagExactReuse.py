from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
text = path.read_text()

old_skip = """        if (candidateTau < tauMinimum || candidateTau > tauMaximum)
            continue;
        const float candidatePeak = sourceLagCorrelation(candidateTau);
"""
new_skip = """        if (candidateTau < tauMinimum || candidateTau > tauMaximum)
            continue;

        // SOURCE_LAG_EXACT_REUSE_V1
        // sourcePeak was computed from bestTau immediately before this loop.
        // offset == 0 therefore requests the identical lag and can only reproduce
        // the same float before a strict '>' comparison that cannot succeed.
        if (offset == 0)
            continue;

        const float candidatePeak = sourceLagCorrelation(candidateTau);
"""

if text.count(old_skip) != 1:
    raise SystemExit(f"expected exactly one source-basin duplicate site, got {text.count(old_skip)}")
text = text.replace(old_skip, new_skip, 1)

old_centre = "        const double centre = sourceLagCorrelation(sourceTau);\n"
new_centre = """        // SOURCE_LAG_EXACT_REUSE_V1
        // sourcePeak is exactly the float returned by sourceLagCorrelation()
        // for the current sourceTau, including the primitive-divisor path.
        // Converting that already-computed float to double is bit-equivalent
        // to calling the lambda again and assigning its float return to double.
        const double centre = static_cast<double>(sourcePeak);
"""

if text.count(old_centre) != 1:
    raise SystemExit(f"expected exactly one parabolic centre duplicate site, got {text.count(old_centre)}")
text = text.replace(old_centre, new_centre, 1)

path.write_text(text)
print("SOURCE_LAG_EXACT_REUSE_PATCH=APPLIED")
