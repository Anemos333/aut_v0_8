from pathlib import Path
p = Path('Source/ModernPitchEngine.cpp')
s = p.read_text(encoding='utf-8')
old = '''        const double withinNoteTolerance = std::clamp(\n            22.0 + 38.0 * static_cast<double>(humanize),\n            18.0,\n            0.42 * static_cast<double>(quantizer.minimumStepCents()));'''
new = '''        const double maximumWithinNoteTolerance = std::max(\n            18.0, 0.42 * static_cast<double>(quantizer.minimumStepCents()));\n        const double withinNoteTolerance = std::clamp(\n            22.0 + 38.0 * static_cast<double>(humanize),\n            18.0, maximumWithinNoteTolerance);'''
if s.count(old) != 1:
    raise RuntimeError(f'within-note tolerance: expected 1 match, got {s.count(old)}')
s = s.replace(old, new, 1)
p.write_text(s, encoding='utf-8')

# Add a dense-scale guard to the existing supervisor test so this cannot regress.
tp = Path('Tests/SupervisorContinuityTest.cpp')
t = tp.read_text(encoding='utf-8')
marker = '''    success &= check(!leftMusicalBody\n                     && vibratoState.noteBodyLatched\n                     && vibratoState.trackingState == ModernPitchEngine::TrackingState::stable,\n                     "long_vibrato_is_classified_as_stable_note_body");\n'''
insert = marker + '''\n    std::array<double, 48> denseScale {};\n    for (int degree = 0; degree < 48; ++degree)\n        denseScale[static_cast<std::size_t>(degree)] = std::exp2(degree / 48.0);\n    ModernPitchEngine::ScaleQuantizer denseQuantizer;\n    denseQuantizer.reset();\n    denseQuantizer.setScale(denseScale.data(), static_cast<int>(denseScale.size()), 440.0);\n    ModernPitchEngine::CorrectionState denseState;\n    ModernPitchEngine::PitchObservation denseObservation = syncObservation;\n    denseObservation.frequencyHz = 442.0f;\n    engine->updateCorrectionState(denseState, denseQuantizer, denseObservation, vibratoParameters);\n    success &= check(std::isfinite(denseState.pitchCentreLog2)\n                     && std::isfinite(denseState.desiredCents),\n                     "dense_microtonal_scale_has_valid_within_note_tolerance");\n'''
if t.count(marker) != 1:
    raise RuntimeError(f'dense-scale test marker: expected 1 match, got {t.count(marker)}')
t = t.replace(marker, insert, 1)
tp.write_text(t, encoding='utf-8')
print('dense microtonal supervisor fix applied')
