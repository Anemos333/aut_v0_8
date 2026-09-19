from pathlib import Path

cpp_path = Path("Source/ModernPitchEngine.cpp")
h_path = Path("Source/ModernPitchEngine.h")
ui_path = Path("Source/ControlRoomPage.cpp")

cpp = cpp_path.read_text()
hdr = h_path.read_text()
ui = ui_path.read_text()

marker = "TARGET_REVISION_DIAGNOSTIC_LATCH_V2"
if marker in cpp or marker in hdr or marker in ui:
    raise SystemExit("diagnostic v2 already present")

for required in [
    "TARGET_REVISION_DIAGNOSTIC_LATCH_V1",
    "RESIDUAL_HANN_LAZY_MEMOIZATION_V1",
    "TERMINAL_TAIL_STABLE_TRANSPORT_REBASE_V1",
]:
    if required not in cpp and required != "TARGET_REVISION_DIAGNOSTIC_LATCH_V1":
        raise SystemExit(f"missing baseline marker {required}")
if "TARGET_REVISION_DIAGNOSTIC_LATCH_V1" not in hdr:
    raise SystemExit("missing diagnostic v1 header marker")

old_meter = r'''        bool targetRevisionMusicalOnset = false;
        bool targetRevisionLiveIdentityBreak = false;

        float tempoBpm = 120.0f;
'''
new_meter = r'''        bool targetRevisionMusicalOnset = false;
        bool targetRevisionLiveIdentityBreak = false;

        // TARGET_REVISION_DIAGNOSTIC_LATCH_V2
        bool targetRevisionDetectorScaleCommit = false;
        bool targetRevisionDeepCentreExit = false;
        bool targetRevisionPersistentBoundaryExit = false;
        bool targetRevisionTerminalStructure = false;
        bool targetRevisionSameTailSide = false;
        bool targetRevisionOutsideStableCore = false;
        float targetRevisionVoiceBodyEnergy = 0.0f;
        float targetRevisionVoiceHarmonicity = 0.0f;
        float targetRevisionVoiceSpectralReliability = 0.0f;
        float targetRevisionVoiceBreathiness = 0.0f;
        float targetRevisionVoiceEventStrength = 0.0f;
        float targetRevisionCorrectionBeforeCents = 0.0f;
        float targetRevisionCorrectionAfterCents = 0.0f;
        float targetRevisionCorrectionDeltaCents = 0.0f;

        float tempoBpm = 120.0f;
'''
if hdr.count(old_meter) != 1:
    raise SystemExit("meter v2 anchor missing")
hdr = hdr.replace(old_meter, new_meter, 1)

old_private = r'''    bool targetRevisionMusicalOnset_ = false;
    bool targetRevisionLiveIdentityBreak_ = false;

    std::atomic<std::uint32_t> meterSequence_ { 0 };
'''
new_private = r'''    bool targetRevisionMusicalOnset_ = false;
    bool targetRevisionLiveIdentityBreak_ = false;

    // TARGET_REVISION_DIAGNOSTIC_LATCH_V2
    bool targetRevisionDetectorScaleCommit_ = false;
    bool targetRevisionDeepCentreExit_ = false;
    bool targetRevisionPersistentBoundaryExit_ = false;
    bool targetRevisionTerminalStructure_ = false;
    bool targetRevisionSameTailSide_ = false;
    bool targetRevisionOutsideStableCore_ = false;
    float targetRevisionVoiceBodyEnergy_ = 0.0f;
    float targetRevisionVoiceHarmonicity_ = 0.0f;
    float targetRevisionVoiceSpectralReliability_ = 0.0f;
    float targetRevisionVoiceBreathiness_ = 0.0f;
    float targetRevisionVoiceEventStrength_ = 0.0f;
    float targetRevisionCorrectionBeforeCents_ = 0.0f;
    float targetRevisionCorrectionAfterCents_ = 0.0f;
    float targetRevisionCorrectionDeltaCents_ = 0.0f;

    std::atomic<std::uint32_t> meterSequence_ { 0 };
'''
if hdr.count(old_private) != 1:
    raise SystemExit("private v2 anchor missing")
