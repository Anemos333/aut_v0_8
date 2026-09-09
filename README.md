# Neumaton

**Experimental microtonal vocal pitch-correction plugin built with C++17 and JUCE 8.0.12.**

> **Project status: active development / pre-release.**
>
> Neumaton is already a functioning audio plugin and the current engine is far beyond a proof of concept, but it is **not yet a finished commercial release**. The main pitch-correction contract is becoming stable; the remaining work is concentrated on timbre, consonants/high-frequency behaviour, vibrato handling, target decisions during difficult transitions, and broad real-world validation.

Current development branch documented here:

`single-wet-quality-reconstruction`

---

## What Neumaton is

Neumaton is a vocal pitch-correction project designed around two ideas:

1. **Pitch correction should remain authoritative when a usable vocal signal is present.**
2. **The modern engine should produce one coherent audible reconstruction path, instead of weakening correction by falling back to parallel dry/rendered paths whenever the detector becomes uncertain.**

The project supports conventional and microtonal tuning systems, custom user scales, multiple latency modes, Scale Lock, tempo-aware correction behaviour, state persistence, and an optional output colour stage.

The current work is focused less on adding features and more on making the existing engine musically reliable enough to deserve a release.

---

## Current status

### Short version

The most important recent improvement is that the engine no longer treats detector confidence/consensus as permission to simply stop correcting audible voiced material.

The rigid Scale Lock path now separates:

* the **continuity / musical identity coordinate**, which may remain smoothed to prevent unstable note ownership;
* the **live correction coordinate**, used to calculate the actual correction depth at the fully rigid endpoint.

That distinction matters because tracker smoothing should help decide *which note owns the sound* without leaving an audible residual pitch error once the target is known.

This is a major architectural improvement, but it does **not** mean the plugin is finished.

### Honest status table

| Area                                            | Current state                                                         |
| ----------------------------------------------- | --------------------------------------------------------------------- |
| Basic plugin operation                          | **Implemented**                                                       |
| VST3 build                                      | **Implemented**                                                       |
| AU build on supported Apple systems             | **Configured**                                                        |
| Conventional 12-EDO scales                      | **Implemented**                                                       |
| Microtonal tuning systems                       | **Implemented**                                                       |
| Custom scales                                   | **Implemented**                                                       |
| Project/session state restoration               | **Implemented**                                                       |
| Modern single-audio-path architecture           | **Implemented**                                                       |
| Multi-rate pitch tracking + consensus           | **Implemented and actively refined**                                  |
| Strong correction / hard Scale Lock             | **Substantially improved; still under validation**                    |
| Sustained-note pitch authority                  | **Promising / currently one of the stronger parts**                   |
| Rap / weakly periodic vocal material            | **Much improved, not considered solved for every source**             |
| Target choice on attacks and glissandi          | **Still an active problem**                                           |
| Consonants / sibilance / high-frequency texture | **Needs more work**                                                   |
| Timbre transparency                             | **Not release-ready yet**                                             |
| Vibrato behaviour                               | **Implemented in part, still being tuned**                            |
| Mode-switch continuity                          | **Still a release-validation item**                                   |
| CPU / real-time performance                     | **Designed for real-time use; final profiling still required**        |
| “Zero latency”                                  | **No — the current modern renderer has real, mode-dependent latency** |
| Large DAW / hardware compatibility matrix       | **Not complete**                                                      |
| Commercial release readiness                    | **Not yet**                                                           |

---

## Audio architecture

The modern engine is intentionally structured so that analysis can become sophisticated without multiplying audible signal paths.

A simplified view is:

```text
Input
  |
  +--> analysis-only conditioning
  |
  +--> multi-rate pitch tracker
          |
          +--> pitch candidates
          +--> confidence / periodicity
          +--> detector consensus
          +--> octave / continuity decoding
  |
  +--> scale quantizer + correction supervisor
          |
          +--> target ownership
          +--> Scale Lock / Humanize / Speed
          +--> correction trajectory
  |
  +--> SingleWetSpectralRenderer
          |
          +--> spectral pitch reconstruction
          +--> phase propagation
          +--> formant-preservation support
  |
  +--> optional analog/output stage
  |
Output
```

### Single-audio-path contract

`VoiceEvidenceAnalyzer` is analysis-only.

It can influence detector and supervisor decisions, but it does not render audio, provide a dry replacement signal, or reduce the requested Amount simply because the analysis becomes uncertain.

Likewise, switching Quality / Live / Experimental selects one already-prepared modern engine. The plugin does not crossfade multiple pitch engines in parallel as a hidden fallback strategy.

This is one of the central design constraints of the current branch.

---

## Pitch tracking

The modern tracker uses four analysis rates:

* full rate;
* half rate;
* quarter rate;
* eighth rate.

Candidates are evaluated with YIN-style difference / cumulative-normalized analysis, periodicity and confidence measures, then combined through cross-rate consensus and temporal decoding.

