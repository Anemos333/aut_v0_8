from pathlib import Path

cpp_path = Path("Source/ModernPitchEngine.cpp")
h_path = Path("Source/ModernPitchEngine.h")
cpp = cpp_path.read_text()
hdr = h_path.read_text()

marker = "RESIDUAL_HANN_LAZY_MEMOIZATION_V1"
if marker in cpp or marker in hdr:
    raise SystemExit("lazy Hann memoization already present")

for required in [
    "TAU_EVIDENCE_MEMOIZATION_V1",
    "RESIDUAL_LINE_EXACT_MEMOIZATION_V1",
    "TERMINAL_TAIL_STABLE_TRANSPORT_REBASE_V1",
    "ANALYSIS_WORKSPACE_REENTRANCY_V1",
]:
    if required not in cpp:
        raise SystemExit(f"missing required source marker {required}")

for rejected in [
    "LAG_CORRELATION_MEMOIZATION_V1",
    "RESIDUAL_LINE_INVARIANT_HOIST_V1",
    "RESIDUAL_LINE_COHERENCE_CPU_V1",
]:
    if rejected in cpp:
        raise SystemExit(f"rejected marker present: {rejected}")

old_workspace = r'''            std::array<float, maxAnalysisSize> difference {};
        };
'''
new_workspace = r'''            std::array<float, maxAnalysisSize> difference {};
            // RESIDUAL_HANN_LAZY_MEMOIZATION_V1: scratch for one analyse() call.
            // The first unique residual line fills this lazily in golden loop
            // order; later unique lines reuse the exact double coefficients.
            std::array<double, maxAnalysisSize> residualHannWindow {};
        };
'''
if hdr.count(old_workspace) != 1:
    raise SystemExit(f"workspace anchor count {hdr.count(old_workspace)}")
hdr = hdr.replace(old_workspace, new_workspace, 1)

old_alias = r'''    auto& frame_ = workspace.frame;
    auto& voiceResidualFrame_ = workspace.voiceResidualFrame;
    auto& difference_ = workspace.difference;

    PitchCandidate result;
'''
new_alias = r'''    auto& frame_ = workspace.frame;
    auto& voiceResidualFrame_ = workspace.voiceResidualFrame;
    auto& difference_ = workspace.difference;
    auto& residualHannWindow_ = workspace.residualHannWindow;

    PitchCandidate result;
'''
if cpp.count(old_alias) != 1:
    raise SystemExit(f"analyse alias anchor count {cpp.count(old_alias)}")
cpp = cpp.replace(old_alias, new_alias, 1)

old_cache = r'''    std::array<std::uint64_t, 96> residualLineCacheKeys {};
    std::array<float, 96> residualLineCacheValues {};
    std::size_t residualLineCacheCount = 0;

    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
'''
new_cache = r'''    std::array<std::uint64_t, 96> residualLineCacheKeys {};
    std::array<float, 96> residualLineCacheValues {};
    std::size_t residualLineCacheCount = 0;

    // RESIDUAL_HANN_LAZY_MEMOIZATION_V1
    // False at the beginning of every analyse() call. The first unique spectral
    // line computes each Hann coefficient at the exact point where the golden
    // loop computed it and stores the resulting double. No sample/window-energy
    // accumulation is moved or precomputed.
    bool residualHannReady = false;

    const auto residualLineCoherence = [&](double cyclesPerSample) noexcept
'''
if cpp.count(old_cache) != 1:
    raise SystemExit(f"line cache anchor count {cpp.count(old_cache)}")
cpp = cpp.replace(old_cache, new_cache, 1)

old_loop = r'''        const double denominatorN = static_cast<double>(std::max(1, analysisLength - 1));
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
'''
new_loop = r'''        const double denominatorN = static_cast<double>(std::max(1, analysisLength - 1));
        for (int index = 0; index < analysisLength; ++index)
        {
            double window = 0.0;
            if (!residualHannReady)
            {
                window = 0.5 - 0.5 * std::cos(
                    twoPi * static_cast<double>(index) / denominatorN);
                residualHannWindow_[static_cast<std::size_t>(index)] = window;
            }
            else
            {
                window = residualHannWindow_[static_cast<std::size_t>(index)];
            }

            const double sample = static_cast<double>(
                voiceResidualFrame_[static_cast<std::size_t>(index)]) * window;
            const double phase = twoPi * cyclesPerSample * static_cast<double>(index);
            real += sample * std::cos(phase);
            imag -= sample * std::sin(phase);
            signalEnergy += sample * sample;
            windowEnergy += window * window;
        }
        residualHannReady = true;
        const double normaliser = std::max(1.0e-20, signalEnergy * windowEnergy);
'''
if cpp.count(old_loop) != 1:
    raise SystemExit(f"golden residual loop anchor count {cpp.count(old_loop)}")
cpp = cpp.replace(old_loop, new_loop, 1)

if marker not in cpp or marker not in hdr:
    raise SystemExit("new marker missing after patch")

cpp_path.write_text(cpp)
h_path.write_text(hdr)
print("RESIDUAL_HANN_LAZY_MEMOIZATION_PATCH=APPLIED")
