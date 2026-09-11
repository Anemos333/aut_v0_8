#!/usr/bin/env python3
from pathlib import Path
import re
p=Path('Source/SingleWetSpectralRenderer.cpp')
s=p.read_text(encoding='utf-8')

# After the main V11 patch, remove peak-dependent transport velocity entirely.
pattern=re.compile(r'''            const double measuredSourceBin =\n                trueSourceBins_\[static_cast<std::size_t>\(sourceBin\)\];\n            double transportTargetBin = measuredSourceBin \* safeRatio;\n            if \(frameSize_ <= 256 && !nearestPeak_\.empty\(\)\)\n            \{.*?\n            \}\n            synthesisPhase \+= expectedPhaseScale \* transportTargetBin;''',re.S)
repl='''            // ONE_VOICE_SELF_COORDINATE_SHIFT_V11_1\n            // Every measured spectral coordinate receives the exact same ratio.\n            // No peak/F0/noise class can select another transport velocity.\n            const double measuredSourceBin =\n                trueSourceBins_[static_cast<std::size_t>(sourceBin)];\n            const double transportTargetBin = measuredSourceBin * safeRatio;\n            synthesisPhase += expectedPhaseScale * transportTargetBin;'''
s,n=pattern.subn(repl,s,count=1)
assert n==1,f'phase self-shift replace={n}'

pattern=re.compile(r'''        double targetPosition = static_cast<double>\(sourceBin\) \* safeRatio;\n        if \(frameSize_ <= 256 && peakValid\)\n        \{.*?\n        \}\n        if \(targetPosition < -1\.0''',re.S)
repl='''        double targetPosition = static_cast<double>(sourceBin) * safeRatio;\n        if (frameSize_ <= 256)\n        {\n            // Translate the analysed FFT sample by the displacement implied by\n            // its own measured instantaneous frequency. For a tonal lobe the\n            // leakage bins therefore share the measured partial displacement;\n            // for breath/noise the same equation still applies without a class.\n            const double measuredSourceBin = trueSourceBins_[sourceIndex];\n            targetPosition = static_cast<double>(sourceBin)\n                + measuredSourceBin * (safeRatio - 1.0);\n        }\n        if (targetPosition < -1.0'''
s,n=pattern.subn(repl,s,count=1)
assert n==1,f'magnitude self-shift replace={n}'

p.write_text(s,encoding='utf-8')
print('ONE_VOICE_SELF_COORDINATE_SHIFT_V11_1=PASS')
