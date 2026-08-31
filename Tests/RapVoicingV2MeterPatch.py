from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
h_path = Path('Source/ModernPitchEngine.h')
test_path = Path('Tests/SupervisorContinuityTest.cpp')

cpp = cpp_path.read_text(encoding='utf-8')
h = h_path.read_text(encoding='utf-8')
test = test_path.read_text(encoding='utf-8')

MARKER = 'meterConsensus_'
TEST_MARKER = 'meter_consensus_is_real_detector_consensus'

if MARKER not in h:
    old = '''    std::atomic<float> meterVoicing_ { 0.0f };
    std::atomic<float> meterPeriodicity_ { 0.0f };
    std::atomic<float> meterCorrectionCents_ { 0.0f };
'''
    new = '''    std::atomic<float> meterVoicing_ { 0.0f };
    std::atomic<float> meterPeriodicity_ { 0.0f };
    std::atomic<float> meterConsensus_ { 0.0f };
    std::atomic<float> meterCorrectionCents_ { 0.0f };
'''
    if old not in h:
        raise RuntimeError('meter atomics block not found')
    h = h.replace(old, new, 1)
    h_path.write_text(h, encoding='utf-8')

if 'meterConsensus_.store(0.0f' not in cpp:
    old = '''    meterVoicing_.store(0.0f, std::memory_order_relaxed);
    meterPeriodicity_.store(0.0f, std::memory_order_relaxed);
    meterCorrectionCents_.store(0.0f, std::memory_order_relaxed);
'''
    new = '''    meterVoicing_.store(0.0f, std::memory_order_relaxed);
    meterPeriodicity_.store(0.0f, std::memory_order_relaxed);
    meterConsensus_.store(0.0f, std::memory_order_relaxed);
    meterCorrectionCents_.store(0.0f, std::memory_order_relaxed);
'''
    if old not in cpp:
        raise RuntimeError('meter reset block not found')
    cpp = cpp.replace(old, new, 1)

if 'meterConsensus_.store(observation.consensus' not in cpp:
    old = '''    meterVoicing_.store(observation.voicing, std::memory_order_relaxed);
    meterPeriodicity_.store(observation.periodicity, std::memory_order_relaxed);
    meterCorrectionCents_.store(static_cast<float>(audibleCents),
'''
    new = '''    meterVoicing_.store(observation.voicing, std::memory_order_relaxed);
    meterPeriodicity_.store(observation.periodicity, std::memory_order_relaxed);
    meterConsensus_.store(observation.consensus, std::memory_order_relaxed);
    meterCorrectionCents_.store(static_cast<float>(audibleCents),
'''
    if old not in cpp:
        raise RuntimeError('meter publish block not found')
    cpp = cpp.replace(old, new, 1)

old = '''        result.consensus = result.harmonicity;
'''
new = '''        result.consensus = meterConsensus_.load(std::memory_order_relaxed);
'''
if old in cpp:
    cpp = cpp.replace(old, new, 1)
elif 'result.consensus = meterConsensus_.load' not in cpp:
    raise RuntimeError('meter consensus getter not found')

cpp_path.write_text(cpp, encoding='utf-8')

if TEST_MARKER not in test:
    anchor = '''    success &= check(zeroConsensusState.targetValid
                     && std::abs(zeroConsensusState.desiredCents) > 5.0,
                     "zero_consensus_valid_f0_drives_real_correction");

'''
    insertion = anchor + r'''    ModernPitchEngine::PitchObservation meterConsensusObservation;
    meterConsensusObservation.valid = true;
    meterConsensusObservation.audioPresent = true;
    meterConsensusObservation.frequencyHz = 452.0f;
    meterConsensusObservation.confidence = 0.12f;
    meterConsensusObservation.periodicity = 0.73f;
    meterConsensusObservation.voicing = 1.0f;
    meterConsensusObservation.consensus = 0.0f;
    ModernPitchEngine::CorrectionState meterConsensusState;
    meterConsensusState.targetValid = true;
    meterConsensusState.targetLog2 = std::log2(440.0);
    CreativeTempo::Metering meterTempo;
    engine->publishMetering(meterConsensusObservation, meterConsensusState,
                            -37.0, meterTempo);
    const auto zeroConsensusMeter = engine->getMetering();
    success &= check(std::abs(zeroConsensusMeter.consensus) < 1.0e-7f
                     && zeroConsensusMeter.harmonicity > 0.70f,
                     "meter_consensus_is_real_detector_consensus");
    success &= check(std::abs(zeroConsensusMeter.correctionCents + 37.0f) < 0.01f,
                     "zero_consensus_meter_can_report_active_correction");

'''
    if anchor not in test:
        raise RuntimeError('zero-consensus correction test anchor not found')
    test = test.replace(anchor, insertion, 1)
    test_path.write_text(test, encoding='utf-8')

print('RapVoicingV2 real consensus meter patch applied/verified')
