# Test standalone del core DSP

Questi target compilano `ModernPitchEngine` e `Tempo` senza JUCE completa, usando uno stub minimo di `juce::AudioBuffer`. Non sostituiscono la compilazione del plug-in o pluginval.

```bash
cmake -S Tests -B build-tests -DCMAKE_BUILD_TYPE=Release
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

Per il corpus:

```bash
cmake -S Tests -B build-tests -DCMAKE_BUILD_TYPE=Release \
  -DAUTOTUNE_CORPUS_DIR=/percorso/alle/voci
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

Per ASan+UBSan con Clang/GCC:

```bash
cmake -S Tests -B build-san -DAUTOTUNE_ENABLE_SANITIZERS=ON
cmake --build build-san
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-san --output-on-failure
```

Nel container usato per questa consegna LeakSanitizer non era avviabile; AddressSanitizer e UndefinedBehaviorSanitizer sono stati eseguiti con `detect_leaks=0`.
