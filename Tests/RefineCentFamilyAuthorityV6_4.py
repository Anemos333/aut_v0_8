from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
cpp = cpp_path.read_text()


def one(text, anchor, replacement, label):
    count = text.count(anchor)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(anchor, replacement, 1)


marker = 'CENT_FAMILY_AUTHORITY_GATE_V6_4'
if marker not in cpp:
    # V6.4 is deliberately measurement-domain only. It does not alter analyse(),
    # detector validity, scale/target ownership, renderer, correction amount, or
    # output. It only decides whether a V6.1 fresh measurement may bypass the
    # existing ambiguity resolver.
    #
    # A simple 2:1 / 3:1 / 4:1 family jump is suspicious only when the previously
    # tracked physical coordinate is STILL measured by a strong path on the
    # current hop. Historical memory alone can never defend the old coordinate.
    # Conversely, two native strong paths agreeing on the new coordinate retain
    # V6.1 authority and can falsify the old coordinate immediately.

    anchor = '''    if (authoritativeIndexV6 >= 0)\n    {\n        const auto& best = candidates[static_cast<std::size_t>(authoritativeIndexV6)];\n        bool genuinelyAmbiguous = false;\n'''

    replacement = '''    // CENT_FAMILY_AUTHORITY_GATE_V6_4\n    // Cheap geometry only: describe how a proposed live coordinate relates to\n    // the currently explained physical F0. This is not a fifth detector path,\n    // not a confidence score and never a veto on correction/output.\n    const auto simpleIntegerFamilyJumpV64 = [](float aHz, float bHz) noexcept\n    {\n        if (!(aHz > 0.0f) || !(bHz > 0.0f)\n            || !std::isfinite(aHz) || !std::isfinite(bHz))\n        {\n            return false;\n        }\n        const float cents = std::abs(1200.0f * std::log2(aHz / bHz));\n        constexpr float familyToleranceCents = 70.0f;\n        constexpr std::array<float, 3> simpleFamilies {\n            1200.0f,        // 2:1\n            1901.955001f,   // 3:1\n            2400.0f         // 4:1\n        };\n        for (const float familyCents : simpleFamilies)\n        {\n            if (std::abs(cents - familyCents) <= familyToleranceCents)\n                return true;\n        }\n        return false;\n    };\n\n    if (authoritativeIndexV6 >= 0)\n    {\n        const auto& best = candidates[static_cast<std::size_t>(authoritativeIndexV6)];\n\n        // Count native strong measurements of the proposed coordinate. Retained\n        // slower-path observations are allowed here exactly as in V6.1: they are\n        // bounded by collectFreshCandidates() and represent independent native\n        // measurements rather than octave-shifted synthetic hypotheses.\n        int proposedNativeSupportV64 = 0;\n        for (int index = 0; index < candidateCount; ++index)\n        {\n            const auto& other = candidates[static_cast<std::size_t>(index)];\n            if (!structurallyAuthoritativeV6(other)\n                || lowerAliasShadowedV6(index)\n                || centsDistance(other.frequencyHz, best.frequencyHz) > 70.0f)\n            {\n                continue;\n            }\n            ++proposedNativeSupportV64;\n        }\n\n        // The previous coordinate is allowed to challenge a rational-family\n        // jump only if CURRENT samples still support it. ageInHops==0 is the\n        // anti-prudence guard: stale tracker/anchor memory cannot suppress a new\n        // measurement. A real harmonic jump therefore passes immediately once\n        // the old coordinate ceases to be physically observed, or once >=2\n        // native paths corroborate the new coordinate.\n        bool previousCoordinateStillObservedV64 = false;\n        if (trackedPitchHz_ > 0.0f && std::isfinite(trackedPitchHz_))\n        {\n            for (int index = 0; index < candidateCount; ++index)\n            {\n                const auto& other = candidates[static_cast<std::size_t>(index)];\n                if (other.ageInHops != 0\n                    || !structurallyAuthoritativeV6(other)\n                    || lowerAliasShadowedV6(index))\n                {\n                    continue;\n                }\n                if (centsDistance(other.frequencyHz, trackedPitchHz_) <= 70.0f)\n                {\n                    previousCoordinateStillObservedV64 = true;\n                    break;\n                }\n            }\n        }\n\n        const bool familyJumpNeedsResolverV64 =\n            trackedPitchHz_ > 0.0f\n            && simpleIntegerFamilyJumpV64(best.frequencyHz, trackedPitchHz_)\n            && previousCoordinateStillObservedV64\n            && proposedNativeSupportV64 < 2;\n\n        // A family-related solitary proposal is not discarded. It simply loses\n        // V6.1's privilege to bypass consensus/beam while another strong current\n        // measurement still explains the old F0. Non-family note changes, loss\n        // of current support for the old F0, or >=2 native new-path witnesses all\n        // retain immediate V6.1 behaviour.\n        bool genuinelyAmbiguous = familyJumpNeedsResolverV64;\n'''

    cpp = one(cpp, anchor, replacement, 'V6.1 authoritative fast-path gate')
    cpp_path.write_text(cpp)

print('CENT_FAMILY_AUTHORITY_GATE_V6_4 materialized')
