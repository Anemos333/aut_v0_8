from pathlib import Path

p = Path('Tests/MaterializeVoiceTransitionContinuumV1.py')
text = p.read_text()
old = '''cpp = one(cpp,
\'''        observation.periodicity = trackedPeriodicity_;\\n        observation.consensus = trackedConsensus_;\\n\''',
\'''        observation.periodicity = provisionalAvailable\\n            ? provisionalMeasurement.periodicity : trackedPeriodicity_;\\n        observation.consensus = trackedConsensus_;\\n\''',
'publish provisional periodicity')
'''
new = '''cpp = one(cpp,
\'''        observation.confidence = provisionalAvailable\\n            ? provisionalMeasurement.confidence : trackedConfidence_;\\n        observation.periodicity = trackedPeriodicity_;\\n        observation.consensus = trackedConsensus_;\\n\''',
\'''        observation.confidence = provisionalAvailable\\n            ? provisionalMeasurement.confidence : trackedConfidence_;\\n        observation.periodicity = provisionalAvailable\\n            ? provisionalMeasurement.periodicity : trackedPeriodicity_;\\n        observation.consensus = trackedConsensus_;\\n\''',
'publish provisional periodicity')
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f'expected materializer block once, got {count}')
p.write_text(text.replace(old, new, 1))
print('voice transition materializer anchor refined')
