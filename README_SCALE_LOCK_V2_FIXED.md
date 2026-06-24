# Aut_1 - Scale Lock v2 fixed patch

Questo pacchetto sostituisce lo ZIP precedente `Aut_1_scale_lock_v2_patch_2026-06-24.zip`.
La patch precedente conteneva hunk counts non coerenti nel file diff e poteva dare:

```text
error: corrupt patch at line 64
```

Questa versione rigenera gli header degli hunk in formato `git diff` valido.

## Applicazione consigliata da repo pulito

```bash
git checkout main
git pull
git apply --check Patches/aut_1_scale_lock_v2_fixed.patch
git apply Patches/aut_1_scale_lock_v2_fixed.patch
```

Se avevi già applicato una patch precedente:

```bash
git reset --hard origin/main
git apply --check Patches/aut_1_scale_lock_v2_fixed.patch
git apply Patches/aut_1_scale_lock_v2_fixed.patch
```

## Semantica Scale Lock v2

- Scale Lock OFF: comportamento invariato.
- Scale Lock ON: Quality/Live/Experimental forzano il wet path al target della scala.
- Amount resta un vero Dry/Wet: non riduce la precisione del wet path.
- Humanize non crea distanza dal target; resta come controllo di naturalezza/noise/breath.
- Retune/Tempo non allontanano il wet path dal target in Scale Lock.
- High Latency/Slow non viene modificato.

## Validazione consigliata

Dopo l'applicazione:

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Poi pluginval strictness 10 e test DAW con Scale Lock OFF/ON.
