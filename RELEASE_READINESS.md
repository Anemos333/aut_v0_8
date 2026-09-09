# Neumaton release-readiness contract

This document applies to `single-wet-quality-reconstruction`, the accepted release line.

## Frozen audio baseline during release hardening

Release-readiness work must not alter the accepted audio engine. `Tests/release_readiness_contract.py` pins the Git blob identity of the single-wet renderer, ModernPitchEngine, LivePitchProcessor and the current PluginProcessor integration. A deliberate later pitch/timbre change must update that baseline only after an explicit audio acceptance test.

The architectural invariants are:

- one audible wet renderer;
- one synthesis layer in `SingleWetSpectralRenderer`;
- no parallel dry/wet rendering architecture;
- detectors and evidence remain analysis/supervision, not alternate rendering paths;
- no historical patch workflow may write back into the release branch.

## Automated release gate

`.github/workflows/release-readiness.yml` is read-only (`contents: read`) and must pass before a release candidate is accepted.

It verifies:

1. the frozen audio baseline and single-wet architecture;
2. DAW session-state save/restore source contracts;
3. stable and unique host automation parameter IDs;
4. custom-scale persistence contract and bundled resources;
5. VST3-only product packaging metadata;
6. historical source transforms are already fully materialized (no auto-commit is permitted);
7. ModernPitchEngine/renderer/GUI invariants on Windows and Linux;
8. VST3-only builds on Windows, Linux and universal macOS (x86_64 + arm64);
9. packaged artifact checksums;
10. absence of AU/AAX payloads from release artifacts.

## Release-blocking manual checks

These cannot be proven solely by repository CI and remain mandatory before a public binary is tagged:

- protect `single-wet-quality-reconstruction` in GitHub and require the `Neumaton Release Readiness` checks;
- install the produced VST3 in at least one current Windows DAW and one current macOS DAW;
- verify project save -> close -> reopen restores processing mode, root, scale/custom scale, all APVTS controls and factory preset state;
- verify mono and stereo instantiation, bypass and mode switching in a host;
- verify 44.1, 48 and 96 kHz sessions and representative host buffer sizes;
- verify reported latency after each processing-mode switch;
- perform distribution signing/notarization where required for the intended platform;
- rerun the accepted V5 audio fixture and confirm no audible regression.

## Release policy

A release candidate is not accepted merely because it compiles. The release gate must be green and the manual host checklist must be completed. Pitch-authority and noise-removal work are separate later phases and must not be mixed into release-hardening changes.
