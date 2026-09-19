from pathlib import Path

path = Path("Source/ModernPitchEngine.cpp")
source = path.read_text()

marker = "STRUCTURED_IDENTITY_AUTHORITY_V1"
if marker in source:
    raise SystemExit("structured identity authority already present")

for required in [
    "TARGET_REVISION_DIAGNOSTIC_LATCH_V2",
    "TERMINAL_TAIL_STABLE_TRANSPORT_REBASE_V1",
    "RESIDUAL_HANN_LAZY_MEMOIZATION_V1",
]:
    if required not in source:
        raise SystemExit(f"missing validated baseline marker {required}")

old_rescue = r'''    const bool rescueBodyFrame = richEvidence
        && observation.audioPresent
        && parameters.voiceBodyEnergy >= 0.34f
        && parameters.voiceHarmonicity >= 0.32f
        && parameters.voiceSpectralReliability >= 0.44f
        && parameters.voiceBreathiness <= 0.34f
        && parameters.voiceEventStrength <= 0.30f;
'''
new_rescue = r'''    const bool rescueBodyFrame = richEvidence
        && observation.audioPresent
        && parameters.voiceBodyEnergy >= 0.34f
        && parameters.voiceHarmonicity >= 0.32f
        && parameters.voiceSpectralReliability >= 0.44f
        && parameters.voiceBreathiness <= 0.34f
        && parameters.voiceEventStrength <= 0.30f;

    // STRUCTURED_IDENTITY_AUTHORITY_V1
    // Audio presence owns continuity/correction, not musical identity. When
    // rich voice evidence exists, a Stable no-onset target revision requires
    // positive structured body evidence. Analysis/challenger accumulation and
    // source transport remain fully live, so a real legato can commit as soon
    // as coherent body structure returns. When rich evidence is unavailable,
    // preserve the legacy detector-only authority path.
    const bool structuredIdentityAuthority = !richEvidence\n        || state.trackingState != TrackingState::stable\n        || rescueBodyFrame;
'''
if source.count(old_rescue) != 1:
    raise SystemExit(f"rescue anchor count {source.count(old_rescue)}")
source = source.replace(old_rescue, new_rescue, 1)

old_commit = r'''                if (state.latentTargetHops >= requiredHops)
                {
                    detectorScaleCommit = true;
                    state.latentTargetValid = false;
                    state.latentTargetLog2 = 0.0;
                    state.latentTargetHops = 0;
                }
'''
new_commit = r'''                if (state.latentTargetHops >= requiredHops
                    && structuredIdentityAuthority)
                {
                    detectorScaleCommit = true;
                    state.latentTargetValid = false;
                    state.latentTargetLog2 = 0.0;
                    state.latentTargetHops = 0;
                }
'''
if source.count(old_commit) != 1:
    raise SystemExit(f"latent commit anchor count {source.count(old_commit)}")
source = source.replace(old_commit, new_commit, 1)

old_live = r'''            liveIdentityBreak = deepCentreExit || persistentBoundaryExit;
            forceTargetSwitch = liveIdentityBreak;
'''
new_live = r'''            const bool geometricIdentityBreak =
                deepCentreExit || persistentBoundaryExit;
            liveIdentityBreak = geometricIdentityBreak
                && structuredIdentityAuthority;
            forceTargetSwitch = liveIdentityBreak;
'''
if source.count(old_live) != 1:
    raise SystemExit(f"live identity anchor count {source.count(old_live)}")
source = source.replace(old_live, new_live, 1)

if marker not in source:
    raise SystemExit("new marker missing after patch")

path.write_text(source)
print("STRUCTURED_IDENTITY_AUTHORITY_PATCH=APPLIED")
