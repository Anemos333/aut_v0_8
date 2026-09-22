# F0 Detector Rebuild Contract V1

This document is the authoritative engineering contract for the F0 rebuild.

## 1. Audio-path invariant

- The audible signal has exactly one path.
- No detector, gate, confidence, transition, breath, consonant, noise, formant or voicing state may create a dry path, a wet/dry blend, a bypass fragment, a parallel reconstruction path, or differently processed audio fragments that are recombined later.
- The only permitted branch is in analysis: the periodic gate decides whether a frame is eligible for F0 measurement. This branch never carries or renders audio.

## 2. Periodic gate

The gate is a measurement switch only.

Input:
- current audio samples.

Output:
- one boolean decision: measure / do not measure.

Rules:
- If tonal/periodic material exists, it MUST pass.
- False positives are acceptable: uncertain material passes.
- False negatives are not acceptable: only material classified with high certainty as non-periodic/noise may be blocked.
- The gate may reject obvious air, hiss, noise and aperiodic material.
- The gate must not export harmonicity, breathiness, formant stability, lower-family evidence, confidence, consensus, reconstruction hints or any other downstream control.
- Gate state must not alter quantization, correction amount, target ownership, renderer behaviour, formant reconstruction, transient treatment or any audio path.
- The gate adds no independent lookahead beyond the samples already available to the detector.

## 3. Detector pipeline

The detector is conceptually:

    estimate -> validate family/periodicity -> refine -> publish stable F0

There are no independent detector paths with frequency-band ownership.

The detector observes audio only. It must not receive:
- previous quantized target;
- renderer state;
- correction trajectory;
- transport-period anchors;
- rescue state;
- downstream confidence authority;
- VoiceEvidenceAnalyzer authority;
- scale information.

The scale remains owned exclusively by ScaleQuantizer.

## 4. Acquisition

The V1 acquisition contract distinguishes product-critical vocal behaviour from observability-limited stress cases. This distinction exists in validation only; it MUST NOT create register modes, frequency-band ownership or different detector paths in production code.

For ordinary voice-like periodic material in the practical live-singing operating corpus:
- the first correct stable F0 must be published in strictly less than 10 ms from the beginning of usable periodic material;
- this remains true for difficult but realistic harmonic structures such as a dominant second harmonic and a missing fundamental;
- acquisition is measured from the actual periodic onset, not from a later confidence event.

For observability-limited cases, including unusually low sparse tones, pure or nearly pure sinusoids, and combinations of very low F0 with poor SNR:
- the 10 ms acquisition deadline is not a V1 release blocker when the available causal samples are physically insufficient to support the normal precision target;
- the detector must remain acquire/transition rather than publish a weak or invented stable F0;
- the first stable F0 must be published as soon as the normal family-safety and precision requirements become defensible from the available samples;
- the measured time-to-stable must be reported explicitly and minimized;
- no arbitrary timeout, register-specific fallback, octave shortcut or artificial subharmonic is permitted.

The relaxation applies to acquisition time, not to family correctness. A hard case may take longer; it may not become wrong.

An estimate may exist internally before validation, but it is not allowed to affect correction until validated stable.

## 5. Stable and transition

stable:
- contains a validated F0 coordinate;
- is the only detector state allowed to update the F0 used by correction.

transition:
- represents temporary ambiguity caused by note transitions, clicks, interruptions, consonants, breaths, sibilants, transients or other non-stable material;
- has no direct audio authority;
- MUST NOT publish an incoherent F0 into correction;
- correction holds the previous stable target until a new stable F0 is established.

Transition has no arbitrary fixed timeout. It lasts only as long as the input is genuinely ambiguous.

Required stress behaviour:
- no transient is accepted as stable F0;
- no genuinely periodic region is left in transition unnecessarily;
- a new periodic note reaches stable promptly;
- aperiodic interruptions cannot invent register or octave changes.

## 6. Accuracy

For every accepted stable F0:
- octave errors: exactly zero in the validation corpus;
- artificial subharmonics are forbidden: a component at F, 2F, 3F... is not by itself evidence that a lower F/n exists; a lower family may be published only when the observed harmonic structure requires that lower fundamental;
- when a low family and a higher primitive family are observationally ambiguous, the detector must not publish the artificial low family; it remains acquire/transition rather than manufacturing bass;
- hallucinated stable F0 on aperiodic/noise-only material: exactly zero;
- F0 error must be very small and measured in cents;
- the target acceptance epsilon for the precision corpus is 1.5 cents unless a stricter later contract replaces it.

A refiner is accepted only if it improves precision without violating acquisition, octave safety, hallucination safety, latency or CPU constraints.

## 7. Continuity

Continuity is physical, not bureaucratic.

