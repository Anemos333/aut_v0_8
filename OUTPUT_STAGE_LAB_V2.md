# Neumaton — Output Stage Laboratory V2

## Decisione

La V2 dimostra che l'uscita può essere costruita come **un solo oggetto spettrale**, senza dry/wet, senza residuale udibile e senza due renderer sommati. Il candidato non è però ancora pronto per il VST: supera i gate principali in Quality, migliora Experimental, ma in Live mostra una cancellazione overlap-add su vibrato, breath e parte della polifonia.

La vecchia uscita in `Source/` resta intenzionalmente invariata.

## Candidato: Temporal Ownership Ledger Renderer

Per ogni bin sorgente viene risolta, prima dell'unica deposizione:

1. l'identità temporale dell'owner;
2. una sola coordinata di destinazione;
3. una sola regola di fase;
4. un solo gain di inviluppo/Jacobiano;
5. una sola scrittura nello spettro d'uscita.

“Tonale”, “evento” e “aperiodico” sono proprietà dell'unico oggetto, non segnali da sommare.

La V2 combina le due proprietà complementari emerse nella V1:

- autorità tonale del renderer a regioni di picco;
- conservazione temporale e timbrica del renderer event-covariant.

Aggiunge memoria dell'owner, acquisizione delle ridge persistenti, veto forte per eventi ad alta banda e un onset auditor che non scambia il beating del missing fundamental per una consonante.

## Risultati ufficiali

Corpus sintetico a 48 kHz, 450 ms per caso, oracle con stessi formanti/eventi e fondamentale target nota.

### Quality — N=512

| Caso | Spectral LSD ↓ | Envelope LSD ↓ | RMS Δ | Target ↑ | Old/target ↓ | Risk ↓ | Peak/width |
|---|---:|---:|---:|---:|---:|---:|---:|
| Vocale pulita | 8.894 | 3.974 | -0.057 dB | 30.47 | -31.78 | 0.001 | — |
| Vocale breathy | 11.080 | 5.451 | +0.102 dB | 7.19 | -8.05 | 0.135 | — |
| Consonante + vocale | 8.045 | 3.429 | +0.003 dB | 24.80 | -26.48 | 0.002 | 0.771 / 1.729 ms |
| Vibrato | 11.961 | 4.863 | -0.156 dB | 14.40 | -18.92 | 0.013 | — |
| Fondamentale mancante | 7.751 | 4.052 | +0.083 dB | 12.00 | -11.48 | 0.066 | — |
| Due voci | 9.523 | 3.000 | +0.123 dB | 10.01 | -9.76 | 0.096 | — |
| Aria aperiodica | 0.025 | 0.018 | 0.000 dB | — | — | 0.000 | 0.000 / 0.000 ms |

Quality è il primo candidato che unisce famiglia target autorevole, aria quasi invariata e transiente sotto i gate di 1 ms/2 ms. Il breath resta il caso timbricamente più debole: la famiglia è corretta, ma la flatness alta banda è ancora distante dall'oracle.

### Experimental — N=128

| Caso | Spectral LSD ↓ | RMS Δ | Target ↑ | Old/target ↓ | Risk ↓ | Peak/width |
|---|---:|---:|---:|---:|---:|---:|
| Vocale pulita | 9.795 | +0.021 dB | 5.29 | -3.75 | 0.297 | — |
| Vocale breathy | 8.573 | +0.006 dB | 4.77 | -11.82 | 0.062 | — |
| Consonante + vocale | 9.664 | -0.013 dB | 9.83 | -5.34 | 0.226 | 0.000 / 0.646 ms |
| Vibrato | 10.186 | -0.057 dB | 4.47 | -5.62 | 0.215 | — |
| Fondamentale mancante | 8.989 | -0.129 dB | 2.72 | -1.28 | 0.427 | — |
| Due voci | 9.791 | -0.070 dB | 3.34 | -4.61 | 0.257 | — |
| Aria aperiodica | 0.127 | 0.000 dB | — | — | 0.000 | 0.000 / 0.000 ms |

Il comportamento temporale e il livello sono solidi. Il missing fundamental resta sotto il gate di autorità desiderato perché la finestra non risolve sufficientemente le ridge.

### Live — N=256

