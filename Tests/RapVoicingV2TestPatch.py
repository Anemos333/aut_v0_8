from pathlib import Path

path = Path('Tests/SupervisorContinuityTest.cpp')
text = path.read_text(encoding='utf-8')

if 'nonzero_audio_never_reports_zero_detector_paths' not in text:
    text = text.replace(
        '    int rapMaxDetectorSupport = 0;\n    float rapMinimumVoicing = 1.0f;\n',
        '    int rapMaxDetectorSupport = 0;\n    int rapMinDetectorSupport = 4;\n    float rapMinimumVoicing = 1.0f;\n',
        1)
    text = text.replace(
        '            rapMaxDetectorSupport = std::max(rapMaxDetectorSupport,\n                                             rapObservation.detectorSupport);\n            rapMinimumVoicing = std::min(rapMinimumVoicing,\n',
        '            rapMaxDetectorSupport = std::max(rapMaxDetectorSupport,\n                                             rapObservation.detectorSupport);\n            rapMinDetectorSupport = std::min(rapMinDetectorSupport,\n                                             rapObservation.detectorSupport);\n            rapMinimumVoicing = std::min(rapMinimumVoicing,\n',
        1)
    text = text.replace(
        '    success &= check(rapMaxDetectorSupport > 0,\n                     "nonzero_audio_keeps_detector_paths_alive");\n',
        '    success &= check(rapMaxDetectorSupport > 0,\n                     "nonzero_audio_keeps_detector_paths_alive");\n    success &= check(rapPresenceHops > 20 && rapMinDetectorSupport > 0,\n                     "nonzero_audio_never_reports_zero_detector_paths");\n',
        1)
    path.write_text(text, encoding='utf-8')

print('Rap voicing V2 strict 0/4 regression applied')
