from pathlib import Path

p = Path(__file__).resolve().parents[1] / 'Tests' / 'SupervisorContinuityTest.cpp'
t = p.read_text()


def one(old: str, new: str, label: str) -> None:
    global t
    count = t.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    t = t.replace(old, new, 1)


# Print only the rare false-valid frames.  This is test-only telemetry; no DSP
# source or public detector state is changed.
one(
'''        if (formantNoiseTracker->processSample(shaped, observed))
        {
            if (observed.measurementAvailable)
                ++formantNoiseMeasurements;
            if (observed.valid)
                ++formantNoiseValid;
        }
''',
'''        if (formantNoiseTracker->processSample(shaped, observed))
        {
            if (observed.measurementAvailable)
                ++formantNoiseMeasurements;
            if (observed.valid)
            {
                ++formantNoiseValid;
                const auto dump = [](const auto& slot)
                {
                    const auto& c = slot.candidate;
                    return std::array<float, 7> {
                        c.frequencyHz,
                        c.confidence,
                        c.periodicity,
                        c.harmonicFamily,
                        c.tonalCleanliness,
                        static_cast<float>(slot.ageInHops),
                        c.valid ? 1.0f : 0.0f
                    };
                };
                const auto full = dump(formantNoiseTracker->fullRateCandidate_);
                const auto half = dump(formantNoiseTracker->halfRateCandidate_);
                const auto quarter = dump(formantNoiseTracker->quarterRateCandidate_);
                const auto eighth = dump(formantNoiseTracker->eighthRateCandidate_);
                std::cerr << "FORMANT_FALSE_VALID sample=" << sample
                          << " out_hz=" << observed.correctionFrequencyHz
                          << " support=" << observed.detectorSupport
                          << " consensus=" << observed.consensus
                          << " confidence=" << observed.confidence
                          << " periodicity=" << observed.periodicity
                          << " tracked=" << formantNoiseTracker->trackedPitchHz_
                          << " invalid_hops=" << formantNoiseTracker->invalidHopCount_
                          << " full=" << full[0] << ',' << full[1] << ',' << full[2]
                          << ',' << full[3] << ',' << full[4] << ',' << full[5] << ',' << full[6]
                          << " half=" << half[0] << ',' << half[1] << ',' << half[2]
                          << ',' << half[3] << ',' << half[4] << ',' << half[5] << ',' << half[6]
                          << " quarter=" << quarter[0] << ',' << quarter[1] << ',' << quarter[2]
                          << ',' << quarter[3] << ',' << quarter[4] << ',' << quarter[5] << ',' << quarter[6]
                          << " eighth=" << eighth[0] << ',' << eighth[1] << ',' << eighth[2]
                          << ',' << eighth[3] << ',' << eighth[4] << ',' << eighth[5] << ',' << eighth[6]
                          << '\\n';
            }
        }
''',
'false-valid frame telemetry')

# Establish the real low-register cleanliness margin on exactly the same
# materialized build, so any later veto can be chosen from measured separation.
one(
'''    int lowVoiceValid = 0;
    int lowVoiceNearFundamental = 0;
    for (int sample = 0; sample < 36000; ++sample)
''',
'''    int lowVoiceValid = 0;
    int lowVoiceNearFundamental = 0;
    float lowVoiceQuarterMinClean = 2.0f;
    float lowVoiceQuarterMaxClean = -1.0f;
    float lowVoiceEighthMinClean = 2.0f;
    float lowVoiceEighthMaxClean = -1.0f;
    for (int sample = 0; sample < 36000; ++sample)
''',
'low voice cleanliness state')

one(
'''        if (lowVoiceTracker->processSample(voice, observed)
            && sample > 12000 && observed.valid)
        {
            ++lowVoiceValid;
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / lowVoiceHz));
            if (cents < 45.0f)
                ++lowVoiceNearFundamental;
        }
    }
    std::cerr << "low_rate_voice_valid=" << lowVoiceValid
              << " near_60=" << lowVoiceNearFundamental << '\\n';
''',
'''        if (lowVoiceTracker->processSample(voice, observed)
            && sample > 12000 && observed.valid)
        {
            ++lowVoiceValid;
            const auto& q = lowVoiceTracker->quarterRateCandidate_.candidate;
            const auto& e = lowVoiceTracker->eighthRateCandidate_.candidate;
            if (q.valid && q.tonalCleanliness >= 0.0f)
            {
                lowVoiceQuarterMinClean = std::min(lowVoiceQuarterMinClean, q.tonalCleanliness);
                lowVoiceQuarterMaxClean = std::max(lowVoiceQuarterMaxClean, q.tonalCleanliness);
            }
            if (e.valid && e.tonalCleanliness >= 0.0f)
            {
                lowVoiceEighthMinClean = std::min(lowVoiceEighthMinClean, e.tonalCleanliness);
                lowVoiceEighthMaxClean = std::max(lowVoiceEighthMaxClean, e.tonalCleanliness);
            }
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / lowVoiceHz));
            if (cents < 45.0f)
                ++lowVoiceNearFundamental;
        }
    }
    std::cerr << "low_rate_voice_valid=" << lowVoiceValid
              << " near_60=" << lowVoiceNearFundamental
              << " quarter_clean_minmax=" << lowVoiceQuarterMinClean << ',' << lowVoiceQuarterMaxClean
              << " eighth_clean_minmax=" << lowVoiceEighthMinClean << ',' << lowVoiceEighthMaxClean
              << '\\n';
''',
'low voice cleanliness telemetry')

p.write_text(t)
print('FORMANT_FALSE_VALID_DIAGNOSTICS_V2 materialized')
