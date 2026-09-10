import re
from pathlib import Path

CPP = Path('Source/ModernPitchEngine.cpp')
HDR = Path('Source/ModernPitchEngine.h')


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one occurrence, got {count}')
    return text.replace(old, new, 1)


def regex_once(text: str, pattern: str, replacement: str, label: str) -> str:
    out, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f'{label}: expected exactly one regex match, got {count}')
    return out


hdr = HDR.read_text(encoding='utf-8')
hdr = replace_once(
    hdr,
    '        void setImmediateAuthority(bool enabled) noexcept { immediateAuthority_ = enabled; } // AUTHORITY_CONTROLS_EXPLICIT_V1\n',
    '',
    'remove detector history-authority setter')
hdr = replace_once(
    hdr,
    '        bool immediateAuthority_ = false; // AUTHORITY_CONTROLS_EXPLICIT_V1\n',
    '',
    'remove detector history-authority state')
hdr = replace_once(
    hdr,
    '        bool pendingValid_ = false;\n        double pendingLog2_ = 0.0;\n        int pendingCount_ = 0;\n',
    '',
    'remove temporal target-confirmation state')
hdr = replace_once(
    hdr,
    '    [[nodiscard]] static bool zeroPrudenceAuthority(const Parameters& parameters) noexcept;\n',
    '',
    'remove zero-prudence special endpoint')
HDR.write_text(hdr, encoding='utf-8')

cpp = CPP.read_text(encoding='utf-8')
cpp = replace_once(
    cpp,
    '    immediateAuthority_ = false; // AUTHORITY_CONTROLS_EXPLICIT_V1\n',
    '',
    'remove history-authority reset')

# Current detector evidence may be fused spatially, but history can no longer
# create held branches or charge a temporal price before current evidence wins.
cpp = regex_once(
    cpp,
    r'void ModernPitchEngine::MultiRatePitchTracker::updateDecoderBeam\(.*?\n\}\n\nModernPitchEngine::MultiRatePitchTracker::DecoderDecision',
    '''void ModernPitchEngine::MultiRatePitchTracker::updateDecoderBeam(
    const std::array<ConsensusHypothesis, maxConsensusHypotheses>& hypotheses,
    int hypothesisCount,
    bool onsetPending) noexcept
{
    (void) onsetPending;

    // MONOTONIC_TARGET_AUTHORITY_V9: the decoder is a CURRENT-EVIDENCE
    // selector. Previous beams, hold branches, continuity bonuses and temporal
    // penalties are forbidden because they can make valid live evidence wait.
    std::array<DecoderState, maxConsensusHypotheses> proposals {};
    int proposalCount = 0;
    for (int index = 0; index < hypothesisCount
         && proposalCount < maxConsensusHypotheses; ++index)
    {
        const auto& hypothesis = hypotheses[static_cast<std::size_t>(index)];
        if (!hypothesis.valid)
            continue;

        DecoderState proposal;
        proposal.valid = true;
        proposal.logFrequency = safeLog2(hypothesis.frequencyHz);
        proposal.score = hypothesis.evidenceScore + 0.26f * hypothesis.consensus;
        proposal.ageInHops = 0;
        proposal.octaveIndex = octaveState_;
        proposals[static_cast<std::size_t>(proposalCount++)] = proposal;
    }

    std::sort(proposals.begin(), proposals.begin() + proposalCount,
              [](const DecoderState& left, const DecoderState& right)
              {
                  return left.score > right.score;
              });

    decoderBeam_.fill({});
    int accepted = 0;
    for (int proposalIndex = 0;
         proposalIndex < proposalCount && accepted < decoderBeamWidth;
         ++proposalIndex)
    {
        const auto& proposal = proposals[static_cast<std::size_t>(proposalIndex)];
        bool duplicate = false;
        for (int existing = 0; existing < accepted; ++existing)
        {
            const float distance = static_cast<float>(1200.0
                * std::abs(proposal.logFrequency
                         - decoderBeam_[static_cast<std::size_t>(existing)].logFrequency));
            if (distance < 24.0f)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            decoderBeam_[static_cast<std::size_t>(accepted++)] = proposal;
    }
}

ModernPitchEngine::MultiRatePitchTracker::DecoderDecision''',
    'replace history decoder with current-evidence decoder')

