from pathlib import Path

path = Path('Tests/SupervisorContinuityTest.cpp')
text = path.read_text(encoding='utf-8')
text = text.replace(
    '    success &= check(rapPresenceHops > 20 && rapPitchlessPresentHops == 0,\\n                     "nonzero_audio_never_reports_pitchless_stable");\\n',
    '    success &= check(rapPresenceHops > 20 && rapPitchlessPresentHops == 0,\n                     "nonzero_audio_never_reports_pitchless_stable");\n')
path.write_text(text, encoding='utf-8')
print('RapVoicingV2 test newline normalized')
