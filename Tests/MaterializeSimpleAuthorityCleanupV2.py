#!/usr/bin/env python3
from pathlib import Path

p = Path('Source/ModernPitchEngine.cpp')
s = p.read_text(encoding='utf-8')

def replace_once(old: str, new: str, label: str) -> None:
    global s
    n = s.count(old)
    if n != 1:
        raise RuntimeError(f'{label}: expected exactly one match, found {n}')
    s = s.replace(old, new, 1)

replace_once(
'''bool ModernPitchEngine::zeroPrudenceAuthority(const Parameters& parameters) noexcept
{
    return exactScaleLockAuthority(parameters)
        && std::clamp(static_cast<double>(finiteOr(parameters.retuneTimeMs, 50.0f)),
                      0.0, 500.0) <= 0.00001
        && std::clamp(static_cast<double>(finiteOr(parameters.lockHysteresis, 24.0f)),
                      0.0, 80.0) <= 0.00001;
}
''',
'''bool ModernPitchEngine::zeroPrudenceAuthority(const Parameters& parameters) noexcept
{
    // Hold is the only user permission to retain the previous target degree.
    // Response controls glide speed only and cannot weaken target authority.
    return exactScaleLockAuthority(parameters)
        && std::clamp(static_cast<double>(finiteOr(parameters.lockHysteresis, 24.0f)),
                      0.0, 80.0) <= 0.00001;
}
''', 'zeroPrudenceAuthority')

replace_once(
'''    if (zeroPrudenceAuthority(parameters))
        return 0.0; // AUTHORITY_CONTROLS_EXPLICIT_V1: Response=0 is literal.
    double response = std::max(0.35, requested);
''',
'''    if (requested <= 0.00001)
        return 0.0; // Response=0 is literal and independent from target authority.
    double response = std::max(0.35, requested);
''', 'response separation')

replace_once(
'''    float modeFactor = 0.78f;
    switch (latencyMode_)
    {
        case LatencyMode::quality:   modeFactor = 1.15f; break;
        case LatencyMode::live:      modeFactor = 0.78f; break;
        case LatencyMode::ultraLive: modeFactor = 0.42f; break;
    }
    const float tempoFactor = parameters.tempo.mode == CreativeTempo::Mode::glideLock
        ? 1.50f : parameters.tempo.mode == CreativeTempo::Mode::tempoGlide
        ? 1.12f : 1.0f;
    const float densityFactor = std::clamp(
        std::sqrt(std::max(0.1f, quantizer.minimumStepCents()) / 100.0f)
            * (1.0f - 0.32f * quantizer.asymmetry()),
        0.22f, 1.40f);
    const float confidenceFactor = 0.65f + 0.70f * clamp01(observation.confidence);
    const float lockStrictness = clamp01(parameters.lockStrictness);
    const float strictnessFactor = 1.0f - 0.28f * lockStrictness;
    const float requestedHysteresis = std::clamp(
        finiteOr(parameters.lockHysteresis, 24.0f)
            * modeFactor * tempoFactor * densityFactor
            * confidenceFactor * strictnessFactor,
        0.0f, 80.0f);
''',
'''    // No mode, tempo, confidence or detector-derived multiplier may add Hold.
    const float lockStrictness = clamp01(parameters.lockStrictness);
    const float requestedHysteresis = std::clamp(
        finiteOr(parameters.lockHysteresis, 24.0f), 0.0f, 80.0f);
''', 'adaptiveHysteresis')

replace_once(
'''    const double jumpCents = std::abs(nearest - targetLog2_) * 1200.0;
    if (!hardLock || jumpCents < 0.5)
    {
        targetLog2_ = nearest;
        pendingValid_ = false;
        pendingCount_ = 0;
        return targetLog2_;
    }

    if (pendingValid_ && std::abs(pendingLog2_ - nearest) * 1200.0 < 2.0)
        ++pendingCount_;
    else
    {
        pendingValid_ = true;
        pendingLog2_ = nearest;
        pendingCount_ = 1;
    }

    const float safeStrictness = ModernPitchEngine::clamp01(strictness);
    const float safeConfidence = ModernPitchEngine::clamp01(confidence);
    const int required = 1 + static_cast<int>(std::lround(
        2.0f * safeStrictness + 1.5f * (1.0f - safeConfidence)));
    pendingObservations = pendingCount_;
    if (pendingCount_ >= required)
    {
        targetLog2_ = pendingLog2_;
        pendingValid_ = false;
        pendingCount_ = 0;
        pendingObservations = 0;
    }
    return targetLog2_;
''',
'''    // Once the explicit Hold boundary is crossed, the nearest degree wins.
    // Confidence/consensus may rank pitch evidence but cannot veto the target.
    (void) strictness;
    (void) confidence;
    (void) hardLock;
    targetLog2_ = nearest;
    pendingValid_ = false;
    pendingCount_ = 0;
    return targetLog2_;
''', 'quantizer confirmation')

p.write_text(s, encoding='utf-8')
print('SIMPLE_AUTHORITY_CLEANUP_V2=materialized')
