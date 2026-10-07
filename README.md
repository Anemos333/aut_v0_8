# Neumaton

**Experimental microtonal vocal pitch-correction plugin built with C++17 and JUCE 8.0.12.**

> **Project status: closed beta.**
>
> The rebuilt V1 pitch engine is now the accepted Neumaton architecture. A multi-voice dry/wet validation matrix, targeted low-latency splice retests, and cross-platform CI have passed the closed-beta gate. Development from this point is refinement and release validation, not a return to the previous pitch engine architecture.

---

## What Neumaton is

Neumaton is a vocal pitch-correction and creative tuning project aimed at conventional and microtonal music.

The current engine follows a deliberately small contract:

1. detect the fundamental accurately and continuously when periodic material is present;
2. keep uncertain consonants, breaths and interruptions from changing the audible target;
3. quantize the stable fundamental to the selected tuning system;
4. move the correction trajectory according to the musical controls;
5. render the result through one and only one audible path.

The project is designed for live and studio use, with particular attention to low latency, strong correction, microtonal scales and predictable behaviour under difficult real-world vocal material.

---

## Current architecture

The V1 engine was rebuilt from zero after the previous architecture became too complex and too dependent on confidence, consensus and fallback behaviour.

The active signal path is now:

```text
Input
  |
  +--> MeasurementGate
  |      |
  |      +--> measure / do not measure
  |
  +--> FundamentalDetector
  |      |
  |      +--> estimate
  |      +--> validate
  |      +--> refine
  |      +--> Acquire / Stable / Transition
  |
  +--> ScaleQuantizer
  |      |
  |      +--> exact scale target
  |
  +--> PitchCorrectionTrajectory
  |      |
  |      +--> Speed / Amount / Humanize
  |      +--> hold previous Stable correction during Transition
  |
  +--> SinglePathPitchRenderer
  |      |
  |      +--> one fractional read head
  |      +--> waveform-matched recentering
  |
  +--> optional output colour / level stage
  |
Output
```

### One-audio-path rule

The V1 renderer has no parallel dry branch, no dry/wet fallback, no OLA reconstruction, no grain bank and no band-by-band recombination.

Analysis may inspect the signal, but exactly one rendered sample path reaches the output.

Host bypass also stays inside the same renderer at ratio 1.0 rather than opening a second delay path.

---

## PitchCore

`PitchCore` contains two responsibilities only:

### Measurement gate

The gate answers one question: **should the detector measure this material?**

It does not produce audio, gain, blend values or downstream timbral descriptors. Uncertain material is deliberately allowed through; clear non-periodic/noise material may be rejected.

### Fundamental detector

The detector is tuner-like rather than consensus-based:

```text
estimate -> validate -> refine
```

There are no parallel pitch trackers and no confidence-weighted voting engines in the active V1 path.

The public tracking states are:

- `Acquire`
- `Stable`
- `Transition`

`Transition` is measurement state only: it cannot move the audible target. Until a new Stable fundamental is accepted, the correction trajectory remains tied to the previous Stable value.

The working pitch range is currently 45–1600 Hz by default.

---

## Quantizer and tuning systems

The quantizer receives an already-stable fundamental and chooses the mathematically nearest scale centre.

The active quantizer supports generic equaves rather than assuming that every tuning system must repeat at 2:1, although the current plugin integration normally uses a 2:1 equave.

Neumaton includes conventional scales, microtonal systems and custom scale support. Scale definitions and user presets remain separate from the detector so musical tuning decisions do not gain hidden authority over F0 measurement.

At the rigid endpoint the intended target is the scale centre itself. `Amount` and `Humanize` control how strongly the trajectory is allowed to approach that target; they do not redefine the mathematical centre of the scale degree.

---

## Renderer

The active renderer is `SinglePathPitchRenderer`.

It uses one fractional read head over one input history buffer with four-point cubic interpolation. When accumulated pitch transport pushes the read head outside its allowed delay region, the renderer selects a new source position by comparing candidate waveform history with the recently emitted waveform.

This comparison is measurement only: there is still no overlap/crossfade or second audible path.

The splice selector uses a longer waveform context than the first V1 implementation so that a recentering decision is based on meaningful vocal-cycle structure rather than only a very short local slope. In the 128-sample profile the selector also evaluates the immediate splice edge and local slope, preventing a history match from winning when it would create a one-sample discontinuity.

This low-latency continuity fix passed the targeted closed-beta retest on the previously failing real-vocal material.

---

## Processing modes and latency

The plugin currently exposes three processing profiles:

| UI mode | V1 mode | Declared latency |
| --- | --- | ---: |
| Experimental | Low Latency | 128 samples |
| Live | Live | 256 samples |
| Quality | Studio | 512 samples |

These are real renderer delays, not labels for hidden parallel engines.

All three V1 engines are prepared in advance; switching mode selects which prepared engine owns the single audible path.

