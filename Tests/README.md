# Current Neumaton V1 tests

The repository keeps tests for the **current** `PitchCore + PitchCorrectionTrajectory + SinglePathPitchRenderer` architecture, scale data and editor geometry, factory presets, licensing and Community Packs.

```bash
cmake -S . -B build-v1 -DCMAKE_BUILD_TYPE=Release -DNEUMATON_BUILD_V1_TESTS=ON
cmake --build build-v1 --target PitchCoreStressTest SinglePathPitchRendererTest ScaleDatabaseV1Test ScaleDegreeDisplayTest ScaleEditorGeometryTest
```

The executable outputs are in `build-v1/v1-tests/` (with a configuration subdirectory on multi-config generators). The detector and renderer tests may be run with one or more paths to recorded voice WAV files for stress cases.

For JUCE-backed factory program/session behaviour, see `Tests/FactoryPresetV1/CMakeLists.txt`. For licensing checks use `-DNEUMATON_BUILD_LICENSING_TESTS=ON` and run `LicensingSkeletonTest`. Independent Community Pack and audio isolation contracts are also used by GitHub Actions.

These are structural and regression checks; do not use green synthetic fixtures as proof of real-world audio quality.
