from pathlib import Path

p = Path(__file__).resolve().parents[1] / 'Tests' / 'SupervisorContinuityTest.cpp'
s = p.read_text()

anchor = '''    // OBSERVATION_MEMORY_IS_FALSIFIABLE_V1: emulate a stale wrong register\n'''
if s.count(anchor) != 1:
    raise RuntimeError(f'rapid-f0 probe anchor: expected one, found {s.count(anchor)}')

probe = r'''    // RAPID_F0_OBSERVATION_MUST_PRECEDE_HOLD_V1
    // Hold can only act after the detector has actually observed the new physical
    // coordinate. Measure detector latency directly, before any quantizer logic.
    struct RapidF0Metrics
    {
        int firstInitialLockSample = -1;
        int firstNewLockSample = -1;
        float lastInitialHz = 0.0f;
        float firstNewHz = 0.0f;
    };

    const auto measureRapidStep = [](float initialHz, float newHz,
                                     int stepSample, int totalSamples)
    {
        RapidF0Metrics metrics;
        auto tracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
        tracker->prepare(48000.0);
        tracker->setRange(70.0f, 900.0f);
        double phase = 0.0;
        for (int sample = 0; sample < totalSamples; ++sample)
        {
            const float sourceHz = sample < stepSample ? initialHz : newHz;
            phase += 2.0 * 3.14159265358979323846
                   * static_cast<double>(sourceHz) / 48000.0;
            if (phase > 2.0 * 3.14159265358979323846)
                phase -= 2.0 * 3.14159265358979323846;

            ModernPitchEngine::PitchObservation observed;
            if (!tracker->processSample(0.08f * static_cast<float>(std::sin(phase)), observed))
                continue;

            const auto near = [](float measured, float expected, float toleranceCents)
            {
                return measured > 0.0f && expected > 0.0f
                    && std::abs(1200.0f * std::log2(measured / expected)) < toleranceCents;
            };

            if (sample < stepSample && observed.valid
                && near(observed.correctionFrequencyHz, initialHz, 18.0f))
            {
                if (metrics.firstInitialLockSample < 0)
                    metrics.firstInitialLockSample = sample;
                metrics.lastInitialHz = observed.correctionFrequencyHz;
            }
            else if (sample >= stepSample && observed.valid
                     && near(observed.correctionFrequencyHz, newHz, 18.0f))
            {
                if (metrics.firstNewLockSample < 0)
                {
                    metrics.firstNewLockSample = sample;
                    metrics.firstNewHz = observed.correctionFrequencyHz;
                }
            }
        }
        return metrics;
    };

    const auto boundaryRapid = measureRapidStep(450.0f, 456.0f, 12000, 18000);
    const auto noteRapid = measureRapidStep(220.0f, 246.94165f, 12000, 18000);
    const int boundaryInitialLatency = boundaryRapid.firstInitialLockSample;
    const int boundaryStepLatency = boundaryRapid.firstNewLockSample >= 0
        ? boundaryRapid.firstNewLockSample - 12000 : 999999;
    const int noteInitialLatency = noteRapid.firstInitialLockSample;
    const int noteStepLatency = noteRapid.firstNewLockSample >= 0
        ? noteRapid.firstNewLockSample - 12000 : 999999;

    std::cerr << "voice_aware_rapid_boundary_first_lock_samples="
              << boundaryInitialLatency
              << " step_recognition_samples=" << boundaryStepLatency
              << " initial_hz=" << boundaryRapid.lastInitialHz
              << " new_hz=" << boundaryRapid.firstNewHz << '\\n';
    std::cerr << "voice_aware_rapid_note_first_lock_samples="
              << noteInitialLatency
              << " step_recognition_samples=" << noteStepLatency
              << " initial_hz=" << noteRapid.lastInitialHz
              << " new_hz=" << noteRapid.firstNewHz << '\\n';

    success &= check(boundaryInitialLatency >= 0 && boundaryInitialLatency <= 3000,
                     "boundary_f0_acquires_before_hold_needs_it");
    success &= check(boundaryStepLatency <= 1536,
                     "small_boundary_f0_change_is_observed_without_prudence_stall");
    success &= check(noteInitialLatency >= 0 && noteInitialLatency <= 3000,
                     "sung_note_f0_acquires_promptly");
    success &= check(noteStepLatency <= 1024,
                     "rapid_real_note_change_is_observed_before_musical_hold");

'''

s = s.replace(anchor, probe + anchor, 1)
p.write_text(s)
print('RAPID_F0_OBSERVATION_MUST_PRECEDE_HOLD_V1 diagnostics materialized')
