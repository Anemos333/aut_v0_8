from pathlib import Path


def between(text, start, end, replacement=''):
    a = text.find(start)
    if a < 0:
        raise RuntimeError(f'missing start: {start[:80]}')
    b = text.find(end, a)
    if b < 0:
        raise RuntimeError(f'missing end: {end[:80]}')
    return text[:a] + replacement + text[b:]

h = Path('Source/ModernPitchEngine.h')
text = h.read_text()
text = text.replace('    static constexpr int maximumLpcOrder = 12;\n    static constexpr int transportRingSize = 16384;\n\n', '', 1)
text = between(text, '    struct TransportPlan\n', '    struct CorrectionState\n')
text = between(text, '    [[nodiscard]] float transportSyncStrength(', '    void publishMetering(')
text = text.replace('    TransportClock linkedClock_;\n    std::array<TransportClock, maxSupportedChannels> channelClocks_ {};\n    std::array<ChannelPath, maxSupportedChannels> channelPaths_ {}; // retained only for invariant comparison; not in audio path\n', '', 1)
text = between(text, '    static constexpr int lpcAnalysisRingSize = 1024;\n', '    PitchObservation latestObservation_ {};\n')
h.write_text(text)

cpp = Path('Source/ModernPitchEngine.cpp')
text = cpp.read_text()
text = between(text,
    '//==============================================================================\n// Full-signal transport\n',
    '//==============================================================================\n// ModernPitchEngine control and processing\n',
    '//==============================================================================\n// ModernPitchEngine control and processing\n')
text = between(text,
    'float ModernPitchEngine::transportSyncStrength(',
    'void ModernPitchEngine::updateCorrectionState(')
text = between(text,
    'std::array<float, ModernPitchEngine::maximumLpcOrder>\nModernPitchEngine::calculateReflectionCoefficients',
    'void ModernPitchEngine::process(\n    juce::AudioBuffer<float>& buffer,')
for old in [
    '    linkedClock_.prepare(sampleRate_, latencySamples_);\n',
    '        channelClocks_[static_cast<std::size_t>(channel)].prepare(sampleRate_, latencySamples_);\n',
    '        channelPaths_[static_cast<std::size_t>(channel)].prepare(sampleRate_, latencySamples_);\n',
    '    linkedClock_.reset();\n',
    '        channelClocks_[static_cast<std::size_t>(channel)].reset();\n',
    '        channelPaths_[static_cast<std::size_t>(channel)].reset();\n',
    '    lpcAnalysisRing_.fill(0.0f);\n    lpcAnalysisScratch_.fill(0.0f);\n    lpcAnalysisWritePosition_ = 0;\n    lpcAnalysisAvailableSamples_ = 0;\n    lpcAnalysisHopCounter_ = 0;\n    currentReflectionTarget_.fill(0.0f);\n'
]:
    if old not in text:
        raise RuntimeError(f'missing cpp cleanup marker: {old[:80]}')
    text = text.replace(old, '', 1)
cpp.write_text(text)

cm = Path('CMakeLists.txt')
text = cm.read_text()
text = between(text,
    '    # Retained as dormant regression fixtures for the superseded transport/LPC\n',
    '    juce_add_console_app(SupervisorContinuityTest')
text = between(text,
    '    juce_add_console_app(StableVoicePathTest',
    '    juce_add_console_app(SingleWetSpectralRendererTest')
cm.write_text(text)
