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
    """    };\n\n    bool detectorScaleCommit = false;\n""",
    """    };\n\n    // TERMINAL_TAIL_KEEPS_OWNED_DEGREE_V1: a weakening terminal trajectory\n    // may describe where the physical source is moving, but it is not positive\n    // evidence for a new musical note. Keep transport live so the falling/rising\n    // source can still be corrected to the already-owned scale degree; remove\n    // only note-identity authority. This is symmetric for falling and rising\n    // tails and is measured in the actual adjacent-degree geometry.\n    bool terminalTailIdentityVeto = false;\n    if (state.targetValid\n        && state.noteBodyLatched\n        && richEvidence\n        && !observation.onset)\n    {\n        const double localTailStep = localDegreeStepCents(\n            state.targetLog2, observedLog2);\n        const double signedTailDistanceCents =\n            (observedLog2 - state.targetLog2) * 1200.0;\n        const double priorSourceLog2 = state.transportPeriodHz > 0.0\n            && std::isfinite(state.transportPeriodHz)\n            ? safeLog2(state.transportPeriodHz)\n            : (state.pitchCentreValid ? state.pitchCentreLog2 : state.targetLog2);\n        const double signedPriorDistanceCents =\n            (priorSourceLog2 - state.targetLog2) * 1200.0;\n        const bool sameTailSide = std::abs(signedPriorDistanceCents)\n                < 0.12 * localTailStep\n            || signedTailDistanceCents * signedPriorDistanceCents > 0.0;\n\n        int degradationVotes = 0;\n        degradationVotes += parameters.voiceBodyEnergy < 0.52f ? 1 : 0;\n        degradationVotes += parameters.voiceHarmonicity < 0.48f ? 1 : 0;\n        degradationVotes += parameters.voiceSpectralReliability < 0.50f ? 1 : 0;\n        degradationVotes += parameters.voiceBreathiness > 0.42f ? 1 : 0;\n        const bool terminalStructure = parameters.voiceEventStrength < 0.55f\n            && degradationVotes >= 2;\n        const bool outsideStableCore =\n            std::abs(signedTailDistanceCents) >= 0.32 * localTailStep;\n\n        if (terminalStructure && sameTailSide && outsideStableCore)\n        {\n            terminalTailIdentityVeto = true;\n            identityOnlyVeto = true;\n            // A tail cannot accumulate hidden note-change credit while weak.\n            // Once strong structured evidence returns, normal target nomination\n            // resumes immediately; no invented F0 and no dry release are used.\n            state.latentTargetValid = false;\n            state.latentTargetLog2 = 0.0;\n            state.latentTargetHops = 0;\n            state.identityChallengerDirection = 0;\n            state.identityChallengerEvidence = 0.0;\n        }\n    }\n\n    bool detectorScaleCommit = false;\n""",
    'insert terminal tail identity veto')