The tracker also contains explicit octave-transition logic. This is necessary because a vocal detector can easily confuse a fundamental with one of its harmonic or subharmonic relatives.

Consensus is therefore used as **evidence**, not as an on/off permission switch for the audible correction.

That distinction is recent and important.

---

## Scale Lock

Scale Lock is intended to provide a genuinely rigid correction mode rather than merely an aggressive version of normal retuning.

Current controls include:

* **Scale Lock**
* **Lock Hysteresis** — 0 to 80 cents
* **Vibrato Preserve** — 0 to 100%
* **Humanize**
* **Speed**
* **Amount**

The hard-lock path is being developed around a simple rule:

> Once a valid target owns the vocal signal, continuity smoothing must not become permanent audible pitch error.

The current engine therefore maintains separate coordinates for stable target identity and live correction depth at the fully rigid endpoint.

This area has dedicated invariant/regression tests, but real recorded voices remain the final authority.

---

## Supported tuning systems

The current scale definitions include:

### 12-EDO

* Chromatic
* Major / Ionian
* Dorian
* Phrygian
* Lydian
* Mixolydian
* Natural Minor / Aeolian
* Locrian
* Melodic Minor
* Harmonic Minor
* Major Pentatonic
* Minor Pentatonic

### Microtonal EDO

* 19 EDO
* 24 EDO
* 31 EDO

### Historical / non-12-EDO systems

* Pythagorean
* Ptolemaic / Just Intonation
* Byzantine modes
* Arabic maqamat
* Slendro
* Pelog

Some non-Western tuning definitions are necessarily encoded as specific computational approximations. They should not be interpreted as a claim that one fixed table can represent every regional, historical or performance practice associated with those traditions.

### Custom scales

User-created scales are supported and stored with the plugin state.

The custom-scale subsystem is an important part of the project, but dense and highly asymmetric user scales are also useful stress tests for target hysteresis and note ownership, so they remain part of ongoing validation.

---

## Processing modes

The codebase currently contains a legacy Slow path plus three modern modes:

* **Quality**
* **Live**
* **Experimental / ultra-live**

The modern modes share the same core architecture but use different latency/analysis configurations.

The legacy Slow mode is intentionally kept separate and largely preserved for regression/reference purposes.

### Latency

Neumaton should **not** currently be described as a zero-latency plugin.

The modern pitch renderer is frame-based and reports/aligns its processing latency. Lower-latency modes trade analysis/spectral resolution for responsiveness.

Final published latency figures should be measured per sample rate and build before release rather than copied from design assumptions.

---

## Main user controls

The current processor exposes:

* **Speed** — 0–500 ms
* **Amount** — 0–100%
* **Humanize**
* **Scale Lock**
* **Lock Hysteresis**
* **Vibrato Preserve**
* **Creative Tempo Mode**

  * Off
  * Tempo Glide
  * Glide Lock
* **Tempo Division**
* **Tempo Glide Length**
* **Glide Lock Strength**
* **Smart Onset**
* **Analog Mode**
* **Output Volume**

The engine also contains additional internal tuning parameters for formant preservation, transient protection, detector sensitivity, correction range and related behaviour. Not every internal parameter is currently a public GUI control.

---

## Creative Tempo

Creative Tempo is an optional target-transition scheduler.

It can use host transport/tempo information for tempo-related glide behaviour. When disabled, it is intended to stay out of the ordinary correction path.

This feature is useful creatively, but it is secondary to the current priority: stable pitch authority and clean reconstruction.

---

## Analog output stage

The plugin includes an optional lightweight output-colour stage with:

* soft saturation;
* low/high shelving;
* output gain;
* a safety ceiling.

This stage is not part of pitch detection or target selection and can be disabled.

It should be considered a small tonal option, not a detailed physical model of analog hardware.

---

## State persistence

Plugin state is serialized through JUCE's `AudioProcessorValueTreeState`.

The saved state includes, among other things:

* exposed parameters;
* selected scale;
* custom scale preset selection;
* root note;
* processing mode;
* factory preset index;
* custom scales.

Backward compatibility logic also exists for older sessions that stored the previous live-mode flag.

---

## What currently sounds good

Based on the present development tests, the project is strongest when:

* a sustained monophonic vocal has a clear intended target;
* hard Scale Lock is deliberately requested;
* the goal is obvious pitch authority rather than maximum naturalness;
* the source stays sufficiently vocal/periodic for stable note ownership.

Recent listening tests have shown an important improvement: long vocal notes can remain visibly and audibly locked instead of drifting between corrected and quasi-natural sections, and correction can remain active on difficult rap-like material where older builds tended to retreat too easily.

That progress is real.

It is also narrower than saying “the autotune is finished.”

---

## What still needs work

### 1. Timbre

This is currently the largest release blocker.

The engine can impose the intended pitch more reliably than before, but the reconstructed voice is not yet consistently transparent enough.

Known areas still being worked on include:

* metallic or synthetic high-frequency texture;
* consonant handling;
* sibilance / hiss character;
* transitions between voiced and noisy phonemes;
* preservation of vocal identity during stronger shifts.

### 2. Vibrato

Vibrato preservation exists, but the final musical behaviour is not closed.

The desired result is not simply “keep all vibrato” or “remove all vibrato.” A release-quality implementation needs to preserve stable expressive modulation without letting it destabilize target ownership or reintroduce obvious pitch error.

### 3. Attacks, glissandi and note ownership

The main pitch-depth problem is much closer to solved than it was earlier in development.

The harder remaining question is often:

> Which target should own this very short transition?

rather than:

> Why did the engine fail to apply enough correction to a target it already chose?

Fast attacks, slides, consonants and deliberately ambiguous vocal gestures still need broader testing.

### 4. Corpus validation

Synthetic/invariant tests are valuable, but a vocal processor cannot be validated only with unit tests.

Before a public release the engine needs a larger and more systematic corpus containing:

* different singers and vocal ranges;
* clean and noisy recordings;
* rap and speech-like delivery;
* soft / breathy singing;
* strong vibrato;
* glissandi;
* extreme retuning;
* dense microtonal scales;
* different sample rates and host block sizes.

### 5. DAW and platform validation

The repository contains CI workflows for major desktop platforms, but that does not replace actual plugin-host testing.

A release should be tested in multiple current DAWs, with automation, project save/restore, bypass, sample-rate changes and mode changes.

---

## Tests

The repository contains dedicated tests for the modern engine, including targets for:

* clean ModernPitchEngine invariants;
* voice-evidence analysis;
* correction/supervisor continuity;
* GUI audibility contracts;
* GUI-to-audio control contracts;
* the single-wet spectral renderer.

There are also regression/corpus-oriented test utilities in the repository.

These tests are useful because they protect architectural contracts such as:

* no accidental correction bypass;
* correct target/correction coordinates;
* scale-lock residual behaviour;
* renderer pitch authority;
* GUI controls reaching the DSP.

They are **not** a substitute for listening tests.

---

## Build

### Requirements

* CMake 3.22 or newer
* C++17 compiler
* Git
* a supported desktop plugin build environment

JUCE **8.0.12** is fetched automatically by CMake through `FetchContent`.

### Clone and select the current development branch

```bash
git clone https://github.com/Anemos333/aut_v0_8.git
cd aut_v0_8
git checkout single-wet-quality-reconstruction
```

### Configure

```bash
cmake -S . -B build -DNEUMATON_BUILD_CLEAN_ENGINE_TESTS=ON
```

### Build

```bash
cmake --build build --config Release
```

The main CMake target is `Neumaton`.

The current CMake project requests:

* **VST3**
* **AU**

Actual format availability depends on the host operating system and JUCE toolchain.

---

## Repository note

This repository is an active R&D workspace.

It contains historical integration notes, older README files, patches and experiments that were useful during development but do not all describe the current audible code path.

For the current branch, the most reliable sources of truth are:

1. the compiled CMake source list;
2. the current `ModernPitchEngine`;
3. `LivePitchProcessor`;
4. `SingleWetSpectralRenderer`;
5. the current tests.

Old design documents should be read as development history, not necessarily as current behaviour.

---

## Roadmap to a release candidate

The present order of work is intentionally conservative:

1. **Freeze the correction-authority contract**
   If a usable vocal signal has a valid musical target, correction must not disappear because a confidence helper becomes conservative.

2. **Finish timbral reconstruction**
   Especially consonants, sibilance, high-frequency texture and vocal identity.

3. **Finish vibrato behaviour**
   Preserve expression without compromising target stability.

4. **Validate transitions and target ownership**
   Attacks, slides, rap, fast phrases and difficult microtonal boundaries.

5. **Run a broad audio/DAW regression matrix**
   Different voices, sample rates, buffer sizes, modes, automation and state restoration.

6. **Only then call it release-ready.**

---

## Project philosophy

Neumaton is not trying to hide incomplete correction behind a pleasant dry blend.

The current direction is:

> **First make pitch decisions and pitch authority correct. Then make the corrected sound beautiful.**

That order can make development builds sound harsher than a superficially safer design, but it also makes problems measurable and fixable instead of masking them.

The final goal is not maximum correction at all times. The goal is **deliberate** correction: hard when the user asks for hard lock, natural when Humanize/Vibrato ask for movement, and predictable across conventional and microtonal scales.

---

## Release warning

At the current stage, Neumaton should be treated as **experimental software**.

Do not rely on this branch for irreplaceable production sessions without keeping backups or rendered stems.

There is currently no reason to pretend the remaining issues are small polishing details: the core concept is credible and increasingly robust, but release-quality vocal timbre and broad compatibility still need real work.

That is exactly what the project is working on now.

---

## License

A public redistribution license is not currently defined in this branch.

A clear license should be added before treating the repository as a finished open-source or redistributable release.
