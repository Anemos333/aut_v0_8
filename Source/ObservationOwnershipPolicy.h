#pragma once

#include <cstdint>

// OBSERVATION_OWNERSHIP_POLICY_V1
//
// This policy owns one narrow question only:
// may the current observation replace the command already owned by the audible
// renderer?  It never scales correction depth, changes a target, edits F0, or
// introduces a dry path.
//
// The three holds below are deliberately explicit because each protects a
// different physical ambiguity:
// - terminal tail: decaying material must not redefine note identity;
// - provisional octave: an unconfirmed octave family must not seize output;
// - uncommitted large innovation: a large within-note coordinate jump remains
//   analysis evidence until musical identity commits it.
//
// Keeping these reasons in one policy prevents equivalent "prudence" from
// reappearing independently in detector, controller and renderer code.
struct ObservationOwnershipPolicy final
{
    enum HoldReason : std::uint8_t
    {
        none = 0u,
        terminalTail = 1u << 0u,
        provisionalOctave = 1u << 1u,
        uncommittedLargeInnovation = 1u << 2u
    };

    struct Evidence
    {
        bool terminalTailIdentityVeto = false;
        bool provisionalOctaveIdentityVeto = false;
        bool uncommittedLargeInnovation = false;
    };

    struct Decision
    {
        bool acceptCurrentObservation = true;
        std::uint8_t holdReasonMask = none;
    };

    [[nodiscard]] static constexpr Decision evaluate(Evidence evidence) noexcept
    {
        std::uint8_t reasons = none;
        if (evidence.terminalTailIdentityVeto)
            reasons = static_cast<std::uint8_t>(reasons | terminalTail);
        if (evidence.provisionalOctaveIdentityVeto)
            reasons = static_cast<std::uint8_t>(reasons | provisionalOctave);
        if (evidence.uncommittedLargeInnovation)
        {
            reasons = static_cast<std::uint8_t>(
                reasons | uncommittedLargeInnovation);
        }

        return Decision { reasons == none, reasons };
    }
};
