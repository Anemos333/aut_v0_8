# Neumaton V1 release-readiness checklist

This checklist applies to **main**, the accepted V1 single-path pitch engine. The old single-wet release contract and SHA pins are historical and no longer govern current builds.

## Automated contracts

- Linux, Windows and macOS workflows build the VST3 from the active `CMakeLists.txt`.
- The V1 workflow compiles the pitch core and single-path renderer regressions, and checks that legacy pitch/render sources are absent from the active VST build list.
- Release Readiness compiles and runs current V1 tests and the licensing skeleton on Linux.
- The licensing workflow additionally checks audio/licence isolation and local Community Pack contracts.
- macOS CI includes scale database, scale-editor geometry and Community Pack contracts.

These checks cover buildability and structural behaviour; they do not prove objective pitch error, subjective timbre, real-time CPU headroom or DAW round-trip latency. Those must be checked separately.

## Manual release blockers

1. Install the built VST3 in current Windows and macOS DAWs; check instantiation, mono/stereo audio, bypass and mode switching.
2. Save, close and reopen a DAW project. Check scale, tonal centre, A4, factory/user preset state and all active controls, including legacy-session migration.
3. Verify 44.1/48/96 kHz and representative host buffer sizes; measure actual audio/host latency for each mode.
4. Test multiple real vocal recordings (sustained vowels, noisy consonants, glides, rap, breaths, challenging pitch/register, microtonal and sparse scales). Compare dry/wet rendered stems for discontinuities, splices, incorrect targets and artifacts.
5. Check user preset and `.ecpk` import/export and integrity handling.
6. Complete platform signing/notarization and legal software/creator licensing before public distribution.

## Change discipline

Local DSP edits should be benchmarked against the last accepted `main` recording set. Do not add a second audible signal path, infer true pitch from UI degrees, or restore deprecated confidence/dry/wet fallback authority. Keep a reversible commit for each validation step.
