from pathlib import Path

test_path = Path('Tests/SupervisorContinuityTest.cpp')
test = test_path.read_text(encoding='utf-8')
MARKER = 'hardDenseResidualQuantizer'

if MARKER not in test:
    old = '''    ModernPitchEngine::CorrectionState hardDenseState;
    auto hardDenseOffset = strongPitch(static_cast<float>(
        440.0 * std::exp2(10.0 / 1200.0)));
    hardDenseOffset.audioPresent = true;
    engine->updateCorrectionState(hardDenseState, denseQuantizer,
                                  hardDenseOffset, hardDenseParameters);
'''
    new = '''    ModernPitchEngine::ScaleQuantizer hardDenseResidualQuantizer;
    hardDenseResidualQuantizer.reset();
    hardDenseResidualQuantizer.setScale(
        denseScale.data(), static_cast<int>(denseScale.size()), 440.0);
    ModernPitchEngine::CorrectionState hardDenseState;
    auto hardDenseOffset = strongPitch(static_cast<float>(
        440.0 * std::exp2(10.0 / 1200.0)));
    hardDenseOffset.audioPresent = true;
    engine->updateCorrectionState(hardDenseState, hardDenseResidualQuantizer,
                                  hardDenseOffset, hardDenseParameters);
'''
    if old not in test:
        raise RuntimeError('V3 residual regression block not found')
    test = test.replace(old, new, 1)
    test_path.write_text(test, encoding='utf-8')

if MARKER not in test_path.read_text(encoding='utf-8'):
    raise SystemExit('isolated dense residual quantizer marker missing')

print('Microtonal hard-lock V3 regression uses an isolated quantizer state')
