# Neumaton — V3 C++ output-stage integration design

## Decision

The first C++ step is a **compiled but inactive scaffold**. It stabilises types,
lifecycle, preallocated storage and ownership boundaries without changing one
sample of the current audio path.

The branch contains:

```text
Source/NeumatonOutputTypes.h
Source/NeumatonRidgeLedger.h
Source/NeumatonRidgeLedger.cpp
Source/NeumatonOutputRenderer.h
Source/NeumatonOutputRenderer.cpp
```

`ModernPitchEngine.h/.cpp` are intentionally unchanged in this stage.

---

## 1. Current `ModernPitchEngine` flow

### Engine-level responsibilities

`ModernPitchEngine` currently owns:

- multi-rate pitch detection;
- scale quantisation;
- correction trajectory/controller;
- creative-tempo trajectory;
- transition management;
- stereo routing;
- one `SpectralVoiceShifter` per prepared channel;
- fixed-delay alignment for unprocessed auxiliary channels;
- metering publication.

`prepare()` chooses the FFT frame from the latency mode and reports that same
frame size as plug-in latency. It prepares only the channels exposed by the
host. This contract must not change during the V3 work.

### Per-sample control path

For every sample the engine:

1. builds the detector input;
2. advances detector and correction controller;
3. advances the tempo controller;
4. asks `TransitionManager` for a command;
5. sends that command and the harmonic/noise context to each shifter.

In linked mid/side mode only the mid channel is pitch processed; the side is
passed through a fixed delay. V3 must preserve this routing exactly.

### Current shifter responsibilities

`SpectralVoiceShifter` is the actual output-stage monolith. It owns:

- input ring and frame scheduling;
- analysis window and FFT tables;
- forward FFT;
- magnitudes, analysis phases and true-frequency estimates;
- envelope, peaks and harmonic/noise analysis;
- two `SynthesisLayer` objects;
- phase propagation per layer;
- current spectral reconstruction and historical active ledgers;
- conjugate symmetry, inverse FFT and overlap-add;
- dual-layer transition blending;
- delayed-dry analysis and legacy dry/wet state;
- level, cancellation and redistribution compensation;
- output diagnostics.

The legacy dry state is forced closed at the final boundary, but it is still
computed and remains coupled to the output implementation. V3 should remove it
only after the new renderer has become the exclusive active path.

---

## 2. Exact insertion point

The correct frame boundary is inside `SpectralVoiceShifter::processFrame()`
after all of the following are available:

```text
forward FFT
magnitudes + analysis phases
spectral envelope
peak regions
trueSourceBins
harmonic/noise analysis
frame detector/controller context
```

and before either call to:

```cpp
synthesiseLayer(primary, ...);
synthesiseLayer(secondary, ...);
```

At that point the engine already has every observation needed by V3-beta, while
no output spectrum has yet been committed.

The eventual active path should become conceptually:

```cpp
const AnalysisFrameView analysis = makeAnalysisFrameView(...);
const CorrectionTrajectoryFrame trajectory = makeTrajectoryFrame(...);
const auto ridges = ridgeLedger_.processFrame(analysis, trajectory);
const auto output = outputRenderer_.renderFrame(analysis, trajectory, ridges);
```

`ModernPitchEngine` must not ask the ledger to choose a musical target. The
controller/tempo layer supplies the target trajectory; the ledger maintains
identity and phase continuity.

---

## 3. Class boundary

## `NeumatonRidgeLedger`

Owns only temporal ridge identity:

- peak observations;
- bounded deterministic matching;
- persistent IDs;
- birth, active, coast and death lifecycle;
- source/target frequency history;
- continuous target phase;
- local phase-field state;
- source-bin-to-ridge assignment;
- identity and phase diagnostics.

It does **not** own:

- pitch detection;
- scale choice;
- correction smoothing;
- FFT/IFFT;
- spectral deposition;
- output gain;
- audio rings.