- Consecutive stable F0 values on sustained periodic material must remain within the precision epsilon expected from the source.
- Clicks, consonants, breaths and signal gaps may cause transition.
- transition never rewrites the last stable correction target.
- No historical detector state may manufacture a stable F0 after a gap.
- No fake-stable tail, dry-translated tail or stale register anchor is permitted.

## 8. Latency

The plugin-reported latency must equal the effective end-to-end audible latency.

Product latency targets:
- Quality mode: 512 samples.
- Other modes: 256 samples.

Detector/gate work must fit inside the existing latency budget and may not silently add extra buffering or lookahead.

The live monitoring path must not acquire additional audible delay from the detector rebuild.

## 9. CPU and size

- CPU is minimized after correctness constraints.
- The smallest correct implementation wins.
- No feature or state survives because it is historically sophisticated.
- A seven-line detector that satisfies the contract is preferable to a seven-hundred-line detector that does not.
- Every retained primitive must have a measurable purpose: gate, estimate, validate or refine.
- Any primitive that does not improve a contract metric is removed.

## 10. Demolition rule

The existing detector architecture is not a compatibility target.

Do not preserve:
- multi-path frequency ownership;
- consensus bureaucracy;
- beam decoding;
- confidence authority;
- rescue modes;
- reacquisition anchors from downstream state;
- detector sensitivity changes driven by stale correction state;
- VoiceEvidence-derived detector authority;
- dry-authority logic;
- provisional F0 that can reach correction before validation;
- historical state machines whose purpose was to preserve naturalness by withholding correction.

Existing code may be reused only as low-level mathematical building blocks after independent validation.

## 11. Quantizer boundary

The ScaleQuantizer is preserved.

The detector publishes a validated physical F0.
The quantizer maps that F0 to scale geometry.
The renderer later reconstructs one authoritative audible path.

Detector uncertainty must never change scale authority. Naturalness controls, if any, belong after correct F0 measurement and around the chosen scale target, not inside the detector.

## 12. Acceptance corpus

At minimum, automated tests must include:
- clean periodic voice-like material over the realistic sung range;
- randomized source-filter voice-like material with independent harmonic phases and changing spectral envelopes;
- multiple vowel/formant profiles;
- breathy material, jitter, shimmer, vibrato and non-instantaneous onset envelopes;
- randomized harmonic amplitudes and spectral holes;
- monophonic instrument-like harmonic spectra whose envelope differs materially from the vocal fixtures;
- dominant second harmonic;
- missing fundamental;
- low and high SNR;
- onset burst;
- abrupt note changes;
- short gaps;
- clicks;
- consonant-like/breath-like/sibilant-like interruptions;
- white noise;
- coloured noise;
- pure sinusoid as a sparse-information safety/observability diagnostic, not as a proxy for ordinary sung material;
- sustained notes;
- transitions between distant registers.

The acceptance report must expose:
- first stable correct F0 time in ms;
- whether each case belongs to the product-critical vocal acquisition corpus or to the observability-limited diagnostic corpus;
- the count of product-critical vocal cases acquiring in <10 ms;
- the measured convergence time of observability-limited cases without converting them into an artificial 10 ms pass/fail;
- stable F0 cents error distribution;
- maximum consecutive stable-step error on sustained material;
- octave error count;
- hallucinated stable count;
- transition duration over truly periodic regions;
- transient-to-stable false acceptance count;
- gate false-negative count;
- CPU cost;
- effective plugin latency.

No detector version is promoted by subjective listening alone.

The simple deterministic harmonic corpus is a smoke test only. Passing it is not evidence of release readiness.

The randomized voice-like stress corpus is a mandatory structural gate. A detector that passes the simple corpus but produces wrong-family stable values, artificial subharmonics or octave errors on the randomized voice-like corpus is structurally failed and remains a test-only experiment.


## 13. V1 release interpretation

The V1 release is blocked by:
- any wrong-family stable F0;
- any artificial subharmonic or octave error;
- any hallucinated stable on aperiodic/noise-only material;
- failure to meet the normal precision target on accepted stable measurements;
- failure to acquire ordinary voice-like harmonic material promptly enough for live monitoring.

The V1 release is not blocked solely because an observability-limited stress case needs more than 10 ms, provided that:
- it remains acquire/transition until a defensible measurement exists;
- it converges to the correct family and precision as soon as the causal evidence allows;
- the delay is measured, documented and minimized;
- no register-specific production path is introduced to hide the limitation.

Such cases are documented as known V1 limitations and remain candidates for a later detector/engine redesign.


## 14. Development order: structure before tuning

Detector development is deliberately split into two ordered phases.

