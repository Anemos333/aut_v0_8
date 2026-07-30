from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
text = path.read_text()


def replace_range(source: str, start_marker: str, end_marker: str, replacement: str,
                  start_at: int = 0) -> str:
    start = source.index(start_marker, start_at)
    end = source.index(end_marker, start)
    return source[:start] + replacement + source[end:]


def function_slice(source: str, signature: str, next_signature: str) -> tuple[int, int, str]:
    start = source.index(signature)
    end = source.index(next_signature, start)
    return start, end, source[start:end]


# Amount is the maximum permitted residual pitch error. Confidence and
# consensus describe the detector but never widen the musical contract.
text = replace_range(
    text,
    "[[nodiscard]] double neumatonApplyAmountToleranceGate",
    "[[nodiscard]] float retuneFloorForLatencyMode",
    '''[[nodiscard]] double neumatonApplyAmountToleranceGate(double errorCents,
                                                       float amount01,
                                                       float confidence,
                                                       float consensus) noexcept
{
    static_cast<void>(confidence);
    static_cast<void>(consensus);

    const float safeAmount = std::clamp(amount01, 0.0f, 1.0f);
    if (safeAmount <= 0.0001f || !std::isfinite(errorCents))
        return 0.0;

    const double forgiveness = 1.0 - static_cast<double>(safeAmount);
    const double toleranceCents = 0.5 + 49.5 * forgiveness * forgiveness;
    const double magnitude = std::abs(errorCents);
    if (magnitude <= toleranceCents)
        return 0.0;

    return std::copysign(magnitude - toleranceCents, errorCents);
}

''')

# Speed may be zero in every latency mode. It is attack delay, not a
# latency-dependent correction-time floor.
retune_start = text.index("[[nodiscard]] float retuneFloorForLatencyMode")
retune_end = text.index("[[nodiscard]] double wrapCorrectionToNearestOctave", retune_start)
text = text[:retune_start] + '''[[nodiscard]] float retuneFloorForLatencyMode(
    ModernPitchEngine::LatencyMode mode) noexcept
{
    static_cast<void>(mode);
    return 0.0f;
}

''' + text[retune_end:]

text = text.replace(
    "1.0 - std::exp(-1.0 / (0.006 * sampleRate_))",
    "1.0 - std::exp(-1.0 / (0.0008 * sampleRate_))",
    1,
)
text = text.replace(
    "1.0 - std::exp(-1.0 / (0.010 * sampleRate_))",
    "1.0 - std::exp(-1.0 / (0.0008 * sampleRate_))",
    1,
)

accept_sig = "void ModernPitchEngine::CorrectionController::acceptObservation("
advance_sig = "void ModernPitchEngine::CorrectionController::advanceOneSample("
a_start, a_end, accept = function_slice(text, accept_sig, advance_sig)

# Speed: exact delay from a note attack before correction begins.
attack_start = accept.index("    if (observation.onset)\n")
invalid_start = accept.index("    if (!observationUsable)\n", attack_start)
accept = accept[:attack_start] + '''    if (observation.onset)
    {
        const double attackDelayMs = std::clamp(
            static_cast<double>(parameters.retuneTimeMs), 0.0, 500.0);
        enterState(TrackingState::attack,
                   std::max(1, static_cast<int>(std::lround(
                       attackDelayMs * 0.001 * sampleRate_))));
        stableObservationCount_ = 0;
        invalidObservationCount_ = 0;
        pitchCentreValid_ = false;
        targetValid_ = false;
        quantizer.resetTarget();
    }

''' + accept[invalid_start:]

# Consonants, breaths and detector gaps hold the last musical ratio. They
# never return to ratio one and never open a dry path.
invalid_start = accept.index("    if (!observationUsable)\n")
invalid_end = accept.index("    invalidObservationCount_ = 0;", invalid_start)
accept = accept[:invalid_start] + '''    if (!observationUsable)
    {
        ++invalidObservationCount_;
        stableObservationCount_ = 0;
        if (targetValid_)
        {
            authorityTarget_ = 1.0f;
            wetMixTarget_ = 1.0f;
        }
        return;
    }

''' + accept[invalid_end:]

# Humanize only decides whether neighbouring observations are the same sung
# note. It cannot alter the exact output target.
centre_start = accept.index("    if (!pitchCentreValid_ || observation.onset)\n")
centre_end_marker = "    double targetBoundaryCents = hysteresisCents;\n"
centre_end = accept.index(centre_end_marker, centre_start) + len(centre_end_marker)
accept = accept[:centre_start] + '''    const double minStepCents = sanitisedMinStepCents(parameters);
    const double sameNoteBandCents = 3.0
        + static_cast<double>(clamp01(parameters.humanize))
            * std::min(60.0, 0.45 * minStepCents);

    if (!pitchCentreValid_ || observation.onset)
    {
        pitchCentreLog2_ = observedLog2_;
        pitchCentreValid_ = true;
    }
    else
    {
        const double distanceCents = std::abs(
            (observedLog2_ - pitchCentreLog2_) * 1200.0);
        const double alpha = distanceCents <= sameNoteBandCents
            ? 0.18 - 0.15 * static_cast<double>(clamp01(parameters.humanize))
            : 0.45;
        pitchCentreLog2_ += alpha * (observedLog2_ - pitchCentreLog2_);
    }

    double hysteresisCents = sameNoteBandCents;
    double targetBoundaryCents = hysteresisCents;
''' + accept[centre_end:]

