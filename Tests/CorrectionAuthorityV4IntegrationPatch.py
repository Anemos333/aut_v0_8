from pathlib import Path

cpp_path = Path('Source/ModernPitchEngine.cpp')
editor_path = Path('Source/PluginEditor.cpp')
contract_path = Path('Tests/GuiAudioControlContractTest.cpp')

cpp = cpp_path.read_text(encoding='utf-8')
editor = editor_path.read_text(encoding='utf-8')
contract = contract_path.read_text(encoding='utf-8')

MARKER = 'ABSOLUTE_SCALE_LOCK_V4_INTEGRATION'


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if old not in text:
        raise RuntimeError(f'{label}: expected source block not found')
    return text.replace(old, new, 1)


if MARKER not in cpp:
    old = '''    const float degreeSafeCap = std::clamp(
        minimumStep * (0.18f - 0.06f * lockStrictness),
        0.35f, 36.0f);
'''
    new = '''    // ABSOLUTE_SCALE_LOCK_V4_INTEGRATION: preserve an audible Hysteresis
    // control on sparse/wide scales without weakening the strict microtonal
    // endpoint. At strictness=1 this is still exactly 0.12 of the minimum
    // scale step (3 cents in 48-EDO); at lower strictness the user deliberately
    // requests more target-hold behaviour, capped well below half a degree.
    const float degreeSafeCap = std::clamp(
        minimumStep * (0.30f - 0.18f * lockStrictness),
        0.35f, 36.0f);
'''
    cpp = replace_once(cpp, old, new, 'restore audible sparse-scale hysteresis')
    cpp_path.write_text(cpp, encoding='utf-8')

# The GUI text must display the exact Scale Lock response law used by V3/V4.
# This is display-only; no extra DSP path or smoothing is introduced here.
old_editor = '''                const double mappedVal = mode == 1 ? 3.0 + 4.0 * norm
                    : mode == 2 ? 1.5 + 3.5 * norm
                                : 0.35 + 2.65 * norm;
'''
new_editor = '''                const double mappedVal = mode == 1 ? 3.0 + 2.0 * norm
                    : mode == 2 ? 1.5 + 1.5 * norm
                                : 0.35 + 1.15 * norm;
'''
if old_editor in editor:
    editor = replace_once(editor, old_editor, new_editor,
                          'align Speed display with Scale Lock DSP curve')
    editor_path.write_text(editor, encoding='utf-8')
elif new_editor not in editor:
    raise SystemExit('Scale Lock Speed display has an unknown mapping')

old_contract = '''    success &= check(has(editor, "3.0 + 4.0 * norm")
                         && has(editor, "1.5 + 3.5 * norm")
                         && has(editor, "0.35 + 2.65 * norm")
                         && has(engine, "3.0 + 4.0 * norm")
                         && has(engine, "1.5 + 3.5 * norm")
                         && has(engine, "0.35 + 2.65 * norm"),
                     "response_display_matches_dsp_curve");
'''
new_contract = '''    success &= check(has(editor, "3.0 + 2.0 * norm")
                         && has(editor, "1.5 + 1.5 * norm")
                         && has(editor, "0.35 + 1.15 * norm")
                         && has(engine, "3.0 + 2.0 * norm")
                         && has(engine, "1.5 + 1.5 * norm")
                         && has(engine, "0.35 + 1.15 * norm"),
                     "response_display_matches_dsp_curve");
'''
if old_contract in contract:
    contract = replace_once(contract, old_contract, new_contract,
                            'update GUI/DSP response contract')
    contract_path.write_text(contract, encoding='utf-8')
elif new_contract not in contract:
    raise SystemExit('GUI response contract has an unknown mapping')

cpp = cpp_path.read_text(encoding='utf-8')
editor = editor_path.read_text(encoding='utf-8')
contract = contract_path.read_text(encoding='utf-8')

if MARKER not in cpp:
    raise SystemExit('V4 integration marker missing')
if 'minimumStep * (0.30f - 0.18f * lockStrictness)' not in cpp:
    raise SystemExit('degree-safe hysteresis integration missing')
if 'minimumStep * (0.18f - 0.06f * lockStrictness)' in cpp:
    raise SystemExit('obsolete V3 hysteresis cap still present')
for expected in ('3.0 + 2.0 * norm', '1.5 + 1.5 * norm', '0.35 + 1.15 * norm'):
    if expected not in editor or expected not in contract or expected not in cpp:
        raise SystemExit(f'Scale Lock response mapping not coherent: {expected}')

print('Absolute Scale Lock V4 integration applied: exact target preserved, sparse hysteresis audible, GUI Speed matches DSP')
