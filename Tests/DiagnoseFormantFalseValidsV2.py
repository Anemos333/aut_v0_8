from pathlib import Path

p = Path(__file__).resolve().parents[1] / 'Tests' / 'SupervisorContinuityTest.cpp'
t = p.read_text()


def one(old: str, new: str, label: str) -> None:
    global t
    count = t.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    t = t.replace(old, new, 1)


# Print the rare formant-noise coordinates. This is test-only telemetry; no DSP
# source or public detector state is changed. Provisional coordinates matter too:
# the remaining failure is measurementAvailable without valid/audio authority.
one(
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
'''        if (formantNoiseTracker->processSample(shaped, observed))
        {
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
            if (observed.measurementAvailable)
            {
                ++formantNoiseMeasurements;
                const auto full = dump(formantNoiseTracker->fullRateCandidate_);
                const auto half = dump(formantNoiseTracker->halfRateCandidate_);
                const auto quarter = dump(formantNoiseTracker->quarterRateCandidate_);
                const auto eighth = dump(formantNoiseTracker->eighthRateCandidate_);
                std::cerr << "FORMANT_COORD sample=" << sample
                          << " valid=" << (observed.valid ? 1 : 0)
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
            if (observed.valid)
                ++formantNoiseValid;
        }
''',
'formant coordinate telemetry')

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

# The 1100-Hz positive control currently yields many valid observations in the
# wrong place. Measure the actual rational family selected before deciding
# whether the detector or the synthetic control is at fault.
one(
'''    int highVoiceValid = 0;
    int highVoiceNear = 0;
    constexpr float highVoiceHz = 1100.0f;
''',
'''    int highVoiceValid = 0;
    int highVoiceNear = 0;
    int highVoiceNearTwoThirds = 0;
    int highVoiceNearThreeQuarters = 0;
    int highVoiceNearFourThirds = 0;
    int highVoiceTraceCount = 0;
    double highVoiceSumHz = 0.0;
    float highVoiceMinHz = 100000.0f;
    float highVoiceMaxHz = 0.0f;
    constexpr float highVoiceHz = 1100.0f;
''',
'high voice geometry counters')

one(
'''            ++highVoiceValid;
            const float cents = std::abs(1200.0f * std::log2(
                std::max(1.0f, observed.correctionFrequencyHz) / highVoiceHz));
            if (cents < 45.0f)
                ++highVoiceNear;
''',
'''            ++highVoiceValid;
            const float outHz = observed.correctionFrequencyHz;
            highVoiceSumHz += outHz;
            highVoiceMinHz = std::min(highVoiceMinHz, outHz);
            highVoiceMaxHz = std::max(highVoiceMaxHz, outHz);
            const auto nearHz = [outHz](float referenceHz)
            {
                return std::abs(1200.0f * std::log2(
                    std::max(1.0f, outHz) / referenceHz)) < 45.0f;
            };
            if (nearHz(highVoiceHz))
                ++highVoiceNear;
            if (nearHz(highVoiceHz * (2.0f / 3.0f)))
                ++highVoiceNearTwoThirds;
            if (nearHz(highVoiceHz * 0.75f))
                ++highVoiceNearThreeQuarters;
            if (nearHz(highVoiceHz * (4.0f / 3.0f)))
                ++highVoiceNearFourThirds;

            if (highVoiceTraceCount < 8)
            {
                ++highVoiceTraceCount;
                const auto& f = highVoiceTracker->fullRateCandidate_.candidate;
                const auto& h = highVoiceTracker->halfRateCandidate_.candidate;
                const auto& q = highVoiceTracker->quarterRateCandidate_.candidate;
                const auto& e = highVoiceTracker->eighthRateCandidate_.candidate;
                std::cerr << "HIGH1100_GEOMETRY sample=" << sample
                          << " out_hz=" << outHz
                          << " support=" << observed.detectorSupport
                          << " consensus=" << observed.consensus
                          << " full=" << f.frequencyHz << ',' << f.confidence << ','
                          << f.periodicity << ',' << f.harmonicFamily << ','
                          << f.tonalCleanliness << ',' << (f.valid ? 1 : 0)
                          << " half=" << h.frequencyHz << ',' << h.confidence << ','
                          << h.periodicity << ',' << h.harmonicFamily << ','
                          << h.tonalCleanliness << ',' << (h.valid ? 1 : 0)
                          << " quarter=" << q.frequencyHz << ',' << q.confidence << ','
                          << q.periodicity << ',' << q.harmonicFamily << ','
                          << q.tonalCleanliness << ',' << (q.valid ? 1 : 0)
                          << " eighth=" << e.frequencyHz << ',' << e.confidence << ','
                          << e.periodicity << ',' << e.harmonicFamily << ','
                          << e.tonalCleanliness << ',' << (e.valid ? 1 : 0)
                          << '\\n';
            }
''',
'high voice geometry accumulation')

one(
'''    std::cerr << "solitary_high_voice_valid=" << highVoiceValid
              << " near_1100=" << highVoiceNear << '\\n';
''',
'''    std::cerr << "solitary_high_voice_valid=" << highVoiceValid
              << " near_1100=" << highVoiceNear
              << " near_733=" << highVoiceNearTwoThirds
              << " near_825=" << highVoiceNearThreeQuarters
              << " near_1467=" << highVoiceNearFourThirds
              << " mean_hz=" << (highVoiceValid > 0
                    ? highVoiceSumHz / static_cast<double>(highVoiceValid) : 0.0)
              << " minmax_hz=" << (highVoiceValid > 0 ? highVoiceMinHz : 0.0f)
              << ',' << highVoiceMaxHz << '\\n';
''',
'high voice geometry summary')

p.write_text(t)
print('FORMANT_FALSE_VALID_DIAGNOSTICS_V2 materialized')
