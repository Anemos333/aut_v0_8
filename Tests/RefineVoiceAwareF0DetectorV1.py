from pathlib import Path

p = Path(__file__).resolve().parents[1] / 'Tests' / 'SupervisorContinuityTest.cpp'
s = p.read_text()

def one(old, new, label):
    global s
    c = s.count(old)
    if c != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {c}')
    s = s.replace(old, new, 1)

one('''    success &= check(breathValidF0 == 0 && breathMeasurements <= 2,
                     "colored_breath_is_not_promoted_to_f0");
''', '''    std::cerr << "voice_aware_breath_valid_f0=" << breathValidF0
              << " provisional=" << breathMeasurements << '\\n';
    success &= check(breathValidF0 == 0 && breathMeasurements <= 2,
                     "colored_breath_is_not_promoted_to_f0");
''', 'breath metrics')

one('''    success &= check(voicedDecisionCount > 20
                     && nearToneCount * 4 >= voicedDecisionCount * 3,
                     "low_snr_vocal_family_isolated_from_colored_noise");
''', '''    std::cerr << "voice_aware_noisy_voice_decisions=" << voicedDecisionCount
              << " near_220=" << nearToneCount << '\\n';
    success &= check(voicedDecisionCount > 20
                     && nearToneCount * 4 >= voicedDecisionCount * 3,
                     "low_snr_vocal_family_isolated_from_colored_noise");
''', 'voice metrics')

p.write_text(s)
print('VOICE_AWARE_F0_DETECTOR_V1 diagnostics refined')
