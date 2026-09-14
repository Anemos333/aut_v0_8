from pathlib import Path

path = Path('Tests/SupervisorContinuityTest.cpp')
text = path.read_text()


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{label}: expected exactly one anchor, found {count}')
    text = text.replace(old, new, 1)

replace_once(
    '''    const double latentPendingTargetHz = std::exp2(latentState.targetLog2);\n    const double latentPendingOutputHz = latentState.transportPeriodHz\n        * std::exp2(latentState.desiredCents / 1200.0);\n    success &= check(std::abs(latentState.targetLog2 - latentCTarget) < 1.0e-12\n                     && std::abs(latentState.transportPeriodHz - latentCTransport) > 0.1\n                     && std::abs(1200.0 * std::log2(\n                         latentPendingOutputHz / latentPendingTargetHz)) < 1.0e-6,\n                     "uncertain_new_degree_keeps_owned_scale_authority");\n''',
    '''    // A deep provisional degree is analysis-only until its identity commits.\n    // The scale target and audible source transport both remain owned by the\n    // current degree; there is no 3-hop catch-up toward an uncommitted F0.\n    success &= check(std::abs(latentState.targetLog2 - latentCTarget) < 1.0e-12\n                     && std::abs(latentState.transportPeriodHz - latentCTransport) < 1.0e-12,\n                     "uncertain_new_degree_cannot_move_owned_transport_before_commit");\n''',
    'pending identity contract')

replace_once(
    '''    engine->updateCorrectionState(softAmountState, softAmountQuantizer,\n                                  authorityMove, softAmountAuthority);\n    const double softAmountTargetHz = std::exp2(softAmountState.targetLog2);\n''',
    '''    // Test Amount on a genuinely local source movement. A single 47-cent\n    // jump is intentionally a large uncommitted innovation now, so it would\n    // test transport veto rather than Amount. About +20 cents remains inside\n    // the chromatic local-motion gate and therefore exposes only depth control.\n    auto softAmountMove = strongPitch(445.0f);\n    softAmountMove.audioPresent = true;\n    softAmountMove.correctionFrequencyHz = 445.0f;\n    engine->updateCorrectionState(softAmountState, softAmountQuantizer,\n                                  softAmountMove, softAmountAuthority);\n    const double softAmountTargetHz = std::exp2(softAmountState.targetLog2);\n''',
    'amount local movement')

path.write_text(text)
print('local-degree transport test contracts refined')
