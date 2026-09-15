from pathlib import Path

root = Path(__file__).resolve().parents[1]
test_path = root / 'Tests' / 'SupervisorContinuityTest.cpp'
test = test_path.read_text()

anchor = '''    ModernPitchEngine::CorrectionState zeroResponseTransition;\n'''
if test.count(anchor) != 1:
    raise RuntimeError(f'boundary path bias anchor: expected one, found {test.count(anchor)}')

diag = r'''    // BOUNDARY_PATH_BIAS_DIAGNOSTIC_V1
    // Reproduce exactly the stronger linked input channel used by the frozen
    // hysteresis regression: 450 Hz -> 456 Hz, with the same two harmonics.
    // Measure only fresh path candidates so slower multirate paths are not
    // over-counted through age reuse. Diagnostic only; no production behavior.
    struct BoundaryPathStats
    {
        double sumHz = 0.0;
        float minimumHz = 1.0e9f;
        float maximumHz = 0.0f;
        int freshValid = 0;
        int near456 = 0;
        int aboveHoldBoundary = 0;
    };

    auto boundaryPathTracker = std::make_unique<ModernPitchEngine::MultiRatePitchTracker>();
    boundaryPathTracker->prepare(48000.0);
    boundaryPathTracker->setRange(70.0f, 1200.0f);
    boundaryPathTracker->setSensitivity(0.85f);

    BoundaryPathStats boundaryFull;
    BoundaryPathStats boundaryHalf;
    BoundaryPathStats boundaryQuarter;
    BoundaryPathStats boundaryEighth;
    BoundaryPathStats boundaryOutput;
    const float holdBoundaryHz = static_cast<float>(440.0 * std::exp2(65.0 / 1200.0));
    const auto addBoundaryPath = [holdBoundaryHz](BoundaryPathStats& stats,
                                                   const auto& slot) noexcept
    {
        const auto& candidate = slot.candidate;
        if (slot.ageInHops != 0 || !candidate.valid
            || !std::isfinite(candidate.frequencyHz) || candidate.frequencyHz <= 0.0f)
        {
            return;
        }
        ++stats.freshValid;
        const float cents456 = std::abs(1200.0f * std::log2(candidate.frequencyHz / 456.0f));
        if (cents456 > 80.0f)
            return;
        ++stats.near456;
        stats.sumHz += candidate.frequencyHz;
        stats.minimumHz = std::min(stats.minimumHz, candidate.frequencyHz);
        stats.maximumHz = std::max(stats.maximumHz, candidate.frequencyHz);
        if (candidate.frequencyHz > holdBoundaryHz)
            ++stats.aboveHoldBoundary;
    };
    const auto addBoundaryOutput = [holdBoundaryHz](BoundaryPathStats& stats,
                                                     const ModernPitchEngine::PitchObservation& observed) noexcept
    {
        if (!observed.valid || !std::isfinite(observed.correctionFrequencyHz)
            || observed.correctionFrequencyHz <= 0.0f)
        {
            return;
        }
        ++stats.freshValid;
        const float cents456 = std::abs(1200.0f * std::log2(
            observed.correctionFrequencyHz / 456.0f));
        if (cents456 > 80.0f)
            return;
        ++stats.near456;
        stats.sumHz += observed.correctionFrequencyHz;
        stats.minimumHz = std::min(stats.minimumHz, observed.correctionFrequencyHz);
        stats.maximumHz = std::max(stats.maximumHz, observed.correctionFrequencyHz);
        if (observed.correctionFrequencyHz > holdBoundaryHz)
            ++stats.aboveHoldBoundary;
    };

    constexpr int boundaryWarmSamples = 24000;
    constexpr int boundaryMeasureSamples = 36000;
    double boundaryPhase = 0.0;
    for (int sample = 0; sample < boundaryWarmSamples + boundaryMeasureSamples; ++sample)
    {
        const double frequencyHz = sample < boundaryWarmSamples ? 450.0 : 456.0;
        boundaryPhase += 2.0 * 3.14159265358979323846 * frequencyHz / 48000.0;
        if (boundaryPhase >= 2.0 * 3.14159265358979323846)
            boundaryPhase -= 2.0 * 3.14159265358979323846;
        const float voice = static_cast<float>(
            0.53 * std::sin(boundaryPhase) + 0.12 * std::sin(2.0 * boundaryPhase));
        ModernPitchEngine::PitchObservation observed;
        if (!boundaryPathTracker->processSample(voice, observed)
            || sample < boundaryWarmSamples)
        {
            continue;
        }
        addBoundaryPath(boundaryFull, boundaryPathTracker->fullRateCandidate_);
        addBoundaryPath(boundaryHalf, boundaryPathTracker->halfRateCandidate_);
        addBoundaryPath(boundaryQuarter, boundaryPathTracker->quarterRateCandidate_);
        addBoundaryPath(boundaryEighth, boundaryPathTracker->eighthRateCandidate_);
        addBoundaryOutput(boundaryOutput, observed);
    }

    const auto printBoundaryStats = [holdBoundaryHz](const char* name,
                                                      const BoundaryPathStats& stats)
    {
        const double meanHz = stats.near456 > 0
            ? stats.sumHz / static_cast<double>(stats.near456) : 0.0;
        const double meanCents = meanHz > 0.0
            ? 1200.0 * std::log2(meanHz / 456.0) : 0.0;
        const double minimumHz = stats.near456 > 0 ? stats.minimumHz : 0.0;
        const double maximumHz = stats.near456 > 0 ? stats.maximumHz : 0.0;
        std::cerr << "BOUNDARY_PATH_BIAS path=" << name
                  << " fresh_valid=" << stats.freshValid
                  << " near456=" << stats.near456
                  << " mean_hz=" << meanHz
                  << " mean_cents=" << meanCents
                  << " minmax_hz=" << minimumHz << ',' << maximumHz
                  << " above_hold_boundary=" << stats.aboveHoldBoundary
                  << " hold_boundary_hz=" << holdBoundaryHz << '\n';
    };
    printBoundaryStats("full", boundaryFull);
    printBoundaryStats("half", boundaryHalf);
    printBoundaryStats("quarter", boundaryQuarter);
    printBoundaryStats("eighth", boundaryEighth);
    printBoundaryStats("output", boundaryOutput);

'''

test = test.replace(anchor, diag + anchor, 1)
test_path.write_text(test)
print('BOUNDARY_PATH_BIAS_DIAGNOSTIC_V1 materialized')
