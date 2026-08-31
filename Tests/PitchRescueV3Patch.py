from pathlib import Path
import runpy

cpp_path = Path('Source/ModernPitchEngine.cpp')
cpp = cpp_path.read_text(encoding='utf-8')
test = Path('Tests/SupervisorContinuityTest.cpp').read_text(encoding='utf-8')

# V3 is now a committed architectural invariant. Do not silently reconstruct
# it from an older implementation: if it disappears, fail before touching DSP.
if 'PITCH_RESCUE_V3_REGISTER_GUARD' not in cpp:
    raise SystemExit('PitchRescueV3 register guard missing from committed source')
if 'rescue_subharmonic_cannot_restart_register' not in test:
    raise SystemExit('PitchRescueV3 regression missing from committed tests')

# V4 may already be materialised by the previous workflow commit. Only apply it
# when its persistent wide-challenger guard is absent.
if 'wideTransitionObservations = 8' not in cpp:
    runpy.run_path('Tests/PitchRescueV4Patch.py', run_name='__main__')
    cpp = cpp_path.read_text(encoding='utf-8')

if 'wideTransitionObservations = 8' not in cpp:
    raise SystemExit('PitchRescueV4 persistent challenger guard missing')

# Audio-presence authority is analysis/supervisor only; the frozen renderer is
# never opened by this chain.
runpy.run_path('Tests/RapVoicingV1Patch.py', run_name='__main__')
print('Pitch rescue V3/V4 verified; rap-presence guard applied')