## `NeumatonOutputRenderer`

Eventually owns the complete single-object synthesis contract:

- final ownership of every bin;
- one destination per source contribution;
- ridge phase-field reconstruction;
- event-covariant treatment;
- identity treatment for aperiodic air;
- one complex output spectrum;
- conjugate symmetry;
- inverse FFT;
- overlap-add;
- output accumulation ring;
- renderer diagnostics.

It must never own a dry rescue, a second vocal layer or a note-transition
crossfade.

## `ModernPitchEngine`

Remains responsible for:

- detector and analysis scheduling;
- quantiser and scale lock;
- correction and tempo trajectory;
- frame context construction;
- channel/stereo routing;
- metering aggregation;
- explicit host bypass.

---

## 4. Types

`NeumatonOutputTypes.h` defines non-owning C++17 array views so the frame can be
passed without copying or allocation.

### `AnalysisFrameView`

Contains immutable views of:

- analysed complex spectrum;
- magnitudes;
- current and previous phases;
- true source bins;
- harmonic mask;
- spectral envelope;
- nearest-peak map and peak list;
- detector, onset, breath, harmonicity, polyphony and reliability context.

### `CorrectionTrajectoryFrame`

Contains the previous and current endpoints of the musical target trajectory.
A target revision is metadata; it is **not a phase reset**.

### `RidgeState`

Contains persistent identity, source/target frequency history, target phase,
local group-delay slope, amplitude, reliability, age and missed-frame count.
The harmonic number is intentionally absent from the identity.

### `RidgeLedgerFrameView`

Exposes the fixed track storage and a per-source-bin track index to the renderer.
The renderer never mutates the ledger.

---

## 5. Lifecycle

### `prepare()`

Called from the existing engine `prepare()` only.

It allocates:

- maximum ridge states;
- maximum observations;
- observation matching flags;
- per-bin ridge ownership;
- final complex spectrum;
- IFFT frame;
- synthesis window;
- output accumulation ring.

No vector is resized in frame or sample processing.

### `reset()`

A hard reset is valid for:

- host transport/reset request routed to the engine reset;
- sample-rate change;
- frame-size/latency-mode change;
- explicit discontinuity after bypass state is re-primed;
- invalid/non-finite state recovery.

A reset is **not** valid merely because:

- an onset occurs;
- the target revision changes;
- scale lock chooses another note;
- a ridge disappears for one or two frames.

### Per frame

1. extract bounded peak observations;
2. continue high-confidence existing tracks;
3. greedily match remaining observations;
4. create reliable births or pending births;
5. coast unmatched tracks while continuing target phase;
6. retire stale tracks;
7. publish source-bin ownership;
8. build one output spectrum.

### Per sample

Only the renderer output ring is consumed. Ridge matching and FFT work happen at
the existing hop boundary.

---

## 6. Preallocated memory contract

All maximum sizes are derived in `prepare()` from the actual frame size.

Recommended production bounds:

```text
positive bins        = frameSize / 2 + 1
maximum observations = min(positive bins, configured peak bound)
maximum ridges       = min(maximum observations, 96..128 after profiling)
output ring          = nextPowerOfTwo(frameSize * 8)
```

The callback must contain none of:

```text
vector::resize
vector::assign
vector::push_back without a fixed bounded count
new/delete
map/unordered_map insertion
locks
file I/O
```

The scaffold uses vectors only as fixed-capacity arrays allocated by `prepare()`.

---

## 7. Channel ownership

The detector/controller trajectory remains shared exactly as today, while ridge
identity is **per processed shifter/channel**.

- linked mid/side: one ledger/renderer for the processed mid channel; side stays
  on the existing aligned delay;
- dual mono: one independent ledger/renderer per channel, with the shared target
  trajectory;
- additional channels: preserve the existing per-channel shifter arrangement.

Sharing ridge IDs across channels would create false identity coupling between
chorus, doubles and decorrelated material.