The plugin should not be described as zero-latency.

---

## Controls

The current parameter surface includes:

- **Speed** — 0–500 ms
- **Amount** — 0–100%
- **Humanize** — 0–100%
- **Scale Lock**
- **Lock Hysteresis**
- **Vibrato Preserve**
- **Creative Tempo Mode**
- **Tempo Division**
- **Tempo Glide Length**
- **Glide Lock Strength**
- **Smart Onset**
- **Analog Mode**
- **Output Volume**

### V1 compatibility note

The rebuilt engine intentionally does not inherit old DSP authority just because a legacy control still exists in the GUI or saved state.

`Speed`, `Amount`, `Humanize`, scale selection/root and the V1 latency mode are part of the active rebuilt correction path.

Some older Scale Lock sub-controls and Creative Tempo values are currently retained for UI/session compatibility while their final V1 semantics are rebuilt. They must not silently reintroduce legacy detector, consensus or renderer logic.

---

## Current status

| Area | State |
| --- | --- |
| VST3 build | Implemented |
| C++17 / JUCE 8.0.12 build | Implemented |
| Rebuilt V1 pitch engine | Active |
| Single-audio-path renderer | Active |
| 128 / 256 / 512 sample modes | Active |
| Conventional scale correction | Active |
| Microtonal/custom scales | Active |
| Stable/Transition target hold | Active |
| Legacy detector/renderer in active VST target | Removed |
| Timbre under strong correction | Passed closed-beta voice matrix; continued refinement |
| Tail / splice continuity | Passed closed-beta gate; sparse scales remain a known stress case |
| Low-latency 128-sample splice continuity | Targeted blocker fixed and retested |
| Difficult attacks and vocal transitions | Continued beta validation |
| Broad DAW/platform matrix | Incomplete |
| Closed beta readiness | Accepted |
| Licensing / entitlement skeleton | Active; isolated from DSP |
| Community Pack integrity | Schema 3 SHA-256 + future signature boundary |
| Public release readiness | Not yet |

The previous `ModernPitchEngine` / `SingleWetSpectralRenderer` implementation is no longer part of the active VST target. Some legacy source and tests remain in the repository as development history and regression material.

---


## Licensing and Community Packs

Neumaton's planned licensing model keeps the **audio engine identical across tiers**. The Free edition is intended to remain a complete, unrestricted VST for music creation; paid tiers add supporter/creator benefits rather than unlocking better pitch correction.

The current release plan is:

| Tier | Planned rights / extras |
| --- | --- |
| **Free** | Complete VST, normal commercial use of music made with Neumaton, creation/export of Community Packs for free redistribution |
| **Supporter — €30 planned** | Same complete VST, additional skins/factory content, roadmap/early access, commercial creator right for Neumaton packs, up to 5 activated devices |
| **Supporter Studio — €50 planned, future** | Same Supporter rights with a larger device allowance, currently planned as 15 devices |

These prices and commercial terms are pre-release plans, not yet the repository's legal software licence.

### Licensing architecture

Licensing is deliberately outside the signal path. `PitchCore`, `PitchEngineV1`, `SinglePathPitchRenderer`, `LivePitchProcessor` and `ModernPitchEngine` do not consult entitlement state, and `processBlock()` does not perform licence checks.

The current closed-beta skeleton consists of:

- `EntitlementManager` with `Free`, `Supporter`, `Supporter Studio` and `Beta` tiers;
- a small policy layer for supporter content, early access, commercial pack export and future device limits;
- no account/login flow, network dependency, hardware fingerprinting or DSP-side DRM;
- safe fallback to the Free tier when a future entitlement cannot be validated.

The closed-beta build currently defaults to the `Beta` entitlement. Beta exposes supporter-facing content for testing but is intentionally **not** treated as a paid commercial creator licence.

### Community Pack schema 3

The existing inspectable `.ecpk` format remains the Community Pack container. Schema 3 adds:

- `Community` / `Commercial` licence metadata;
- optional creator identity metadata;
- a deterministic SHA-256 digest of scales, presets and scenes;
- reserved `keyId` and `signature` fields;
- a stable signing payload for a future server-side signature verifier.

Local packs remain readable and portable rather than encrypted. A locally edited or corrupted schema-3 payload fails its integrity check. A pack that merely claims to be `Commercial` without a valid future server signature is never considered a verified commercial pack.

Schema 1/2 packs remain readable and are conservatively interpreted as legacy Community Packs.

The future production design keeps private signing keys on the Neumaton server. The plugin will contain only public verification material; account activation, payment and server-side device counting are intentionally not part of the closed-beta skeleton.

---

## What currently matters most

The new architecture has shifted the development problem from “make a large fragile engine behave” to much smaller, testable questions. The architecture itself is frozen for closed beta: changes should now be local, evidence-driven fixes or release work rather than structural rewrites.

