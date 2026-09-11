#!/usr/bin/env python3
from pathlib import Path
import re
p=Path('Source/SingleWetSpectralRenderer.cpp')
s=p.read_text(encoding='utf-8')

# Live/256 can use the fully content-agnostic self-coordinate law without
# source copy. Experimental/128 cannot: the same law moves the air but leaves
# the fundamental near its original position, so 128 keeps the existing
# measured-peak region transport until a better full-band shifter is proven.
pattern=re.compile(r'''            const double measuredSourceBin =\n                trueSourceBins_\[static_cast<std::size_t>\(sourceBin\)\];\n            double transportTargetBin = measuredSourceBin \* safeRatio;\n            if \(frameSize_ <= 256 && !nearestPeak_\.empty\(\)\)\n            \{.*?\n            \}\n            synthesisPhase \+= expectedPhaseScale \* transportTargetBin;''',re.S)
repl='''            const double measuredSourceBin =\n                trueSourceBins_[static_cast<std::size_t>(sourceBin)];\n            double transportTargetBin = measuredSourceBin * safeRatio;\n            if (frameSize_ == 256)\n            {\n                // ONE_VOICE_LIVE_SELF_COORDINATE_SHIFT_V11_1\n                // Every measured coordinate receives the same ratio. F0,\n                // harmonic family, breath and peak identity have no authority.\n                transportTargetBin = measuredSourceBin * safeRatio;\n            }\n            else if (frameSize_ <= 128 && !nearestPeak_.empty())\n            {\n                // Experimental keeps the proven measured-region velocity law.\n                // A pure per-bin 128 law reintroduced a strong source pitch.\n                const int velocityPeak =\n                    nearestPeak_[static_cast<std::size_t>(sourceBin)];\n                if (velocityPeak >= 0 && velocityPeak <= positiveBins)\n                {\n                    const float distance = static_cast<float>(\n                        std::abs(velocityPeak - sourceBin));\n                    const float baseCoherence = 1.0f\n                        - smoothStep(0.50f, 2.50f, distance);\n                    const float coherence = clamp01(baseCoherence * 0.850000f);\n                    const double peakMeasuredBin =\n                        trueSourceBins_[static_cast<std::size_t>(velocityPeak)];\n                    const double coherentSourceBin = measuredSourceBin\n                        + static_cast<double>(coherence)\n                        * (peakMeasuredBin - measuredSourceBin);\n                    transportTargetBin = coherentSourceBin * safeRatio;\n                }\n            }\n            synthesisPhase += expectedPhaseScale * transportTargetBin;'''
s,n=pattern.subn(repl,s,count=1)
assert n==1,f'phase live self-shift replace={n}'

pattern=re.compile(r'''        double targetPosition = static_cast<double>\(sourceBin\) \* safeRatio;\n        if \(frameSize_ <= 256 && peakValid\)\n        \{.*?\n        \}\n        if \(targetPosition < -1\.0''',re.S)
repl='''        double targetPosition = static_cast<double>(sourceBin) * safeRatio;\n        if (frameSize_ <= 128 && peakValid)\n        {\n            const double truePeakBin =\n                trueSourceBins_[static_cast<std::size_t>(peak)];\n            const double regionShiftBins = truePeakBin * (safeRatio - 1.0);\n            targetPosition = static_cast<double>(sourceBin) + regionShiftBins;\n        }\n        else if (frameSize_ == 256)\n        {\n            // ONE_VOICE_LIVE_SELF_COORDINATE_SHIFT_V11_1\n            const double measuredSourceBin = trueSourceBins_[sourceIndex];\n            targetPosition = static_cast<double>(sourceBin)\n                + measuredSourceBin * (safeRatio - 1.0);\n        }\n        if (targetPosition < -1.0'''
s,n=pattern.subn(repl,s,count=1)
assert n==1,f'magnitude live self-shift replace={n}'

p.write_text(s,encoding='utf-8')
print('ONE_VOICE_LIVE_SELF_COORDINATE_SHIFT_V11_1=PASS')
