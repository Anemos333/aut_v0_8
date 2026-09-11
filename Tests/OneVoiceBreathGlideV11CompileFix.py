#!/usr/bin/env python3
from pathlib import Path
p=Path('Source/SingleWetSpectralRenderer.cpp')
s=p.read_text(encoding='utf-8')
needle='''        const float spatialLock = peakValid\n            ? 1.0f - smoothStep(coreRadiusBins, fadeRadiusBins, peakDistance)\n            : 0.0f;'''
repl='''        const float coreRadiusBins = frameSize_ >= 512 ? 2.0f : 1.0f;\n        const float fadeRadiusBins = frameSize_ >= 512 ? 3.0f : 2.0f;\n        const float spatialLock = peakValid\n            ? 1.0f - smoothStep(coreRadiusBins, fadeRadiusBins, peakDistance)\n            : 0.0f;'''
assert s.count(needle)==1, f'spatial lock insertion count={s.count(needle)}'
s=s.replace(needle,repl,1)
p.write_text(s,encoding='utf-8')
print('ONE_VOICE_BREATH_GLIDE_V11_COMPILE_FIX=PASS')
