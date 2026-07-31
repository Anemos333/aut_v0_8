# Clean ModernPitchEngine contract

This branch is valid only while all of the following remain true:

1. The proven multi-rate detector and scale decision are the only legacy DSP retained.
2. No legacy output renderer, ridge ledger, synthesis layer, transition crossfade, dry/wet path or confidence-authority gate is compiled or present in ModernPitchEngine.
3. Every active audio channel passes through one complete pitch-transport path.
4. In linked stereo every channel receives the same correction trajectory; no Side component bypasses correction.
5. In dual-mono each channel may detect and quantize independently, but neither channel may bypass its transport.
6. Speed is only the post-attack delay before correction starts.
7. Amount is only the tolerated output error in cents.
8. Humanize is only the range of local pitch movement treated as the same sung note.
9. Periodicity, breath, consonants, onset evidence and polyphony may alter envelope reconstruction or target identity checks, but never reduce correction authority and never expose unprocessed audio.
10. The output is one time-domain full-signal transport followed by LPC envelope reconstruction; it does not remap FFT bins.

The branch must remain draft until real Windows DAW listening confirms scale transport, absence of wind/flanging, intelligible consonants and acceptable formant preservation.
