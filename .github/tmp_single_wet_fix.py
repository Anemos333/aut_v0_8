from pathlib import Path

path = Path('Source/SingleWetSpectralRenderer.cpp')
text = path.read_text()
replacements = [
    ("double wrapCorrectionToNearestOctave(double c) noexcept { if(!std::isfinite(c)) return 0.0; double w=std::fmod(c+600.0,1200.0); if(w<0) w+=1200.0; return w-600.0; }",
     "double sanitiseCorrectionCents(double c) noexcept { return std::clamp(std::isfinite(c) ? c : 0.0, -2400.0, 2400.0); }"),
    ("    const double safeCents = wrapCorrectionToNearestOctave(correctionCents);",
     "    // Correction authority comes from the musical trajectory. The renderer\n    // must not reinterpret register or reduce the requested interval.\n    const double safeCents = sanitiseCorrectionCents(correctionCents);"),
    ("    // more, change the harmonic/noise mask more slowly and reduce correction\n    // authority when the spectrum becomes noise-dominant.\n",
     "    // more and change the harmonic/noise mask more slowly. These analysis\n    // profiles classify reconstruction components; they never scale correction cents.\n"),
    ("    // harmonic family is present.\n", "    // harmonic family is present.\n"),
    ("    // are preserved rather than mistaken for removable noise; their pitch\n    // correction authority is reduced through spectralReliability below.\n",
     "    // are preserved rather than mistaken for removable noise. Spectral\n    // reliability remains a reconstruction diagnostic, never correction authority.\n"),
    ("    // weak, do not gate the whole signal.  The controller will move toward the\n    // aligned dry path instead; retaining at least 82% prevents breath holes.\n",
     "    // weak, do not gate the whole residual; retaining at least 82% prevents\n    // breath holes without introducing any dry path.\n")
]
for old, new in replacements:
    if old not in text:
        raise RuntimeError(f'missing replacement marker: {old[:80]}')
    text = text.replace(old, new, 1)
path.write_text(text)