hdr = hdr.replace(old_private, new_private, 1)

old_atomics = r'''    std::atomic<bool> meterTargetRevisionMusicalOnset_ { false };
    std::atomic<bool> meterTargetRevisionLiveIdentityBreak_ { false };
    std::atomic<float> meterTempoBpm_ { 120.0f };
'''
new_atomics = r'''    std::atomic<bool> meterTargetRevisionMusicalOnset_ { false };
    std::atomic<bool> meterTargetRevisionLiveIdentityBreak_ { false };
    std::atomic<bool> meterTargetRevisionDetectorScaleCommit_ { false };
    std::atomic<bool> meterTargetRevisionDeepCentreExit_ { false };
    std::atomic<bool> meterTargetRevisionPersistentBoundaryExit_ { false };
    std::atomic<bool> meterTargetRevisionTerminalStructure_ { false };
    std::atomic<bool> meterTargetRevisionSameTailSide_ { false };
    std::atomic<bool> meterTargetRevisionOutsideStableCore_ { false };
    std::atomic<float> meterTargetRevisionVoiceBodyEnergy_ { 0.0f };
    std::atomic<float> meterTargetRevisionVoiceHarmonicity_ { 0.0f };
    std::atomic<float> meterTargetRevisionVoiceSpectralReliability_ { 0.0f };
    std::atomic<float> meterTargetRevisionVoiceBreathiness_ { 0.0f };
    std::atomic<float> meterTargetRevisionVoiceEventStrength_ { 0.0f };
    std::atomic<float> meterTargetRevisionCorrectionBeforeCents_ { 0.0f };
    std::atomic<float> meterTargetRevisionCorrectionAfterCents_ { 0.0f };
    std::atomic<float> meterTargetRevisionCorrectionDeltaCents_ { 0.0f };
    std::atomic<float> meterTempoBpm_ { 120.0f };
'''
if hdr.count(old_atomics) != 1:
    raise SystemExit("atomic v2 anchor missing")
hdr = hdr.replace(old_atomics, new_atomics, 1)

# Capture pre-update correction command.
old_top = r'''    const int hopSamples = MultiRatePitchTracker::hopSize();
    const double hopSeconds = static_cast<double>(hopSamples) / sampleRate_;
'''
new_top = r'''    const int hopSamples = MultiRatePitchTracker::hopSize();
    const double hopSeconds = static_cast<double>(hopSamples) / sampleRate_;
    const double diagnosticDesiredBeforeUpdate = state.desiredCents;
    const std::uint32_t diagnosticRevisionSerialBeforeUpdate =
        targetRevisionDiagnosticSerial_;
'''
if cpp.count(old_top) != 1:
    raise SystemExit("top diagnostic anchor missing")
cpp = cpp.replace(old_top, new_top, 1)

# Expose terminal-tail predicate parts.
old_tail_decl = r'''    bool terminalTailIdentityVeto = false;
    bool terminalTailStableCompensation = false;
'''
new_tail_decl = r'''    bool terminalTailIdentityVeto = false;
    bool terminalTailStableCompensation = false;
    bool diagnosticTerminalStructure = false;
    bool diagnosticSameTailSide = false;
    bool diagnosticOutsideStableCore = false;
'''
if cpp.count(old_tail_decl) != 1:
    raise SystemExit("tail decl anchor missing")
cpp = cpp.replace(old_tail_decl, new_tail_decl, 1)

old_same = r'''        const bool sameTailSide = std::abs(signedPriorDistanceCents)
                < 0.12 * localTailStep
            || signedTailDistanceCents * signedPriorDistanceCents > 0.0;
'''
new_same = r'''        const bool sameTailSide = std::abs(signedPriorDistanceCents)
                < 0.12 * localTailStep
            || signedTailDistanceCents * signedPriorDistanceCents > 0.0;
        diagnosticSameTailSide = sameTailSide;
'''
if cpp.count(old_same) != 1:
    raise SystemExit("same tail anchor missing")
cpp = cpp.replace(old_same, new_same, 1)

