# Neumaton — Output Stage Laboratory V1

## Stato

Questo documento descrive un laboratorio sintetico separato dal plug-in. **Non modifica il DSP di produzione** e non cambia latenza, parametri, detector, quantizzatore, Scale Lock o gestione delle formanti.

La conclusione principale è netta: nessuna delle soluzioni provate è pronta per essere inserita nel VST. I test però identificano con precisione quali proprietà devono essere unite nel nuovo renderer.

## Diagnosi del codice attuale

Nel `ModernPitchEngine` convivono ancora quattro contratti d'uscita diversi:

1. `synthesiseLayer()` costruisce una prima rappresentazione armonica/residuale con trasporto, inviluppo, formanti e phase locking;
2. `applyV62QualityActiveLedger()` ricostruisce successivamente un nuovo campo spettrale wide-field, usando il precedente come proposal;
3. `processSample()` continua a calcolare wet authority, delayed dry, dry trust, correlazione, leak risk e compensazioni, ma al confine finale forza il dry a zero e restituisce soltanto il ricostruito;
4. i cambi di target possono ancora creare due layer sintetici e incrociarli con un crossfade energeticamente compensato.

Questi blocchi non sono tutti inutili, ma rispondono a idee incompatibili. Il risultato è difficile da prevedere, difficile da testare e costoso da mantenere. La nuova uscita non deve essere aggiunta in fondo: deve sostituire questo insieme.

## Contratto non negoziabile

- nessun dry/wet durante la correzione attiva;
- nessun residuo tonale lasciato alla vecchia fondamentale;
- nessuna somma finale tra una “voce corretta” e una seconda componente audio;
- respiri, consonanti, noise e materiale aperiodico devono essere descritti nello **stesso oggetto spettrale**;
- la polifonia non deve diventare una singola serie armonica artificiale;
- nessun nuovo look-ahead e nessuna modifica alla latenza dichiarata;
- nessuna allocazione nel callback;
- livello corretto tramite conservazione/redistribuzione di energia, non compressione;
- `ModernPitchEngine.cpp` deve accorciarsi: il renderer va estratto in file dedicati.

## Metodo del laboratorio

Il laboratorio lavora a 48 kHz con le tre dimensioni di finestra già corrispondenti alle modalità del motore:

- 128 campioni — Experimental/Ultra Live;
- 256 campioni — Live;
- 512 campioni — Quality.

Hop: `N/4`. Non viene usato il detector del plug-in: F0 sorgente e rapporto target sono noti. In questo modo il test isola il renderer e non confonde errori di tracking con errori d'uscita.

Ogni caso possiede:

- un segnale sorgente;
- un **oracle** generato con la stessa voce, gli stessi formanti e gli stessi eventi, ma alla nuova fondamentale;
- una traiettoria F0 e un rapporto microtonale noti.

### Corpus sintetico

1. vocale sostenuta, trasporto `7/6`;
2. vocale breathy, trasporto di 6 passi in 19-EDO;
3. consonante + burst + vocale;
4. vibrato conservato, 19-EDO;
5. fondamentale mancante;
6. due voci simultanee, entrambe trasportate dello stesso rapporto;
7. aria completamente aperiodica, il cui oracle è lo stesso evento.

La versione rapida usa 450 ms per caso. Le forme temporali sono scalate, non eliminate.

## Soluzioni provate

### A. `uniform_wide_field`

Baseline simile al principio V10.2: ogni bin viene trasportato uniformemente di `ratio`, con deposizione larga e phase locking.

### B. `envelope_covariant`

Come A, ma con compensazione dell'inviluppo/formanti nel campo trasportato.

### C. `single_field_ownership`

Un solo campo di coordinate. La quantità di warp dipende da harmonic proximity, prominenza e voicing; non esistono due segnali da sommare.

### D. `phase_covariant_ownership`

Come C, ma la fase locale può seguire la covarianza dell'evento invece della sola propagazione armonica.

### E. `event_anchored_ownership`

Il flusso spettrale e l'instabilità locale mantengono gli eventi aperiodici temporalmente ancorati, mentre il materiale periodico viene trasportato.

### F. `phase_coherent_peak_regions`

Ogni regione di picco è trasportata attorno al proprio owner, con phase locking coerente. È il candidato più severo sulla famiglia armonica.

È stato anche provato un primo `unified_ownership_ledger`; è stato escluso dai risultati ufficiali perché una regola di ownership sovrastimava l'armonicità del noise puro e, anche corretta, non superava i candidati E/F. È un esperimento respinto, non un risultato nascosto.

## Metriche

- spectral log-distance rispetto all'oracle, dB — minore è meglio;
- distanza dell'inviluppo spettrale, dB — minore è meglio;
- errore assoluto di RMS, dB — minore è meglio;
- salienza della famiglia target — maggiore è meglio;
- salienza della vecchia famiglia rispetto alla target — minore è meglio;
- rischio di doppia famiglia, 0–1 — minore è meglio;
- errore di spectral flatness nell'alta banda;
- errore temporale del picco e della larghezza del transiente.

