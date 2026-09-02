# Integrated clean ModernPitchEngine contract

This branch is a release-candidate source branch only while every invariant below remains true.

1. **One production output.** Quality, Live and Experimental use one full-signal time-domain pitch transport followed by one LPC envelope reconstruction path. The legacy spectral renderer, dry/wet rescue, Side bypass, parallel renderer, secondary synthesis layer and confidence-authority mix are not compiled into `ModernPitchEngine`.
2. **Replacement, never addition.** The clean transport is the complete audible ModernPitchEngine output. It is not summed with, crossfaded against or placed beside the renderer from `main`.
3. **Equal algorithmic quality.** Quality, Live and Experimental run the same detector, scale decision, transport interpolation and LPC reconstruction. Mode changes only the reported/working latency budget and the mode-aware Scale Lock timing/hysteresis defaults; it must not select a lower-quality renderer.
4. **Every active channel is corrected.** Linked stereo publishes one correction trajectory to every channel. Dual mono may detect and quantize independently, but every channel still passes through a complete transport and reconstruction path.
5. **No timid authority gate.** Confidence, periodicity, onset and breath evidence may stabilise note identity and decide whether analysis state is trustworthy. They must not expose an unprocessed parallel signal or reduce a valid requested correction.
6. **Amount is pitch depth.** `Amount` scales the correction destination in cents inside the single renderer. Zero amount produces ratio 1 through the same latency-aligned path; 100% requests the complete correction.
7. **Speed is continuous response.** `Speed` controls a continuous critically damped pitch trajectory. A target change must never reset correction to zero. With Scale Lock active, the same GUI knob is mapped to the mode-aware 0–7 ms domain.
8. **Humanize is audible and deterministic.** Humanize changes same-note interpretation, target timing and safe vibrato admission. It is not random drift and is not a dry-signal control. Its same-note tolerance is bounded by the actual minimum scale spacing so dense microtonal degrees remain distinguishable.
9. **Scale Lock is isolated and adaptive.** When disabled, its state does not alter normal correction. When enabled, adaptive hysteresis uses mode, tempo state, scale density, scale asymmetry and confidence. Target changes require coherent confirmation; custom and microtonal scales use their measured spacing rather than a 12-EDO assumption.
10. **Vibrato stays inside the locked note.** Vibrato Preserve is admitted only from stable, periodic observations and is attenuated near a target boundary. Humanize may increase safe admission but may not cause target hopping.
11. **Creative Tempo controls the pitch trajectory.** Tempo Glide changes the continuous correction time. Glide Lock freezes both audible and internal correction until release, then glides through the same renderer. Host PPQ/BPM loss falls back safely without a second audio path.
12. **Formant and breath controls remain full-signal.** LPC strength and breath-band conditioning operate after/inside the same full-signal transport. They do not recreate dry/wet mixing.
13. **No realtime allocation or blocking.** Audio processing performs no heap allocation, file I/O, locks or engine preparation. Scale publication and mode changes remain non-blocking for the callback.
14. **High Latency remains legacy.** Mode 0 is not rewritten by this branch. The integration replaces only the three ModernPitchEngine modes.
15. **Release-candidate gate.** Source is not a final binary release until Windows JUCE/VST3 CI passes and real DAW listening confirms: scale/root/custom-scale transport, obvious response from every GUI control, no wind/flanging, intelligible consonants, stable octave identity, equal perceived quality between the three modern modes and correct latency reporting.
16. **Parameter sensitivity gate.** Automated validation must fail when Amount, Speed, Humanize, Lock Hysteresis, Vibrato Preserve or Creative Tempo no longer produces a measurable change in the requested pitch trajectory or target identity.
17. **Compiler portability gate.** The same source and invariant suite must compile under the workflow's GCC and MSVC C++17 toolchains before the branch can be treated as a binary candidate.
18. **Sensors are evidence, never reduced power.** `VoiceEvidenceAnalyzer` is read-only and may publish harmonicity, breathiness, body energy, event strength, formant stability and spectral reliability. `LivePitchProcessor` may pass those values to the musical supervisor, but sensor evidence must not rewrite `Amount`, detector sensitivity, Formant, Transient Protection, Vibrato Preserve, Breath Reduction, Lock Hysteresis or Lock Strictness. Sensors decide how carefully state is interpreted; user-authoritative processing remains at the requested strength.