old_terminal = r'''        const bool terminalStructure = parameters.voiceEventStrength < 0.55f
            && degradationVotes >= 2;
'''
new_terminal = r'''        const bool terminalStructure = parameters.voiceEventStrength < 0.55f
            && degradationVotes >= 2;
        diagnosticTerminalStructure = terminalStructure;
'''
if cpp.count(old_terminal) != 1:
    raise SystemExit("terminal structure anchor missing")
cpp = cpp.replace(old_terminal, new_terminal, 1)

old_outside = r'''        const bool outsideStableCore =
            std::abs(signedTailDistanceCents) >= 0.32 * localTailStep;
'''
new_outside = r'''        const bool outsideStableCore =
            std::abs(signedTailDistanceCents) >= 0.32 * localTailStep;
        diagnosticOutsideStableCore = outsideStableCore;
'''
if cpp.count(old_outside) != 1:
    raise SystemExit("outside core anchor missing")
cpp = cpp.replace(old_outside, new_outside, 1)

# Capture which live identity route fired.
old_live_decl = r'''    bool liveIdentityBreak = false;
    bool forceTargetSwitch = false;
'''
new_live_decl = r'''    bool liveIdentityBreak = false;
    bool forceTargetSwitch = false;
    bool diagnosticDeepCentreExit = false;
    bool diagnosticPersistentBoundaryExit = false;
'''
if cpp.count(old_live_decl) != 1:
    raise SystemExit("live decl anchor missing")
cpp = cpp.replace(old_live_decl, new_live_decl, 1)

old_routes = r'''            const bool deepCentreExit = !octaveAmbiguous
                && centreDistanceFromTarget >= deepExitRatio * scaleStep;
            const double persistentEvidenceRequired = octaveAmbiguous ? 12.0 : 6.0;
            const bool persistentBoundaryExit =
                state.identityChallengerEvidence >= persistentEvidenceRequired;

            liveIdentityBreak = deepCentreExit || persistentBoundaryExit;
'''
new_routes = r'''            const bool deepCentreExit = !octaveAmbiguous
                && centreDistanceFromTarget >= deepExitRatio * scaleStep;
            const double persistentEvidenceRequired = octaveAmbiguous ? 12.0 : 6.0;
            const bool persistentBoundaryExit =
                state.identityChallengerEvidence >= persistentEvidenceRequired;
            diagnosticDeepCentreExit = deepCentreExit;
            diagnosticPersistentBoundaryExit = persistentBoundaryExit;

            liveIdentityBreak = deepCentreExit || persistentBoundaryExit;
'''
if cpp.count(old_routes) != 1:
    raise SystemExit("route anchor missing")
cpp = cpp.replace(old_routes, new_routes, 1)

# Extend event latch.
old_latch = r'''            targetRevisionMusicalOnset_ = musicalOnset;
            targetRevisionLiveIdentityBreak_ = liveIdentityBreak;

            // TRANSITION_DESTINATION_FROZEN_V2: every committed note boundary
'''
new_latch = r'''            targetRevisionMusicalOnset_ = musicalOnset;
            targetRevisionLiveIdentityBreak_ = liveIdentityBreak;
            targetRevisionDetectorScaleCommit_ = detectorScaleCommit;
            targetRevisionDeepCentreExit_ = diagnosticDeepCentreExit;
            targetRevisionPersistentBoundaryExit_ =
                diagnosticPersistentBoundaryExit;
            targetRevisionTerminalStructure_ = diagnosticTerminalStructure;
            targetRevisionSameTailSide_ = diagnosticSameTailSide;
            targetRevisionOutsideStableCore_ = diagnosticOutsideStableCore;
            targetRevisionVoiceBodyEnergy_ = parameters.voiceBodyEnergy;
            targetRevisionVoiceHarmonicity_ = parameters.voiceHarmonicity;
            targetRevisionVoiceSpectralReliability_ =
                parameters.voiceSpectralReliability;
            targetRevisionVoiceBreathiness_ = parameters.voiceBreathiness;
            targetRevisionVoiceEventStrength_ = parameters.voiceEventStrength;
            targetRevisionCorrectionBeforeCents_ =
                static_cast<float>(diagnosticDesiredBeforeUpdate);

            // TRANSITION_DESTINATION_FROZEN_V2: every committed note boundary
'''
if cpp.count(old_latch) != 1:
    raise SystemExit("latch extension anchor missing")