La metrica “vecchia famiglia” è comparativa: nei registri bassi le due serie possono condividere frequenze. Non va letta da sola, ma insieme a salienza target, oracle distance e ascolto.

## Risultati — Quality, N=512, corpus completo

| Renderer | Spectral LSD ↓ | Envelope LSD ↓ | |RMS Δ| ↓ | Target salience ↑ | Old/target ↓ | Double risk ↓ |
|---|---:|---:|---:|---:|---:|---:|
| uniform wide field | 15.40 | 8.40 | 9.83 | 0.00 | -1.56 | 0.42 |
| envelope covariant | 15.34 | 8.86 | 9.27 | 1.05 | -2.61 | 0.38 |
| single field ownership | 12.71 | 6.43 | 3.36 | 1.09 | -3.16 | 0.36 |
| phase covariant ownership | 10.23 | 4.79 | 4.03 | 0.86 | 1.24 | 0.56 |
| event anchored ownership | **9.56** | **4.07** | **3.18** | 0.98 | 2.63 | 0.61 |
| phase coherent peak regions | 12.04 | 6.64 | 3.20 | **10.66** | **-11.55** | **0.21** |

Non esiste un vincitore globale:

- E è il più vicino all'oracle come timbro e forma spettrale;
- F è di gran lunga il più autorevole sulla famiglia armonica;
- C riduce parte della vecchia famiglia, ma spalma i transienti;
- A e B perdono molto livello e sono distanti dall'oracle.

## Risultati decisivi per caso

### Aria completamente aperiodica, N=512

| Renderer | Spectral LSD ↓ | Envelope LSD ↓ | RMS Δ | Flatness error ↓ |
|---|---:|---:|---:|---:|
| uniform wide field | 12.72 | 8.96 | -7.13 dB | 0.053 |
| phase covariant | 0.036 | 0.026 | 0.000 dB | ~0.000 |
| event anchored | **0.029** | **0.020** | **0.000 dB** | **~0.000** |
| peak regions | 10.53 | 7.23 | -4.98 dB | 0.196 |

Conclusione: l'aperiodico può essere conservato quasi perfettamente **senza dry**, purché la sua fase sia trattata come proprietà dell'evento e non come fase di una nuova serie armonica.

### Vocale pulita, N=512

| Renderer | Target salience ↑ | Old/target ↓ | Double risk ↓ |
|---|---:|---:|---:|
| event anchored | 0.46 | 3.11 | 0.67 |
| single field | -1.91 | 3.56 | 0.69 |
| peak regions | **26.71** | **-27.78** | **0.002** |

Conclusione: la fase event-covariant da sola non impone abbastanza la famiglia target. Le regioni tonali stabili devono avere owner di picco e phase ledger espliciti.

### Fondamentale mancante, N=512

| Renderer | Spectral LSD ↓ | Old/target ↓ | Double risk ↓ |
|---|---:|---:|---:|
| single field | 9.91 | -2.24 | 0.37 |
| event anchored | **9.47** | 10.21 | 0.91 |
| peak regions | 11.47 | **-3.14** | **0.33** |

Conclusione: il renderer event-anchored conserva bene il timbro ma non possiede abbastanza memoria tonale; è proprio il caso in cui serve ownership temporale dei picchi, non un harmonic skeleton inventato.

### Consonante + vocale, N=512

| Renderer | Peak error ↓ | Width error ↓ | Flatness error ↓ |
|---|---:|---:|---:|
| single field | 1.33 ms | 59.27 ms | 0.046 |
| phase covariant | 0.65 ms | 5.17 ms | 0.081 |
| event anchored | **0.63 ms** | **4.33 ms** | **0.068** |
| peak regions | 2.04 ms | 9.19 ms | 0.155 |

Conclusione: il semplice warp continuo non sposta molto il picco, ma allunga l'evento in modo inaccettabile. L'event anchoring è necessario.

## Verifica sulle finestre corte

Il confronto N=128/N=256 usa i cinque casi più critici e i cinque renderer principali.

| Frame | Renderer | Spectral LSD ↓ | Envelope LSD ↓ | Old/target ↓ | Double risk ↓ | Transient peak ↓ |
|---:|---|---:|---:|---:|---:|---:|
| 256 | event anchored | **8.48** | **3.59** | 8.55 | 0.86 | **0.18 ms** |
| 256 | peak regions | 11.52 | 6.35 | **-3.74** | **0.33** | 8.06 ms |
| 128 | event anchored | **8.83** | **4.61** | 6.59 | 0.81 | 0.29 ms |
| 128 | peak regions | 11.67 | 6.33 | 0.09 | 0.50 | **0.20 ms** |

Le finestre corte rendono ancora più evidente il conflitto: E salva gli eventi ma lascia troppa periodicità concorrente; F controlla meglio la periodicità ma degrada noise/aria. La differenza tra modalità deve restare principalmente risoluzione/latency, non un cambio di filosofia del renderer.

## Soluzioni respinte

### Wide-field uniforme

Respinto come uscita finale. Non è dry/wet, ma applica lo stesso contratto a informazioni fisicamente diverse. Perde livello, inviluppo e struttura temporale.

### Envelope compensation sopra il wide-field

