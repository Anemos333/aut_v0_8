#!/usr/bin/env python3
from pathlib import Path
p=Path('Source/SingleWetSpectralRenderer.cpp')
s=p.read_text()
old='''    const float threshold = maximumMagnitude * 0.018f;\n    for (int bin = 1; bin < positiveBins; ++bin)\n    {\n        const float centre = magnitudes_[static_cast<std::size_t>(bin)];\n        if (centre >= threshold\n            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]\n            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])'''
new='''    // ONE_VOICE_AIR_ANCHORS_V10: air/noise is not a separate phase class.\n    // Every measurable local maximum can anchor its own local spectral region.\n    for (int bin = 1; bin < positiveBins; ++bin)\n    {\n        const float centre = magnitudes_[static_cast<std::size_t>(bin)];\n        if (centre > 1.0e-12f\n            && centre >= magnitudes_[static_cast<std::size_t>(bin - 1)]\n            && centre > magnitudes_[static_cast<std::size_t>(bin + 1)])'''
assert old in s
s=s.replace(old,new,1)
p.write_text(s)
print('PEAK_THRESHOLD_ONLY_PROBE=PASS')