cpp = cpp.replace(old_latch, new_latch, 1)

# After final errorCents is known, record command consequence only for new revision.
old_desired = r'''    state.desiredCents = errorCents;
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);
'''
new_desired = r'''    if (targetRevisionDiagnosticSerial_
        != diagnosticRevisionSerialBeforeUpdate)
    {
        targetRevisionCorrectionAfterCents_ =
            static_cast<float>(errorCents);
        targetRevisionCorrectionDeltaCents_ = static_cast<float>(
            errorCents - diagnosticDesiredBeforeUpdate);
    }

    state.desiredCents = errorCents;
    state.responseMs = responseTimeMs(parameters, targetChanged, targetJump);
'''
if cpp.count(old_desired) != 1:
    raise SystemExit("desired consequence anchor missing")
cpp = cpp.replace(old_desired, new_desired, 1)

# Reset private + atomic v2 fields.
old_reset_private = r'''    targetRevisionMusicalOnset_ = false;
    targetRevisionLiveIdentityBreak_ = false;
    meterTargetRevisionDiagnosticSerial_.store(0, std::memory_order_relaxed);
'''
new_reset_private = r'''    targetRevisionMusicalOnset_ = false;
    targetRevisionLiveIdentityBreak_ = false;
    targetRevisionDetectorScaleCommit_ = false;
    targetRevisionDeepCentreExit_ = false;
    targetRevisionPersistentBoundaryExit_ = false;
    targetRevisionTerminalStructure_ = false;
    targetRevisionSameTailSide_ = false;
    targetRevisionOutsideStableCore_ = false;
    targetRevisionVoiceBodyEnergy_ = 0.0f;
    targetRevisionVoiceHarmonicity_ = 0.0f;
    targetRevisionVoiceSpectralReliability_ = 0.0f;
    targetRevisionVoiceBreathiness_ = 0.0f;
    targetRevisionVoiceEventStrength_ = 0.0f;
    targetRevisionCorrectionBeforeCents_ = 0.0f;
    targetRevisionCorrectionAfterCents_ = 0.0f;
    targetRevisionCorrectionDeltaCents_ = 0.0f;
    meterTargetRevisionDiagnosticSerial_.store(0, std::memory_order_relaxed);
'''
if cpp.count(old_reset_private) != 1:
    raise SystemExit("reset private anchor missing")
cpp = cpp.replace(old_reset_private, new_reset_private, 1)

old_reset_atomic = r'''    meterTargetRevisionMusicalOnset_.store(false, std::memory_order_relaxed);
    meterTargetRevisionLiveIdentityBreak_.store(false, std::memory_order_relaxed);
    meterDetectorSupport_.store(0, std::memory_order_relaxed);
'''
new_reset_atomic = r'''    meterTargetRevisionMusicalOnset_.store(false, std::memory_order_relaxed);
    meterTargetRevisionLiveIdentityBreak_.store(false, std::memory_order_relaxed);
    meterTargetRevisionDetectorScaleCommit_.store(false, std::memory_order_relaxed);
    meterTargetRevisionDeepCentreExit_.store(false, std::memory_order_relaxed);
    meterTargetRevisionPersistentBoundaryExit_.store(false, std::memory_order_relaxed);
    meterTargetRevisionTerminalStructure_.store(false, std::memory_order_relaxed);
    meterTargetRevisionSameTailSide_.store(false, std::memory_order_relaxed);
    meterTargetRevisionOutsideStableCore_.store(false, std::memory_order_relaxed);
    meterTargetRevisionVoiceBodyEnergy_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionVoiceHarmonicity_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionVoiceSpectralReliability_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionVoiceBreathiness_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionVoiceEventStrength_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionCorrectionBeforeCents_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionCorrectionAfterCents_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionCorrectionDeltaCents_.store(0.0f, std::memory_order_relaxed);
    meterDetectorSupport_.store(0, std::memory_order_relaxed);
'''
if cpp.count(old_reset_atomic) != 1:
    raise SystemExit("reset atomics anchor missing")