Respinta. Migliora poco il problema fondamentale: la coordinata e la fase restano sbagliate per molti eventi.

### Peak-region renderer puro

Respinto come uscita generale. È ottimo sulle vocali stabili, ma trasforma aria, consonanti e noise in strutture troppo organizzate.

### Event anchoring puro

Respinto come uscita generale. È eccellente sull'aperiodico, ma può mantenere una periodicità concorrente, soprattutto con fondamentale mancante e finestre corte.

### Due layer o dry rescue

Respinti per definizione. Possono mascherare un difetto del renderer, ma non risolvono l'identità musicale della nota corretta.

## Architettura proposta per V2

Nome di lavoro: **Single-Object Temporal Ownership Renderer**.

Non separa il segnale in “armonico + noise” da sommare. Costruisce una sola mappa di proprietà per bin e una sola FFT d'uscita.

### 1. Temporal ownership ledger

Per ogni bin/peak, memoria preallocata di:

- owner di picco;
- età e stabilità dell'owner;
- ownership tonale;
- ownership di evento/aperiodico;
- affidabilità di fase;
- energia recente;
- eventuale appartenenza a una seconda ridge stabile.

Le informazioni esistenti del motore alimentano il ledger: `harmonicMask`, `nearestPeak`, `trueSourceBins`, spectral reliability, polyphony, mask stability, onset/flux, breathiness, voicing e consensus.

### 2. Una sola coordinata di uscita per bin

- regione tonale stabile: centro del peak owner trasportato al target; offset locale preservato per non distruggere formanti e corpo;
- evento aperiodico/transiente: coordinata event-covariant o identità quando non esiste pitch fisico da correggere;
- ridge polifonica stabile: propria ownership locale, senza forzarla nella serie del solo F0 primario;
- proiezione monotona finale per evitare fold/crossing.

Non esistono due campioni o due spettri finali da crossfadare: ogni bin possiede una sola destinazione.

### 3. Una sola fase per bin

- tonale stabile: phase propagation + peak locking;
- aperiodico/transiente: fase covariante con lo spostamento dell'evento;
- zona ambigua: interpolazione circolare delle regole di fase **prima** della deposizione, non somma di due segnali dopo la sintesi.

### 4. Formanti e livello

- inviluppo sorgente usato come vincolo di ampiezza sullo stesso bin;
- compensazione tramite Jacobiano locale della mappa di frequenza;
- ledger energetico unico e limitato;
- nessun compressore e nessuna normalizzazione aggressiva per frame.

### 5. Transizioni di nota

Eliminare il dual-synthesis crossfade. Un solo target ratio continuo viene aggiornato nel tempo; la velocità deriva dal controller già esistente. Su salti grandi:

- le ridge stabili mantengono fase continua verso il nuovo target;
- solo i bin instabili possono resettare la fase;
- nessuna seconda voce viene prerollata o sommata.

### 6. Organizzazione del codice

Proposta:

```text
Source/
  NeumatonOutputRenderer.h
  NeumatonOutputRenderer.cpp
  NeumatonOutputTypes.h
```

`ModernPitchEngine` conserva detector, controller, quantizzazione e produzione del contesto. Il renderer riceve una struttura `AnalysisFrameView` preallocata e restituisce un unico spettro complesso.

Da rimuovere dal percorso attivo:

- `desiredWetMix` nel renderer;
- delayed-dry analysis durante la correzione;
- dry trust/leak/correlation state;
- wet/dry cancellation compensation;
- due `SynthesisLayer` per la transizione;
- `blendLayers()`;
- l'attuale sequenza “prima ricostruzione + active ledger wide-field”.

Il bypass esplicito può continuare a restituire il dry ritardato: in bypass non esiste una nota corretta da compromettere.

## Gate prima dell'integrazione nel VST

La V2 non va integrata finché non supera almeno:

1. tutte le uscite finite, zero allocazioni nel callback;
2. latenza bit-per-bit uguale a 128/256/512 campioni secondo modalità;
3. aria aperiodica: spectral LSD < 0.5 dB e RMS delta < 0.2 dB;
4. consonante: peak error < 1 ms e width error < 2 ms;
5. vocale pulita: target salience nettamente superiore alla vecchia famiglia;
6. fondamentale mancante: nessuna vecchia famiglia dominante;
7. polifonia: nessun collasso sistematico nella sola serie del F0 primario;
8. errore di livello medio < 1.5 dB senza compressione;
9. test di transizione senza dual synthesis e senza click;
10. ascolto cieco su materiale reale dopo il superamento sintetico.

## Prossimo esperimento

Implementare nel laboratorio, non ancora nel plug-in, `TemporalOwnershipLedgerRenderer`:

- memoria dei peak owner tra frame;
- owner multipli per le ridge polifoniche;
- phase rule F per i bin stabili;
- event rule E per bin instabili/noise;
- una sola deposizione e un solo energy ledger;
- target trajectory singola per i cambi di nota.

Questo è il primo candidato che risponde davvero alla richiesta: correggere la nota come un unico oggetto sonoro, invece di nascondere i difetti con un dry, un residuale o una seconda voce.
