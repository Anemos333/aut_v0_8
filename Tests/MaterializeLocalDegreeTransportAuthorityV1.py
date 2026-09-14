from pathlib import Path

source_path = Path('Source/ModernPitchEngine.cpp')
test_path = Path('Tests/SupervisorContinuityTest.cpp')
source = source_path.read_text()
tests = test_path.read_text()


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one anchor, found {count}')
    return text.replace(old, new, 1)

source = replace_once(
    source,
    """    const double observedLog2 = safeLog2(correctionFrequencyHz);\n    const double correctionObservedLog2 = observedLog2;\n\n    bool detectorScaleCommit = false;\n""",
    """    const double observedLog2 = safeLog2(correctionFrequencyHz);\n    const double correctionObservedLog2 = observedLog2;\n\n    // LOCAL_DEGREE_GEOMETRY_V1: every within-note authority threshold is\n    // measured against the actual adjacent scale degree in the direction of\n    // motion. A 70-cent motion therefore has a completely different meaning\n    // in 75-EDO than inside a 200-cent diatonic gap. The minimum global step is\n    // only a numerical fallback, never the musical geometry when a target exists.\n    const auto localDegreeStepCents = [&quantizer](double targetLog2,\n                                                   double probeLog2) noexcept\n    {\n        const int direction = probeLog2 >= targetLog2 ? 1 : -1;\n        const double adjacent = quantizer.adjacentTargetLog2(targetLog2, direction);\n        const double stepCents = std::abs(adjacent - targetLog2) * 1200.0;\n        if (std::isfinite(stepCents) && stepCents >= 0.1)\n            return stepCents;\n        return std::max(0.1, static_cast<double>(quantizer.minimumStepCents()));\n    };\n\n    bool detectorScaleCommit = false;\n""",
    'insert local degree helper')

source = replace_once(
    source,
    """            const double scaleStep = std::max(0.1,\n                static_cast<double>(quantizer.minimumStepCents()));\n            const double observedDistanceFromOwnedTarget =\n""",
    """            const double scaleStep = localDegreeStepCents(\n                state.targetLog2, observedLog2);\n            const double observedDistanceFromOwnedTarget =\n""",
    'latent candidate local step')

source = replace_once(
    source,
    """        const double distanceCents = std::abs(observedLog2 - state.pitchCentreLog2) * 1200.0;\n        const double scaleStep = std::max(0.1,\n            static_cast<double>(quantizer.minimumStepCents()));\n        const double maximumWithinNoteTolerance = std::clamp(\n""",
    """        const double distanceCents = std::abs(observedLog2 - state.pitchCentreLog2) * 1200.0;\n        const double scaleStep = localDegreeStepCents(\n            state.targetLog2, observedLog2);\n        const double maximumWithinNoteTolerance = std::clamp(\n""",
    'identity centre local step')

source = replace_once(
    source,
    """            const double innovationCents =\n                (observedSourceLog2 - predictedSourceLog2) * 1200.0;\n            const double localScaleStep = std::max(0.1,\n                static_cast<double>(quantizer.minimumStepCents()));\n            const double innovationGateCents = std::clamp(\n                0.28 * localScaleStep, 8.0, 28.0);\n""",
    """            const double innovationCents =\n                (observedSourceLog2 - predictedSourceLog2) * 1200.0;\n            const double localScaleStep = localDegreeStepCents(\n                state.targetLog2, observedSourceLog2);\n            // DEGREE_RELATIVE_TRANSPORT_GATE_V1: no fixed 8-cent floor. Dense\n            // scales must see a correspondingly small local-motion gate, while\n            // wide diatonic gaps may follow ordinary sung vibrato continuously.\n            const double innovationGateCents = std::max(\n                0.5, 0.28 * localScaleStep);\n""",
    'transport local step and gate')

old_catchup = """                    if (state.transportChallengerHops >= 3)\n                    {\n                        const double challengerDeltaCents =\n                            (state.transportChallengerLog2 - currentSourceLog2) * 1200.0;\n                        const double maximumCatchupStep = std::clamp(\n                            0.32 * localScaleStep, 18.0, 36.0);\n                        const double catchupStep = std::clamp(\n                            challengerDeltaCents,\n                            -maximumCatchupStep,\n                             maximumCatchupStep);\n                        state.transportPeriodHz = std::exp2(\n                            currentSourceLog2 + catchupStep / 1200.0);\n                        state.transportVelocityCentsPerHop = catchupStep;\n                    }\n                    else\n                    {\n                        // First/second large observation has zero audible\n                        // authority. This is the consonant/outlier safety wall.\n                        state.transportVelocityCentsPerHop *= 0.35;\n                    }\n"""
new_catchup = """                    // UNCOMMITTED_LARGE_INNOVATION_ZERO_TRANSPORT_AUTHORITY_V1:\n                    // persistence may inform analysis, but it cannot move the\n                    // audible source coordinate. A real new note is handled by\n                    // the target-identity commit above, which rebases target and\n                    // transport atomically. This removes the old 3-hop catch-up\n                    // path that could turn a repeated detector error into a\n                    // 100-300 cent audible excursion on a sustained vowel.\n                    state.transportVelocityCentsPerHop *= 0.35;\n"""
source = replace_once(source, old_catchup, new_catchup,
                      'remove uncommitted large innovation catchup')

