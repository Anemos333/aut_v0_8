#include <JuceHeader.h>

#define private public
#include "ModernPitchEngine.h"
#undef private

#include <array>
#include <cmath>
#include <iostream>

namespace
{
bool check(bool condition, const char* name)
{
    std::cerr << name << '=' << (condition ? "PASS" : "FAIL") << '\n';
    return condition;
}

ModernPitchEngine::PitchObservation pitch(float frequency)
{
    ModernPitchEngine::PitchObservation observation;
    observation.valid = true;
    observation.audioPresent = true;
    observation.frequencyHz = frequency;
    observation.correctionFrequencyHz = frequency;
    observation.confidence = 0.95f;
    observation.periodicity = 0.95f;
    observation.consensus = 0.88f;
    observation.voicing = 1.0f;
    return observation;
}
}

int main()
{
    bool success = true;
    ModernPitchEngine engine;
    engine.prepare(48000.0, 512, 1, ModernPitchEngine::LatencyMode::live);

    const double unison = 1.0;
    ModernPitchEngine::ScaleQuantizer quantizer;
    quantizer.reset();
    quantizer.setScale(&unison, 1, 440.0);

    ModernPitchEngine::Parameters hard;
    hard.amount = 1.0f;
    hard.retuneTimeMs = 0.0f;
    hard.humanize = 0.0f;
    hard.preserveVibrato = 0.0f;
    hard.scaleLock = true;
    hard.lockHysteresis = 0.0f;
    hard.vibratoPreserve = 0.0f;
    hard.lockStrictness = 1.0f;      // must not add hidden prudence
    hard.hardLockActive = true;     // must not select a second law
    hard.maximumCorrectionSemitones = 24.0f;
    hard.tempo.mode = CreativeTempo::Mode::off;

    auto observation = pitch(452.0f);
    ModernPitchEngine::CorrectionState state;
    engine.updateCorrectionState(state, quantizer, observation, hard);
    const double targetHz = std::exp2(state.targetLog2);
    const double expected = 1200.0 * std::log2(targetHz / 452.0);

    success &= check(state.targetValid
                     && state.trackingState == ModernPitchEngine::TrackingState::stable,
                     "signal_with_target_is_immediately_voiced_stable");
    success &= check(std::abs(state.desiredCents - expected) < 1.0e-9,
                     "amount100_humanize0_vibrato0_requests_exact_target");
    success &= check(state.responseMs >= 0.349 && state.responseMs <= 0.351,
                     "response_zero_is_fastest_finite_glide");

    const double firstGlide = engine.advanceCorrection(state);
    success &= check(std::abs(firstGlide - state.desiredCents) > 1.0e-6
                     && std::abs(firstGlide) > 0.0,
                     "response_zero_does_not_bypass_glide");
    for (int sample = 0; sample < 4800; ++sample)
        static_cast<void>(engine.advanceCorrection(state));
    success &= check(state.currentCents == state.desiredCents,
                     "glide_converges_exactly_to_requested_target");

    // Low confidence/consensus describes measurement quality; once the tracker
    // publishes a valid F0 it cannot reduce correction authority or voice state.
    auto weak = pitch(452.0f);
    weak.confidence = 0.01f;
    weak.periodicity = 0.05f;
    weak.consensus = 0.0f;
    ModernPitchEngine::CorrectionState weakState;
    ModernPitchEngine::ScaleQuantizer weakQuantizer;
    weakQuantizer.reset();
    weakQuantizer.setScale(&unison, 1, 440.0);
    engine.updateCorrectionState(weakState, weakQuantizer, weak, hard);
    success &= check(weakState.trackingState == ModernPitchEngine::TrackingState::stable
                     && std::abs(weakState.desiredCents - expected) < 1.0e-9,
                     "weak_evidence_cannot_reduce_valid_pitch_authority");

    // Consonant / transient hole: audible signal remains the same voice and the
    // already selected musical destination is held without falling to acquire.
    ModernPitchEngine::PitchObservation consonant;
    consonant.audioPresent = true;
    consonant.voicing = 1.0f;
    const double held = weakState.desiredCents;
    engine.updateCorrectionState(weakState, weakQuantizer, consonant, hard);
    success &= check(weakState.trackingState == ModernPitchEngine::TrackingState::stable
                     && weakState.targetValid
                     && std::abs(weakState.desiredCents - held) < 1.0e-12,
                     "audible_consonant_keeps_voiced_target_and_correction");

    // Hold owns target identity by itself. Even hostile hidden confidence and
    // hard-lock values cannot require repeated confirmation after Hold is crossed.
    std::array<double, 12> chromatic {};
    for (int degree = 0; degree < 12; ++degree)
        chromatic[static_cast<std::size_t>(degree)] = std::exp2(degree / 12.0);
    ModernPitchEngine::ScaleQuantizer targetQuantizer;
    targetQuantizer.reset();
    targetQuantizer.setScale(chromatic.data(), static_cast<int>(chromatic.size()), 440.0);
    int pending = 99;
    const double a4 = std::log2(440.0);
    static_cast<void>(targetQuantizer.chooseTargetLog2(
        a4, 0.0f, 1.0f, 0.0f, true, false, pending));
    const double as4 = std::log2(466.1637615180899);
    const double movedTarget = targetQuantizer.chooseTargetLog2(
        as4, 0.0f, 1.0f, 0.0f, true, false, pending);
    success &= check(std::abs((movedTarget - as4) * 1200.0) < 0.001
                     && pending == 0,
                     "hold_crossing_changes_degree_without_confidence_confirmation");

    success &= check(std::abs(engine.adaptiveHysteresis(hard, targetQuantizer, weak)) < 1.0e-12,
                     "hold_zero_is_literally_zero_for_every_evidence_level");

    // Amount is ordinary scaling of the same target error, not another path.
    ModernPitchEngine::Parameters half = hard;
    half.amount = 0.5f;
    ModernPitchEngine::CorrectionState halfState;
    ModernPitchEngine::ScaleQuantizer halfQuantizer;
    halfQuantizer.reset();
    halfQuantizer.setScale(&unison, 1, 440.0);
    engine.updateCorrectionState(halfState, halfQuantizer, observation, half);
    success &= check(std::abs(halfState.desiredCents - 0.5 * expected) < 1.0e-9,
                     "amount_softens_same_authoritative_target_law");

    return success ? 0 : 1;
}