# Octave logic may identify a register, but it may never wait N hops, hold the
# old pitch, or rewrite a challenger back to trackedPitchHz_.
cpp = regex_once(
    cpp,
    r'bool ModernPitchEngine::MultiRatePitchTracker::confirmOctaveTransition\(.*?\n\}\n\nbool ModernPitchEngine::MultiRatePitchTracker::processSample',
    '''bool ModernPitchEngine::MultiRatePitchTracker::confirmOctaveTransition(
    DecoderDecision& decision,
    bool onsetPending) noexcept
{
    (void) onsetPending;

    if (!decision.valid
        || !decision.candidate.valid
        || !std::isfinite(decision.candidate.frequencyHz)
        || decision.candidate.frequencyHz <= 0.0f)
    {
        pendingOctaveDelta_ = 0;
        pendingOctaveCount_ = 0;
        pendingOctaveFrequencyHz_ = 0.0f;
        decision.valid = false;
        return false;
    }

    // MONOTONIC_TARGET_AUTHORITY_V9: octave transitions are committed from the
    // current accepted F0. There is no confirmation timer and no stale-pitch
    // substitution. Uncertainty is handled by the continuous correction
    // trajectory after target selection, never by waiting at the source pitch.
    if (trackedPitchHz_ > 0.0f)
    {
        int octaveDelta = 0;
        float residualCents = 0.0f;
        if (isOctaveLikeTransition(trackedPitchHz_,
                                   decision.candidate.frequencyHz,
                                   octaveDelta,
                                   residualCents))
        {
            octaveState_ = std::clamp(octaveState_ + octaveDelta, -4, 4);
        }
    }

    committedOctaveFrequencyHz_ = decision.candidate.frequencyHz;
    octaveCommitGuardHops_ = 0;
    pendingOctaveDelta_ = 0;
    pendingOctaveCount_ = 0;
    pendingOctaveFrequencyHz_ = 0.0f;
    decision.decoderOctaveIndex = octaveState_;
    decision.valid = true;
    return true;
}

bool ModernPitchEngine::MultiRatePitchTracker::processSample''',
    'remove octave confirmation timers')

cpp = replace_once(
    cpp,
    '''        observation.correctionFrequencyHz = presenceMode_
            && std::isfinite(decision.candidate.frequencyHz)
            && decision.candidate.frequencyHz > 0.0f
            ? decision.candidate.frequencyHz
            : trackedPitchHz_;
''',
    '''        // MONOTONIC_TARGET_AUTHORITY_V9: correction always sees the
        // freshest accepted F0. trackedPitchHz_ is continuity/metering only.
        observation.correctionFrequencyHz =
            std::isfinite(decision.candidate.frequencyHz)
            && decision.candidate.frequencyHz > 0.0f
            ? decision.candidate.frequencyHz
            : trackedPitchHz_;
''',
    'make fresh correction coordinate universal')

# Quantizer hysteresis is spatial only. Once a challenger wins the explicit
# hysteresis margin, it becomes the target immediately; no pending observations.
cpp = regex_once(
    cpp,
    r'double ModernPitchEngine::ScaleQuantizer::chooseTargetLog2\(.*?\n\}\n\n//==============================================================================\n// ModernPitchEngine control and processing',
    '''double ModernPitchEngine::ScaleQuantizer::chooseTargetLog2(
    double inputLog2,
    float hysteresisCents,
    float strictness,
    float confidence,
    bool hardLock,
    bool onset,
    int& pendingObservations) noexcept
{
    (void) strictness;
    (void) confidence;
    (void) hardLock;
    pendingObservations = 0;
    if (!std::isfinite(inputLog2) || ratioCount_ <= 0)
        return inputLog2;

    const double relative = inputLog2 - rootLog2_;
    const double octave = std::floor(relative);
    double nearest = inputLog2;
    double nearestDistance = std::numeric_limits<double>::infinity();
    for (int i = 0; i < ratioCount_; ++i)
    {
        const double degree = logRatios_[static_cast<std::size_t>(i)];
        for (int octaveOffset = -1; octaveOffset <= 1; ++octaveOffset)
        {
            const double candidate = rootLog2_ + octave
                + static_cast<double>(octaveOffset) + degree;
            const double distance = std::abs(candidate - inputLog2);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearest = candidate;
            }
        }
    }

    if (!targetValid_ || onset)
    {
        targetLog2_ = nearest;
        targetValid_ = true;
        return targetLog2_;
    }

    const double previousDistance = std::abs(targetLog2_ - inputLog2);
    const double hysteresisOctaves = std::max(0.0f, hysteresisCents) / 1200.0;
    if (previousDistance <= nearestDistance + hysteresisOctaves)
        return targetLog2_;

    // MONOTONIC_TARGET_AUTHORITY_V9: winning target identity is immediate.
    targetLog2_ = nearest;
    return targetLog2_;
}

//==============================================================================
// ModernPitchEngine control and processing''',
    'remove temporal quantizer confirmation')

