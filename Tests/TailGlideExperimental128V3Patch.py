from pathlib import Path
import re


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}: {old[:80]!r}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def regex_once(path: str, pattern: str, replacement: str) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    updated, count = re.subn(pattern, replacement, text, count=1, flags=re.MULTILINE)
    if count != 1:
        raise RuntimeError(f"{path}: regex expected one match, found {count}: {pattern!r}")
    p.write_text(updated, encoding="utf-8")


engine = "Source/ModernPitchEngine.cpp"
renderer = "Source/SingleWetSpectralRenderer.cpp"
renderer_test = "Tests/SingleWetSpectralRendererTest.cpp"
clean_test = "Tests/CleanModernPitchEngineTest.cpp"
gui_test = "Tests/GuiAudibilityTest.cpp"
supervisor_test = "Tests/SupervisorContinuityTest.cpp"

# ---------------------------------------------------------------------------
# Experimental: restore the genuinely low-latency lattice. The current V2
# renderer has been independently probed at 128 samples and now clears the same
# spectral-purity contract that previously forced Experimental to 256.
regex_once(
    engine,
    r"    // SINGLE_WET_PURITY_V6\n(?:    //.*\n)+    switch \(mode\)",
    "    // EXPERIMENTAL_128_V3\n"
    "    // CONTINUOUS_PHASE_FIELD_V2 removed the old 128-sample source-copy\n"
    "    // failure: the current renderer measures >2000:1 target/source power\n"
    "    // on the one-semitone regression while retaining sub-cent ratio error.\n"
    "    // Experimental can therefore use and report the honest 128-sample\n"
    "    // lattice again; Live remains 256 and Quality remains frozen at 512.\n"
    "    switch (mode)",
)
replace_once(
    engine,
    "        case LatencyMode::ultraLive: return 256;",
    "        case LatencyMode::ultraLive: return 128; // EXPERIMENTAL_128_V3",
)

# ---------------------------------------------------------------------------
# Short-lattice phase velocity coherence. Magnitudes and commanded ratio remain
# untouched. Quality >=512 follows the exact previous expression. Only 128/256
# bins close to the same spectral peak share temporal phase velocity smoothly,
# preventing leakage bins from becoming independently moving phase populations.
replace_once(
    renderer,
    """            double& synthesisPhase =
                layer.synthesisPhases[static_cast<std::size_t>(sourceBin)];
            synthesisPhase += expectedPhaseScale
                * trueSourceBins_[static_cast<std::size_t>(sourceBin)]
                * safeRatio;
""",
    """            double& synthesisPhase =
                layer.synthesisPhases[static_cast<std::size_t>(sourceBin)];

            // SHORT_LATTICE_COHERENT_PHASE_V3
            // On 128/256-sample lattices, neighbouring leakage bins belonging
            // to one partial have noisy independent instantaneous-frequency
            // estimates. Let only their phase velocity converge continuously
            // toward the nearest peak's velocity. This is still one phase
            // field and one spectral transport: no residual/noise path, no dry
            // contribution, no detector gate, and no change to targetPosition
            // or safeRatio. Quality (512+) executes the previous equation.
            double transportSourceBin =
                trueSourceBins_[static_cast<std::size_t>(sourceBin)];
            if (frameSize_ <= 256 && !nearestPeak_.empty())
            {
                const int velocityPeak = nearestPeak_[static_cast<std::size_t>(sourceBin)];
                if (velocityPeak >= 0 && velocityPeak <= positiveBins)
                {
                    const float distance = static_cast<float>(
                        std::abs(velocityPeak - sourceBin));
                    const float coherenceCore = frameSize_ <= 128 ? 0.50f : 0.75f;
                    const float coherenceFade = frameSize_ <= 128 ? 2.50f : 2.75f;
                    const float coherence = 1.0f
                        - smoothStep(coherenceCore, coherenceFade, distance);
                    const double peakVelocityBin =
                        trueSourceBins_[static_cast<std::size_t>(velocityPeak)];
                    transportSourceBin += static_cast<double>(coherence)
                        * (peakVelocityBin - transportSourceBin);
                }
            }
            synthesisPhase += expectedPhaseScale
                * transportSourceBin
                * safeRatio;
""",
)

