# F0 Dirty Real-World Validation Plan

This phase begins only after the current structural whole-note detector has resolved the remaining family errors on the controlled voice-like corpus.

Its purpose is adversarial validation, not benchmark decoration.

## 1. Method rule

Results from deterministic/synthetic development fixtures are not sufficient to support product-quality claims.

Before detector promotion, the same frozen candidate must be tested on hostile material that was not used to choose thresholds, windows, validators or tuning constants.

Do not modify the detector while evaluating a held-out dirty set. If a failure causes an algorithm change, that set becomes development data and a new unseen holdout is required.

Reported quality claims must identify the tested population and interference conditions. Do not generalize clean-corpus numbers to noisy home-studio or live-stage use.

## 2. Dirty scenarios

At minimum include foreground sung voice mixed with independent real-world interference from these families:

- home-studio environmental noise: computer/fan/HVAC, room noise, chair/desk movement, keyboard/mouse, distant speech;
- road and construction noise: traffic, engines, trucks, pneumatic/impact tools, drilling, intermittent bangs;
- weather: heavy rain, thunder, wind, window/roof rain impact;
- live-stage noise: crowd, PA bleed, drums, bass, guitar, keys, cymbals, monitor spill;
- musical bleed: one competing instrument, several instruments, and full backing track under the vocal;
- impulsive contamination: doors, cable/contact bumps, mic handling, stand impact, claps and short broadband events.

Use multiple recordings per family and keep source recordings independent from the synthetic generator used during detector development.

## 3. Mixture ladder

Each interference family must be tested across a foreground-to-interference ladder rather than one convenient level.

Suggested initial ladder for evaluation:

- +20 dB foreground/interference;
- +12 dB;
- +6 dB;
- 0 dB;
- -6 dB;
- lower levels only as diagnostic stress when useful.

The exact release boundary is empirical and must be reported from the results rather than selected in advance to make the detector pass.

For strongly non-stationary interference, also test short local bursts whose instantaneous ratio is much worse than the file-average ratio.

## 4. Ground truth

The clean foreground vocal is the F0 reference.

Generate the contaminated input by mixing a clean vocal/reference signal with an independently sourced interference recording after level normalization.

Keep the clean foreground and its known/reference F0 track separate from the mixed detector input. Never derive ground truth from the contaminated signal itself.

Real vocal recordings with reliable independent annotation are preferred for final validation. Synthetic/source-filter voices remain useful for controlled diagnostics but are not sufficient alone.

## 5. Required behaviour

When the foreground voice remains physically observable:

- correct F0 family is preferred over acquisition speed;
- no artificial subharmonic;
- no octave flip caused by an interferer;
- no stable F0 may lock to construction machinery, thunder resonance, instruments, bass line or backing-track melody instead of the foreground voice;
- time-to-safe-family and time-to-precision must remain as short as the evidence allows.

When the foreground voice becomes genuinely non-identifiable in the mixture:

- transition/no-new-F0 is acceptable;
- publishing a confident but wrong competing source is not acceptable;
- the previous stable target may be held according to the detector contract, but the interference must not create a new stable target.

The detector is still a monophonic foreground detector. A polyphonic mixture is a robustness stress, not permission to add source separation, multiple pitch owners or a second audio path.

## 6. Metrics

For every interference family and mixture level report at least:

- total voiced reference duration/cases;
- correct stable family rate;
- wrong-family stable count and rate;
- octave-high count;
- artificial-low/subharmonic count;
- competing-source lock count where identifiable;
- transition/withhold rate;
- time-to-safe-family distribution;
- time-to-precision distribution;
- cents-error distribution for accepted stable F0;
- maximum uninterrupted false-stable duration;
- recovery time after a transient interferer disappears;
- CPU cost under the same detector candidate.

A low publication rate and a low wrong-stable rate are not equivalent. Report both.

## 7. Holdout discipline

Use at least three data roles:

1. development stress set -- failures may inform algorithm changes;
2. validation set -- used to select between frozen candidates, not to tune thresholds repeatedly;
3. final unseen holdout -- opened only after the detector and constants are frozen.

After any detector change caused by final-holdout behaviour, that holdout is no longer final and must be replaced.

## 8. Promotion and claims

The detector is not promoted because it performs well on clean synthetic fixtures.

Promotion requires:

- controlled structural corpus passed;
- precision corpus passed to the accepted target;
- hostile real-world corpus evaluated with no hidden subset removal;
- dirty-condition failures documented by interference type and level;
- no marketing/project claim broader than the actual validated range.

If performance degrades beyond a certain SIR/SNR, report that boundary as a measured limitation instead of hiding it behind an aggregate score.

The objective of this phase is to find conditions under which the detector fails before users do.