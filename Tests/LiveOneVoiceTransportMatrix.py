#!/usr/bin/env python3
from pathlib import Path
import os,re
p=Path('Source/SingleWetSpectralRenderer.cpp')
s=p.read_text(encoding='utf-8')
phase=os.environ.get('LIVE_PHASE','self')
mag=os.environ.get('LIVE_MAG','self')
assert phase in ('self','peak') and mag in ('self','peak')

# Current materialized V11 uses self/self for Live 256. Replace only the Live
# branch when probing peak-local alternatives. Experimental and Quality remain
# untouched in this matrix.
if phase=='peak':
    old='''            if (frameSize_ == 256)\n            {\n                // ONE_VOICE_LIVE_SELF_COORDINATE_SHIFT_V11_1\n                // Every measured coordinate receives the same ratio. F0,\n                // harmonic family, breath and peak identity have no authority.\n                transportTargetBin = measuredSourceBin * safeRatio;\n            }'''
    new='''            if (frameSize_ == 256)\n            {\n                // MATRIX: measured local region coherence, no F0 semantics.\n                const int velocityPeak = nearestPeak_.empty()\n                    ? sourceBin : nearestPeak_[static_cast<std::size_t>(sourceBin)];\n                if (velocityPeak >= 0 && velocityPeak <= positiveBins)\n                {\n                    const float distance = static_cast<float>(std::abs(velocityPeak-sourceBin));\n                    const float coherence = 1.0f - smoothStep(0.75f,2.75f,distance);\n                    const double peakMeasuredBin = trueSourceBins_[static_cast<std::size_t>(velocityPeak)];\n                    const double coherentSourceBin = measuredSourceBin\n                        + static_cast<double>(coherence)*(peakMeasuredBin-measuredSourceBin);\n                    const double regionShiftBins = peakMeasuredBin*(safeRatio-1.0);\n                    transportTargetBin = coherentSourceBin + regionShiftBins;\n                }\n            }'''
    assert s.count(old)==1,s.count(old)
    s=s.replace(old,new,1)

if mag=='peak':
    old='''        else if (frameSize_ == 256)\n        {\n            // ONE_VOICE_LIVE_SELF_COORDINATE_SHIFT_V11_1\n            const double measuredSourceBin = trueSourceBins_[sourceIndex];\n            targetPosition = static_cast<double>(sourceBin)\n                + measuredSourceBin * (safeRatio - 1.0);\n        }'''
    new='''        else if (frameSize_ == 256 && peakValid)\n        {\n            // MATRIX: rigidly translate the measured local lobe, no F0 semantics.\n            const double truePeakBin = trueSourceBins_[static_cast<std::size_t>(peak)];\n            const double regionShiftBins = truePeakBin*(safeRatio-1.0);\n            targetPosition = static_cast<double>(sourceBin)+regionShiftBins;\n        }'''
    assert s.count(old)==1,s.count(old)
    s=s.replace(old,new,1)

p.write_text(s,encoding='utf-8')
print(f'LIVE_ONE_VOICE_MATRIX phase={phase} mag={mag}')
