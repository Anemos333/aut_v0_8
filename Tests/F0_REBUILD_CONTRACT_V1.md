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

For periodic material:
- the first correct stable F0 must be published in strictly less than 10 ms from the beginning of usable periodic material;
- this applies to difficult but realistic sung material, including dominant second harmonic and missing-fundamental cases;
- acquisition is measured from the actual periodic onset, not from a later confidence event.

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
- pure sinusoid;
- sustained notes;
- transitions between distant registers.

The acceptance report must expose:
- first stable correct F0 time in ms;
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
