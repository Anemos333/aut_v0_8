# GUI and Control Room cleanup — V1

Base: `7aef7e2419ccdcd0b92590d7e310a8abc20830aa` (main, 7 October 2026).

Backup: `backup/main-before-gui-cleanup-20261007`. This branch retains the complete original tree, including removed sources and generated build files. To inspect or recover it, check out the backup branch in a separate clone/worktree; individual files can also be restored from that branch with Git.

## Interface

- Consensus becomes **Scale Degree**, using the actual metered target, the selected scale, Center and A4 tuning. Degrees are numbered from 1 in ascending pitch order within the scale's equave. Repeated endpoints are deduplicated with the same normalization as the V1 quantizer; non-octave and custom scales are supported.
- Transition/Acquire retains the metered target and displays **Held target**. No target, invalid data, or a held target that does not belong to the newly selected scale displays `--`.
- The Scale Lock lever and its obsolete Response time conversion are removed. Response always displays the actual milliseconds passed to V1.
- Hold and Vibrato Preserve remain visible on the main page. They are marked inactive and disabled because V1 stores their values for session compatibility without using them in audio processing. Restoring their effect would require a separate DSP change.
- Mode names match the current engine: Studio (512), Live (256), Low Latency (128).
- Control Room shows Confidence, Periodicity, Stable duration, the reported latency, target, degree, correction and output settings. Internal Machine depicts the analysis branch (gate → PitchCore → scale/trajectory) and the single audio path (input → renderer → output). The gate is a schematic node; no unexported gate-open status is invented. Confidence/Periodicity display `--` when the sampled frame has no valid new measurement, instead of presenting between-hop zero placeholders as 0% quality.
- Obsolete multirate paths, dual synthesis, mask, noise/breath/polyphony and spectral reconstruction diagnostics are removed from the visible page.

## Physical cleanup

Removed unreferenced sources: `ScaleLock.cpp`, `ScaleLock.h`, `CommunityPackPage.h` (superseded by V2), and `NeumatonOutputTypes.h`.

Removed 824 tracked generated files/gitlinks in `build-bootstrap/` and `build-detector-veto/`, including stale objects, caches and binaries. Neither directory is referenced by the repository's workflows or test scripts.

Legacy engine/renderer sources still referenced by regression tests and optional CMake targets are retained. The active processor, adapter, detector, quantizer, trajectory, renderer, licensing/community logic and CMakeLists.txt remain byte-for-byte identical to the base commit. Only GUI files are modified; new UI helpers/tests are separate.

## Verification

The standalone scale-degree test checks V1 quantizer targets across registers, unsorted/duplicate degrees, non-octave equaves, one-degree scales, held/stale targets and invalid input:

```sh
c++ -std=c++17 -O2 -ISource Tests/ScaleDegreeDisplayTest.cpp Source/PitchCore.cpp -o /tmp/ScaleDegreeDisplayTest
/tmp/ScaleDegreeDisplayTest
```

Validation: the degree test passes. Existing PitchCoreStressTest and SinglePathPitchRendererTest pass on two supplied real vocal recordings in the 128/256/512 modes, with identity transport error 0 and no nonfinite samples. These checks are transport/regression checks; they do not establish correction accuracy or subjective sound quality.

The Linux VST3 builds successfully with JUCE 8.0.12. A native JUCE editor inspection rendered the GUI and Control Room at 640×510, 720×560 and 1200×820, checked recalled Scale Lock state and back navigation, and rendered a held target after real voice input. Screenshots were visually reviewed. Windows/macOS builds are left to the existing CI workflows.