`Tests/CleanModernPitchEngineTest.cpp` is the automated minimum pitch/output gate. It must verify finite stereo output, target pitch in all modes, mode latency, equal corrected pitch between modes, Amount, Speed, Humanize, Lock Hysteresis, Vibrato Preserve, Tempo division/glide length, Glide Lock strength and Smart Onset.

`Tests/VoiceEvidenceAnalyzerTest.cpp` is the analysis-only gate. It must verify second-harmonic detection, voiced-vs-noise evidence, event response, coherent metric publication and byte-for-byte preservation of the analysed audio samples.

## Stable voice reconstruction and transport hand-off

- Formant-envelope analysis is streaming and host-block independent: a fixed 512-sample analysis window advances on a fixed 64-sample hop.
- LPC target/morph state is represented as bounded reflection coefficients (PARCOR). Every interpolated state stays inside the Schur-stable region; direct predictor coefficients are derived from that stable state and are never independently clamped or interpolated.
- Strong onset/noisy observations may freeze the last trustworthy envelope, but they never open a dry path or attenuate correction Amount.
- Formant evidence decides whether a new PARCOR target is trustworthy; it does not multiply down the user's Formant amount.
- The production dual-read transport remains one renderer. Its overlap window is narrowed and its read-head separation may receive only a bounded, smoothed period-guided nudge.
- Period guidance preserves the gain-weighted mean delay exactly, stays inside the original causal delay excursion, and cannot change declared latency.
- Period guidance is supervisory geometry only: it cannot create a second synthesis layer, parallel renderer, dry crossfade, or confidence-authority mix.

## Musical note-body supervisor

- **Missing F0 is not missing voice.** Once a note body is latched, a pitch-detector dropout must keep the exact current musical target while body/harmonic evidence remains positive.
- **Breath requires positive evidence.** Release/unvoiced is driven by sustained breath or absence evidence, and must also win when a breath happens to produce a spurious formally valid F0.
- **Stable means musically stable.** A long sung note with vibrato remains `stable` even while pitch and controller velocity move. `stable` never means zero derivative.
- **Transition is a bounded note boundary.** `transition` is entered only for a meaningful target-identity change and cannot persist beyond 120 ms merely because the correction controller has not converged.
- **Humanize describes within-note motion.** Its tolerance is capped to a fraction of the measured minimum scale step, so a dense custom/microtonal scale cannot inherit a chromatic-size same-note window.
- **Period guidance follows the note, not every F0 frame.** The transport period is latched to musical identity and then follows within-note motion on a long time constant. During attack/acquire/transition/release, the existing period-guidance memory is frozen instead of being ramped by the state change; exact unity must still collapse both read heads to declared latency.
- **Evidence is causal at the host-block boundary.** The current host block may update analysis for the next block, but evidence from future samples inside that same block cannot classify its first sample.


## Single wet Quality reconstruction experiment
- Production audio is the historical spectral shifter wet reconstruction before any delayed-dry, wet gate, level matching, Dry Trust, or cancellation logic.
- One synthesis layer only: note changes are expressed by the continuous correction trajectory, never a second synthesis layer/crossfade.
- Harmonic body and aperiodic residual are committed into one complex spectrum and one IFFT/OLA signal.
- Voice evidence may alter harmonic/noise classification and search urgency, but cannot scale Amount or correction cents.
- A latched note body with stale F0 enters acquire/search after 70 ms while preserving the last exact correction until a fresh pitch anchor is found or positive breath/absence releases the note.