| Caso | Spectral LSD ↓ | RMS Δ | Target ↑ | Old/target ↓ | Risk ↓ | Peak/width |
|---|---:|---:|---:|---:|---:|---:|
| Vocale pulita | 9.541 | -0.206 dB | 13.08 | -12.91 | 0.049 | — |
| Vocale breathy | 9.199 | **-3.233 dB** | 4.08 | -9.27 | 0.106 | — |
| Consonante + vocale | 10.322 | +0.057 dB | 6.98 | -6.39 | 0.187 | 0.167 / 0.917 ms |
| Vibrato | 12.277 | **-6.364 dB** | 3.54 | -4.90 | 0.245 | — |
| Fondamentale mancante | 8.434 | +0.041 dB | 16.95 | -19.07 | 0.012 | — |
| Due voci | 9.292 | **-1.607 dB** | 4.58 | -4.70 | 0.253 | — |
| Aria aperiodica | 0.050 | 0.000 dB | — | — | 0.000 | 0.000 / 0.000 ms |

Clean, missing fundamental, consonante e aria funzionano. Breath, vibrato e parte della polifonia perdono energia quando i frame sintetizzati si sovrappongono. Il problema è una fase locale non abbastanza persistente quando una ridge cambia owner nella regione di risoluzione intermedia.

## Esperimenti respinti

- **Gain energetico più ampio:** recupera il livello ma non i buchi d'inviluppo; sul vibrato Live l'Envelope LSD può superare 10 dB.
- **Phase ledger indicizzato solo per numero armonico:** non risolve la cancellazione Live e può peggiorare finestre corte e polifonia.
- **Contesto causale lungo come sola correzione:** migliora la classificazione armonico/noise, non la continuità della fase di sintesi.
- **Rilascio event-covariant durante il vibrato:** recupera parte del livello ma riapre la vecchia famiglia; il double-family risk può superare 0.5.

## Diagnosi

La V2 possiede la corretta ontologia d'uscita. Il difetto residuo è specifico:

> l'identità della fase è ancora legata in parte al bin/owner corrente; a N=256 una ridge può cambiare owner abbastanza spesso da produrre frame validi singolarmente ma incoerenti nella zona di overlap.

A N=512 le ridge sono risolte. A N=128 il detector-grid prior domina. N=256 è la regione intermedia più critica.

## V3 richiesta: Persistent Ridge Phase Ledger

Ogni ridge stabile deve avere un ID persistente basato su frequenza vera prevista, continuità energetica, harmonic mask, target precedente, probabilità di seconda ridge, età e affidabilità.

Per ogni ridge ID:

- accumulatore di fase target continuo;
- offset locali memorizzati circolarmente;
- reassignment di owner senza reset;
- fase event-covariant solo per bin privi di ridge affidabile;
- interpolazione circolare della regola di fase prima della sintesi, mai crossfade audio tra due voci.

Le transizioni devono usare un solo rapporto target continuo. Il vecchio `dualSynthesis` e `blendLayers()` vanno eliminati.

## Integrazione futura

Solo dopo il superamento della V3:

```text
Source/
  NeumatonOutputRenderer.h
  NeumatonOutputRenderer.cpp
  NeumatonRidgeLedger.h
  NeumatonRidgeLedger.cpp
  NeumatonOutputTypes.h
```

Da rimuovere dal percorso attivo di `ModernPitchEngine`:

- `desiredWetMix` e wet gate;
- delayed dry durante la correzione;
- dry trust, correlation e leak risk;
- compensazione dry/wet;
- due `SynthesisLayer` e `blendLayers()`;
- la doppia ricostruzione proposal + wide-field ledger.

`ModernPitchEngine.cpp` deve accorciarsi: detector/controller/quantizzatore producono un `AnalysisFrameView`; il renderer separato restituisce un unico spettro.

## Gate aggiornati

La V3 non entra nel VST finché non soddisfa in tutte le modalità:

1. aria: LSD < 0.5 dB e RMS < 0.2 dB;
2. consonante: peak < 1 ms e width < 2 ms;
3. livello: |RMS Δ| < 1.5 dB senza gain estremo;
4. old/target negativo sulla vocale pulita;
5. missing fundamental con risk < 0.25;
6. vibrato senza collasso d'inviluppo o riapertura della vecchia famiglia;
7. seconda ridge polifonica trasportata, non eliminata;
8. zero dual synthesis e zero click nelle transizioni;
9. latenza identica a 128/256/512;
10. zero allocazioni nel callback.

**Decisione:** conservare la V2 come baseline Quality, non integrarla ancora; concentrare la V3 esclusivamente sulla persistenza delle ridge e sulla coerenza overlap-add di N=256.