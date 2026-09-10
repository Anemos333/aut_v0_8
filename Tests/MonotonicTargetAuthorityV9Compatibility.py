from pathlib import Path

HDR = Path('Source/ModernPitchEngine.h')
text = HDR.read_text(encoding='utf-8')
anchor = '        void setRange(float minimumPitchHz, float maximumPitchHz) noexcept;\n'
if anchor not in text:
    raise SystemExit('V9 compatibility anchor not found')
setter = (
    '        // MONOTONIC_TARGET_AUTHORITY_V9: compatibility only. All accepted\\n'
    '        // evidence already has immediate authority; this API has no state/effect.\\n'
    '        void setImmediateAuthority(bool enabled) noexcept { (void) enabled; }\\n'
)
# The string above intentionally contains escaped newlines for exact generation.
setter = setter.replace('\\n', '\n')
if 'void setImmediateAuthority(bool enabled)' not in text:
    text = text.replace(anchor, anchor + setter, 1)
if 'immediateAuthority_' in text:
    raise SystemExit('forbidden V9 state immediateAuthority_ still present')
HDR.write_text(text, encoding='utf-8')
print('MONOTONIC_TARGET_AUTHORITY_V9_COMPATIBILITY=PASS')