# ---------------------------------------------------------------------------
# Tail identity conditioning and micro-glide. It is deliberately Scale-Lock
# scoped, so Scale Lock OFF remains unchanged. A real endpoint is never moved or
# weakened: moderate weak-body target changes get a tiny response floor; very
# weak breath-like tail evidence cannot invent another tiny note identity.
replace_once(
    engine,
    """    const bool targetChanged = !state.targetValid
        || std::abs(newTarget - state.targetLog2) * 1200.0 > 0.1;
    const double targetJump = state.targetValid
        ? (newTarget - state.targetLog2) * 1200.0 : 0.0;
    const double identityThreshold = std::clamp(
        0.18 * static_cast<double>(quantizer.minimumStepCents()), 0.5, 30.0);
    const bool targetIdentityChanged = state.targetValid
        && std::abs(targetJump) >= identityThreshold;
""",
    """    // TAIL_MELODIC_MICRO_GLIDE_V1
    // The last voiced fragments of a note can still contain enough periodicity
    // to make a dense quantizer publish several tiny target identities. That is
    // musically different from a new note. Keep exact correction authority, but
    // condition identity only in an evidence-backed Scale-Lock tail: a very weak
    // breath-like fragment keeps the already owned degree; a moderate fragment
    // may change degree and will receive a tiny single-path micro-glide below.
    const double preliminaryTailJump = state.targetValid
        ? (newTarget - state.targetLog2) * 1200.0 : 0.0;
    const double preliminaryIdentityThreshold = std::clamp(
        0.18 * static_cast<double>(quantizer.minimumStepCents()), 0.5, 30.0);
    const bool scaleLockTail = parameters.scaleLock
        && state.targetValid
        && state.noteBodyLatched
        && !musicalOnset
        && richEvidence
        && parameters.voiceEventStrength < 0.35f
        && bodyScore < 0.52f;
    const bool tailTooWeakForNewDegree = scaleLockTail
        && bodyScore < 0.28f
        && parameters.voiceBreathiness > 0.42f
        && std::abs(preliminaryTailJump) >= preliminaryIdentityThreshold;
    if (tailTooWeakForNewDegree)
        newTarget = state.targetLog2;

    const bool targetChanged = !state.targetValid
        || std::abs(newTarget - state.targetLog2) * 1200.0 > 0.1;
    const double targetJump = state.targetValid
        ? (newTarget - state.targetLog2) * 1200.0 : 0.0;
    const double identityThreshold = std::clamp(
        0.18 * static_cast<double>(quantizer.minimumStepCents()), 0.5, 30.0);
    const bool targetIdentityChanged = state.targetValid
        && std::abs(targetJump) >= identityThreshold;
""",
)
replace_once(
    engine,
    """    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);

    if (!musicalOnset)
""",
    """    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);

    // TAIL_MELODIC_MICRO_GLIDE_V1: Response=0 remains literal on a real note
    // body. Only an evidence-backed weak tail that actually changes degree gets
    // 5.5..13 ms of continuous movement. desiredCents remains the exact same
    // destination and the renderer remains 100% wet throughout the glide.
    if (scaleLockTail && targetIdentityChanged)
    {
        const double weakness = std::clamp(
            (0.52 - static_cast<double>(bodyScore)) / 0.24,
            0.0, 1.0);
        const double tailGlideMs = 5.5 + 7.5 * weakness;
        state.responseMs = std::max(state.responseMs, tailGlideMs);
    }

    if (!musicalOnset)
""",
)

# ---------------------------------------------------------------------------
# Renderer regression now permanently exercises the production 128 lattice.
replace_once(
    renderer_test,
    "const std::array<int, 2> frameSizes { 512, 256 };",
    "const std::array<int, 3> frameSizes { 512, 256, 128 };",
)
replace_once(
    renderer_test,
    """        success &= check(targetPower > 1000.0 * sourcePower,
                         frameSize == 512 ? "quality_has_no_audible_source_copy"
                                          : "live_and_experimental_have_no_audible_source_copy");
""",
    """        success &= check(targetPower > 1000.0 * sourcePower,
                         frameSize == 512 ? "quality_has_no_audible_source_copy"
                         : frameSize == 256 ? "live_has_no_audible_source_copy"
                                            : "experimental_128_has_no_audible_source_copy");
""",
)
replace_once(
    renderer_test,
    """            success &= check(std::abs(error) < 0.35,
                             frameSize == 512
                                ? "quality_realizes_commanded_pitch_ratio"
                                : "live_realizes_commanded_pitch_ratio");
""",
    """            success &= check(std::abs(error) < 0.35,
                             frameSize == 512
                                ? "quality_realizes_commanded_pitch_ratio"
                             : frameSize == 256
                                ? "live_realizes_commanded_pitch_ratio"
                                : "experimental_128_realizes_commanded_pitch_ratio");
""",
)

# Latency contracts.
replace_once(
    clean_test,
    "const std::array<int, 3> expectedLatencies { 256, 256, 512 };",
    "const std::array<int, 3> expectedLatencies { 128, 256, 512 };",
)
replace_once(
    gui_test,
    "{ ModernPitchEngine::LatencyMode::ultraLive, 256 }",
    "{ ModernPitchEngine::LatencyMode::ultraLive, 128 }",
)

