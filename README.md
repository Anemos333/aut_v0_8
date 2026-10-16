# Neumaton

**Experimental microtonal vocal pitch correction · C++17 · JUCE 8.0.12 · VST3**

**Status: closed beta / pre-release.** The V1 architecture on `main` is the accepted development baseline. Future DSP work should be local, measurable and validated against real vocal recordings rather than rebuilding the engine or adding hidden fallback paths.

Neumaton is designed as a small, human-centred musical instrument for independent and experimental musicians, with conventional tuning, microtonal systems and user-defined scales.

## The V1 signal path

```text
Input
  ├── Measurement gate → fundamental estimate → validate → refine
  │                      (Acquire / Stable / Transition)
  └── PitchCore → stable F0 → scale quantizer → correction trajectory
                                             │
                        SinglePathPitchRenderer (one audible path)
                                             │
                              optional analogue colour + output gain
                                             │
                                           Output
```

- `Source/PitchCore.*`: a measurement gate, fundamental detector and scale quantizer. The gate does **not** set output gain or dry/wet authority.
- `Source/PitchCorrectionTrajectory.h`: Speed/Amount/Humanize-driven correction. An uncertain `Transition` cannot publish an arbitrary audible target; the previous accepted Stable target remains authoritative until validated replacement.
- `Source/PitchEngineV1.*`: connects measurement, quantization, trajectory and renderer.
- `Source/SinglePathPitchRenderer.*`: one fractional read head over a single input history with interpolation and waveform-matched recentering. No spectral OLA, independent dry path, dual wet synthesis or hidden bypass fallback.
- `Source/LivePitchProcessor.h`: JUCE block adapter and prepared mode instances. Historical compatibility fields here are **not** extra V1 DSP stages.

This describes the intended and implemented architecture, not a guarantee that all recordings are artifact-free. In particular, real-voice transitions, consonants, dense or sparse tunings and low-latency splices still deserve DAW listening tests.

## Modes and reported latency

| Interface | Renderer delay | At 48 kHz |
| --- | ---: | ---: |
| **Studio** | 512 samples | 10.67 ms |
| **Live** | 256 samples | 5.33 ms |
| **Low Latency** | 128 samples | 2.67 ms |

Delays are specified in samples and are reported to the host. These figures are the declared renderer delay at 48 kHz, **not** a measured end-to-end microphone/DAW/device round-trip latency. Neumaton is not a zero-latency plugin.

## Musical interface

The main UI provides scale selection, tonal centre, A4 tuning reference, the three processing modes, factory presets, **Response** (Speed, 0–50 ms), **Amount**, **Human Drift** (Humanize), **Boundary Stability** (stored as `boundaryStability`), **Output Drive** (stored as `outputDrive`), **Analog Texture**, output volume and a separate Control Room.

The Control Room displays meaningful pitch/target/scale-degree metering and a colour-coded schematic of the current single-path audio machine. The schematic is informational: it does not imply that old consensus trackers or additional spectral paths run behind the scenes.

`Scale Lock`, its historical `lockHysteresis` and `vibratoPreserve` controls, and the old Creative Tempo / Tempo Lab values are **not current V1 UI features**. Some parameter IDs and state migration remain deliberately in `PluginProcessor` and `LivePitchProcessor` for older DAW sessions. Do not remove those compatibility fields solely because they are not shown.

### Scales and presets

- Factory conventional, historical and microtonal systems, with separate tuning reference and tonal centre.
- Custom scales and local Community Pack presets/scenes; the quantizer does not feed its target back into fundamental measurement.
- Twelve V1 factory programs, including **Full Correction** and experimental/drifting alternatives. Factory recall touches the supported preset controls, not retired Scale Lock or Tempo settings.

See [factory presets](PRESETS_V1.md) for the current values and restoration behaviour.

### Community Packs and planned licensing

`.ecpk` packs can carry scales, presets and scenes in a portable, inspectable format. Schema 3 adds integrity digests and licence metadata; a digest is **not** a creator-authenticity signature. The entitlement skeleton (`Entitlement.h`, `PackVerifier.h`) is separate from the DSP and is not an account/DRM system.

Free / Supporter / Supporter Studio packaging, prices, device policies and creator rights remain **pre-release plans**. No public software/source licence or binding commercial terms are supplied by this repository.

## Build

Requirements: CMake 3.22+, Git, a C++17 toolchain and the system dependencies needed by JUCE. CMake fetches JUCE **8.0.12**.

```bash
git clone https://github.com/Anemos333/aut_v0_8.git
cd aut_v0_8
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Neumaton_VST3
```

The plugin format configured by this repository is **VST3 only**. Output paths vary with platform and generator. The GitHub Actions workflows build on Linux, Windows and macOS.

To compile the current independent V1 tests (no historical DSP engine):

```bash
cmake -S . -B build-v1 -DCMAKE_BUILD_TYPE=Release -DNEUMATON_BUILD_V1_TESTS=ON
cmake --build build-v1 --config Release --target PitchCoreStressTest SinglePathPitchRendererTest ScaleDatabaseV1Test ScaleDegreeDisplayTest ScaleEditorGeometryTest
```

Run the built tests from `build-v1/v1-tests/` (or the corresponding multi-config subdirectory). `PitchCoreStressTest` and `SinglePathPitchRendererTest` also accept paths to real vocal WAV files. For factory-program and state tests, see `Tests/FactoryPresetV1/CMakeLists.txt`.

Licensing and Community Pack checks remain separate:

```bash
cmake -S . -B build-licensing -DCMAKE_BUILD_TYPE=Release -DNEUMATON_BUILD_LICENSING_TESTS=ON
cmake --build build-licensing --config Release --target LicensingSkeletonTest
```

See [release validation](RELEASE_READINESS.md) for what CI does and does not prove.

## Repository layout

- `Source/`: only current V1 plugin integration, GUI, musical data, preset/community/licensing infrastructure and required compatibility glue.
- `Resources/`: artwork embedded by the current `CMakeLists.txt`.
- `Tests/`: focused V1 regression, musical-scale, Community Pack, preset and licensing checks.
- `.github/workflows/`: cross-platform builds and current release/contract validation.
- `PRESETS_V1.md`: factory programs and preset/session compatibility.

The abandoned multirate/spectral-renderer experiments and patch-materialisation pipelines do not form part of the production VST. The `single-wet-quality-reconstruction` branch is retained as a **historical reference**, not as a second supported architecture.

## Development and beta caveats

Prefer changes with a concrete audible defect, a local code hypothesis and a regression using challenging real-world voices, including noisy bedroom and live recordings. Synthetic inputs alone are insufficient to claim vocal quality or musical precision. Test across sample rates, host buffer sizes, mono/stereo, bypass, mode changes, scale selection and DAW project restore.

A successful build does not guarantee that a host will accept a plugin or that timbre and timing meet release quality. Signed/notarized distribution and broader DAW validation are still pending.

**Copyright and licence:** this public repository does not currently contain a general-purpose source-code redistribution licence. Do not infer open-source permissions from visibility on GitHub.
