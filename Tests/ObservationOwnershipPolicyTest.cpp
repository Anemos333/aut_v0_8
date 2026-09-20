#include "../Source/ObservationOwnershipPolicy.h"

#include <array>
#include <cstdint>
#include <iostream>

int main()
{
    using Policy = ObservationOwnershipPolicy;

    struct Case
    {
        Policy::Evidence evidence;
        bool accept;
        std::uint8_t reasons;
    };

    constexpr std::array<Case, 8> cases {{
        {{false, false, false}, true,  Policy::none},
        {{true,  false, false}, false, Policy::terminalTail},
        {{false, true,  false}, false, Policy::provisionalOctave},
        {{false, false, true }, false, Policy::uncommittedLargeInnovation},
        {{true,  true,  false}, false,
          static_cast<std::uint8_t>(Policy::terminalTail | Policy::provisionalOctave)},
        {{true,  false, true }, false,
          static_cast<std::uint8_t>(Policy::terminalTail | Policy::uncommittedLargeInnovation)},
        {{false, true,  true }, false,
          static_cast<std::uint8_t>(Policy::provisionalOctave | Policy::uncommittedLargeInnovation)},
        {{true,  true,  true }, false,
          static_cast<std::uint8_t>(Policy::terminalTail
                                  | Policy::provisionalOctave
                                  | Policy::uncommittedLargeInnovation)}
    }};

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        const auto decision = Policy::evaluate(cases[index].evidence);
        if (decision.acceptCurrentObservation != cases[index].accept
            || decision.holdReasonMask != cases[index].reasons)
        {
            std::cerr << "OBSERVATION_OWNERSHIP_POLICY=FAIL case=" << index
                      << " accept=" << decision.acceptCurrentObservation
                      << " reasons=" << static_cast<int>(decision.holdReasonMask)
                      << "\n";
            return 2;
        }
    }

    std::cout << "OBSERVATION_OWNERSHIP_POLICY=PASS cases="
              << cases.size() << "\n";
    return 0;
}
