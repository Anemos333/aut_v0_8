from pathlib import Path

cpp_path = Path("Source/ModernPitchEngine.cpp")
h_path = Path("Source/ModernPitchEngine.h")
ui_path = Path("Source/ControlRoomPage.cpp")

cpp = cpp_path.read_text()
hdr = h_path.read_text()
ui = ui_path.read_text()

marker = "TARGET_REVISION_DIAGNOSTIC_LATCH_V1"
if marker in cpp or marker in hdr or marker in ui:
    raise SystemExit("diagnostic latch already present")

for required in [
    "TAU_EVIDENCE_MEMOIZATION_V1",
    "RESIDUAL_LINE_EXACT_MEMOIZATION_V1",
    "RESIDUAL_HANN_LAZY_MEMOIZATION_V1",
    "TERMINAL_TAIL_STABLE_TRANSPORT_REBASE_V1",
]:
    if required not in cpp:
        raise SystemExit(f"missing validated baseline marker {required}")

# Public metering fields.
old_meter = r'''        int detectorSupport = 0;
        int octaveState = 0;
        int pendingOctaveObservations = 0;
        TrackingState state = TrackingState::unvoiced;

        float tempoBpm = 120.0f;
'''
new_meter = r'''        int detectorSupport = 0;
        int octaveState = 0;
        int pendingOctaveObservations = 0;
        TrackingState state = TrackingState::unvoiced;

        // TARGET_REVISION_DIAGNOSTIC_LATCH_V1
        // Latched debug-only description of the most recent committed target
        // identity change. It has no authority over detector, controller or DSP.
        std::uint32_t targetRevisionDiagnosticSerial = 0;
        float targetRevisionBeforeHz = 0.0f;
        float targetRevisionAfterHz = 0.0f;
        float targetRevisionJumpCents = 0.0f;
        bool targetRevisionFromStable = false;
        bool targetRevisionVoiceEvidenceValid = false;
        bool targetRevisionTerminalTailVeto = false;
        bool targetRevisionBodyPresent = false;
        bool targetRevisionMusicalOnset = false;
        bool targetRevisionLiveIdentityBreak = false;

        float tempoBpm = 120.0f;
'''
if hdr.count(old_meter) != 1:
    raise SystemExit(f"meter anchor count {hdr.count(old_meter)}")
hdr = hdr.replace(old_meter, new_meter, 1)

# Private latched event state + publication atomics.
old_private = r'''    double audibleCorrectionCents_ = 0.0;
    std::int64_t sustainedSamples_ = 0;

    std::atomic<std::uint32_t> meterSequence_ { 0 };
'''
new_private = r'''    double audibleCorrectionCents_ = 0.0;
    std::int64_t sustainedSamples_ = 0;

    // TARGET_REVISION_DIAGNOSTIC_LATCH_V1
    // Audio-thread-only latched event state, copied into the normal coherent
    // metering publication at block end. Never read by DSP decisions.
    std::uint32_t targetRevisionDiagnosticSerial_ = 0;
    float targetRevisionBeforeHz_ = 0.0f;
    float targetRevisionAfterHz_ = 0.0f;
    float targetRevisionJumpCents_ = 0.0f;
    bool targetRevisionFromStable_ = false;
    bool targetRevisionVoiceEvidenceValid_ = false;
    bool targetRevisionTerminalTailVeto_ = false;
    bool targetRevisionBodyPresent_ = false;
    bool targetRevisionMusicalOnset_ = false;
    bool targetRevisionLiveIdentityBreak_ = false;

    std::atomic<std::uint32_t> meterSequence_ { 0 };
'''
if hdr.count(old_private) != 1:
    raise SystemExit(f"private latch anchor count {hdr.count(old_private)}")
hdr = hdr.replace(old_private, new_private, 1)

old_atomics = r'''    std::atomic<int> meterTrackingState_ { static_cast<int>(TrackingState::unvoiced) };
    std::atomic<float> meterTempoBpm_ { 120.0f };
'''
new_atomics = r'''    std::atomic<int> meterTrackingState_ { static_cast<int>(TrackingState::unvoiced) };
    std::atomic<std::uint32_t> meterTargetRevisionDiagnosticSerial_ { 0 };
    std::atomic<float> meterTargetRevisionBeforeHz_ { 0.0f };
    std::atomic<float> meterTargetRevisionAfterHz_ { 0.0f };
    std::atomic<float> meterTargetRevisionJumpCents_ { 0.0f };
    std::atomic<bool> meterTargetRevisionFromStable_ { false };
    std::atomic<bool> meterTargetRevisionVoiceEvidenceValid_ { false };
    std::atomic<bool> meterTargetRevisionTerminalTailVeto_ { false };
    std::atomic<bool> meterTargetRevisionBodyPresent_ { false };
    std::atomic<bool> meterTargetRevisionMusicalOnset_ { false };
    std::atomic<bool> meterTargetRevisionLiveIdentityBreak_ { false };
    std::atomic<float> meterTempoBpm_ { 120.0f };
'''
if hdr.count(old_atomics) != 1:
    raise SystemExit(f"meter atomic anchor count {hdr.count(old_atomics)}")