The main validation targets now are:

- long vowels and note tails under strong correction;
- waveform recentering/splice continuity;
- noisy room and live material;
- consonants, breaths and sibilants without target corruption;
- rapid note changes and glissandi;
- low-frequency acquisition where more signal history is physically necessary;
- extreme correction without octave or subharmonic capture;
- real DAW behaviour at small block sizes.

Some transitions to another scale degree during long vowels can be musically intentional under extreme settings. Sparse scales with widely separated centres remain a known stress case for tail behaviour, but the closed-beta matrix did not show a persistent false-target failure class. The previously identified one-sample low-latency splice discontinuity has been fixed and retested.

---

## Tests and CI

The repository contains focused V1 diagnostics including:

- `PitchCoreStressTest`
- `SinglePathPitchRendererTest`
- detector diagnostics and corpus probes
- correction-trajectory invariants
- renderer continuity/phase diagnostics
- VST build checks

The dedicated V1 workflow also verifies that legacy pitch DSP is not compiled into the active plugin target.

Synthetic tests are used only for structural invariants where appropriate. Pitch-quality decisions are increasingly tested on real recorded vocal material, including difficult/noisy sources rather than only ideal periodic signals.

---

## Build

### Requirements

- CMake 3.22 or newer
- C++17 compiler
- Git
- a desktop plugin build environment supported by JUCE

JUCE 8.0.12 is fetched automatically by CMake.

### Clone

```bash
git clone https://github.com/Anemos333/aut_v0_8.git
cd aut_v0_8
```

The current development architecture lives on `main`.

### Configure

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

To include the standalone DSP diagnostics:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEUMATON_BUILD_CLEAN_ENGINE_TESTS=ON
```

To build the licensing / pack-integrity contract test:

```bash
cmake -S . -B build-licensing -DCMAKE_BUILD_TYPE=Release -DNEUMATON_BUILD_LICENSING_TESTS=ON
cmake --build build-licensing --target LicensingSkeletonTest
```

### Build

```bash
cmake --build build --config Release
```

The active plugin target is `Neumaton` and the current CMake configuration builds **VST3**.

---

## Repository structure

The most relevant active V1 files are:

- `Source/PitchCore.h/.cpp` — measurement gate, F0 detector and scale quantizer
- `Source/PitchCorrectionTrajectory.h` — correction motion and Stable/Transition ownership
- `Source/SinglePathPitchRenderer.h/.cpp` — single audible pitch transport path
- `Source/PitchEngineV1.h/.cpp` — serial V1 engine
- `Source/LivePitchProcessor.h` — JUCE/block adapter and mode selection
- `Source/PluginProcessor.cpp` — plugin integration, state and output stage
- `Source/ScaleDefinitions.*` / `CustomScalePresets.*` — tuning material
- `Source/Entitlement.h` — tier/policy skeleton; no audio authority
- `Source/ShareablePack.h` — versioned `.ecpk` container and schema-3 payload integrity
- `Source/PackVerifier.h` — future signature-verification boundary
- `Source/CommunityPackLibrary.h` — local Community Pack discovery/composition

For the current audible plugin, the CMake target source list is the primary source of truth. Files belonging to older engines may still exist for regression/reference work without being compiled into the active VST.

---

## Project philosophy

Neumaton is intended as a small, human-centred musical tool rather than a collection of hidden corrective fallbacks.

The current design principle is:

> **Measure the pitch correctly, make the musical target explicit, then move one coherent audio path to that target.**

Uncertainty in analysis should not automatically create a second audible signal or weaken correction in an uncontrolled way. When a detector enters `Transition`, the last valid musical trajectory remains authoritative until new periodic evidence is stable enough to replace it.

Strong or robotic correction is not treated as a failure mode when the user explicitly asks for it. Naturalness is a musical control, not a justification for an engine that retreats from the target.

---

## Closed beta status

Neumaton is now in **closed beta**.

The V1 architecture is the accepted production direction for the project. The closed-beta gate included multiple real vocal recordings processed through repeated preset/scale configurations, explicit inspection of correction behaviour, timbre, tails and low-latency splice continuity, plus successful Linux, Windows and macOS VST3 CI builds.

This is still pre-release software. Closed beta does not mean public-release readiness: latency still deserves dedicated end-to-end measurement, the GUI is awaiting its final artwork/workflow pass, sparse-scale tail behaviour remains a known stress case, and broader DAW/platform validation is still required. Keep backups or rendered stems when using beta builds in important sessions.

---

## License

A public repository/software redistribution licence is not currently defined.

The planned Free / Supporter / Supporter Studio product tiers described above govern the intended product and Community Pack model; they do **not** yet replace a formal software EULA, Community Pack licence, Commercial Pack creator licence or repository source-code licence.

Those legal documents should be finalised before public release or a production marketplace.
