from pathlib import Path
import runpy

cpp = Path('Source/ModernPitchEngine.cpp').read_text(encoding='utf-8')
test = Path('Tests/SupervisorContinuityTest.cpp').read_text(encoding='utf-8')

# V3 is now a committed architectural invariant. Do not silently reconstruct
# it from an older implementation: if it disappears, fail before touching DSP.
if 'PITCH_RESCUE_V3_REGISTER_GUARD' not in cpp:
    raise SystemExit('PitchRescueV3 register guard missing from committed source')
if 'rescue_subharmonic_cannot_restart_register' not in test:
    raise SystemExit('PitchRescueV3 regression missing from committed tests')

# The workflow already invokes this V3 gate, so chain later detector/supervisor
# guards here. The frozen SingleWetSpectralRenderer is never touched.
runpy.run_path('Tests/PitchRescueV4Patch.py', run_name='__main__')
runpy.run_path('Tests/RapVoicingV1Patch.py', run_name='__main__')
print('Pitch rescue V3 invariant verified; V4 + rap-presence guards applied')
