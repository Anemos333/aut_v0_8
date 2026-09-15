from pathlib import Path
import runpy

root = Path(__file__).resolve().parents[1]
base_script = root / 'Tests' / 'RefineSolitaryVoiceStructureV3Base.py'
runpy.run_path(str(base_script), run_name='__main__')

cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()

# DIRECT_HIGH_FAMILY_FAST_CONFIRM_V1
# A clean full-rate F0 above the direct half-rate band may be accompanied by
# simultaneous 1/2 and 1/3 aliases on the lower-rate paths. Once decodeCandidate
# has restored the direct high coordinate, this geometry is evidence that the
# old low register was a detector alias, not a new musical permission. Keep a
# short finite confirmation (3 fresh hops) instead of the generic 24-hop octave
# guard. This changes detector observation only; supervisor/quantizer/rendering
# authority remains untouched.
confirm_start = cpp.find(
    'bool ModernPitchEngine::MultiRatePitchTracker::confirmOctaveTransition(')
confirm_end = cpp.find(
    'bool ModernPitchEngine::MultiRatePitchTracker::processSample(', confirm_start)
if confirm_start < 0 or confirm_end < 0:
    raise RuntimeError('direct-high fast confirm: confirm function not found')
confirm = cpp[confirm_start:confirm_end]

normal_marker = '''    // Count only genuinely refreshed evidence.  Reusing an old low-rate
    // candidate over several full-rate hops must not confirm a subharmonic.
'''
normal_start = confirm.find(normal_marker)
if normal_start < 0:
    raise RuntimeError('direct-high fast confirm: normal tracking marker not found')

anchor = '''    const bool multiPathDirect = decision.directSupportCount >= 2
        && decision.consensus >= 0.24f;
    const int requiredObservations = multiPathDirect
        ? (octaveDelta < 0 ? 12 : 10)
        : (octaveDelta < 0 ? 28 : 24);
'''
normal_tail = confirm[normal_start:]
if normal_tail.count(anchor) != 1:
    raise RuntimeError(
        f'direct-high fast confirm: expected one normal guard anchor, found {normal_tail.count(anchor)}')

replacement = '''    const bool multiPathDirect = decision.directSupportCount >= 2
        && decision.consensus >= 0.24f;

    // DIRECT_HIGH_FAMILY_FAST_CONFIRM_V1
    // The full path is the only direct authority above 900 Hz. Half and quarter
    // paths may still verify the same source as exact 1/2 and 1/3 aliases. When
    // all three are simultaneously voice-clean, do not demand the generic
    // octave-jump persistence from a register that was itself the 1/2 alias.
    const auto& directHighFull = fullRateCandidate_.candidate;
    const auto& directHighHalf = halfRateCandidate_.candidate;
    const auto& directHighQuarter = quarterRateCandidate_.candidate;
    const bool directHighFullValid = fullRateCandidate_.ageInHops == 0
        && directHighFull.valid
        && std::isfinite(directHighFull.frequencyHz)
        && directHighFull.frequencyHz > 900.0f
        && directHighFull.harmonicFamily >= 0.68f
        && directHighFull.tonalCleanliness >= 0.68f;
    const bool directHighHalfFamily = halfRateCandidate_.ageInHops <= 3
        && directHighHalf.valid
        && directHighHalf.frequencyHz > 0.0f
        && directHighHalf.harmonicFamily >= 0.68f
        && directHighHalf.tonalCleanliness >= 0.68f
        && centsDistance(2.0f * directHighHalf.frequencyHz,
                         directHighFull.frequencyHz) <= 55.0f;
    const bool directHighQuarterFamily = quarterRateCandidate_.ageInHops <= 5
        && directHighQuarter.valid
        && directHighQuarter.frequencyHz > 0.0f
        && directHighQuarter.harmonicFamily >= 0.68f
        && directHighQuarter.tonalCleanliness >= 0.68f
        && centsDistance(3.0f * directHighQuarter.frequencyHz,
                         directHighFull.frequencyHz) <= 55.0f;
    const bool directHighFamilyFastConfirm = octaveDelta == 1
        && directHighFullValid
        && directHighHalfFamily
        && directHighQuarterFamily
        && centsDistance(decision.candidate.frequencyHz,
                         directHighFull.frequencyHz) <= 55.0f
        && centsDistance(2.0f * trackedPitchHz_,
                         directHighFull.frequencyHz) <= 85.0f;

    const int requiredObservations = directHighFamilyFastConfirm
        ? 3
        : (multiPathDirect
            ? (octaveDelta < 0 ? 12 : 10)
            : (octaveDelta < 0 ? 28 : 24));
'''
normal_tail = normal_tail.replace(anchor, replacement, 1)
confirm = confirm[:normal_start] + normal_tail
cpp = cpp[:confirm_start] + confirm + cpp[confirm_end:]

if 'DIRECT_HIGH_FAMILY_FAST_CONFIRM_V1' not in cpp:
    raise RuntimeError('direct-high fast confirm marker missing')

