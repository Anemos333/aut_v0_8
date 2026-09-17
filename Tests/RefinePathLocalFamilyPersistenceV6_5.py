from pathlib import Path

root = Path(__file__).resolve().parents[1]
cpp_path = root / 'Source' / 'ModernPitchEngine.cpp'
h_path = root / 'Source' / 'ModernPitchEngine.h'
cpp = cpp_path.read_text()
h = h_path.read_text()


def one(text, anchor, replacement, label):
    count = text.count(anchor)
    if count != 1:
        raise RuntimeError(f'{label}: expected one anchor, found {count}')
    return text.replace(anchor, replacement, 1)


marker = 'PATH_LOCAL_FAMILY_PERSISTENCE_V6_5'
if marker not in cpp:
    # V6.5 is a bounded measurement-domain ambiguity rule. It does not alter
    # analyse(), detector validity, scale ownership, correction authority,
    # renderer, or output. It only prevents a single native path from replacing
    # its own just-demonstrated coordinate with a 2:1/3:1/4:1 family member on
    # one update and prevents a range-limited sibling from exploiting that brief
    # ambiguity as a new coordinate owner.
    #
    # A real family transition is not vetoed: three coherent fresh updates from
    # the same path commit it, and old path memory expires after 12 detector hops.
    # Non-family note changes remain immediate.

    h = one(
        h,
        '''        CandidateSlot fullRateCandidate_;\n        CandidateSlot halfRateCandidate_;\n        CandidateSlot quarterRateCandidate_;\n        CandidateSlot eighthRateCandidate_;\n        std::array<float, maxAnalysisSize> frame_ {};\n''',
        '''        CandidateSlot fullRateCandidate_;\n        CandidateSlot halfRateCandidate_;\n        CandidateSlot quarterRateCandidate_;\n        CandidateSlot eighthRateCandidate_;\n\n        // PATH_LOCAL_FAMILY_PERSISTENCE_V6_5\n        // Short detector-only memory of each path's last physically strong\n        // coordinate. It is never published as a fresh measurement and never\n        // reaches scale/renderer authority directly.\n        std::array<float, detectorPathCount> pathFamilyCoordinateHz_ {};\n        std::array<int, detectorPathCount> pathFamilyCoordinateAgeHops_ {};\n        std::array<float, detectorPathCount> pathFamilyPendingHz_ {};\n        std::array<int, detectorPathCount> pathFamilyPendingCount_ {};\n\n        std::array<float, maxAnalysisSize> frame_ {};\n''',
        'path family state fields')

    cpp = one(
        cpp,
        '''    fullRateCandidate_ = {};\n    halfRateCandidate_ = {};\n    quarterRateCandidate_ = {};\n    eighthRateCandidate_ = {};\n    decoderBeam_.fill({});\n''',
        '''    fullRateCandidate_ = {};\n    halfRateCandidate_ = {};\n    quarterRateCandidate_ = {};\n    eighthRateCandidate_ = {};\n    pathFamilyCoordinateHz_.fill(0.0f);\n    pathFamilyCoordinateAgeHops_.fill(1000);\n    pathFamilyPendingHz_.fill(0.0f);\n    pathFamilyPendingCount_.fill(0);\n    decoderBeam_.fill({});\n''',
        'reset path family state')

    cpp = one(
        cpp,
        '''    fullRateCandidate_ = {};\n    halfRateCandidate_ = {};\n    quarterRateCandidate_ = {};\n    eighthRateCandidate_ = {};\n    halfRateAntiAlias_.reset();\n''',
        '''    fullRateCandidate_ = {};\n    halfRateCandidate_ = {};\n    quarterRateCandidate_ = {};\n    eighthRateCandidate_ = {};\n    pathFamilyCoordinateHz_.fill(0.0f);\n    pathFamilyCoordinateAgeHops_.fill(1000);\n    pathFamilyPendingHz_.fill(0.0f);\n    pathFamilyPendingCount_.fill(0);\n    halfRateAntiAlias_.reset();\n''',
        'clear path family state')

    family_state_anchor = '''    const auto lowerAliasShadowedV6 = [&](int sourceIndex) noexcept\n'''
    family_state = '''    // PATH_LOCAL_FAMILY_PERSISTENCE_V6_5\n    // Track only strong native measurements and only for a very short physical\n    // window. The old coordinate is never rewritten as age==0: this state can\n    // withhold V6.1 fast-path privilege, but cannot fabricate a fresh F0.\n    constexpr int pathFamilyMaximumAgeHopsV65 = 12;\n    constexpr int pathFamilyRequiredFreshUpdatesV65 = 3;\n    std::array<bool, detectorPathCount> familyTransitionPendingV65 {};\n\n    for (int path = 0; path < detectorPathCount; ++path)\n        pathFamilyCoordinateAgeHops_[static_cast<std::size_t>(path)] = std::min(\n            1000, pathFamilyCoordinateAgeHops_[static_cast<std::size_t>(path)] + 1);\n\n    const auto simplePathFamilyJumpV65 = [](float aHz, float bHz) noexcept\n    {\n        if (!(aHz > 0.0f) || !(bHz > 0.0f)\n            || !std::isfinite(aHz) || !std::isfinite(bHz))\n        {\n            return false;\n        }\n        const float cents = std::abs(1200.0f * std::log2(aHz / bHz));\n        constexpr float toleranceCents = 70.0f;\n        constexpr std::array<float, 3> families {\n            1200.0f, 1901.955001f, 2400.0f\n        };\n        for (const float familyCents : families)\n            if (std::abs(cents - familyCents) <= toleranceCents)\n                return true;\n        return false;\n    };\n\n    for (int index = 0; index < candidateCount; ++index)\n    {\n        const auto& candidate = candidates[static_cast<std::size_t>(index)];\n        if (candidate.ageInHops != 0\n            || !structurallyAuthoritativeV6(candidate)\n            || candidate.pathIndex < 0\n            || candidate.pathIndex >= detectorPathCount)\n        {\n            continue;\n        }\n\n        const int path = candidate.pathIndex;\n        const std::size_t pathSlot = static_cast<std::size_t>(path);\n        if (candidate.frequencyHz > directMaximumForPathV6(path) + 5.0f)\n            continue;\n\n        float& ownedHz = pathFamilyCoordinateHz_[pathSlot];\n        int& ownedAge = pathFamilyCoordinateAgeHops_[pathSlot];\n        float& pendingHz = pathFamilyPendingHz_[pathSlot];\n        int& pendingCount = pathFamilyPendingCount_[pathSlot];\n\n        if (!(ownedHz > 0.0f)\n            || !std::isfinite(ownedHz)\n            || ownedAge > pathFamilyMaximumAgeHopsV65)\n        {\n            ownedHz = candidate.frequencyHz;\n            ownedAge = 0;\n            pendingHz = 0.0f;\n            pendingCount = 0;\n            continue;\n        }\n\n        if (centsDistance(candidate.frequencyHz, ownedHz) <= 70.0f)\n        {\n            ownedHz = candidate.frequencyHz;\n            ownedAge = 0;\n            pendingHz = 0.0f;\n            pendingCount = 0;\n            continue;\n        }\n\n        if (!simplePathFamilyJumpV65(candidate.frequencyHz, ownedHz))\n        {\n            // Ordinary melodic movement is not a harmonic-family ambiguity.\n            ownedHz = candidate.frequencyHz;\n            ownedAge = 0;\n            pendingHz = 0.0f;\n            pendingCount = 0;\n            continue;\n        }\n\n        if (pendingHz > 0.0f\n            && centsDistance(candidate.frequencyHz, pendingHz) <= 70.0f)\n        {\n            ++pendingCount;\n        }\n        else\n        {\n            pendingHz = candidate.frequencyHz;\n            pendingCount = 1;\n        }\n\n        if (pendingCount >= pathFamilyRequiredFreshUpdatesV65)\n        {\n            // A persistent family transition is a real current measurement, not\n            // an error to suppress. Commit it and return to normal V6.1 speed.\n            ownedHz = candidate.frequencyHz;\n            ownedAge = 0;\n            pendingHz = 0.0f;\n            pendingCount = 0;\n        }\n        else\n        {\n            familyTransitionPendingV65[pathSlot] = true;\n        }\n    }\n\n    const auto lowerAliasShadowedV6 = [&](int sourceIndex) noexcept\n'''
    cpp = one(cpp, family_state_anchor, family_state, 'insert path family state')

    shadow_anchor = '''            if (centsDistance(other.frequencyHz, upperHz) <= 85.0f)\n                return true;\n        }\n        return false;\n    };\n'''
    shadow_replacement = '''            if (centsDistance(other.frequencyHz, upperHz) <= 85.0f)\n                return true;\n        }\n\n        // PATH_LOCAL_FAMILY_PERSISTENCE_V6_5\n        // If a path capable of the upper coordinate measured it moments ago and\n        // is currently in a bounded family-transition ambiguity toward this\n        // lower coordinate, the range-limited path is family evidence only.\n        // This is the 440/220 case: eighth-rate 220 cannot own 440's register\n        // while quarter-rate is resolving one brief 440->220 family collapse.\n        for (int path = 0; path < detectorPathCount; ++path)\n        {\n            if (path == source.pathIndex\n                || upperHz > directMaximumForPathV6(path) + 5.0f)\n            {\n                continue;\n            }\n            const std::size_t pathSlot = static_cast<std::size_t>(path);\n            if (pathFamilyPendingCount_[pathSlot] <= 0\n                || pathFamilyCoordinateAgeHops_[pathSlot] > pathFamilyMaximumAgeHopsV65\n                || !(pathFamilyCoordinateHz_[pathSlot] > 0.0f)\n                || !(pathFamilyPendingHz_[pathSlot] > 0.0f))\n            {\n                continue;\n            }\n            if (centsDistance(pathFamilyCoordinateHz_[pathSlot], upperHz) <= 70.0f\n                && centsDistance(pathFamilyPendingHz_[pathSlot], source.frequencyHz) <= 85.0f)\n            {\n                return true;\n            }\n        }\n        return false;\n    };\n'''
    cpp = one(cpp, shadow_anchor, shadow_replacement, 'bounded pending family shadow')

    authority_anchor = '''        if (candidate.ageInHops != 0\n            || !structurallyAuthoritativeV6(candidate)\n            || lowerAliasShadowedV6(index))\n'''
    authority_replacement = '''        if (candidate.ageInHops != 0\n            || !structurallyAuthoritativeV6(candidate)\n            || (candidate.pathIndex >= 0\n                && candidate.pathIndex < detectorPathCount\n                && familyTransitionPendingV65[static_cast<std::size_t>(candidate.pathIndex)])\n            || lowerAliasShadowedV6(index))\n'''
    cpp = one(cpp, authority_anchor, authority_replacement, 'withhold solitary pending family fast path')

    cpp_path.write_text(cpp)
    h_path.write_text(h)

print('PATH_LOCAL_FAMILY_PERSISTENCE_V6_5 materialized')