# Preserve quantizer, Scale Lock and octave decision. Replace only the later
# correction branch: the output centre is always the exact selected target.
target_assignment = "    targetLog2_ = newTargetLog2;\n    targetValid_ = true;\n"
correction_search = accept.index(target_assignment) + len(target_assignment)
correction_start = accept.index("    if (parameters.scaleLock)\n", correction_search)
correction_end = accept.index("    const float confidenceGate", correction_start)
accept = accept[:correction_start] + '''    double errorCents = (targetLog2_ - observedLog2_) * 1200.0;
    errorCents = wrapCorrectionToNearestOctave(errorCents);
    const double maxCorrectionCents = 1200.0 * std::clamp(
        static_cast<double>(parameters.maximumCorrectionSemitones), 0.0, 24.0);
    errorCents = std::clamp(errorCents,
                            -maxCorrectionCents,
                            maxCorrectionCents);
    desiredCorrectionCents_ = neumatonApplyAmountToleranceGate(
        errorCents,
        parameters.amount,
        currentConfidence_,
        observation.consensus);

''' + accept[correction_end:]

# Confidence, breath, polyphony and transient evidence guide reconstruction,
# never permission to miss the target.
authority_start = accept.index("    const float confidenceGate")
authority_end_marker = "    ++stableObservationCount_;"
authority_end = accept.index(authority_end_marker, authority_start) + len(authority_end_marker)
accept = accept[:authority_start] + '''    authorityTarget_ = targetValid_ ? 1.0f : authorityTarget_;
    wetMixTarget_ = targetValid_ ? 1.0f : wetMixTarget_;

    ++stableObservationCount_;''' + accept[authority_end:]
text = text[:a_start] + accept + text[a_end:]

# The attack state is the only intentional delay. Afterwards use a fixed
# one-millisecond click-safe servo.
ad_start, ad_end, advance = function_slice(
    text,
    advance_sig,
    "double ModernPitchEngine::CorrectionController::getPitchRatio() const noexcept",
)
brace = advance.index("{\n") + 2
advance = advance[:brace] + "    static_cast<void>(parameters);\n" + advance[brace:]
advance = advance.replace(
    '''if (state_ == TrackingState::attack)
                enterState(TrackingState::acquire,
                           std::max(1, static_cast<int>(std::lround(0.007 * sampleRate_))));''',
    '''if (state_ == TrackingState::attack)
                enterState(TrackingState::stable);''',
    1,
)
state_start = advance.index("    float stateAuthorityScale = 1.0f;\n")
state_end = advance.index("    const float effectiveAuthorityTarget", state_start)
advance = advance[:state_start] + '''    float stateAuthorityScale = targetValid_ ? 1.0f : 0.0f;
    if (state_ == TrackingState::attack)
        stateAuthorityScale = 0.0f;

''' + advance[state_end:]
response_start = advance.index("    double responseMs = ")
response_end = advance.index("    const double dt = 1.0 / sampleRate_;", response_start)
advance = advance[:response_start] + "    const double responseMs = 1.0;\n" + advance[response_end:]
text = text[:ad_start] + advance + text[ad_end:]

# Main audio block: one trajectory, direct formant request and complete Mid/Side
# processing. The legacy TransitionManager cannot create two audible paths.
process_sig = '''void ModernPitchEngine::process(juce::AudioBuffer<float>& buffer,
                                 const double* scaleRatios,
                                 int numberOfScaleRatios,
                                 double rootFrequency,
                                 const Parameters& parameters,
                                 const CreativeTempo::HostPosition& hostTempoPosition)'''
next_process_sig = '''void ModernPitchEngine::process(juce::AudioBuffer<float>& buffer,
                                 const std::vector<double>& scaleRatios'''
p_start, p_end, process = function_slice(text, process_sig, next_process_sig)

transition_start = process.index("        const auto transition = transitionManager_.processSample(")
transition_end_marker = "            tempoDecision.forceTransition);"
transition_end = process.index(transition_end_marker, transition_start) + len(transition_end_marker)
process = process[:transition_start] + '''        TransitionManager::Command transition;
        transition.primaryCents = tempoDecision.destinationCents;
        transition.secondaryCents = tempoDecision.destinationCents;
        transition.blend = 0.0f;
        transition.dualSynthesis = false;
        transition.beginSecondary = false;
        transition.commitSecondary = false;''' + process[transition_end:]

formant_old = '''        const float formant = clamp01(safeParameters.formantPreservation
            * correctionController_.getFormantStability());'''
if formant_old not in process:
    raise RuntimeError("formant boundary not found")
process = process.replace(
    formant_old,
    "        const float formant = clamp01(safeParameters.formantPreservation);",
    1,
)

mid_start = process.index("            const float processedMid = shifters_[0].processSample(mid,")
mid_end_marker = "            channelData[1][sampleIndex] = processedMid - delayedSide;"
mid_end = process.index(mid_end_marker, mid_start) + len(mid_end_marker)
process = process[:mid_start] + '''            const float processedMid = shifters_[0].processSample(mid,
                                                                  transition,
                                                                  1.0f,
                                                                  formant,
                                                                  harmonicNoiseContext,
                                                                  forcePhaseReset);
            const float processedSide = shifters_[1].processSample(side,
                                                                   transition,
                                                                   1.0f,
                                                                   formant,
                                                                   harmonicNoiseContext,
                                                                   forcePhaseReset);
            channelData[0][sampleIndex] = processedMid + processedSide;
            channelData[1][sampleIndex] = processedMid - processedSide;''' + process[mid_end:]

if "const int meteredShifters = useMidSide ? 1 : numberOfChannels;" not in process:
    raise RuntimeError("metered shifter boundary not found")
process = process.replace(
    "const int meteredShifters = useMidSide ? 1 : numberOfChannels;",
    "const int meteredShifters = useMidSide ? 2 : numberOfChannels;",
    1,
)
text = text[:p_start] + process + text[p_end:]

path.write_text(text)
print("scale-cage patch applied")
