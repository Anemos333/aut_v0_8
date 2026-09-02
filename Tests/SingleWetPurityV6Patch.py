from pathlib import Path
import subprocess

MARKER = 'SINGLE_WET_PURITY_V6'

cpp_path = Path('Source/ModernPitchEngine.cpp')
clean_test_path = Path('Tests/CleanModernPitchEngineTest.cpp')
gui_test_path = Path('Tests/GuiAudibilityTest.cpp')
integration_test_path = Path('Tests/IntegrationSmokeTest.cpp')
renderer_test_path = Path('Tests/SingleWetSpectralRendererTest.cpp')

cpp = cpp_path.read_text(encoding='utf-8')
clean_test = clean_test_path.read_text(encoding='utf-8')
gui_test = gui_test_path.read_text(encoding='utf-8')
integration_test = integration_test_path.read_text(encoding='utf-8')
renderer_test = renderer_test_path.read_text(encoding='utf-8')

if MARKER not in cpp:
    old = '''    switch (mode)\n    {\n        case LatencyMode::ultraLive: return 128;\n        case LatencyMode::live:      return 256;\n        case LatencyMode::quality:   return 512;\n    }\n'''
    new = '''    // SINGLE_WET_PURITY_V6\n    // The 128-sample spectral lattice is not a valid production transport for\n    // this renderer: the measured +100-cent target/source power ratio is only\n    // about 2.07 (roughly 3 dB), which is an audibly strong source-frequency\n    // component.  256 samples is the smallest currently proven single-wet\n    // lattice (>2000:1 on the same regression), so Experimental must report\n    // and use that honest latency until a genuinely low-latency transport can\n    // satisfy the same spectral-purity contract.\n    switch (mode)\n    {\n        case LatencyMode::ultraLive: return 256;\n        case LatencyMode::live:      return 256;\n        case LatencyMode::quality:   return 512;\n    }\n'''
    if old not in cpp:
        raise RuntimeError('latency profile block not found')
    cpp = cpp.replace(old, new, 1)
    cpp_path.write_text(cpp, encoding='utf-8')

# Production-mode latency expectations now match the smallest renderer lattice
# that actually satisfies the no-audible-source-copy requirement.
old = '    const std::array<int, 3> expectedLatencies { 128, 256, 512 };'
new = '    const std::array<int, 3> expectedLatencies { 256, 256, 512 };'
if old in clean_test:
    clean_test = clean_test.replace(old, new, 1)
    clean_test_path.write_text(clean_test, encoding='utf-8')
elif new not in clean_test:
    raise RuntimeError('CleanModernPitchEngine latency expectation not found')

old = '''    const std::vector<std::pair<ModernPitchEngine::LatencyMode, int>> modes {\n        { ModernPitchEngine::LatencyMode::quality, 512 },\n        { ModernPitchEngine::LatencyMode::live, 256 },\n        { ModernPitchEngine::LatencyMode::ultraLive, 128 }\n    };'''
new = '''    const std::vector<std::pair<ModernPitchEngine::LatencyMode, int>> modes {\n        { ModernPitchEngine::LatencyMode::quality, 512 },\n        { ModernPitchEngine::LatencyMode::live, 256 },\n        { ModernPitchEngine::LatencyMode::ultraLive, 256 }\n    };'''
if old in gui_test:
    gui_test = gui_test.replace(old, new, 1)
    gui_test_path.write_text(gui_test, encoding='utf-8')
elif new not in gui_test:
    raise RuntimeError('GuiAudibility latency expectation not found')

old = '    testLatency(ModernPitchEngine::LatencyMode::ultraLive, 128, "Ultra Live");'
new = '    testLatency(ModernPitchEngine::LatencyMode::ultraLive, 256, "Ultra Live");'
if old in integration_test:
    integration_test = integration_test.replace(old, new, 1)
    integration_test_path.write_text(integration_test, encoding='utf-8')
elif new not in integration_test:
    raise RuntimeError('IntegrationSmokeTest latency expectation not found')

# 128 is deliberately no longer a production frame. Tighten the renderer test
# around the two supported production lattices so a clearly audible stationary
# source component can never be accepted again.
old = '''    const std::array<int, 3> frameSizes { 512, 256, 128 };\n    for (const int frameSize : frameSizes)\n    {\n        const auto output = renderTone(frameSize, 100.0);\n        const double targetPower = tonePower(output, semitoneTargetHz, 12000);\n        const double sourcePower = tonePower(output, 220.0, 12000);\n        const double ratio = targetPower / std::max(1.0e-20, sourcePower);\n        std::cerr << "frame_" << frameSize << "_target_source_ratio=" << ratio << '\\n';\n        success &= check(targetPower > 1.5 * sourcePower,\n                         frameSize == 512 ? "quality_obeys_exact_transport"\n                         : frameSize == 256 ? "live_obeys_exact_transport"\n                                            : "experimental_obeys_exact_transport");\n    }\n'''
new = '''    // SINGLE_WET_PURITY_V6: a production lattice must suppress the original\n    // pitch by at least 30 dB in power on this deterministic one-semitone test.\n    // The former 128-sample profile measured only ~2.07:1 and is therefore not\n    // a production option until its transport is redesigned.\n    const std::array<int, 2> frameSizes { 512, 256 };\n    for (const int frameSize : frameSizes)\n    {\n        const auto output = renderTone(frameSize, 100.0);\n        const double targetPower = tonePower(output, semitoneTargetHz, 12000);\n        const double sourcePower = tonePower(output, 220.0, 12000);\n        const double ratio = targetPower / std::max(1.0e-20, sourcePower);\n        std::cerr << "frame_" << frameSize << "_target_source_ratio=" << ratio << '\\n';\n        success &= check(targetPower > 1000.0 * sourcePower,\n                         frameSize == 512 ? "quality_has_no_audible_source_copy"\n                                          : "live_and_experimental_have_no_audible_source_copy");\n    }\n'''
if old in renderer_test:
    renderer_test = renderer_test.replace(old, new, 1)
    renderer_test_path.write_text(renderer_test, encoding='utf-8')
elif 'quality_has_no_audible_source_copy' not in renderer_test:
    raise RuntimeError('SingleWetSpectralRenderer production purity loop not found')

# The workflow's historical add-list predates these stronger regression files.
# Stage them here so the guarded transformation commit contains the expectations
# atomically with the production latency change.
subprocess.run([
    'git', 'add',
    'Tests/CleanModernPitchEngineTest.cpp',
    'Tests/GuiAudibilityTest.cpp',
    'Tests/IntegrationSmokeTest.cpp',
    'Tests/SingleWetSpectralRendererTest.cpp',
], check=True)

print('Single-wet production spectral purity V6 applied')