cpp_path.write_text(cpp)

# Temporary end-to-end telemetry for the only newly exposed frozen invariant.
# This lives in SupervisorContinuityTest only, so the detector-only source guard
# remains exact and no production/audio code is broadened for diagnosis.
test_path = root / 'Tests' / 'SupervisorContinuityTest.cpp'
test = test_path.read_text()
diag_anchor = '    ModernPitchEngine::CorrectionState zeroResponseTransition;\n'
if test.count(diag_anchor) != 1:
    raise RuntimeError(f'hysteresis diagnostic anchor: expected one, found {test.count(diag_anchor)}')

diag = r'''    // HYSTERESIS_F0_END_TO_END_DIAGNOSTIC_V1
    const auto probeHysteresisTarget = [](float lockHysteresis)
    {
        constexpr double probeRate = 48000.0;
        constexpr int probeBlock = 256;
        constexpr int probeSamples = 5 * 48000;
        constexpr int probeStep = static_cast<int>(2.5 * probeRate);
        const double semitoneRatio = std::exp2(1.0 / 12.0);
        const std::array<double, 2> scale { 1.0, semitoneRatio };

        ModernPitchEngine probeEngine;
        probeEngine.prepare(probeRate, probeBlock, 2,
                            ModernPitchEngine::LatencyMode::quality);
        ModernPitchEngine::Parameters p;
        p.amount = 1.0f;
        p.retuneTimeMs = 0.0f;
        p.humanize = 0.0f;
        p.formantPreservation = 0.0f;
        p.detectorSensitivity = 0.85f;
        p.minimumPitchHz = 70.0f;
        p.maximumPitchHz = 1200.0f;
        p.maximumCorrectionSemitones = 12.0f;
        p.stereoMode = ModernPitchEngine::StereoMode::linkedMidSide;
        p.scaleLock = true;
        p.hardLockActive = true;
        p.lockHysteresis = lockHysteresis;

        juce::AudioBuffer<float> block(2, probeBlock);
        double phase = 0.0;
        float previousTarget = 0.0f;
        int targetSwitches = 0;
        for (int produced = 0; produced < probeSamples; produced += probeBlock)
        {
            const int n = std::min(probeBlock, probeSamples - produced);
            block.setSize(2, n, false, false, true);
            float* left = block.getWritePointer(0);
            float* right = block.getWritePointer(1);
            for (int sample = 0; sample < n; ++sample)
            {
                const int absolute = produced + sample;
                const double hz = absolute < probeStep ? 450.0 : 456.0;
                phase += 2.0 * 3.14159265358979323846 * hz / probeRate;
                if (phase >= 2.0 * 3.14159265358979323846)
                    phase -= 2.0 * 3.14159265358979323846;
                left[sample] = static_cast<float>(
                    0.53 * std::sin(phase) + 0.12 * std::sin(2.0 * phase));
                right[sample] = static_cast<float>(
                    0.31 * std::sin(phase + 0.31)
                    + 0.21 * std::sin(2.0 * phase + 0.73));
            }
            probeEngine.process(block, scale.data(), static_cast<int>(scale.size()),
                                440.0, p);
            const auto meter = probeEngine.getMetering();
            const bool targetChanged = meter.targetPitchHz > 0.0f
                && (previousTarget <= 0.0f
                    || std::abs(meter.targetPitchHz - previousTarget) > 5.0f);
            if (targetChanged)
            {
                ++targetSwitches;
                std::cerr << "HYST_DIAG hold=" << lockHysteresis
                          << " sample=" << produced + n
                          << " detected=" << meter.detectedPitchHz
                          << " target=" << meter.targetPitchHz
                          << " state=" << static_cast<int>(meter.state)
                          << " consensus=" << meter.consensus << '\n';
                previousTarget = meter.targetPitchHz;
            }
            if (produced + n >= probeStep - 1024
                && produced + n <= probeStep + 4096
                && ((produced / probeBlock) % 4 == 0))
            {
                std::cerr << "HYST_WINDOW hold=" << lockHysteresis
                          << " sample=" << produced + n
                          << " detected=" << meter.detectedPitchHz
                          << " target=" << meter.targetPitchHz
                          << " state=" << static_cast<int>(meter.state)
                          << " consensus=" << meter.consensus << '\n';
            }
        }
        const auto finalMeter = probeEngine.getMetering();
        std::cerr << "HYST_FINAL hold=" << lockHysteresis
                  << " switches=" << targetSwitches
                  << " detected=" << finalMeter.detectedPitchHz
                  << " target=" << finalMeter.targetPitchHz
                  << " state=" << static_cast<int>(finalMeter.state)
                  << " consensus=" << finalMeter.consensus << '\n';
    };
    probeHysteresisTarget(0.0f);
    probeHysteresisTarget(80.0f);

'''
test = test.replace(diag_anchor, diag + diag_anchor, 1)
test_path.write_text(test)
print('DIRECT_HIGH_FAMILY_FAST_CONFIRM_V1 + hysteresis diagnostics materialized')