cpp = cpp.replace(old_reset_atomic, new_reset_atomic, 1)

# Publish atomics.
old_publish = r'''    meterTargetRevisionMusicalOnset_.store(
        targetRevisionMusicalOnset_, std::memory_order_relaxed);
    meterTargetRevisionLiveIdentityBreak_.store(
        targetRevisionLiveIdentityBreak_, std::memory_order_relaxed);
    meterTempoBpm_.store(tempoMeter.bpm, std::memory_order_relaxed);
'''
new_publish = r'''    meterTargetRevisionMusicalOnset_.store(
        targetRevisionMusicalOnset_, std::memory_order_relaxed);
    meterTargetRevisionLiveIdentityBreak_.store(
        targetRevisionLiveIdentityBreak_, std::memory_order_relaxed);
    meterTargetRevisionDetectorScaleCommit_.store(
        targetRevisionDetectorScaleCommit_, std::memory_order_relaxed);
    meterTargetRevisionDeepCentreExit_.store(
        targetRevisionDeepCentreExit_, std::memory_order_relaxed);
    meterTargetRevisionPersistentBoundaryExit_.store(
        targetRevisionPersistentBoundaryExit_, std::memory_order_relaxed);
    meterTargetRevisionTerminalStructure_.store(
        targetRevisionTerminalStructure_, std::memory_order_relaxed);
    meterTargetRevisionSameTailSide_.store(
        targetRevisionSameTailSide_, std::memory_order_relaxed);
    meterTargetRevisionOutsideStableCore_.store(
        targetRevisionOutsideStableCore_, std::memory_order_relaxed);
    meterTargetRevisionVoiceBodyEnergy_.store(
        targetRevisionVoiceBodyEnergy_, std::memory_order_relaxed);
    meterTargetRevisionVoiceHarmonicity_.store(
        targetRevisionVoiceHarmonicity_, std::memory_order_relaxed);
    meterTargetRevisionVoiceSpectralReliability_.store(
        targetRevisionVoiceSpectralReliability_, std::memory_order_relaxed);
    meterTargetRevisionVoiceBreathiness_.store(
        targetRevisionVoiceBreathiness_, std::memory_order_relaxed);
    meterTargetRevisionVoiceEventStrength_.store(
        targetRevisionVoiceEventStrength_, std::memory_order_relaxed);
    meterTargetRevisionCorrectionBeforeCents_.store(
        targetRevisionCorrectionBeforeCents_, std::memory_order_relaxed);
    meterTargetRevisionCorrectionAfterCents_.store(
        targetRevisionCorrectionAfterCents_, std::memory_order_relaxed);
    meterTargetRevisionCorrectionDeltaCents_.store(
        targetRevisionCorrectionDeltaCents_, std::memory_order_relaxed);
    meterTempoBpm_.store(tempoMeter.bpm, std::memory_order_relaxed);
'''
if cpp.count(old_publish) != 1:
    raise SystemExit("publish v2 anchor missing")
cpp = cpp.replace(old_publish, new_publish, 1)

