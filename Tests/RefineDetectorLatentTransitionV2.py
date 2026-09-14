from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


def region(text, start, end, replacement, label):
    begin = text.find(start)
    if begin < 0:
        raise SystemExit(f"{label}: start not found")
    finish = text.find(end, begin)
    if finish < 0:
        raise SystemExit(f"{label}: end not found")
    if text.find(start, begin + 1) >= 0:
        raise SystemExit(f"{label}: start not unique")
    return text[:begin] + replacement + text[finish:]


cpp_p = Path('Source/ModernPitchEngine.cpp')
test_p = Path('Tests/SupervisorContinuityTest.cpp')
cpp = cpp_p.read_text()
test = test_p.read_text()

# The first V2 materialization made every observation that quantized to another
# degree enter the latent wall. That was too broad: ordinary vibrato can cross a
# half-cell boundary without becoming a new musical identity. Preserve the old
# successful geometric distinction: boundary-zone motion remains local source
# motion, while only a coordinate deep inside the challenger cell may accumulate
# note-change authority.
start = '''    bool detectorScaleCommit = false;\n'''
end = '''    if (rescueBodyFrame && !observation.onset)\n'''
replacement = r'''    bool detectorScaleCommit = false;
    if (state.targetValid)
    {
        // LATENT_SCALE_CANDIDATE_V2: detector uncertainty is represented in a
        // separate analysis state. The currently owned scale degree is never
        // revised merely because an instantaneous F0 crosses a Voronoi boundary.
        const double nominatedTarget = quantizer.nearestTargetLog2(observedLog2);
        const double targetDeltaCents =
            (nominatedTarget - state.targetLog2) * 1200.0;
        const bool nominatesOwnedTarget = std::abs(targetDeltaCents) < 0.5;

        if (nominatesOwnedTarget)
        {
            state.latentTargetValid = false;
            state.latentTargetLog2 = 0.0;
            state.latentTargetHops = 0;
        }
        else
        {
            const double scaleStep = std::max(0.1,
                static_cast<double>(quantizer.minimumStepCents()));
            const double observedDistanceFromOwnedTarget =
                std::abs(observedLog2 - state.targetLog2) * 1200.0;
            const double deepExitRatio = scaleStep <= 50.0 ? 0.52 : 0.72;

            const double absoluteJump = std::abs(targetDeltaCents);
            const int nearestOctave = static_cast<int>(std::lround(
                absoluteJump / 1200.0));
            const bool octaveLikeTarget = nearestOctave >= 1
                && nearestOctave <= 2
                && std::abs(absoluteJump
                    - 1200.0 * static_cast<double>(nearestOctave)) <= 35.0;

            // AMBIGUOUS_BOUNDARY_IS_LOCAL_MOTION_V2: on a wide scale a sung
            // vibrato may live beyond the half-cell boundary for many hops.
            // Until it penetrates the challenger cell deeply enough, it is not
            // even a note-change candidate. Let the pre-existing continuity and
            // local-transport tracker see it so zero Vibrato can remove the
            // physical modulation, but do not grant this latent gate any target
            // authority. Octave-like observations are always treated as deep
            // ambiguity because their error cost is catastrophic.
            const bool deepCandidate = octaveLikeTarget
                || observedDistanceFromOwnedTarget >= deepExitRatio * scaleStep;

            if (!deepCandidate)
            {
                state.latentTargetValid = false;
                state.latentTargetLog2 = 0.0;
                state.latentTargetHops = 0;
                // Deliberately continue: target identity remains governed by the
                // old bounded continuity logic, which already rejects ±70-cent
                // long vibrato, while transport may still track local motion.
            }
            else
            {
                const bool sameLatent = state.latentTargetValid
                    && std::abs(nominatedTarget - state.latentTargetLog2)
                        * 1200.0 < 0.5;
                if (sameLatent)
                {
                    state.latentTargetHops = std::min(64,
                        state.latentTargetHops + 1);
                }
                else
                {
                    state.latentTargetValid = true;
                    state.latentTargetLog2 = nominatedTarget;
                    state.latentTargetHops = 1;
                }

                int requiredHops = 0;
                if (octaveLikeTarget)
                {
                    // A provisional single detector family may describe an
                    // octave candidate forever but can never own the register.
                    // Trusted/corroborated octave evidence remains possible and
                    // bounded, just deliberately slower than ordinary notes.
                    if (!trustedPitch && observation.detectorSupport < 2)
                        requiredHops = 1000000;
                    else if (trustedPitch && observation.detectorSupport >= 2)
                        requiredHops = 8;
                    else if (trustedPitch)
                        requiredHops = 14;
                    else
                        requiredHops = 18;
                }
                else
                {
                    // Precision is concentrated at the boundary, not paid for
                    // by normal note speed. A real coordinate deep in the next
                    // cell changes quickly once it repeats geometrically.
                    requiredHops = trustedPitch
                        ? (observation.detectorSupport >= 2 ? 2 : 3)
                        : 6;
                }

                if (state.latentTargetHops < requiredHops)
                {
                    // Stable C -> uncertain material => exactly stable C.
                    // Freeze all audible coordinates until this deep challenger
                    // has actually earned a scale-domain commit.
                    return;
                }

                detectorScaleCommit = true;
                state.latentTargetValid = false;
                state.latentTargetLog2 = 0.0;
                state.latentTargetHops = 0;
            }
        }
    }

'''
cpp = region(cpp, start, end, replacement, 'refine latent deep-cell gate')

# The old regression encoded the V2 8-hop octave window. V3 intentionally makes
# a single-family upward octave finite but much stricter: 24 consecutive fresh
# observations. Update only that temporal expectation; confidence remains absent
# from the authorization law.
old_test = '''    bool octaveCommittedTooEarly = false;\n    bool octaveCommittedInFiniteTime = false;\n    for (int hop = 0; hop < 8; ++hop)\n    {\n        auto decision = liveRescueDecision;\n        decision.valid = true;\n        decision.candidate.valid = true;\n        const bool accepted = liveRescueTracker->confirmOctaveTransition(\n            decision, false);\n        if (hop < 7)\n            octaveCommittedTooEarly = octaveCommittedTooEarly || accepted;\n        else\n            octaveCommittedInFiniteTime = accepted && decision.valid;\n    }\n    success &= check(!octaveCommittedTooEarly && octaveCommittedInFiniteTime,\n                     "octave_veto_is_bounded_not_confidence_gated");\n'''
new_test = '''    bool octaveCommittedTooEarly = false;\n    bool octaveCommittedInFiniteTime = false;\n    for (int hop = 0; hop < 24; ++hop)\n    {\n        auto decision = liveRescueDecision;\n        decision.valid = true;\n        decision.candidate.valid = true;\n        const bool accepted = liveRescueTracker->confirmOctaveTransition(\n            decision, false);\n        if (hop < 23)\n            octaveCommittedTooEarly = octaveCommittedTooEarly || accepted;\n        else\n            octaveCommittedInFiniteTime = accepted && decision.valid;\n    }\n    success &= check(!octaveCommittedTooEarly && octaveCommittedInFiniteTime,\n                     "octave_veto_is_bounded_not_confidence_gated");\n'''
test = one(test, old_test, new_test, 'update legacy octave window')

cpp_p.write_text(cpp)
test_p.write_text(test)
print('detector latent transition v2 boundary refinement applied')
