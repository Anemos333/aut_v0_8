from pathlib import Path

path = Path('Tests/GuiAudioControlContractTest.cpp')
text = path.read_text(encoding='utf-8')
old = '''                         && has(renderer,\n                                "* trueSourceBins_[static_cast<std::size_t>(sourceBin)]")\n'''
new = '''                         && has(renderer, "SHORT_LATTICE_COHERENT_PHASE_V3")\n                         && has(renderer, "* transportSourceBin")\n                         && has(renderer, "frameSize_ <= 256")\n'''
count = text.count(old)
if count != 1:
    raise RuntimeError(f'expected one legacy transport contract, found {count}')
text = text.replace(old, new, 1)
path.write_text(text, encoding='utf-8')
print('V3 renderer contract updated')
