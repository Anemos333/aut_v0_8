from pathlib import Path
import os

factor = int(os.environ.get('NEUMATON_EXPERIMENTAL_FFT_FACTOR', '2'))
if factor not in (2, 4):
    raise SystemExit(f'unsupported factor {factor}')

hpp = Path('Source/SingleWetSpectralRenderer.h')
cpp = Path('Source/SingleWetSpectralRenderer.cpp')
h = hpp.read_text()
s = cpp.read_text()


def once(text, old, new, label):
    if text.count(old) != 1:
        raise SystemExit(f'{label}: expected exactly one occurrence, got {text.count(old)}')
    return text.replace(old, new, 1)

h = once(h,
'''    double sampleRate_ = 48000.0;\n    int frameSize_ = 0;\n    int hopSize_ = 0;\n''',
'''    double sampleRate_ = 48000.0;\n    int frameSize_ = 0;\n    int fftSize_ = 0;\n    int hopSize_ = 0;\n''', 'header fftSize')

s = once(s,
'''    frameSize_ = std::max(64, nextPowerOfTwo(frameSize));\n    hopSize_ = std::max(1, frameSize_ / 4);\n''',
'''    frameSize_ = std::max(64, nextPowerOfTwo(frameSize));\n    hopSize_ = std::max(1, frameSize_ / 4);\n    // Probe: spectral oversampling changes coordinate density only. The\n    // temporal analysis/synthesis footprint and reported latency remain 128.\n    fftSize_ = frameSize_ <= 128 ? frameSize_ * ''' + str(factor) + ''' : frameSize_;\n''', 'prepare fftSize')

replacements = [
('fftBitReversal_.resize(static_cast<std::size_t>(frameSize_));',
 'fftBitReversal_.resize(static_cast<std::size_t>(fftSize_));', 'bit reversal resize'),
('while ((1 << fftBits) < frameSize_)', 'while ((1 << fftBits) < fftSize_)', 'fft bits'),
('for (int index = 0; index < frameSize_; ++index)\n    {\n        unsigned value = static_cast<unsigned>(index);',
 'for (int index = 0; index < fftSize_; ++index)\n    {\n        unsigned value = static_cast<unsigned>(index);', 'bit reversal loop'),
('fftTwiddles_.resize(static_cast<std::size_t>(frameSize_ / 2));',
 'fftTwiddles_.resize(static_cast<std::size_t>(fftSize_ / 2));', 'twiddle resize'),
('for (int index = 0; index < frameSize_ / 2; ++index)\n    {\n        const double angle = -twoPi * static_cast<double>(index)\n                           / static_cast<double>(frameSize_);',
 'for (int index = 0; index < fftSize_ / 2; ++index)\n    {\n        const double angle = -twoPi * static_cast<double>(index)\n                           / static_cast<double>(fftSize_);', 'twiddle loop'),
('const int positiveBinCount = frameSize_ / 2 + 1;',
 'const int positiveBinCount = fftSize_ / 2 + 1;', 'positive bins prepare'),
('fftBuffer_.assign(static_cast<std::size_t>(frameSize_), Complex {});',
 'fftBuffer_.assign(static_cast<std::size_t>(fftSize_), Complex {});', 'fft buffer size'),
('layer_.spectrum.assign(static_cast<std::size_t>(frameSize_), Complex {});',
 'layer_.spectrum.assign(static_cast<std::size_t>(fftSize_), Complex {});', 'layer spectrum size'),
('if (size != frameSize_ || fftBitReversal_.size() != data.size())',
 'if (size != fftSize_ || fftBitReversal_.size() != data.size())', 'fft guard'),
('const double binWidthHz = sampleRate_ / static_cast<double>(frameSize_);',
 'const double binWidthHz = sampleRate_ / static_cast<double>(fftSize_);', 'envelope bin width'),
('const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)\n                                    / static_cast<double>(frameSize_);',
 'const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)\n                                    / static_cast<double>(fftSize_);', 'synthesis phase scale'),
]
for old, new, label in replacements:
    s = once(s, old, new, label)

# There are two harmonic-guide bin conversions inside synthesiseLayer.
old_fund = 'sourceFundamentalHz * static_cast<double>(frameSize_) / sampleRate_'
if s.count(old_fund) != 2:
    raise SystemExit(f'harmonic bin scale: expected 2 occurrences, got {s.count(old_fund)}')
s = s.replace(old_fund, 'sourceFundamentalHz * static_cast<double>(fftSize_) / sampleRate_')

s = once(s,
'''        layer.spectrum[static_cast<std::size_t>(frameSize_ - bin)] =\n''',
'''        layer.spectrum[static_cast<std::size_t>(fftSize_ - bin)] =\n''', 'negative spectrum mirror')

s = once(s,
'''    const std::int64_t frameStartSample = frameEndSample - frameSize_ + 1;\n    for (int index = 0; index < frameSize_; ++index)\n    {\n        const float input = readInputSample(frameStartSample + index);\n''',
'''    const std::int64_t frameStartSample = frameEndSample - frameSize_ + 1;\n    std::fill(fftBuffer_.begin(), fftBuffer_.end(), Complex {});\n    for (int index = 0; index < frameSize_; ++index)\n    {\n        const float input = readInputSample(frameStartSample + index);\n''', 'zero padded frame')

s = once(s, 'const int positiveBins = frameSize_ / 2;',
         'const int positiveBins = fftSize_ / 2;', 'positive bins process')

# processFrame has its own phase-coordinate conversion.
s = once(s,
'''    const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)\n                                    / static_cast<double>(frameSize_);\n    const double binFromPhaseScale = static_cast<double>(frameSize_)\n                                   / (twoPi * static_cast<double>(hopSize_));\n''',
'''    const double expectedPhaseScale = twoPi * static_cast<double>(hopSize_)\n                                    / static_cast<double>(fftSize_);\n    const double binFromPhaseScale = static_cast<double>(fftSize_)\n                                   / (twoPi * static_cast<double>(hopSize_));\n''', 'analysis phase scale')

hpp.write_text(h)
cpp.write_text(s)
print(f'EXPERIMENTAL_OVERSAMPLED_LATTICE_PROBE_FACTOR={factor}')