hdr = hdr.replace(old_atomics, new_atomics, 1)

# Reset latched diagnostics alongside existing meters.
old_reset = r'''    meterTargetJumpCents_.store(0.0f, std::memory_order_relaxed);
    meterSustainedSeconds_.store(0.0f, std::memory_order_relaxed);
'''
new_reset = r'''    meterTargetJumpCents_.store(0.0f, std::memory_order_relaxed);
    meterSustainedSeconds_.store(0.0f, std::memory_order_relaxed);

    targetRevisionDiagnosticSerial_ = 0;
    targetRevisionBeforeHz_ = 0.0f;
    targetRevisionAfterHz_ = 0.0f;
    targetRevisionJumpCents_ = 0.0f;
    targetRevisionFromStable_ = false;
    targetRevisionVoiceEvidenceValid_ = false;
    targetRevisionTerminalTailVeto_ = false;
    targetRevisionBodyPresent_ = false;
    targetRevisionMusicalOnset_ = false;
    targetRevisionLiveIdentityBreak_ = false;
    meterTargetRevisionDiagnosticSerial_.store(0, std::memory_order_relaxed);
    meterTargetRevisionBeforeHz_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionAfterHz_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionJumpCents_.store(0.0f, std::memory_order_relaxed);
    meterTargetRevisionFromStable_.store(false, std::memory_order_relaxed);
    meterTargetRevisionVoiceEvidenceValid_.store(false, std::memory_order_relaxed);
    meterTargetRevisionTerminalTailVeto_.store(false, std::memory_order_relaxed);
    meterTargetRevisionBodyPresent_.store(false, std::memory_order_relaxed);
    meterTargetRevisionMusicalOnset_.store(false, std::memory_order_relaxed);
    meterTargetRevisionLiveIdentityBreak_.store(false, std::memory_order_relaxed);
'''
if cpp.count(old_reset) != 1:
    raise SystemExit(f"reset anchor count {cpp.count(old_reset)}")
cpp = cpp.replace(old_reset, new_reset, 1)

# Latch exact conditions immediately before transition state is mutated.
old_revision = r'''        if (targetIdentityChanged)
        {
            // TRANSITION_DESTINATION_FROZEN_V2: every committed note boundary
            // starts one clean monotonic trajectory. A later detector candidate
            // may be analysed, but cannot continuously rewrite this destination.
            state.trackingState = TrackingState::transition;
'''
new_revision = r'''        if (targetIdentityChanged)
        {
            // TARGET_REVISION_DIAGNOSTIC_LATCH_V1
            // Observe the committed identity change before mutating tracking
            // state. These fields are debug metering only and never participate
            // in any subsequent DSP or authority decision.
            ++targetRevisionDiagnosticSerial_;
            targetRevisionBeforeHz_ = state.targetValid
                ? static_cast<float>(std::exp2(state.targetLog2)) : 0.0f;
            targetRevisionAfterHz_ = static_cast<float>(std::exp2(newTarget));
            targetRevisionJumpCents_ = static_cast<float>(targetJump);
            targetRevisionFromStable_ =
                state.trackingState == TrackingState::stable;
            targetRevisionVoiceEvidenceValid_ = richEvidence;
            targetRevisionTerminalTailVeto_ = terminalTailIdentityVeto;
            targetRevisionBodyPresent_ = bodyPresent;
            targetRevisionMusicalOnset_ = musicalOnset;
            targetRevisionLiveIdentityBreak_ = liveIdentityBreak;

            // TRANSITION_DESTINATION_FROZEN_V2: every committed note boundary
            // starts one clean monotonic trajectory. A later detector candidate
            // may be analysed, but cannot continuously rewrite this destination.
            state.trackingState = TrackingState::transition;
'''
if cpp.count(old_revision) != 1:
    raise SystemExit(f"revision anchor count {cpp.count(old_revision)}")
cpp = cpp.replace(old_revision, new_revision, 1)

