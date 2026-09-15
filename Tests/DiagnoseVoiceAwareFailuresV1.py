from pathlib import Path

p = Path(__file__).resolve().parents[1] / 'Tests' / 'SupervisorContinuityTest.cpp'
t = p.read_text()

def one(old, new, label):
    global t
    c = t.count(old)
    if c != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {c}')
    t = t.replace(old, new, 1)

one(
'''    int recoveredAtSample = -1;
    for (int sample = 0; sample < 12000; ++sample)
''',
'''    int recoveredAtSample = -1;
    int staleNear110 = 0;
    int staleNear220 = 0;
    int staleOther = 0;
    for (int sample = 0; sample < 12000; ++sample)
''',
'stale counters')

one(
'''        if (staleAnchorTracker->processSample(voice, observed) && observed.valid)
        {
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / 220.0f));
            if (cents < 45.0f)
            {
                recoveredAtSample = sample;
                break;
            }
        }
    }
    success &= check(recoveredAtSample >= 0 && recoveredAtSample < 6000,
''',
'''        if (staleAnchorTracker->processSample(voice, observed) && observed.valid)
        {
            const float hz = std::max(1.0f, observed.correctionFrequencyHz);
            const float cents220 = std::abs(1200.0f * std::log2(hz / 220.0f));
            const float cents110 = std::abs(1200.0f * std::log2(hz / 110.0f));
            if (cents220 < 45.0f)
                ++staleNear220;
            else if (cents110 < 45.0f)
                ++staleNear110;
            else
                ++staleOther;
            if (cents220 < 45.0f)
            {
                recoveredAtSample = sample;
                break;
            }
        }
    }
    const auto staleDumpSlot = [](const auto& slot)
    {
        const auto& c = slot.candidate;
        return std::array<float, 5> { c.frequencyHz, c.confidence, c.periodicity,
                                     c.harmonicFamily, c.tonalCleanliness };
    };
    const auto staleFull = staleDumpSlot(staleAnchorTracker->fullRateCandidate_);
    const auto staleHalf = staleDumpSlot(staleAnchorTracker->halfRateCandidate_);
    const auto staleQuarter = staleDumpSlot(staleAnchorTracker->quarterRateCandidate_);
    const auto staleEighth = staleDumpSlot(staleAnchorTracker->eighthRateCandidate_);
    std::cerr << "stale_recovery_at=" << recoveredAtSample
              << " near110=" << staleNear110
              << " near220=" << staleNear220
              << " other=" << staleOther
              << " tracked=" << staleAnchorTracker->trackedPitchHz_
              << " pending_count=" << staleAnchorTracker->pendingOctaveCount_
              << " pending_hz=" << staleAnchorTracker->pendingOctaveFrequencyHz_
              << " full=" << staleFull[0] << ',' << staleFull[3] << ',' << staleFull[4]
              << " half=" << staleHalf[0] << ',' << staleHalf[3] << ',' << staleHalf[4]
              << " quarter=" << staleQuarter[0] << ',' << staleQuarter[3] << ',' << staleQuarter[4]
              << " eighth=" << staleEighth[0] << ',' << staleEighth[3] << ',' << staleEighth[4]
              << '\\n';
    success &= check(recoveredAtSample >= 0 && recoveredAtSample < 6000,
''',
'stale telemetry')

# Path-level telemetry at the end of the formant-noise test.
one(
'''    std::cerr << "voice_aware_formant_noise_valid=" << formantNoiseValid
              << " provisional=" << formantNoiseMeasurements << '\\n';
''',
'''    const auto formantDumpSlot = [](const auto& slot)
    {
        const auto& c = slot.candidate;
        return std::array<float, 5> { c.frequencyHz, c.confidence, c.periodicity,
                                     c.harmonicFamily, c.tonalCleanliness };
    };
    const auto formantFull = formantDumpSlot(formantNoiseTracker->fullRateCandidate_);
    const auto formantHalf = formantDumpSlot(formantNoiseTracker->halfRateCandidate_);
    const auto formantQuarter = formantDumpSlot(formantNoiseTracker->quarterRateCandidate_);
    const auto formantEighth = formantDumpSlot(formantNoiseTracker->eighthRateCandidate_);
    std::cerr << "voice_aware_formant_noise_valid=" << formantNoiseValid
              << " provisional=" << formantNoiseMeasurements
              << " full=" << formantFull[0] << ',' << formantFull[3] << ',' << formantFull[4]
              << " half=" << formantHalf[0] << ',' << formantHalf[3] << ',' << formantHalf[4]
              << " quarter=" << formantQuarter[0] << ',' << formantQuarter[3] << ',' << formantQuarter[4]
              << " eighth=" << formantEighth[0] << ',' << formantEighth[3] << ',' << formantEighth[4]
              << '\\n';
''',
'formant telemetry')

p.write_text(t)

# Keep the boundary-bias probe isolated in its own diagnostic materializer while
# reusing this already-scheduled workflow stage. It modifies tests only.
boundary_script = Path(__file__).resolve().parent / 'DiagnoseBoundaryPathBiasV1.py'
namespace = {'__file__': str(boundary_script), '__name__': '__main__'}
exec(compile(boundary_script.read_text(), str(boundary_script), 'exec'), namespace)

print('VOICE_AWARE_FAILURE_DIAGNOSTICS_V1 materialized')
