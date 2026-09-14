from pathlib import Path


def one(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)

engine_p = Path('Source/ModernPitchEngine.cpp')
test_p = Path('Tests/SupervisorContinuityTest.cpp')
engine = engine_p.read_text()
test = test_p.read_text()

# A provisional single-family octave is explicitly denied identity authority.
# Removing the old latent early-return must not let the later continuity centre
# bypass that veto after enough hops.
engine = one(engine,
'''    bool detectorScaleCommit = false;
    if (state.targetValid && !identityOnlyVeto)
''',
'''    bool detectorScaleCommit = false;
    bool provisionalOctaveIdentityVeto = false;
    if (state.targetValid && !identityOnlyVeto)
''',
'declare provisional octave identity veto')

engine = one(engine,
'''                    if (!trustedPitch && observation.detectorSupport < 2)
                        requiredHops = 1000000;
                    else if (trustedPitch && observation.detectorSupport >= 2)
''',
'''                    if (!trustedPitch && observation.detectorSupport < 2)
                    {
                        requiredHops = 1000000;
                        // PROVISIONAL_OCTAVE_CANNOT_BYPASS_IDENTITY_VETO_V1:
                        // the latent gate owns this negative decision. Later
                        // centre/boundary logic may not promote the same
                        // uncorroborated octave by accumulating time alone.
                        provisionalOctaveIdentityVeto = true;
                    }
                    else if (trustedPitch && observation.detectorSupport >= 2)
''',
'persist provisional octave identity veto')

engine = one(engine,
'''    if (identityOnlyVeto)
    {
        // IDENTITY_VETO_TRANSPORT_CONTINUES_V1: hold musical identity and its
''',
'''    if (identityOnlyVeto || provisionalOctaveIdentityVeto)
    {
        // IDENTITY_VETO_TRANSPORT_CONTINUES_V1: hold musical identity and its
''',
'apply provisional octave veto to later identity logic')

# Old test required pending uncertainty to freeze target, transport and desired
# correction. That freeze is the long-note discontinuity we are removing. The
# correct contract is: identity stays on C, transport can follow the real source,
# and the scale-owned output remains exactly on C at the rigid endpoint.
test = one(test,
'''    const double latentCTarget = latentState.targetLog2;
    const double latentCTransport = latentState.transportPeriodHz;
    const double latentCDesired = latentState.desiredCents;
''',
'''    const double latentCTarget = latentState.targetLog2;
    const double latentCTransport = latentState.transportPeriodHz;
''',
'remove obsolete frozen correction reference')

test = one(test,
'''    success &= check(std::abs(latentState.targetLog2 - latentCTarget) < 1.0e-12
                     && std::abs(latentState.transportPeriodHz - latentCTransport) < 1.0e-12
                     && std::abs(latentState.desiredCents - latentCDesired) < 1.0e-12,
                     "uncertain_new_degree_has_zero_audible_authority");
''',
'''    const double latentPendingTargetHz = std::exp2(latentState.targetLog2);
    const double latentPendingOutputHz = latentState.transportPeriodHz
        * std::exp2(latentState.desiredCents / 1200.0);
    success &= check(std::abs(latentState.targetLog2 - latentCTarget) < 1.0e-12
                     && std::abs(latentState.transportPeriodHz - latentCTransport) > 0.1
                     && std::abs(1200.0 * std::log2(
                         latentPendingOutputHz / latentPendingTargetHz)) < 1.0e-6,
                     "uncertain_new_degree_keeps_owned_scale_authority");
''',
'update latent uncertainty authority contract')

engine_p.write_text(engine)
test_p.write_text(test)
print('scale-always-authority v1 refinement materialized')