insert_anchor = """    ModernPitchEngine::CorrectionState zeroResponseTransition;\n"""
new_tests = r'''    // TERMINAL_TAIL_KEEPS_OWNED_DEGREE_V1: a degrading falling tail is
    // still part of the previously owned note. Its physical F0 may continue to
    // move so correction can cancel that motion, but it may not nominate lower
    // scale degrees until positive structured note evidence returns.
    ModernPitchEngine::Parameters fallingTailParameters = explicitAuthorityParameters;
    fallingTailParameters.voiceEvidenceValid = true;
    fallingTailParameters.voiceBodyEnergy = 0.34f;
    fallingTailParameters.voiceHarmonicity = 0.32f;
    fallingTailParameters.voiceSpectralReliability = 0.34f;
    fallingTailParameters.voiceBreathiness = 0.48f;
    fallingTailParameters.voiceEventStrength = 0.08f;
    ModernPitchEngine::ScaleQuantizer fallingTailQuantizer;
    fallingTailQuantizer.reset();
    fallingTailQuantizer.setScale(authorityChromatic.data(),
                                  static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState fallingTailState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(fallingTailState, fallingTailQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double fallingTailOwnedTarget = fallingTailState.targetLog2;
    const double fallingTailStartTransport = fallingTailState.transportPeriodHz;
    double fallingTailRawHz = 440.0;
    for (int cents = -10; cents >= -110; cents -= 10)
    {
        fallingTailRawHz = 440.0 * std::exp2(static_cast<double>(cents) / 1200.0);
        auto tailHop = strongPitch(static_cast<float>(fallingTailRawHz));
        tailHop.audioPresent = true;
        tailHop.correctionFrequencyHz = tailHop.frequencyHz;
        engine->updateCorrectionState(fallingTailState, fallingTailQuantizer,
                                      tailHop, fallingTailParameters);
    }
    const double fallingTailTargetHz = std::exp2(fallingTailState.targetLog2);
    const double fallingTailOutputHz = fallingTailRawHz * std::exp2(
        fallingTailState.desiredCents / 1200.0);
    const double fallingTailResidual = std::abs(1200.0 * std::log2(
        fallingTailOutputHz / fallingTailTargetHz));
    success &= check(std::abs(fallingTailState.targetLog2
                              - fallingTailOwnedTarget) < 1.0e-12
                     && fallingTailState.transportPeriodHz
                        < fallingTailStartTransport * std::exp2(-45.0 / 1200.0)
                     && fallingTailResidual < 3.0,
                     "falling_terminal_tail_stays_on_previous_degree_and_remains_corrected");

    // The rule is direction symmetric: a weakening rising tail also belongs to
    // the previous degree instead of earning an upward target revision.
    ModernPitchEngine::ScaleQuantizer risingTailQuantizer;
    risingTailQuantizer.reset();
    risingTailQuantizer.setScale(authorityChromatic.data(),
                                 static_cast<int>(authorityChromatic.size()), 440.0);
    ModernPitchEngine::CorrectionState risingTailState;
    for (int hop = 0; hop < 12; ++hop)
        engine->updateCorrectionState(risingTailState, risingTailQuantizer,
                                      latentC, explicitAuthorityParameters);
    const double risingTailOwnedTarget = risingTailState.targetLog2;
    double risingTailRawHz = 440.0;
    for (int cents = 10; cents <= 110; cents += 10)
    {
        risingTailRawHz = 440.0 * std::exp2(static_cast<double>(cents) / 1200.0);
        auto tailHop = strongPitch(static_cast<float>(risingTailRawHz));
        tailHop.audioPresent = true;
        tailHop.correctionFrequencyHz = tailHop.frequencyHz;
        engine->updateCorrectionState(risingTailState, risingTailQuantizer,
                                      tailHop, fallingTailParameters);
    }
    const double risingTailTargetHz = std::exp2(risingTailState.targetLog2);
    const double risingTailOutputHz = risingTailRawHz * std::exp2(
        risingTailState.desiredCents / 1200.0);
    const double risingTailResidual = std::abs(1200.0 * std::log2(
        risingTailOutputHz / risingTailTargetHz));
    success &= check(std::abs(risingTailState.targetLog2
                              - risingTailOwnedTarget) < 1.0e-12
                     && risingTailResidual < 3.0,
                     "rising_terminal_tail_stays_on_previous_degree_and_remains_corrected");

    // Tail ownership is not a permanent lock. As soon as a genuinely structured
    // new note appears, ordinary scale nomination/commit resumes and the next
    // exact degree may own the output.
    ModernPitchEngine::Parameters recoveredNoteParameters = explicitAuthorityParameters;
    setBodyEvidence(recoveredNoteParameters);
    const double lowerChromaticHz = 440.0 * std::exp2(-100.0 / 1200.0);
    auto recoveredLowerNote = strongPitch(static_cast<float>(lowerChromaticHz));
    recoveredLowerNote.audioPresent = true;
    recoveredLowerNote.correctionFrequencyHz = recoveredLowerNote.frequencyHz;
    for (int hop = 0; hop < 8; ++hop)
        engine->updateCorrectionState(fallingTailState, fallingTailQuantizer,
                                      recoveredLowerNote, recoveredNoteParameters);
    const double recoveredTargetHz = std::exp2(fallingTailState.targetLog2);
    success &= check(recoveredTargetHz > 414.0 && recoveredTargetHz < 417.0,
                     "strong_new_note_after_terminal_tail_can_commit_normally");

'''
if insert_anchor not in tests:
    raise RuntimeError('test insertion anchor missing')
tests = tests.replace(insert_anchor, new_tests + insert_anchor, 1)

source_path.write_text(source)
test_path.write_text(tests)
print('terminal-tail-scale-anchor v1 materialized')
