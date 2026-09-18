from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
source = path.read_text()

if "CORRELATION_CENSUS_V1" in source:
    raise SystemExit("census already present")

source = source.replace(
    '#include <cmath>\n#include <cstring>\n#include <limits>\n',
    '#include <cmath>\n#include <cstdio>\n#include <cstdint>\n#include <cstring>\n#include <limits>\n',
    1
)

namespace_anchor = """constexpr float numericalPresenceRms = 1.0e-8f;\n"""
namespace_insert = r'''
// CORRELATION_CENSUS_V1
// CI-only instrumentation. This never ships: it counts exact repeated requests
// while leaving every original calculation in place.
struct CorrelationCensusTotals
{
    std::uint64_t analyses = 0;
    std::uint64_t residualLagRequests = 0;
    std::uint64_t residualLagDuplicates = 0;
    std::uint64_t sourceLagRequests = 0;
    std::uint64_t sourceLagDuplicates = 0;
    std::uint64_t lineRequests = 0;
    std::uint64_t lineExactDuplicates = 0;

    ~CorrelationCensusTotals() noexcept
    {
        std::fprintf(stderr,
            "CORRELATION_CENSUS analyses=%llu residual_lag_requests=%llu "
            "residual_lag_duplicates=%llu source_lag_requests=%llu "
            "source_lag_duplicates=%llu line_requests=%llu "
            "line_exact_duplicates=%llu\\n",
            static_cast<unsigned long long>(analyses),
            static_cast<unsigned long long>(residualLagRequests),
            static_cast<unsigned long long>(residualLagDuplicates),
            static_cast<unsigned long long>(sourceLagRequests),
            static_cast<unsigned long long>(sourceLagDuplicates),
            static_cast<unsigned long long>(lineRequests),
            static_cast<unsigned long long>(lineExactDuplicates));
    }
};

CorrelationCensusTotals correlationCensusTotals;
'''
if namespace_anchor not in source:
    raise SystemExit("namespace anchor missing")
source = source.replace(namespace_anchor, namespace_anchor + namespace_insert, 1)

analyse_anchor = r'''    auto& difference_ = workspace.difference;

    PitchCandidate result;
'''
analyse_insert = r'''    auto& difference_ = workspace.difference;

    ++correlationCensusTotals.analyses;
    std::array<int, 64> seenResidualLags {};
    int seenResidualLagCount = 0;
    std::array<int, 64> seenSourceLags {};
    int seenSourceLagCount = 0;
    std::array<std::uint64_t, 96> seenLineKeys {};
    int seenLineKeyCount = 0;

    PitchCandidate result;
'''
if analyse_anchor not in source:
    raise SystemExit("analyse anchor missing")
source = source.replace(analyse_anchor, analyse_insert, 1)

residual_anchor = r'''    const auto residualLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return 0.0f;
'''
residual_insert = r'''    const auto residualLagCorrelation = [&](int lag) noexcept
    {
        ++correlationCensusTotals.residualLagRequests;
        bool duplicateLag = false;
        for (int seenIndex = 0; seenIndex < seenResidualLagCount; ++seenIndex)
        {
            if (seenResidualLags[static_cast<std::size_t>(seenIndex)] == lag)
            {
                duplicateLag = true;
                break;
            }
        }
        if (duplicateLag)
        {
            ++correlationCensusTotals.residualLagDuplicates;
        }
        else if (seenResidualLagCount < static_cast<int>(seenResidualLags.size()))
        {
            seenResidualLags[static_cast<std::size_t>(seenResidualLagCount++)] = lag;
        }

        if (lag <= 0 || lag >= analysisLength - 8)
            return 0.0f;
'''
if source.count(residual_anchor) != 1:
    raise SystemExit(f"residual anchor count {source.count(residual_anchor)}")
source = source.replace(residual_anchor, residual_insert, 1)

line_anchor = r'''    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
    {
        if (!std::isfinite(cyclesPerSample)
            || cyclesPerSample <= 0.0 || cyclesPerSample >= 0.48)
'''
line_insert = r'''    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
    {
        ++correlationCensusTotals.lineRequests;
        std::uint64_t lineKey = 0;
        std::memcpy(&lineKey, &cyclesPerSample, sizeof(lineKey));
        bool duplicateLine = false;
        for (int seenIndex = 0; seenIndex < seenLineKeyCount; ++seenIndex)
        {
            if (seenLineKeys[static_cast<std::size_t>(seenIndex)] == lineKey)
            {
                duplicateLine = true;
                break;
            }
        }
        if (duplicateLine)
        {
            ++correlationCensusTotals.lineExactDuplicates;
        }
        else if (seenLineKeyCount < static_cast<int>(seenLineKeys.size()))
        {
            seenLineKeys[static_cast<std::size_t>(seenLineKeyCount++)] = lineKey;
        }

        if (!std::isfinite(cyclesPerSample)
            || cyclesPerSample <= 0.0 || cyclesPerSample >= 0.48)
'''
if source.count(line_anchor) != 1:
    raise SystemExit(f"line anchor count {source.count(line_anchor)}")
source = source.replace(line_anchor, line_insert, 1)

source_anchor = r'''    const auto sourceLagCorrelation = [&](int lag) noexcept
    {
        if (lag <= 0 || lag >= analysisLength - 8)
            return -1.0f;
'''
source_insert = r'''    const auto sourceLagCorrelation = [&](int lag) noexcept
    {
        ++correlationCensusTotals.sourceLagRequests;
        bool duplicateLag = false;
        for (int seenIndex = 0; seenIndex < seenSourceLagCount; ++seenIndex)
        {
            if (seenSourceLags[static_cast<std::size_t>(seenIndex)] == lag)
            {
                duplicateLag = true;
                break;
            }
        }
        if (duplicateLag)
        {
            ++correlationCensusTotals.sourceLagDuplicates;
        }
        else if (seenSourceLagCount < static_cast<int>(seenSourceLags.size()))
        {
            seenSourceLags[static_cast<std::size_t>(seenSourceLagCount++)] = lag;
        }

        if (lag <= 0 || lag >= analysisLength - 8)
            return -1.0f;
'''
if source.count(source_anchor) != 1:
    raise SystemExit(f"source anchor count {source.count(source_anchor)}")
source = source.replace(source_anchor, source_insert, 1)

for marker in [
    "TAU_EVIDENCE_MEMOIZATION_V1",
    "ANALYSIS_WORKSPACE_REENTRANCY_V1",
    "CORRELATION_CENSUS_V1",
]:
    if marker not in source:
        raise SystemExit(f"missing {marker}")

if "RESIDUAL_LINE_INVARIANT_HOIST_V1" in source:
    raise SystemExit("rejected residual hoist present")

path.write_text(source)
print("CORRELATION_CENSUS_PATCH=APPLIED")