for old in (
    '    pendingValid_ = false;\n',
    '    pendingLog2_ = 0.0;\n',
    '    pendingCount_ = 0;\n',
):
    while old in cpp:
        cpp = cpp.replace(old, '', 1)

cpp = regex_once(
    cpp,
    r'bool ModernPitchEngine::zeroPrudenceAuthority\(.*?\n\}\n\nint ModernPitchEngine::latencyForMode',
    'int ModernPitchEngine::latencyForMode',
    'remove special zero-prudence authority function')

# Hysteresis is now exactly an explicit Scale Lock identity control plus a
# degree-safe cap. Mode/confidence/tempo no longer secretly enlarge/shrink it.
cpp = regex_once(
    cpp,
    r'float ModernPitchEngine::adaptiveHysteresis\(.*?\n\}\n\ndouble ModernPitchEngine::responseTimeMs',
    '''float ModernPitchEngine::adaptiveHysteresis(
    const Parameters& parameters,
    const ScaleQuantizer& quantizer,
    const PitchObservation& observation) const noexcept
{
    (void) observation;
    if (!parameters.scaleLock)
        return 0.0f;

    const float requested = std::clamp(
        finiteOr(parameters.lockHysteresis, 24.0f), 0.0f, 80.0f);
    const float minimumStep = std::max(0.1f, quantizer.minimumStepCents());
    const float lockStrictness = clamp01(parameters.lockStrictness);
    const float degreeSafeCap = std::clamp(
        minimumStep * (0.30f - 0.18f * lockStrictness),
        0.35f, 36.0f);
    return std::min(requested, degreeSafeCap);
}

double ModernPitchEngine::responseTimeMs''',
    'remove hidden hysteresis modifiers')

