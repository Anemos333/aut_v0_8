from pathlib import Path

path = Path("Source/SingleWetSpectralRenderer.cpp")
source = path.read_text()

marker = "LINEAR_COMPLEX_BIN_INTERPOLATION_V1"
if marker in source:
    raise SystemExit("candidate already present")

old = r'''        float lowerWeight = 1.0f - fraction;
        float upperWeight = fraction;
        const float weightPower = lowerWeight * lowerWeight
                                + upperWeight * upperWeight;
        const float weightNormalisation = weightPower > 1.0e-12f
            ? 1.0f / std::sqrt(weightPower)
            : 1.0f;
        lowerWeight *= weightNormalisation;
        upperWeight *= weightNormalisation;
'''
new = r'''        // LINEAR_COMPLEX_BIN_INTERPOLATION_V1
        // Preserve the complex linear interpolation itself. Power-normalising
        // each individual source-bin split changes the relative amplitude of
        // neighbouring leakage bins as the fractional destination moves and can
        // distort a multi-harmonic spectrum even though the commanded ratio is
        // correct. Overall pitch-shift energy is already handled by energyScale.
        const float lowerWeight = 1.0f - fraction;
        const float upperWeight = fraction;
'''

if source.count(old) != 1:
    raise SystemExit(f"interpolation anchor count {source.count(old)}")
source = source.replace(old, new, 1)

path.write_text(source)
print("LINEAR_COMPLEX_BIN_INTERPOLATION_PATCH=APPLIED")
