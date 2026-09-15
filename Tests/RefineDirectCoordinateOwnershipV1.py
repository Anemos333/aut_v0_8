from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()


def one(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(old, new, 1)


# DIRECT_COORDINATE_OWNS_F0_V1
# Octave-shifted lower-rate paths are useful evidence that a harmonic family is
# coherent, but their transposed estimate must not pull the numeric F0 when a
# detector path measures that same hypothesis directly. Keep a separate direct
# coordinate ledger; fall back to the existing all-support coordinate only when
# no direct path exists. This is detector analysis only.
cpp = one(cpp,
'''        double weightedLogFrequency = 0.0;
        float coordinateWeightSum = 0.0f;
        float evidenceWeightSum = 0.0f;
''',
'''        double weightedLogFrequency = 0.0;
        float coordinateWeightSum = 0.0f;
        // DIRECT_COORDINATE_OWNS_F0_V1: octave-transposed support verifies a
        // family but cannot steer a coordinate that is measured directly.
        double directWeightedLogFrequency = 0.0;
        float directCoordinateWeightSum = 0.0f;
        float evidenceWeightSum = 0.0f;
''',
'direct coordinate ledger state')

cpp = one(cpp,
'''            weightedLogFrequency += static_cast<double>(coordinateWeight)
                                  * safeLog2(static_cast<double>(bestFrequency));
            coordinateWeightSum += coordinateWeight;
            evidenceWeightSum += evidenceWeight;
''',
'''            weightedLogFrequency += static_cast<double>(coordinateWeight)
                                  * safeLog2(static_cast<double>(bestFrequency));
            coordinateWeightSum += coordinateWeight;
            if (direct)
            {
                directWeightedLogFrequency += static_cast<double>(coordinateWeight)
                    * safeLog2(static_cast<double>(bestFrequency));
                directCoordinateWeightSum += coordinateWeight;
            }
            evidenceWeightSum += evidenceWeight;
''',
'direct coordinate accumulation')

cpp = one(cpp,
'''        ConsensusHypothesis hypothesis;
        hypothesis.frequencyHz = static_cast<float>(std::exp2(
            weightedLogFrequency / static_cast<double>(coordinateWeightSum)));
''',
'''        ConsensusHypothesis hypothesis;
        const bool hasDirectCoordinate = directCoordinateWeightSum > 1.0e-6f;
        const double coordinateLogFrequency = hasDirectCoordinate
            ? directWeightedLogFrequency / static_cast<double>(directCoordinateWeightSum)
            : weightedLogFrequency / static_cast<double>(coordinateWeightSum);
        hypothesis.frequencyHz = static_cast<float>(std::exp2(coordinateLogFrequency));
''',
'direct coordinate publish')

if 'DIRECT_COORDINATE_OWNS_F0_V1' not in cpp:
    raise RuntimeError('direct coordinate marker missing')

cpp_path.write_text(cpp)
print('DIRECT_COORDINATE_OWNS_F0_V1 materialized')