# Publish through the existing meter sequence.
old_publish = r'''    meterTrackingState_.store(static_cast<int>(state.trackingState),
                              std::memory_order_relaxed);
    meterTempoBpm_.store(tempoMeter.bpm, std::memory_order_relaxed);
'''
new_publish = r'''    meterTrackingState_.store(static_cast<int>(state.trackingState),
                              std::memory_order_relaxed);
    meterTargetRevisionDiagnosticSerial_.store(
        targetRevisionDiagnosticSerial_, std::memory_order_relaxed);
    meterTargetRevisionBeforeHz_.store(
        targetRevisionBeforeHz_, std::memory_order_relaxed);
    meterTargetRevisionAfterHz_.store(
        targetRevisionAfterHz_, std::memory_order_relaxed);
    meterTargetRevisionJumpCents_.store(
        targetRevisionJumpCents_, std::memory_order_relaxed);
    meterTargetRevisionFromStable_.store(
        targetRevisionFromStable_, std::memory_order_relaxed);
    meterTargetRevisionVoiceEvidenceValid_.store(
        targetRevisionVoiceEvidenceValid_, std::memory_order_relaxed);
    meterTargetRevisionTerminalTailVeto_.store(
        targetRevisionTerminalTailVeto_, std::memory_order_relaxed);
    meterTargetRevisionBodyPresent_.store(
        targetRevisionBodyPresent_, std::memory_order_relaxed);
    meterTargetRevisionMusicalOnset_.store(
        targetRevisionMusicalOnset_, std::memory_order_relaxed);
    meterTargetRevisionLiveIdentityBreak_.store(
        targetRevisionLiveIdentityBreak_, std::memory_order_relaxed);
    meterTempoBpm_.store(tempoMeter.bpm, std::memory_order_relaxed);
'''
if cpp.count(old_publish) != 1:
    raise SystemExit(f"publish anchor count {cpp.count(old_publish)}")
cpp = cpp.replace(old_publish, new_publish, 1)

old_get = r'''        result.state = static_cast<TrackingState>(
            meterTrackingState_.load(std::memory_order_relaxed));
        result.tempoBpm = meterTempoBpm_.load(std::memory_order_relaxed);
'''
new_get = r'''        result.state = static_cast<TrackingState>(
            meterTrackingState_.load(std::memory_order_relaxed));
        result.targetRevisionDiagnosticSerial =
            meterTargetRevisionDiagnosticSerial_.load(std::memory_order_relaxed);
        result.targetRevisionBeforeHz =
            meterTargetRevisionBeforeHz_.load(std::memory_order_relaxed);
        result.targetRevisionAfterHz =
            meterTargetRevisionAfterHz_.load(std::memory_order_relaxed);
        result.targetRevisionJumpCents =
            meterTargetRevisionJumpCents_.load(std::memory_order_relaxed);
        result.targetRevisionFromStable =
            meterTargetRevisionFromStable_.load(std::memory_order_relaxed);
        result.targetRevisionVoiceEvidenceValid =
            meterTargetRevisionVoiceEvidenceValid_.load(std::memory_order_relaxed);
        result.targetRevisionTerminalTailVeto =
            meterTargetRevisionTerminalTailVeto_.load(std::memory_order_relaxed);
        result.targetRevisionBodyPresent =
            meterTargetRevisionBodyPresent_.load(std::memory_order_relaxed);
        result.targetRevisionMusicalOnset =
            meterTargetRevisionMusicalOnset_.load(std::memory_order_relaxed);
        result.targetRevisionLiveIdentityBreak =
            meterTargetRevisionLiveIdentityBreak_.load(std::memory_order_relaxed);
        result.tempoBpm = meterTempoBpm_.load(std::memory_order_relaxed);
'''
if cpp.count(old_get) != 1:
    raise SystemExit(f"get meter anchor count {cpp.count(old_get)}")
cpp = cpp.replace(old_get, new_get, 1)

# Compact persistent footer readout in Control Room.
old_footer = r'''    if (metering_.pendingOctaveObservations > 0)
        line += "   |   Confirm " + juce::String (metering_.pendingOctaveObservations);

    line += "   |   Tempo ";
'''
new_footer = r'''    if (metering_.pendingOctaveObservations > 0)
        line += "   |   Confirm " + juce::String (metering_.pendingOctaveObservations);

    // TARGET_REVISION_DIAGNOSTIC_LATCH_V1
    if (metering_.targetRevisionDiagnosticSerial > 0)
    {
        line += "   |   Rev#" + juce::String (
            static_cast<int> (metering_.targetRevisionDiagnosticSerial))
            + " " + juce::String (metering_.targetRevisionBeforeHz, 1)
            + ">" + juce::String (metering_.targetRevisionAfterHz, 1)
            + "Hz " + juce::String (metering_.targetRevisionJumpCents, 0) + "c";
        if (metering_.targetRevisionFromStable)
            line += " S";
        line += metering_.targetRevisionVoiceEvidenceValid ? " V1" : " V0";
        line += metering_.targetRevisionTerminalTailVeto ? " T1" : " T0";
        line += metering_.targetRevisionBodyPresent ? " B1" : " B0";
        line += metering_.targetRevisionMusicalOnset ? " O1" : " O0";
        line += metering_.targetRevisionLiveIdentityBreak ? " I1" : " I0";
    }

    line += "   |   Tempo ";
'''
if ui.count(old_footer) != 1:
    raise SystemExit(f"UI footer anchor count {ui.count(old_footer)}")
ui = ui.replace(old_footer, new_footer, 1)

for content, name in [(cpp, "cpp"), (hdr, "header"), (ui, "ui")]:
    if marker not in content:
        raise SystemExit(f"diagnostic marker missing in {name}")

cpp_path.write_text(cpp)
h_path.write_text(hdr)
ui_path.write_text(ui)
print("TARGET_REVISION_DIAGNOSTIC_LATCH_PATCH=APPLIED")