# Read coherent meter.
old_get = r'''        result.targetRevisionLiveIdentityBreak =
            meterTargetRevisionLiveIdentityBreak_.load(std::memory_order_relaxed);
        result.tempoBpm = meterTempoBpm_.load(std::memory_order_relaxed);
'''
new_get = r'''        result.targetRevisionLiveIdentityBreak =
            meterTargetRevisionLiveIdentityBreak_.load(std::memory_order_relaxed);
        result.targetRevisionDetectorScaleCommit =
            meterTargetRevisionDetectorScaleCommit_.load(std::memory_order_relaxed);
        result.targetRevisionDeepCentreExit =
            meterTargetRevisionDeepCentreExit_.load(std::memory_order_relaxed);
        result.targetRevisionPersistentBoundaryExit =
            meterTargetRevisionPersistentBoundaryExit_.load(std::memory_order_relaxed);
        result.targetRevisionTerminalStructure =
            meterTargetRevisionTerminalStructure_.load(std::memory_order_relaxed);
        result.targetRevisionSameTailSide =
            meterTargetRevisionSameTailSide_.load(std::memory_order_relaxed);
        result.targetRevisionOutsideStableCore =
            meterTargetRevisionOutsideStableCore_.load(std::memory_order_relaxed);
        result.targetRevisionVoiceBodyEnergy =
            meterTargetRevisionVoiceBodyEnergy_.load(std::memory_order_relaxed);
        result.targetRevisionVoiceHarmonicity =
            meterTargetRevisionVoiceHarmonicity_.load(std::memory_order_relaxed);
        result.targetRevisionVoiceSpectralReliability =
            meterTargetRevisionVoiceSpectralReliability_.load(std::memory_order_relaxed);
        result.targetRevisionVoiceBreathiness =
            meterTargetRevisionVoiceBreathiness_.load(std::memory_order_relaxed);
        result.targetRevisionVoiceEventStrength =
            meterTargetRevisionVoiceEventStrength_.load(std::memory_order_relaxed);
        result.targetRevisionCorrectionBeforeCents =
            meterTargetRevisionCorrectionBeforeCents_.load(std::memory_order_relaxed);
        result.targetRevisionCorrectionAfterCents =
            meterTargetRevisionCorrectionAfterCents_.load(std::memory_order_relaxed);
        result.targetRevisionCorrectionDeltaCents =
            meterTargetRevisionCorrectionDeltaCents_.load(std::memory_order_relaxed);
        result.tempoBpm = meterTempoBpm_.load(std::memory_order_relaxed);
'''
if cpp.count(old_get) != 1:
    raise SystemExit("get v2 anchor missing")
cpp = cpp.replace(old_get, new_get, 1)

# UI: compact two-line diagnostic appended to existing persistent Rev.
old_ui = r'''        line += metering_.targetRevisionMusicalOnset ? " O1" : " O0";
        line += metering_.targetRevisionLiveIdentityBreak ? " I1" : " I0";
    }

    line += "   |   Tempo ";
'''
new_ui = r'''        line += metering_.targetRevisionMusicalOnset ? " O1" : " O0";
        line += metering_.targetRevisionLiveIdentityBreak ? " I1" : " I0";

        // TARGET_REVISION_DIAGNOSTIC_LATCH_V2
        line += metering_.targetRevisionDetectorScaleCommit ? " D1" : " D0";
        line += metering_.targetRevisionDeepCentreExit ? " C1" : " C0";
        line += metering_.targetRevisionPersistentBoundaryExit ? " P1" : " P0";
        line += metering_.targetRevisionTerminalStructure ? " TS1" : " TS0";
        line += metering_.targetRevisionSameTailSide ? " SS1" : " SS0";
        line += metering_.targetRevisionOutsideStableCore ? " X1" : " X0";
        line += " | vE " + juce::String (metering_.targetRevisionVoiceBodyEnergy, 2)
            + " h " + juce::String (metering_.targetRevisionVoiceHarmonicity, 2)
            + " r " + juce::String (metering_.targetRevisionVoiceSpectralReliability, 2)
            + " br " + juce::String (metering_.targetRevisionVoiceBreathiness, 2)
            + " ev " + juce::String (metering_.targetRevisionVoiceEventStrength, 2);
        line += " | corr "
            + juce::String (metering_.targetRevisionCorrectionBeforeCents, 0)
            + ">" + juce::String (metering_.targetRevisionCorrectionAfterCents, 0)
            + " d" + juce::String (metering_.targetRevisionCorrectionDeltaCents, 0);
    }

    line += "   |   Tempo ";
'''
if ui.count(old_ui) != 1:
    raise SystemExit("UI v2 anchor missing")
ui = ui.replace(old_ui, new_ui, 1)

for content, name in [(cpp, "cpp"), (hdr, "header"), (ui, "ui")]:
    if marker not in content:
        raise SystemExit(f"v2 marker missing in {name}")

cpp_path.write_text(cpp)
h_path.write_text(hdr)
ui_path.write_text(ui)
print("TARGET_REVISION_DIAGNOSTIC_LATCH_V2_PATCH=APPLIED")
