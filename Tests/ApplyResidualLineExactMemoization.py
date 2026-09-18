from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
source = path.read_text()

if "RESIDUAL_LINE_EXACT_MEMOIZATION_V1" in source:
    raise SystemExit("exact line memoization already present")
if "CORRELATION_CENSUS_V1" in source:
    raise SystemExit("census instrumentation must not be present")

if "#include <cstdint>\n" not in source:
    source = source.replace(
        "#include <cmath>\n#include <cstring>\n",
        "#include <cmath>\n#include <cstdint>\n#include <cstring>\n",
        1
    )

old = r'''    // RESIDUAL_HARMONIC_CONTRAST_V1
    // A real voiced source produces narrow coherent lines at F0 multiples after
    // inverse filtering. Broadband breath/background produces comparable energy
    // between those lines. The half-harmonic subtraction also suppresses the
    // common 2F0 alias without requiring detector history to own the register.
    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
    {
        if (!std::isfinite(cyclesPerSample)
            || cyclesPerSample <= 0.0 || cyclesPerSample >= 0.48)
        {
            return 0.0f;
        }
        double real = 0.0;
        double imag = 0.0;
        double signalEnergy = 0.0;
        double windowEnergy = 0.0;
        const double denominatorN = static_cast<double>(std::max(1, analysisLength - 1));
        for (int index = 0; index < analysisLength; ++index)
        {
            const double window = 0.5 - 0.5 * std::cos(
                twoPi * static_cast<double>(index) / denominatorN);
            const double sample = static_cast<double>(
                voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
            const double phase = twoPi * cyclesPerSample * static_cast<double>(index);
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
            signalEnergy += sample * sample;
            windowEnergy += window * window;
        }
        const double normaliser = std::max(1.0e-20, signalEnergy * windowEnergy);
        return clamp01(static_cast<float>(std::sqrt(
            2.0 * (real * real + imag * imag) / normaliser)));
    };
'''

new = r'''    // RESIDUAL_HARMONIC_CONTRAST_V1
    // A real voiced source produces narrow coherent lines at F0 multiples after
    // inverse filtering. Broadband breath/background produces comparable energy
    // between those lines. The half-harmonic subtraction also suppresses the
    // common 2F0 alias without requiring detector history to own the register.
    //
    // RESIDUAL_LINE_EXACT_MEMOIZATION_V1
    // Cache only an exactly identical IEEE-754 argument bit pattern within this
    // analyse() call. The first request executes the golden loop unchanged.
    std::array<std::uint64_t, 96> residualLineCacheKeys {};
    std::array<float, 96> residualLineCacheValues {};
    std::size_t residualLineCacheCount = 0;

    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
    {
        if (!std::isfinite(cyclesPerSample)
            || cyclesPerSample <= 0.0 || cyclesPerSample >= 0.48)
        {
            return 0.0f;
        }

        std::uint64_t cacheKey = 0;
        static_assert(sizeof(cacheKey) == sizeof(cyclesPerSample));
        std::memcpy(&cacheKey, &cyclesPerSample, sizeof(cacheKey));
        for (std::size_t cacheIndex = 0;
             cacheIndex < residualLineCacheCount;
             ++cacheIndex)
        {
            if (residualLineCacheKeys[cacheIndex] == cacheKey)
                return residualLineCacheValues[cacheIndex];
        }

        double real = 0.0;
        double imag = 0.0;
        double signalEnergy = 0.0;
        double windowEnergy = 0.0;
        const double denominatorN = static_cast<double>(std::max(1, analysisLength - 1));
        for (int index = 0; index < analysisLength; ++index)
        {
            const double window = 0.5 - 0.5 * std::cos(
                twoPi * static_cast<double>(index) / denominatorN);
            const double sample = static_cast<double>(
                voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
            const double phase = twoPi * cyclesPerSample * static_cast<double>(index);
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
            signalEnergy += sample * sample;
            windowEnergy += window * window;
        }
        const double normaliser = std::max(1.0e-20, signalEnergy * windowEnergy);
        const float value = clamp01(static_cast<float>(std::sqrt(
            2.0 * (real * real + imag * imag) / normaliser)));

        if (residualLineCacheCount < residualLineCacheKeys.size())
        {
            residualLineCacheKeys[residualLineCacheCount] = cacheKey;
            residualLineCacheValues[residualLineCacheCount] = value;
            ++residualLineCacheCount;
        }
        return value;
    };
'''

if source.count(old) != 1:
    raise SystemExit(f"residual line block count {source.count(old)}")
source = source.replace(old, new, 1)

for marker in [
    "TAU_EVIDENCE_MEMOIZATION_V1",
    "RESIDUAL_LINE_EXACT_MEMOIZATION_V1",
    "ANALYSIS_WORKSPACE_REENTRANCY_V1",
]:
    if marker not in source:
        raise SystemExit(f"missing required marker {marker}")

for rejected in [
    "RESIDUAL_LINE_INVARIANT_HOIST_V1",
    "RESIDUAL_LINE_COHERENCE_CPU_V1",
    "CORRELATION_CENSUS_V1",
]:
    if rejected in source:
        raise SystemExit(f"rejected marker present: {rejected}")

path.write_text(source)
print("RESIDUAL_LINE_EXACT_MEMOIZATION_PATCH=APPLIED")
