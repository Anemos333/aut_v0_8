from pathlib import Path

p = Path('Tests/RefineVoiceTransitionContinuumV1.py')
text = p.read_text()
old = r'''cpp = one(cpp,
''' + "'''" + r'''    if (state.trackingState == TrackingState::transition
        && state.noteBodyLatched
        && state.stateAgeSamples >= maximumTransitionSamples)
    {
        state.trackingState = TrackingState::stable;
        state.stateAgeSamples = 0;
    }
''' + "'''" + r''',
''' + "'''" + r'''    if (state.trackingState == TrackingState::transition
        && state.noteBodyLatched
        && state.stateAgeSamples >= maximumTransitionSamples)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
        state.trackingState = TrackingState::stable;
        state.stateAgeSamples = 0;
    }
''' + "'''" + r''',
'transition hard bound lands without momentum')'''
new = r'''cpp = one(cpp,
''' + "'''" + r'''    if (state.trackingState == TrackingState::transition
        && state.noteBodyLatched
        && state.stateAgeSamples >= maximumTransitionSamples)
    {
        state.trackingState = TrackingState::stable;
        state.stateAgeSamples = 0;
        state.velocityCentsPerSecond = 0.0;
    }
''' + "'''" + r''',
''' + "'''" + r'''    if (state.trackingState == TrackingState::transition
        && state.noteBodyLatched
        && state.stateAgeSamples >= maximumTransitionSamples)
    {
        state.currentCents = state.desiredCents;
        state.velocityCentsPerSecond = 0.0;
        state.trackingState = TrackingState::stable;
        state.stateAgeSamples = 0;
    }
''' + "'''" + r''',
'transition hard bound lands without momentum')'''
if text.count(old) != 1:
    raise SystemExit(f'hard-bound refiner block count={text.count(old)}')
p.write_text(text.replace(old, new, 1))
print('voice transition hard-bound anchor aligned')
