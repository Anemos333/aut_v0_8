from pathlib import Path

p = Path('Source/ModernPitchEngine.cpp')
s = p.read_text()

def one(old, new, label):
    global s
    n = s.count(old)
    if n != 1:
        raise RuntimeError(f'{label}: expected one match, found {n}')
    s = s.replace(old, new, 1)

one(
"""    phase_ += phaseIncrement;\n    phase_ -= std::floor(phase_);\n    return plan;\n}\n""",
"""    // Exact unity is a declared-latency identity point. Preserve phase, but\n    // make both read coordinates exactly equal to the centre rather than merely\n    // numerically close after gain-weighted reconstruction.\n    if (deviation < 1.0e-12)\n    {\n        plan.delayA = centreDelay;\n        plan.delayB = centreDelay;\n    }\n\n    phase_ += phaseIncrement;\n    phase_ -= std::floor(phase_);\n    return plan;\n}\n""",
'exact unity centre')

one(
"""        double baseAlpha = distanceCents > 95.0 ? 0.30 : 0.07;\n        if (state.noteBodyLatched && distanceCents <= withinNoteTolerance)\n            baseAlpha = 0.018 + 0.035 * static_cast<double>(1.0f - humanize);\n""",
"""        double baseAlpha = distanceCents > 95.0 ? 0.30 : 0.07;\n        const double observedDistanceFromCurrentTarget = state.targetValid\n            ? std::abs(observedLog2 - state.targetLog2) * 1200.0\n            : 0.0;\n        const double currentIdentityRadius = 0.48 * scaleStep;\n        const bool insideCurrentMusicalIdentity = !state.targetValid\n            || observedDistanceFromCurrentTarget < currentIdentityRadius;\n        if (state.noteBodyLatched\n            && insideCurrentMusicalIdentity\n            && distanceCents <= withinNoteTolerance)\n        {\n            baseAlpha = 0.018 + 0.035 * static_cast<double>(1.0f - humanize);\n        }\n""",
'microtonal centre movement')

p.write_text(s)
print('fixed exact unity and microtonal identity crossing')
