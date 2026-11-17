# Factory presets for the current V1 engine

Factory presets recall Mode, Response, Amount, Human Drift, Boundary Stability,
Output Drive, Analog Texture and Output. Scale, tonal centre and A4 remain the user's musical choices.
The bank retains all twelve host program slots and their ordering.

| Slot | Preset | Mode | Response (ms) | Amount (%) | Human Drift (%) | Boundary (%) | Drive (%) | Analog | Output (dB) |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| 1 | Studio Gentle | Studio | 25 | 80 | 55 | 5 | 25 | On | -1.2 |
| 2 | Slow Drift | Studio | 45 | 55 | 75 | 25 | 0 | Off | -1.2 |
| 3 | Clean Correction | Studio | 5 | 75 | 35 | 30 | 0 | Off | -0.3 |
| 4 | Full Correction | Studio | 0 | 100 | 0 | 0 | 0 | Off | -1.0 |
| 5 | Live Anchor | Live | 10 | 65 | 45 | 15 | 0 | Off | -1.5 |
| 6 | Live Drift | Live | 19 | 70 | 70 | 50 | 75 | On | -1.8 |
| 7 | Stage Tight | Live | 5 | 95 | 25 | 75 | 26 | On | -0.9 |
| 8 | Emergency Tight | Low Latency | 5 | 100 | 3 | 80 | 0 | Off | -1.5 |
| 9 | Robot Rite | Studio | 0 | 100 | 0 | 0 | 100 | On | -2.0 |
| 10 | Slow Orbit | Studio | 45 | 75 | 65 | 0 | 0 | Off | -1.2 |
| 11 | Liquid Steps | Studio | 50 | 100 | 0 | 100 | 48 | On | -2.0 |
| 12 | Open Window | Low Latency | 50 | 25 | 100 | 5 | 0 | Off | -1.5 |

Full Correction requests the exact nearest scale target with no allowed pitch
residual and no response smoothing, once the detector supplies a stable F0.
The normal detector behaviour, boundary transitions and declared processing latency still apply.
Robot Rite uses the same pitch settings with Analog Texture and maximum Output Drive.
Slow Orbit allows room for drift; Liquid Steps slides to the target with Analog Texture;
Open Window combines slow retuning with a wide pitch window. These effects do not use
host tempo, a lock or an LFO. Response is limited to 0–50 ms in integer steps;
Boundary Stability and Output Drive use integer percentages. Output Drive is audible
only when Analog Texture is enabled.

In V1, Amount and Human Drift define an allowed residual pitch window rather than
a dry/wet mix. If the smallest interval in the scale is S cents, the window is
`S * (0.18 * (1 - Amount / 100) + 0.12 * HumanDrift / 100)`.
Increasing Amount narrows it; increasing Human Drift widens it. Response is
approximately the time to reach 95% of a new requested correction. The twelve
profiles are distinct in mode, response, effective window or texture, independently
of output gain. The previous bank had no exact effective duplicates after cleanup.

Names that referred to retired behaviour have been updated in their existing
slots: Natural Vibrato → Slow Drift, Hard Tune Studio → Full Correction,
Live Natural → Live Drift, Backing Track Lock → Stage Tight,
Tempo Glide → Slow Orbit, Glide Lock → Liquid Steps, and
Experimental Window → Open Window. The latter three Lab slots and Full Correction
also have new active settings; the other slots retain their active settings.

## Saved sessions and shared presets

Factory recall no longer writes Scale Lock, Lock Hysteresis, Vibrato Preserve,
Tempo Mode, Tempo Division, Tempo Glide Length, Glide Lock Strength or Smart Onset.
Their parameter IDs and stored session values remain available for compatibility.
Restoring a session uses its saved parameter values; it does not apply the new
settings of the factory slot. The menu shows **Custom** when those values differ
from the last selected factory preset, including fresh default settings and
modified presets. Scale/centre/A4 and retired controls do not affect that label.

New community preset captures store only the eight active APVTS controls
(`speed`, `amount`, `humanize`, `boundaryStability`, `outputDrive`, `analogMode`,
`outVolume`, `tuningReferenceHz`)
plus the existing Mode and tonal centre metadata. Old pack Values trees preserve
all their properties during serialization, so their integrity hashes remain
valid; applying a preset ignores retired properties. Existing user packs are not
rewritten on disk. Older packs that lack a newly introduced control leave that
control's current value unchanged when recalled. The **Custom** label also compares
Boundary Stability and Output Drive, even when Analog Texture is switched off.


## Verification

`Tests/FactoryPresetV1Test.cpp` links the actual plugin processor/editor and checks
factory recall, unchanged musical/legacy controls, old session restoration,
community capture and old pack integrity, maximum correction through the actual
quantizer/trajectory, profile uniqueness without gain, and live menu updates.
Optional WAV arguments run every preset on up to twelve seconds of real audio,
checking stable pitch acquisition, finite bounded output and pairwise differences
after removing preset output gain.

Build the separate test configuration without changing the production CMake file:

```sh
cmake -S Tests/FactoryPresetV1 -B build/preset-v1 -DCMAKE_BUILD_TYPE=Release
cmake --build build/preset-v1 --target FactoryPresetV1Test Neumaton_VST3
build/preset-v1/FactoryPresetV1Test /absolute/path/to/voice.wav
```

On headless Linux, run the executable through `xvfb-run -a`. Multi-configuration
generators put the executable in the selected configuration directory.
