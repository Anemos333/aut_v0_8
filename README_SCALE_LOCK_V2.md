# Scale Lock v2 — hard target, Amount dry/wet

Questa patch sostituisce la prima interpretazione di Scale Lock.

## Semantica utente

- **Scale Lock OFF**: comportamento invariato.
- **Scale Lock ON**: usa il motore selezionato (**Quality**, **Live** o **Experimental**) ma il wet path viene quantizzato hard al target della scala.
- **Amount / Dry-Wet**: non è più profondità di correzione in Scale Lock; è un vero dry/wet.
  - 0% = dry.
  - 100% = solo wet hard-locked.
  - valori intermedi = blend dry/wet.
- **Humanize**: non crea più dead-band, vibrato pitch o offset dal target quando Scale Lock è ON. Viene usato come naturalness/noise control: basso = più robotico/de-noised, alto = più breath/noise preservato.
- **Retune Time / Creative Tempo**: in Scale Lock non ammorbidiscono il target pitch, perché introdurrebbero deviazione dal target. Restano invariati quando Scale Lock è OFF.

## Note tecniche

- Nessun nuovo motore DSP.
- Nessuna modifica di latenza.
- Nessuna allocazione runtime aggiuntiva prevista.
- High Latency/Slow non viene modificato da Scale Lock: il toggle è pensato per Quality/Live/Experimental.
- I vecchi ID dei parametri restano invariati; viene aggiunto solo `scaleLock`.

## Applicazione

Da repo pulito su `main`:

```bash
git checkout main
git pull
git apply Patches/aut_1_scale_lock_v2.patch
```

Se avevi già applicato la patch v1, conviene tornare a `main` pulito e applicare questa v2:

```bash
git reset --hard origin/main
git apply Patches/aut_1_scale_lock_v2.patch
```

## Test consigliati

1. Scale Lock OFF: null/regression rispetto alla build precedente.
2. Scale Lock ON, Amount 100%: controllare che il wet path arrivi hard al target su Quality/Live/Experimental.
3. Scale Lock ON, Amount 0/25/50/75/100: verificare dry/wet reale.
4. Humanize 0 vs 100: il pitch deve restare sul target, cambia solo la naturalezza del residuo/noise/breath.
5. Cambi rapidi scala/root/motore con audio in playback.
6. pluginval strictness 10 e DAW test su Windows.