# Replace the old note-body/release supervisor wholesale. Evidence may describe
# reliability, but only explicit user controls set destination and trajectory.
cpp = regex_once(
    cpp,
    r'void ModernPitchEngine::updateCorrectionState\(.*?\n\}\n\ndouble ModernPitchEngine::advanceCorrection',
    '''void ModernPitchEngine::updateCorrectionState(
    CorrectionState& state,
    ScaleQuantizer& quantizer,
    const PitchObservation& observation,
    const Parameters& parameters) noexcept
{
    const int hopSamples = MultiRatePitchTracker::hopSize();
    const float humanize = clamp01(parameters.humanize);
    const bool exactAuthority = exactScaleLockAuthority(parameters);
    const bool validPitch = observation.valid
        && std::isfinite(observation.frequencyHz)
        && observation.frequencyHz > 0.0f;

    const auto setState = [&state](TrackingState next) noexcept
    {
        if (state.trackingState != next)
        {
            state.trackingState = next;
            state.stateAgeSamples = 0;
        }
    };

    if (!validPitch)
    {
        ++state.invalidObservations;
        state.pitchStaleSamples = std::min(
            std::numeric_limits<int>::max() - hopSamples,
            state.pitchStaleSamples + hopSamples);

        // MONOTONIC_TARGET_AUTHORITY_V9: detector uncertainty cannot create
        // unity, dry, release, or a weaker correction. With a previous musical
        // destination we keep that exact correction trajectory while searching.
        if (state.targetValid)
        {
            if (observation.audioPresent)
                state.noteBodyLatched = true;
            setState(TrackingState::acquire);
        }
        else
        {
            setState(TrackingState::unvoiced);
        }
        return;
    }

    state.invalidObservations = 0;
    state.pitchStaleSamples = 0;
    state.noteBodyLatched = true;
    state.breathEvidenceSamples = 0;
    state.uncertainSamples = 0;

    // Voice evidence is diagnostic only. It may label the current observation
    // as less certain, but it cannot change target, Amount or correction depth.
    const float reliability = clamp01(
        0.45f * observation.confidence
      + 0.35f * observation.periodicity
      + 0.20f * observation.consensus);
    state.noteBodyConfidence += 0.25f
        * (reliability - state.noteBodyConfidence);
    state.noteBodyConfidence = clamp01(state.noteBodyConfidence);
    if (reliability >= 0.50f)
        state.stableBodyObservations = std::min(32, state.stableBodyObservations + 1);
    else
        state.stableBodyObservations = std::max(0, state.stableBodyObservations - 1);

    const double observedLog2 = safeLog2(observation.frequencyHz);
    const float correctionFrequencyHz =
        std::isfinite(observation.correctionFrequencyHz)
        && observation.correctionFrequencyHz > 0.0f
        ? observation.correctionFrequencyHz
        : observation.frequencyHz;
    const double correctionObservedLog2 = safeLog2(correctionFrequencyHz);

    // Continuity centre is analysis-only (mainly expressive/vibrato reference).
    // It never owns target selection or the correction ratio.
    if (!state.pitchCentreValid || observation.onset)
    {
        state.pitchCentreLog2 = observedLog2;
        state.pitchCentreValid = true;
        state.stableObservations = 0;
    }
    else
    {
        const double centreAlpha = 0.08 - 0.04 * static_cast<double>(humanize);
        state.pitchCentreLog2 += centreAlpha
            * (observedLog2 - state.pitchCentreLog2);
        ++state.stableObservations;
    }

    const float hysteresis = adaptiveHysteresis(parameters, quantizer, observation);
    int pending = 0;
    double newTarget = quantizer.chooseTargetLog2(
        correctionObservedLog2,
        hysteresis,
        parameters.lockStrictness,
        1.0f,
        parameters.scaleLock && parameters.hardLockActive,
        observation.onset,
        pending);

    // Fresh F0 also owns the target register. There is no smoothed-centre or
    // confidence-delayed alternate musical world.
    newTarget += std::round(correctionObservedLog2 - newTarget);

    const bool targetChanged = !state.targetValid
        || std::abs(newTarget - state.targetLog2) * 1200.0 > 0.1;
    const double targetJump = state.targetValid
        ? (newTarget - state.targetLog2) * 1200.0 : 0.0;
    if (targetChanged)
    {
        ++state.revision;
        state.lastTargetJumpCents = targetJump;
    }
    state.targetLog2 = newTarget;
    state.targetValid = true;

    // This memory exists only to help detector reacquisition. It follows the
    // newest accepted coordinate immediately and is never sent to the renderer
    // as a substitute F0.
    state.transportPeriodHz = static_cast<double>(correctionFrequencyHz);

    const double vibratoComponent = observedLog2 - state.pitchCentreLog2;
    float preserve = parameters.scaleLock
        ? clamp01(parameters.vibratoPreserve + 0.35f * humanize)
        : clamp01(parameters.preserveVibrato + 0.25f * humanize);

    double correctedLog2 = state.targetLog2
        + static_cast<double>(preserve) * vibratoComponent;

    if (parameters.scaleLock)
    {
        // Explicit expressive controls may preserve motion, but that motion is
        // bounded inside the chosen scale degree. Detector confidence is absent.
        const double minimumStep = std::max(0.1,
            static_cast<double>(quantizer.minimumStepCents()));
        const double lockStrictness = static_cast<double>(clamp01(parameters.lockStrictness));
        const double residualBudgetCents = std::clamp(
            minimumStep * (0.18 - 0.06 * lockStrictness),
            1.0, 6.0);
        const double requestedVibratoCents =
            static_cast<double>(preserve) * vibratoComponent * 1200.0;
        const double preservedVibratoCents = std::clamp(
            requestedVibratoCents,
            -residualBudgetCents,
            residualBudgetCents);
        correctedLog2 = state.targetLog2 + preservedVibratoCents / 1200.0;

        if (exactAuthority)
        {
            preserve = 0.0f;
            correctedLog2 = state.targetLog2;
        }
    }

    // MONOTONIC_TARGET_AUTHORITY_V9: there is one pitch coordinate and one
    // destination. No deadband, confidence attenuation or hidden max-correction
    // clamp is allowed between the selected target and the wet renderer.
    const double errorCents = (correctedLog2 - correctionObservedLog2) * 1200.0;
    state.desiredCents = errorCents * static_cast<double>(clamp01(parameters.amount));

    // Response/Creative Tempo are explicit user trajectory controls. State and
    // detector uncertainty may never add a minimum delay. A nonzero Response is
    // an immediate glide; Response=0 is an immediate exact snap.
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);

    if (targetChanged || observation.onset)
        setState(TrackingState::transition);
    else if (reliability >= 0.50f)
        setState(TrackingState::stable);
    else
        setState(TrackingState::acquire);

    meterPendingOctave_.store(0, std::memory_order_relaxed);
    meterOctaveState_.store(observation.octaveState, std::memory_order_relaxed);
}

double ModernPitchEngine::advanceCorrection''',
    'replace supervisor with monotonic target authority')

cpp = replace_once(
    cpp,
    '''    if (!state.targetValid)
        return 0.0;
''',
    '''    if (!state.targetValid)
        return state.currentCents;
''',
    'target invalid must not force unity')

