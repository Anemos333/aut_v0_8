#!/usr/bin/env python3
from pathlib import Path
p=Path('Source/SingleWetSpectralRenderer.cpp')
s=p.read_text()

# F0 cannot reorganise the renderer.
needle='''    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});\n\n    // Correction authority comes from the musical trajectory.'''
repl='''    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});\n\n    // EXPERIMENTAL_CONTENT_AGNOSTIC_PV_PROBE\n    sourceFundamentalHz = 0.0;\n\n    // Correction authority comes from the musical trajectory.'''
assert needle in s
s=s.replace(needle,repl,1)

# At 128 remove peak-coherent instantaneous frequency: every spectral sample
# uses the same measured-frequency * ratio law.
old='else if (frameSize_ <= 256 && !nearestPeak_.empty())'
new='else if (frameSize_ == 256 && !nearestPeak_.empty())'
assert old in s
s=s.replace(old,new,1)

# At 128 remove peak-region magnitude translation. All bins use sourceBin*ratio.
old='if (frameSize_ <= 128 && peakValid)\n        {'
new='if (false && frameSize_ <= 128 && peakValid)\n        {'
assert old in s
s=s.replace(old,new,1)

# Final identity lock is already zero for 128; assert it stays so.
assert '* (frameSize_ <= 128 ? 0.000000f : 1.0f));' in s
p.write_text(s)
print('EXPERIMENTAL_CONTENT_AGNOSTIC_PV_PROBE=PASS')
