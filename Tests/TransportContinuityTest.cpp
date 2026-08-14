#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace
{
bool check(bool condition, const std::string& name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

[[nodiscard]] double effectiveDelay(const ModernPitchEngine::TransportPlan& plan)
{
    return static_cast<double>(plan.gainA) * plan.delayA
         + static_cast<double>(plan.gainB) * plan.delayB;
}
} // namespace

int main()
{
    bool success = true;
    ModernPitchEngine::TransportClock clock;
    clock.prepare(256);

    for (int sample = 0; sample < 6000; ++sample)
        static_cast<void>(clock.next(std::exp2(18.0 / 1200.0)));

    const double phaseBeforeUnity = clock.phase_;
    const auto unity = clock.next(1.0);
    success &= check(std::abs(clock.phase_ - phaseBeforeUnity) < 1.0e-12,
                     "unity_does_not_reset_transport_phase");
    success &= check(std::abs(unity.delayA - 256.0) < 1.0e-9
                  && std::abs(unity.delayB - 256.0) < 1.0e-9,
                     "unity_collapses_both_taps_to_reported_latency");
    success &= check(std::abs((unity.gainA + unity.gainB) - 1.0f) < 1.0e-6f,
                     "unity_gain_is_normalised");

    ModernPitchEngine::TransportClock crossing;
    crossing.prepare(256);
    double previousEffectiveDelay = 0.0;
    ModernPitchEngine::TransportPlan previousPlan;
    bool havePrevious = false;
    double maximumEffectiveJump = 0.0;
    double maximumAudibleHeadJump = 0.0;

    constexpr int trajectorySamples = 24000;
    for (int sample = 0; sample < trajectorySamples; ++sample)
    {
        const double t = static_cast<double>(sample)
                       / static_cast<double>(trajectorySamples - 1);
        const double cents = 20.0 - 40.0 * t;
        const double ratio = std::exp2(cents / 1200.0);
        const auto plan = crossing.next(ratio);

        if (!(std::isfinite(plan.delayA)
           && std::isfinite(plan.delayB)
           && std::isfinite(plan.gainA)
           && std::isfinite(plan.gainB)))
        {
            success &= check(false, "transport_plan_is_finite");
            return 1;
        }

        const double currentEffectiveDelay = effectiveDelay(plan);
        if (havePrevious)
        {
            maximumEffectiveJump = std::max(
                maximumEffectiveJump,
                std::abs(currentEffectiveDelay - previousEffectiveDelay));

            const double headAJump = std::abs(plan.delayA - previousPlan.delayA)
                * std::min(static_cast<double>(plan.gainA),
                           static_cast<double>(previousPlan.gainA));
            const double headBJump = std::abs(plan.delayB - previousPlan.delayB)
                * std::min(static_cast<double>(plan.gainB),
                           static_cast<double>(previousPlan.gainB));
            maximumAudibleHeadJump = std::max(
                maximumAudibleHeadJump, std::max(headAJump, headBJump));
        }

        previousEffectiveDelay = currentEffectiveDelay;
        previousPlan = plan;
        havePrevious = true;
    }

    success &= check(true, "transport_plan_is_finite");
    std::cerr << "maximum_effective_delay_jump=" << maximumEffectiveJump << '\n';
    std::cerr << "maximum_audible_head_jump=" << maximumAudibleHeadJump << '\n';
    success &= check(maximumEffectiveJump < 0.10,
                     "effective_read_position_is_continuous_through_unity");
    success &= check(maximumAudibleHeadJump < 0.15,
                     "weighted_head_motion_is_continuous_through_unity");

    ModernPitchEngine::TransportClock direction;
    direction.prepare(256);
    const double startPhase = direction.phase_;
    static_cast<void>(direction.next(std::exp2(20.0 / 1200.0)));
    const double upwardCorrectionPhase = direction.phase_;
    static_cast<void>(direction.next(std::exp2(-20.0 / 1200.0)));
    const double returnPhase = direction.phase_;
    success &= check(upwardCorrectionPhase < startPhase,
                     "positive_ratio_moves_phase_backward");
    success &= check(returnPhase > upwardCorrectionPhase,
                     "negative_ratio_reverses_phase_without_geometry_mirror");

    return success ? 0 : 1;
}
