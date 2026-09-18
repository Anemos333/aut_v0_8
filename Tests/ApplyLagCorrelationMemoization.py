from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
source = path.read_text()

if "LAG_CORRELATION_MEMOIZATION_V1" in source:
    raise SystemExit("lag memoization already present")
if "CORRELATION_CENSUS_V1" in source:
    raise SystemExit("census instrumentation must not be present")

analyse_anchor = r'''    auto& difference_ = workspace.difference;

    PitchCandidate result;
'''
analyse_insert = r'''    auto& difference_ = workspace.difference;

    // LAG_CORRELATION_MEMOIZATION_V1
    // Exact per-analyse memoization. The first request for a lag executes the
    // golden arithmetic unchanged; repeated requests return the exact float
    // already produced by that arithmetic.
    std::array<float, maxAnalysisSize> residualLagCache {};
    std::array<bool, maxAnalysisSize> residualLagCacheValid {};
    std::array<float, maxAnalysisSize> sourceLagCache {};
    std::array<bool, maxAnalysisSize> sourceLagCacheValid {};

    PitchCandidate result;
'''
if source.count(analyse_anchor) != 1:
    raise SystemExit(f"analyse anchor count {source.count(analyse_anchor)}")
source = source.replace(analyse_anchor, analyse_insert, 1)

old_residual = r'''    const auto residualLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return 0.0f;
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
            const double b = voiceResidualFrame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        return denominator > 0.0
            ? clamp01(static_cast<float>(correlation / denominator)) : 0.0f;
    };
'''
new_residual = r'''    const auto residualLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return 0.0f;

        const std::size_t cacheIndex = static_cast<std::size_t>(lag);
        if (residualLagCacheValid[cacheIndex])
            return residualLagCache[cacheIndex];

        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = voiceResidualFrame_[static_cast<std::size_t>(index)];
            const double b = voiceResidualFrame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        const float value = denominator > 0.0
            ? clamp01(static_cast<float>(correlation / denominator)) : 0.0f;
        residualLagCache[cacheIndex] = value;
        residualLagCacheValid[cacheIndex] = true;
        return value;
    };
'''
if source.count(old_residual) != 1:
    raise SystemExit(f"residual lambda count {source.count(old_residual)}")
source = source.replace(old_residual, new_residual, 1)

old_source = r'''    const auto sourceLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return -1.0f;
        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = frame_[static_cast<std::size_t>(index)];
            const double b = frame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        return denominator > 0.0
            ? static_cast<float>(correlation / denominator) : -1.0f;
    };
'''
new_source = r'''    const auto sourceLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return -1.0f;

        const std::size_t cacheIndex = static_cast<std::size_t>(lag);
        if (sourceLagCacheValid[cacheIndex])
            return sourceLagCache[cacheIndex];

        double correlation = 0.0;
        double energyA = 0.0;
        double energyB = 0.0;
        const int overlap = analysisLength - lag;
        for (int index = 0; index < overlap; ++index)
        {
            const double a = frame_[static_cast<std::size_t>(index)];
            const double b = frame_[static_cast<std::size_t>(index + lag)];
            correlation += a * b;
            energyA += a * a;
            energyB += b * b;
        }
        const double denominator = std::sqrt(std::max(1.0e-20, energyA * energyB));
        const float value = denominator > 0.0
            ? static_cast<float>(correlation / denominator) : -1.0f;
        sourceLagCache[cacheIndex] = value;
        sourceLagCacheValid[cacheIndex] = true;
        return value;
    };
'''
if source.count(old_source) != 1:
    raise SystemExit(f"source lambda count {source.count(old_source)}")
source = source.replace(old_source, new_source, 1)

for marker in [
    "TAU_EVIDENCE_MEMOIZATION_V1",
    "LAG_CORRELATION_MEMOIZATION_V1",
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
print("LAG_CORRELATION_MEMOIZATION_PATCH=APPLIED")
