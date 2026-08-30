# GUI to Audio Control Contract — Single-Wet branch

The release rule is **no placebo controls**. A user-facing sound control must either change a defined audible dimension or be disabled while its owning DSP feature is not applicable. Navigation and metering controls are explicitly exempt.

| GUI control | Active modes | Audible contract |
|---|---|---|
| Scale | all | changes the quantizer degree set and therefore target pitch |
| Root | all | transposes the quantizer reference and target pitch |
| Mode | all | selects untouched High Latency/YIN or one of the single-wet frame/latency profiles |
| Response | all | changes correction trajectory time; when Scale Lock remaps Response, the displayed ms value follows the same DSP curve |
| Amount | all | scales correction cents; it is never a dry/wet control |
| Humanize | modern only | changes same-note tolerance/correction window; disabled in High Latency |
| Scale Lock | modern only | changes target-identity hold/commit behaviour; disabled in High Latency |
| Hold | modern + Scale Lock | changes scale-degree hysteresis; hidden/disabled otherwise |
| Vibrato Preserve | modern + Scale Lock | changes retained same-note vibrato; hidden/disabled otherwise |
| Tempo Mode | modern only | Off bypasses tempo scheduling; Glide/Lock modify the same correction trajectory |
| Tempo Division | modern + Tempo active | changes beat grid and beat-derived glide time; disabled in Tempo Off |
| Glide Length | modern + Tempo active | changes beat-derived transition duration; disabled in Tempo Off |
| Glide Lock Strength | modern + Glide Lock | changes release position toward the next grid; disabled otherwise |
| Smart Onset | modern + Glide Lock | can release a pending target near the grid on a musical onset; disabled otherwise |
| Analog Texture | all | engages output soft saturation plus fixed low/high shelves |
| Output | all | changes final output gain before the safety ceiling |
| Preset | all | macro: changes sound parameters and processing mode through host-notifying writes |
| Custom Scale editor/selection | all | changes the published scale snapshot used by the target quantizer |

Navigation-only controls (Tempo page/back, Control Room/back) and meters are not sound controls and are not required to alter audio.

The Modern Quality/Live/Experimental renderer remains one wet spectral path: there is no delayed dry, dry/wet blend, confidence-authority attenuation, or secondary synthesis layer. Sensor evidence may change identity/search caution but never Amount/correction depth.
