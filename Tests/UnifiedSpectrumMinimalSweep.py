#!/usr/bin/env python3
from pathlib import Path
import os

p=Path('Source/SingleWetSpectralRenderer.cpp')
s=p.read_text()
lock=float(os.environ.get('EXPERIMENTAL_LOCK','0.0'))
assert 0.0 <= lock <= 1.0

# 1) Do not classify diffuse air/noise by magnitude threshold. Every local
# maximum is a geometric phase anchor candidate.
old='''    const float threshold = maximumMagnitude * 0.018f;\n    for (int bin = 1; bin < positiveBins; ++bin)\n    {\n        const float centre = magnitudes_[static_cast<std::size_t>(bin)];\n        if (centre >= threshold\n            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]\n            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])'''
new='''    for (int bin = 1; bin < positiveBins; ++bin)\n    {\n        const float centre = magnitudes_[static_cast<std::size_t>(bin)];\n        if (centre > 1.0e-12f\n            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]\n            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])'''
assert old in s
s=s.replace(old,new,1)

# 2) Probe content-agnostic renderer authority: F0 must not reorganise Live.
needle='''    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});\n\n    // Correction authority comes from the musical trajectory.'''
repl='''    std::fill(layer.spectrum.begin(), layer.spectrum.end(), Complex {});\n\n    // UNIFIED_SPECTRUM_MINIMAL_SWEEP: F0 is target authority upstream only.\n    // It cannot reorganise or reconstruct a spectral family in the renderer.\n    sourceFundamentalHz = 0.0;\n\n    // Correction authority comes from the musical trajectory.'''
assert needle in s
s=s.replace(needle,repl,1)

# 3) Experimental probe: no owning-peak magnitude translation. Scale every
# spectral coordinate by the same commanded ratio, exactly like a full-spectrum
# transport. The existing instantaneous-frequency phase analysis remains.
old='if (frameSize_ <= 128 && peakValid)\n        {'
new='if (false && frameSize_ <= 128 && peakValid)\n        {'
assert old in s
s=s.replace(old,new,1)

# 4) Sweep only Experimental final phase coherence. Quality/Live formulas are
# otherwise the current ones, preserving their proven magnitude geometry.
old='* (frameSize_ <= 128 ? 0.000000f : 1.0f));'
new=f'* (frameSize_ <= 128 ? {lock:.6f}f : 1.0f));'
assert old in s
s=s.replace(old,new,1)

p.write_text(s)
print(f'UNIFIED_SPECTRUM_MINIMAL_SWEEP=PASS lock={lock:.3f}')
