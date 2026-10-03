# Neumaton

**Experimental microtonal vocal pitch-correction plugin built with C++17 and JUCE 8.0.12.**

> **Project status: active development / pre-release.**
>
> Neumaton is a functioning VST3 plugin with a rebuilt low-latency pitch engine now serving as the main architecture. The current focus is real-world validation, tail/splice continuity, difficult transitions, timbre under strong correction, and release readiness.

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

The current splice selector uses a longer waveform context than the first V1 implementation so that a recentering decision is based on meaningful vocal-cycle structure rather than only a very short local slope.

This area is currently undergoing real-world listening tests, especially on long vowels and note tails under extreme settings.

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
| Timbre under strong correction | Much improved; still being validated |
| Tail / splice continuity | Active real-world validation |
| Difficult attacks and vocal transitions | Active validation |
| Broad DAW/platform matrix | Incomplete |
| Public release readiness | Not yet |

The previous `ModernPitchEngine` / `SingleWetSpectralRenderer` implementation is no longer part of the active VST target. Some legacy source and tests remain in the repository as development history and regression material.

---

## What currently matters most

The new architecture has shifted the development problem from “make a large fragile engine behave” to much smaller, testable questions.

The main validation targets now are:

- long vowels and note tails under strong correction;
- waveform recentering/splice continuity;
- noisy room and live material;
- consonants, breaths and sibilants without target corruption;
- rapid note changes and glissandi;
- low-frequency acquisition where more signal history is physically necessary;
- extreme correction without octave or subharmonic capture;
- real DAW behaviour at small block sizes.

Some transitions to another scale degree during long vowels can be musically intentional under extreme settings. The bug class currently being isolated is different: short holes or discontinuities that pull the rendered voice away from the intended trajectory without a legitimate target change.

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

To include the standalone diagnostics:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNEUMATON_BUILD_CLEAN_ENGINE_TESTS=ON
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

For the current audible plugin, the CMake target source list is the primary source of truth. Files belonging to older engines may still exist for regression/reference work without being compiled into the active VST.

---

## Project philosophy

Neumaton is intended as a small, human-centred musical tool rather than a collection of hidden corrective fallbacks.

The current design principle is:

> **Measure the pitch correctly, make the musical target explicit, then move one coherent audio path to that target.**

Uncertainty in analysis should not automatically create a second audible signal or weaken correction in an uncontrolled way. When a detector enters `Transition`, the last valid musical trajectory remains authoritative until new periodic evidence is stable enough to replace it.

Strong or robotic correction is not treated as a failure mode when the user explicitly asks for it. Naturalness is a musical control, not a justification for an engine that retreats from the target.

---

## Release warning

Neumaton is still pre-release software.

The rebuilt V1 architecture is substantially simpler and has performed better than the previous engine in current development tests, but broad validation is still in progress. Keep backups or rendered stems when using development builds in important sessions.

---

## License

A public redistribution license is not currently defined.

A clear license should be added before treating the repository as a finished open-source or redistributable release.