Phase A -- structural family safety:
- recover the physical F0 family on the product-critical vocal corpus;
- wrong-family stable count must be zero;
- artificial-low/subharmonic count must be zero;
- octave-error count must be zero;
- hallucinated stable count on aperiodic/noise-only material must be zero;
- ambiguous material may remain acquire/transition rather than publish a guessed coordinate;
- the same rules must hold on the randomized voice-like stress corpus, not only on deterministic harmonic fixtures.

No refiner or cents-level tuning result may promote a detector that fails Phase A.

Phase B -- precision tuning:
- begins only after Phase A is structurally safe;
- improves accepted stable measurements toward the precision epsilon;
- must not reduce structural safety or reintroduce family errors;
- tuning must generalize across seeds, spectral envelopes, formants and source profiles.

Experimental rescue, predictive, derivative, partial-spacing or other validators remain shadow/test-only until they independently improve the mandatory stress corpus without creating any new wrong-family or artificial-low result.

## 15. Efficiency gate

Correctness is necessary but not sufficient for the live product.

The acceptance report must continue to expose detector analysis cost. A candidate whose analysis cost is of the same order as, or greater than, the audio interval it analyzes is not a production candidate even if its offline accuracy improves.

Optimization must follow structural correctness, but additional validator layers are not accepted merely because they recover cases. If a new layer does not produce a clear improvement in structural safety per unit of CPU cost, it is removed rather than accumulated.


## 16. Whole-note periodicity principle

The detector is not a sinusoid hunter.

Its primary physical question is: what single periodic coordinate best explains the monophonic note as a whole?

Implications:
- a formant peak, strongest partial or isolated sinusoidal component is evidence about the note, not the note itself;
- harmonic amplitudes may change radically with vowel, instrument, microphone, articulation and dynamics without changing F0;
- missing fundamental and dominant-second cases must be solved as properties of the complete periodic waveform, not by assigning authority to one partial;
- the preferred family is the primitive repetition/fundamental of the complete periodic structure, not an arbitrary subharmonic grid that merely contains observed components;
- analysis may use spectral, time-domain or parametric mathematics, but no individual partial, detector band or historical track owns the physical F0;
- the architecture should resemble a robust monophonic tuner: one note-level periodicity estimate, independently validated, then refined.

This principle does NOT authorize restoring the old YIN/multi-rate tracker. The old implementation started from whole-wave difference/correlation but then generated alternative periods, band-dependent paths, octave-transposed consensus hypotheses, temporal beam state and confidence authority. Those mechanisms are not part of the new contract.

A whole-note method must still satisfy the single-ring, estimate -> validate -> refine -> stable/transition architecture and the mandatory randomized voice-like stress gate.


## 17. Downward-only fallback witness (Plan B)

A secondary downward-looking mechanism is permitted only as a future validation experiment if the primary whole-note primitive-period validator cannot eliminate octave-high errors by itself.

It is not a second detector and has no independent F0 ownership.

Constraints:
- it runs only inside the validate stage after the primary whole-note estimate exists;
- it may inspect only lower-family alternatives related to the primary candidate, not perform an unrestricted second F0 search;
- it is inactive unless the primary estimate contains strong internal evidence of being too high;
- it may speak only when the lower-family evidence is substantially stronger than the evidence required for an ordinary primary publication;
- it may veto or demote the primary estimate to transition, or confirm a longer primitive period when the waveform itself proves it;
- it may never publish a lower F0 merely because a lower harmonic grid can contain the observed components;
- it may never create a new artificial-low/subharmonic result on any training, holdout or verification corpus;
- if primary-error evidence and downward evidence are not both decisive, the result is transition, not a guessed lower stable F0;
- no confidence/evidence value from this witness survives beyond validation or reaches the renderer, quantizer or audio path.

The intended asymmetry is deliberate: octave-high recovery is useful, but manufacturing bass is considered the more serious structural failure.

This mechanism remains Plan B. It is not introduced into production while the single whole-note path can still be improved with a simpler primitive-period validation rule.


## 18. Evidence-adaptive acquisition window

The nominal live target remains a correct stable F0 in less than 10 ms whenever the available causal evidence is already sufficient.

The detector may continue observing beyond 10 ms only when the current whole-note estimate is internally ambiguous or not yet structurally defensible.

Rules:
- adaptation is driven by evidence quality/consistency, never by a hard-coded vocal register or frequency band;
- an easy low note and an easy high note are treated identically if the periodic evidence is equally decisive;
- cases that are already structurally stable inside the normal window must not be delayed merely because other cases need more observation;
- the same estimator/validator continues accumulating evidence; no alternate detector path is activated;
- the extension ends immediately when a structurally valid F0 becomes available;
- the acceptance report must expose how many cases publish inside the normal window and the additional acquisition time of delayed cases;
- adaptive delay is preferable to publishing a wrong family, octave or artificial subharmonic.

This mechanism is part of the primary whole-note path. It is not the downward-only Plan B described below.
