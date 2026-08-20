from pathlib import Path


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected 1 match, got {count}')
    return text.replace(old, new, 1)

cpp = Path('Source/ModernPitchEngine.cpp')
s = cpp.read_text(encoding='utf-8')
s = replace_once(
    s,
    '1.0 - std::exp(-1.0 / (0.0200 * sampleRate_))), 0.0005f, 0.04f);',
    '1.0 - std::exp(-1.0 / (0.0080 * sampleRate_))), 0.0005f, 0.04f);',
    'period-sync handoff smoothing')
cpp.write_text(s, encoding='utf-8')

test = Path('Tests/SupervisorContinuityTest.cpp')
t = test.read_text(encoding='utf-8')
old = '''    // PARCOR envelope memory should not be replaced by a transient/noisy frame.\n    juce::AudioBuffer<float> block(1, 1024);'''
new = '''    // Restore a clearly voiced analysis context. The previous breath test must\n    // not make this PARCOR freeze check pass trivially by rejecting both frames.\n    parameters.voiceEvidenceValid = true;\n    parameters.voiceBodyEnergy = 0.92f;\n    parameters.voiceHarmonicity = 0.90f;\n    parameters.voiceSpectralReliability = 0.88f;\n    parameters.voiceBreathiness = 0.04f;\n    parameters.voiceEventStrength = 0.0f;\n\n    // PARCOR envelope memory should not be replaced by a transient/noisy frame.\n    juce::AudioBuffer<float> block(1, 1024);'''
t = replace_once(t, old, new, 'PARCOR test evidence reset')
test.write_text(t, encoding='utf-8')
print('musical supervisor handoff tuning applied')