---

## 8. Integration stages

### Stage A — compiled inactive scaffold

Current branch.

- classes are compiled;
- no instance is added to `SpectralVoiceShifter`;
- `ModernPitchEngine` is unchanged;
- audio is bit-identical by construction.

### Stage B — shadow ledger

Add one `NeumatonRidgeLedger` to each shifter and call it at the insertion point.
Do not instantiate or call the renderer.

Publish only:

- active ridges;
- births/deaths/coasts;
- phase prediction error;
- identity switches;
- resolved-bin coverage.

Acceptance:

- zero allocation in callback;
- bounded CPU;
- no audio writes;
- stable identity on synthetic clean/vibrato/missing-fundamental/two-voice tests.

### Stage C — shadow spectrum renderer

Call `NeumatonOutputRenderer::inspectFrame()` after the shadow ledger.
The preview spectrum is retained only for diagnostics or offline dumps. No IFFT
is executed and no output ring is consumed.

Compare against the laboratory metrics and add:

- complex-spectrum distance;
- ridge OLA prediction;
- ownership coverage;
- requested gain before normalisation.

### Stage D — exclusive offline A/B

Introduce a compile-time flag, default off:

```cpp
#ifndef NEUMATON_OUTPUT_V3_EXPERIMENTAL
#define NEUMATON_OUTPUT_V3_EXPERIMENTAL 0
#endif
```

The selection must be exclusive:

```cpp
#if NEUMATON_OUTPUT_V3_EXPERIMENTAL
    v3Renderer_.renderAndCommitFrame(...);
#else
    synthesiseLegacyLayers(...);
#endif
```

Never sum, crossfade or use one path as rescue for the other. Build separate
artifacts for legacy and V3 comparisons.

### Stage E — single transition trajectory

Before production, replace the two correction values in `TransitionManager`
with one continuous trajectory endpoint for the renderer.

During temporary shadow comparison, the audible legacy interpolation may be
recorded as:

```text
primary + blend * (secondary - primary)
```

but V3 must ultimately receive one trajectory and maintain phase through it.
There must be no `beginSecondary`, `commitSecondary`, second `SynthesisLayer` or
`blendLayers()` in the active V3 path.

### Stage F — production replacement

Only after synthetic, real-audio and realtime gates pass:

- make V3 the only output path;
- remove both legacy synthesis layers;
- remove dual-transition state;
- remove wet gate and delayed-dry correction state;
- remove dry trust/leak/coexistence memories;
- remove layer and wet/dry cancellation compensation;
- keep explicit host `processBypassed()` with the declared fixed delay;
- rename or remove obsolete meters.

---

## 9. Activation gates

V3 must not become audible until all of the following are true:

1. laboratory V3-beta observed tracker passes the synthetic corpus;
2. ridge identity switches are bounded on missing fundamental and two voices;
3. Live/N=256 keeps breath, vibrato and two voices inside level gates;
4. N=128 is improved rather than merely protected by a no-op;
5. the group-delay field is sample-rate stable at 44.1/48/88.2/96 kHz;
6. note transitions use one trajectory with no click or double voice;
7. real male, female, rap and chorus tests pass;
8. zero allocations and locks are verified in the callback;
9. CPU is measured for mono, linked mid/side and dual mono;
10. reported latency remains exactly the current frame size in every mode;
11. bypass remains sample aligned;
12. no global gain stage masks repeated OLA incoherence beyond the accepted gate.

---

## 10. Immediate next code change

The next justified change is **Stage B only**:

- add inactive ledger state to `SpectralVoiceShifter`;
- prepare/reset it with the shifter;
- construct `AnalysisFrameView` at the exact frame boundary;
- construct a shadow `CorrectionTrajectoryFrame`;
- call the ledger and publish diagnostics;
- prove the audio output is bit-identical.

The renderer should remain uncalled until the observed tracker has been tested
against the V3-alpha laboratory contract.