source = replace_once(
    source,
    """    const double minimumStep = std::max(0.1,\n        static_cast<double>(quantizer.minimumStepCents()));\n    const double halfStep = 0.5 * minimumStep;\n""",
    """    // TARGET_LOCAL_SOFTNESS_GEOMETRY_V1: explicit Amount/Humanize/\n    // Vibrato softness is bounded inside the actual local target cell. Response\n    // is intentionally absent here: it controls convergence time only and can\n    // never weaken the destination.\n    const double minimumStep = localDegreeStepCents(\n        state.targetLog2, audibleSourceLog2);\n    const double halfStep = 0.5 * minimumStep;\n""",
    'output local softness step')

insert_anchor = """    ModernPitchEngine::CorrectionState zeroResponseTransition;\n"""
new_tests = r'''    // UNCOMMITTED_LARGE_INNOVATION_ZERO_TRANSPORT_AUTHORITY_V1: a repeated
    // grey-zone jump may accumulate identity evidence, but before commit it may
    // not drag the audible source coordinate. This directly covers the measured
    // long-vowel failure where 3 repeated bad F0 frames produced 100-300 cent
    // output excursions.
    ModernPitchEngine::ScaleQuantizer largeInnovationQuantizer;
    largeInnovationQuantizer.reset();
    largeInnovationQuantizer.setScale(authorityChromatic.data(),
                                       static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState largeInnovationState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(largeInnovationState,
                                      largeInnovationQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double largeInnovationTarget = largeInnovationState.targetLog2;
    const double largeInnovationTransport = largeInnovationState.transportPeriodHz;
    ModernPitchEngine::PitchObservation greyLargeInnovation = greyD;
    greyLargeInnovation.correctionFrequencyHz = static_cast<float>(
        440.0 * std::exp2(240.0 / 1200.0));
    for (int hop = 0; hop < 5; ++hop)
        engine->updateCorrectionState(largeInnovationState,
                                      largeInnovationQuantizer,
                                      greyLargeInnovation,
                                      explicitAuthorityParameters);
    success &= check(std::abs(largeInnovationState.targetLog2
                              - largeInnovationTarget) < 1.0e-12
                     && std::abs(largeInnovationState.transportPeriodHz
                                 - largeInnovationTransport) < 1.0e-12,
                     "uncommitted_large_innovation_has_zero_transport_authority");

    // DEGREE_RELATIVE_TRANSPORT_GATE_V1: the exact same 12-cent-per-hop source
    // movement is local motion inside a 200-cent diatonic gap, but is already a
    // cross-degree event in 75-EDO (~16 cents/degree). Geometry, not an absolute
    // cents threshold, decides which path is allowed to refine transport.
    std::array<double, 75> edo75Scale {};
    for (int degree = 0; degree < 75; ++degree)
        edo75Scale[static_cast<std::size_t>(degree)] = std::exp2(
            static_cast<double>(degree) / 75.0);
    const std::array<double, 7> diatonicScale {
        1.0,
        std::exp2(2.0 / 12.0),
        std::exp2(4.0 / 12.0),
        std::exp2(5.0 / 12.0),
        std::exp2(7.0 / 12.0),
        std::exp2(9.0 / 12.0),
        std::exp2(11.0 / 12.0)
    };
    ModernPitchEngine::ScaleQuantizer edo75Quantizer;
    ModernPitchEngine::ScaleQuantizer diatonicQuantizer;
    edo75Quantizer.reset();
    diatonicQuantizer.reset();
    edo75Quantizer.setScale(edo75Scale.data(), static_cast<int>(edo75Scale.size()), 440.0);
    diatonicQuantizer.setScale(diatonicScale.data(), static_cast<int>(diatonicScale.size()), 440.0);
    ModernPitchEngine::CorrectionState edo75State;
    ModernPitchEngine::CorrectionState diatonicState;
    for (int hop = 0; hop < 12; ++hop)
    {
        engine->updateCorrectionState(edo75State, edo75Quantizer,
                                      latentC, explicitAuthorityParameters);
        engine->updateCorrectionState(diatonicState, diatonicQuantizer,
                                      latentC, explicitAuthorityParameters);
    }
    const double edo75TransportBefore = edo75State.transportPeriodHz;
    const double diatonicTransportBefore = diatonicState.transportPeriodHz;
    auto twelveCentTrajectory = strongPitch(440.0f);
    twelveCentTrajectory.audioPresent = true;
    twelveCentTrajectory.correctionFrequencyHz = static_cast<float>(
        440.0 * std::exp2(12.0 / 1200.0));
    engine->updateCorrectionState(edo75State, edo75Quantizer,
                                  twelveCentTrajectory, explicitAuthorityParameters);
    engine->updateCorrectionState(diatonicState, diatonicQuantizer,
                                  twelveCentTrajectory, explicitAuthorityParameters);
    success &= check(std::abs(edo75State.transportPeriodHz - edo75TransportBefore) < 1.0e-12
                     && diatonicState.transportPeriodHz > diatonicTransportBefore,
                     "transport_gate_scales_with_actual_adjacent_degree_width");

    // TARGET_REMAINS_DESTINATION_V1: Response changes only convergence speed.
    // It may never change target identity or desired correction depth. Amount is
    // the explicit depth control, and even when softened it remains inside the
    // target-owned local cell rather than restoring dry authority.
    ModernPitchEngine::Parameters fastAuthority = explicitAuthorityParameters;
    ModernPitchEngine::Parameters slowAuthority = explicitAuthorityParameters;
    fastAuthority.amount = 1.0f;
    slowAuthority.amount = 1.0f;
    fastAuthority.humanize = 0.0f;
    slowAuthority.humanize = 0.0f;
    fastAuthority.vibratoPreserve = 0.0f;
    slowAuthority.vibratoPreserve = 0.0f;
    fastAuthority.retuneTimeMs = 0.0f;
    slowAuthority.retuneTimeMs = 250.0f;
    ModernPitchEngine::ScaleQuantizer fastAuthorityQuantizer;
    ModernPitchEngine::ScaleQuantizer slowAuthorityQuantizer;
    fastAuthorityQuantizer.reset();
    slowAuthorityQuantizer.reset();
    fastAuthorityQuantizer.setScale(authorityChromatic.data(),
                                     static_cast<int>(authorityChromatic.size()), 440.0);
    slowAuthorityQuantizer.setScale(authorityChromatic.data(),
                                     static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState fastAuthorityState;
    ModernPitchEngine::CorrectionState slowAuthorityState;
    for (int hop = 0; hop < 12; ++hop)
    {
        engine->updateCorrectionState(fastAuthorityState, fastAuthorityQuantizer,
                                      latentC, fastAuthority);
        engine->updateCorrectionState(slowAuthorityState, slowAuthorityQuantizer,
                                      latentC, slowAuthority);
    }
    auto authorityMove = strongPitch(452.0f);
    authorityMove.audioPresent = true;
    authorityMove.correctionFrequencyHz = 452.0f;
    engine->updateCorrectionState(fastAuthorityState, fastAuthorityQuantizer,
                                  authorityMove, fastAuthority);
    engine->updateCorrectionState(slowAuthorityState, slowAuthorityQuantizer,
                                  authorityMove, slowAuthority);
    success &= check(std::abs(fastAuthorityState.targetLog2
                              - slowAuthorityState.targetLog2) < 1.0e-12
                     && std::abs(fastAuthorityState.desiredCents
                                 - slowAuthorityState.desiredCents) < 1.0e-9
                     && slowAuthorityState.responseMs > fastAuthorityState.responseMs,
                     "response_changes_time_not_target_authority");

    ModernPitchEngine::Parameters softAmountAuthority = fastAuthority;
    softAmountAuthority.amount = 0.0f;
    ModernPitchEngine::ScaleQuantizer softAmountQuantizer;
    softAmountQuantizer.reset();
    softAmountQuantizer.setScale(authorityChromatic.data(),
                                 static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState softAmountState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(softAmountState, softAmountQuantizer,
                                      latentC, softAmountAuthority);
    engine->updateCorrectionState(softAmountState, softAmountQuantizer,
                                  authorityMove, softAmountAuthority);
    const double softAmountTargetHz = std::exp2(softAmountState.targetLog2);
    const double softAmountOutputHz = softAmountState.transportPeriodHz
        * std::exp2(softAmountState.desiredCents / 1200.0);
    const double softAmountResidual = std::abs(1200.0 * std::log2(
        softAmountOutputHz / softAmountTargetHz));
    success &= check(std::abs(softAmountState.targetLog2
                              - fastAuthorityState.targetLog2) < 1.0e-12
                     && std::abs(softAmountState.desiredCents) > 0.1
                     && softAmountResidual < 34.1,
                     "amount_softens_inside_target_cell_never_restores_dry_authority");

'''
if insert_anchor not in tests:
    raise RuntimeError('test insertion anchor missing')
tests = tests.replace(insert_anchor, new_tests + insert_anchor, 1)

source_path.write_text(source)
test_path.write_text(tests)
print('local-degree-transport-authority v1 materialized')
