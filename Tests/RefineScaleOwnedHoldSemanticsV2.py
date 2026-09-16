from pathlib import Path

# V2 keeps the literal-cent Hold contract from V1, but restores the crucial
# distinction between a scale-boundary excursion and a musically qualified new
# note. Hold may suppress fast/deep promotion while the voice remains inside the
# user-selected radius; it must never manufacture a new-note candidate by itself.
script_path = Path(__file__).resolve().parent / 'RefineScaleOwnedHoldSemanticsV1.py'
source = script_path.read_text()

old = """new_deep = '''            const bool outsideUserHold = observedDistanceFromOwnedTarget
                > static_cast<double>(holdRadiusCents) + 1.0e-6;
            const bool deepCandidate = octaveLikeTarget || outsideUserHold;
'''
"""
new = """new_deep = '''            // HOLD_DOES_NOT_CREATE_NOTE_IDENTITY_V2: crossing the Hold radius
            // is not, by itself, evidence of a new musical note. Preserve the
            // existing scale-relative deep geometry so vibrato and continuous
            // trajectories cannot chatter between degrees. A wider Hold may
            // suppress this fast/deep promotion, while the independent
            // same-side persistence path below remains free to qualify a real
            // sustained/legato note and then bypass Hold.
            const bool outsideUserHold = observedDistanceFromOwnedTarget
                > static_cast<double>(holdRadiusCents) + 1.0e-6;
            const bool deepCandidate = octaveLikeTarget
                || (outsideUserHold
                    && observedDistanceFromOwnedTarget >= deepExitRatio * scaleStep);
'''
"""
if source.count(old) != 1:
    raise RuntimeError(
        f'Hold V2 deep-candidate anchor: expected one, found {source.count(old)}')
source = source.replace(old, new, 1)

namespace = {'__file__': str(script_path), '__name__': '__main__'}
exec(compile(source, str(script_path), 'exec'), namespace)