cpp = regex_once(
    cpp,
    r'    const auto settleTrajectory = \[&state\]\(\) noexcept\n    \{.*?\n    \};',
    '''    const auto settleTrajectory = [&state]() noexcept
    {
        if ((state.trackingState == TrackingState::attack
             || state.trackingState == TrackingState::acquire
             || state.trackingState == TrackingState::transition)
            && state.noteBodyLatched)
        {
            state.trackingState = TrackingState::stable;
            state.stateAgeSamples = 0;
        }
    };''',
    'remove release-to-unity settling')

cpp = replace_once(
    cpp,
    '    const bool immediateAuthority = zeroPrudenceAuthority(safe); // AUTHORITY_CONTROLS_EXPLICIT_V1\n',
    '',
    'remove zero-prudence process switch')
cpp = replace_once(
    cpp,
    '    linkedTracker_.setImmediateAuthority(immediateAuthority);\n',
    '',
    'remove linked history-authority switch')
cpp = replace_once(
    cpp,
    '''        channelTrackers_[static_cast<std::size_t>(channel)].setImmediateAuthority(
            immediateAuthority);
''',
    '',
    'remove channel history-authority switch')

# A scale/root update changes the destination map, never the current audible
# correction trajectory. The next F0 immediately selects the new target.
cpp = replace_once(
    cpp,
    '''        if (changed)
            channelCorrections_[static_cast<std::size_t>(channel)] = {};
''',
    '''        (void) changed;
''',
    'scale change must not reset channel correction')
cpp = replace_once(
    cpp,
    '''    if (linkedScaleChanged)
        linkedCorrection_ = {};
''',
    '''    (void) linkedScaleChanged;
''',
    'scale change must not reset linked correction')

# The renderer must never receive a stale/smoothed transport period as a second
# harmonic world. Fresh valid F0 or no guide at all.
cpp = replace_once(
    cpp,
    '''                const double renderFundamentalHz =
                    std::isfinite(renderObservation.correctionFrequencyHz)
                    && renderObservation.correctionFrequencyHz > 0.0f
                    ? static_cast<double>(renderObservation.correctionFrequencyHz)
                    : correction.transportPeriodHz;
''',
    '''                const double renderFundamentalHz =
                    renderObservation.valid
                    && std::isfinite(renderObservation.correctionFrequencyHz)
                    && renderObservation.correctionFrequencyHz > 0.0f
                    ? static_cast<double>(renderObservation.correctionFrequencyHz)
                    : 0.0;
''',
    'remove dual-mono stale renderer F0 fallback')
cpp = replace_once(
    cpp,
    '''            const double renderFundamentalHz =
                std::isfinite(latestObservation_.correctionFrequencyHz)
                && latestObservation_.correctionFrequencyHz > 0.0f
                ? static_cast<double>(latestObservation_.correctionFrequencyHz)
                : linkedCorrection_.transportPeriodHz;
''',
    '''            const double renderFundamentalHz =
                latestObservation_.valid
                && std::isfinite(latestObservation_.correctionFrequencyHz)
                && latestObservation_.correctionFrequencyHz > 0.0f
                ? static_cast<double>(latestObservation_.correctionFrequencyHz)
                : 0.0;
''',
    'remove linked stale renderer F0 fallback')

# Hard source-contract guards: any hit below would reintroduce the forbidden
# architecture in ModernPitchEngine itself.
forbidden = {
    'hidden unity destination': 'state.desiredCents = 0.0',
    'hidden transition floor': 'std::max(5.5',
    'decoder history hold': 'held.score',
    'decoder history weight': 'historyWeight',
    'temporal target confirmation': 'pendingCount_ >=',
    'correction deadband': 'std::abs(errorCents) <=',
    'stale renderer pitch fallback': ': correction.transportPeriodHz',
    'stale linked renderer pitch fallback': ': linkedCorrection_.transportPeriodHz',
    'special history authority': 'immediateAuthority_',
    'zero prudence mode split': 'zeroPrudenceAuthority',
}
for label, needle in forbidden.items():
    if needle in cpp:
        raise SystemExit(f'{label}: forbidden token still present: {needle}')

required = (
    'MONOTONIC_TARGET_AUTHORITY_V9',
    'return state.currentCents;',
    'state.desiredCents = errorCents * static_cast<double>(clamp01(parameters.amount));',
    'state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);',
    ': 0.0;',
)
for needle in required:
    if needle not in cpp:
        raise SystemExit(f'missing required V9 contract: {needle}')

CPP.write_text(cpp, encoding='utf-8')
print('MONOTONIC_TARGET_AUTHORITY_V9_PATCH=PASS')