# ---------------------------------------------------------------------------
# Tail regressions are inserted next to the existing rigid Response=0 contract.
anchor = """    success &= check(std::abs(immediateController - explicitAuthorityState.desiredCents) < 1.0e-9
                     && std::abs(explicitAuthorityState.velocityCentsPerSecond) < 1.0e-12,
                     "response_zero_reaches_destination_in_one_sample");
"""
insert = anchor + """

    // TAIL_MELODIC_MICRO_GLIDE_V1 regression: a strong body keeps literal
    // Response=0 above. A weak-but-still-musical Scale-Lock tail may move to the
    // next real degree, but it must do so on the same wet trajectory and still
    // converge exactly to the unattenuated destination.
    const std::array<double, 2> tailScale {
        1.0, std::exp2(1.0 / 12.0)
    };
    ModernPitchEngine::ScaleQuantizer tailQuantizer;
    tailQuantizer.reset();
    tailQuantizer.setScale(tailScale.data(), static_cast<int>(tailScale.size()), 440.0);
    ModernPitchEngine::Parameters tailParameters = explicitAuthorityParameters;
    setBodyEvidence(tailParameters);
    tailParameters.voiceEventStrength = 0.0f;
    ModernPitchEngine::CorrectionState tailState;
    auto tailBody = strongPitch(444.0f);
    tailBody.audioPresent = true;
    tailBody.correctionFrequencyHz = 444.0f;
    engine->updateCorrectionState(tailState, tailQuantizer,
                                  tailBody, tailParameters);
    static_cast<void>(engine->advanceCorrection(tailState));
    const double bodyTarget = tailState.targetLog2;

    tailParameters.voiceEvidenceValid = true;
    tailParameters.voiceBodyEnergy = 0.30f;
    tailParameters.voiceHarmonicity = 0.35f;
    tailParameters.voiceSpectralReliability = 0.38f;
    tailParameters.voiceBreathiness = 0.42f;
    tailParameters.voiceEventStrength = 0.05f;
    auto melodicTail = strongPitch(465.0f);
    melodicTail.audioPresent = true;
    melodicTail.correctionFrequencyHz = 465.0f;
    melodicTail.voicing = 0.52f;
    melodicTail.periodicity = 0.48f;
    melodicTail.confidence = 0.50f;
    melodicTail.consensus = 0.34f;
    engine->updateCorrectionState(tailState, tailQuantizer,
                                  melodicTail, tailParameters);
    const double tailTargetHz = std::exp2(tailState.targetLog2);
    const double tailExpectedCents = 1200.0 * std::log2(
        tailTargetHz / static_cast<double>(melodicTail.correctionFrequencyHz));
    success &= check(std::abs(tailState.targetLog2 - bodyTarget) * 1200.0 > 30.0
                     && tailState.responseMs >= 5.5
                     && tailState.responseMs <= 13.1,
                     "weak_tail_target_change_uses_single_wet_micro_glide");
    success &= check(std::abs(tailState.desiredCents - tailExpectedCents) < 1.0e-6,
                     "tail_micro_glide_does_not_weaken_authority_destination");
    for (int sample = 0; sample < 8000; ++sample)
        static_cast<void>(engine->advanceCorrection(tailState));
    success &= check(std::abs(tailState.currentCents - tailState.desiredCents) < 0.001,
                     "tail_micro_glide_converges_to_exact_destination");

    // Once the tail is breath-like and extremely weak, a lone pitch accident
    // may not create another tiny note. The owned degree remains corrected;
    // this is target identity hold, not a bypass or reduction of correction.
    const double ownedTailTarget = tailState.targetLog2;
    tailParameters.voiceBodyEnergy = 0.08f;
    tailParameters.voiceHarmonicity = 0.10f;
    tailParameters.voiceSpectralReliability = 0.16f;
    tailParameters.voiceBreathiness = 0.78f;
    auto weakTailAccident = strongPitch(440.5f);
    weakTailAccident.audioPresent = true;
    weakTailAccident.correctionFrequencyHz = 440.5f;
    weakTailAccident.voicing = 0.20f;
    weakTailAccident.periodicity = 0.18f;
    weakTailAccident.confidence = 0.20f;
    weakTailAccident.consensus = 0.08f;
    engine->updateCorrectionState(tailState, tailQuantizer,
                                  weakTailAccident, tailParameters);
    success &= check(std::abs(tailState.targetLog2 - ownedTailTarget) < 1.0e-12
                     && std::abs(tailState.desiredCents) > 5.0,
                     "very_weak_tail_keeps_owned_degree_without_dry_bypass");
"""
replace_once(supervisor_test, anchor, insert)

print("TailGlideExperimental128V3 patch applied")
