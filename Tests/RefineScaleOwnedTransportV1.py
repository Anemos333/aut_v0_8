from pathlib import Path

p = Path('Source/ModernPitchEngine.cpp')
text = p.read_text()
old = '''        parameters.scaleLock && parameters.hardLockActive,
        musicalOnset || forceTargetSwitch,
        pending);'''
new = '''        parameters.scaleLock && parameters.hardLockActive,
        // USER_HOLD_REMAINS_AUTHORITY_V1: a supervisor-confirmed musical
        // challenger may select the candidate coordinate, but it cannot reuse
        // the quantizer onset bypass to defeat explicit user Hold. Only a real
        // musical onset gets onset semantics; otherwise existing hysteresis is
        // applied exactly as requested by the user.
        musicalOnset,
        pending);'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'hold authority: expected 1 match, got {count}')
p.write_text(text.replace(old, new, 1))
print('scale-owned transport v1 hold refinement materialized')
