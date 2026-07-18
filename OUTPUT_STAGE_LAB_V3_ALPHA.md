# Neumaton — Output Stage Laboratory V3-alpha

## Decisione

La V3-alpha conferma la domanda sperimentale stretta del laboratorio:

> un phase ledger con identità di ridge oracle persistenti elimina la cancellazione overlap-add della V2 a 256 campioni senza cambiare magnitudini, ownership, destinazioni, regole event/air, finestre, hop o target ratio.

A 48 kHz il collasso di livello scompare nei tre casi critici:

- breathy vowel: RMS `-3.233 -> +0.051 dB`;
- vibrato: RMS `-6.364 -> -0.112 dB`;
- due voci: RMS `-1.607 -> -0.069 dB`.

Il candidato resta **solo da laboratorio**. Non è ancora una V3 produttiva perché usa identità e traiettorie oracle, contiene una regolarizzazione di ritardo di gruppo empirica e non include il tracker osservato, le transizioni reali, audio umano o misure realtime C++.

Nessun file in `Source/` viene modificato.

---

## Contratto sperimentale

La V2 resta la baseline immutabile. Il nuovo renderer importa il sorgente V2 ripristinato e intercetta soltanto la singola chiamata a `render_mapped_spectrum()`.

Restano identici:

- classificazione e ownership V2;
- magnitudini e gain;
- coordinate di destinazione;
- sigma e deposito gaussiano a sette tap;
- event covariance e trattamento dell'aria;
- FFT, finestra sqrt-Hann, overlap e hop `N/4`;
- target ratio e metriche;
- compensazione energetica già presente nella V2.

Cambiano soltanto la memoria e la ricostruzione della fase nelle regioni di ridge affidabili.

---

## Implementazione V3-alpha

### 1. ID oracle persistenti

Ogni voce sintetica fornisce una famiglia di traiettorie note. Gli ID non dipendono dal bin FFT corrente e sopravvivono ai reassignment dell'owner V2.

Nel caso `missing_fundamental` la prima ridge non viene creata; le superiori restano indipendenti dalla presenza della fondamentale.

### 2. Matching senza sostituire l'ownership V2

Ogni peak owner V2 viene associato alla ridge oracle più vicina usando la true frequency già stimata dal laboratorio. Un'associazione viene respinta quando:

- la distanza supera la tolleranza frequenziale;
- due ridge sono troppo ambigue nello stesso intorno;
- l'owner è troppo debole;
- il trasporto tonale V2 è insufficiente.

Il matching assegna solo l'identità di fase. Non sposta bin e non cambia la classificazione.

### 3. Ledger e coast

Il ledger viene inizializzato in un unico frame coerente. Dopo l'inizializzazione **ogni ridge avanza a ogni hop**, anche senza osservazione.

L'avanzamento usa integrazione trapezoidale:

```text
phase += 2*pi * hop/sampleRate * 0.5 * (previousTargetHz + targetHz)
```

La traiettoria target è per il 75% quella musicale oracle (`ridgeHz * ratio`) e per il 25% la coordinata realmente depositata dalla V2. Il piccolo termine osservato mantiene il campo di fase allineato alla deposizione senza lasciare che il jitter dell'owner domini il ledger.

### 4. Campo di fase per bin di destinazione

Il solo centro persistente non basta. Ogni tap di destinazione riceve una fase ricostruita come:

```text
persistent centre + regularised group-delay slope * target-bin offset
```

La pendenza corrente è un prior di laboratorio pari a `1.35*pi rad/bin`. La curvatura sorgente è disattivata nel candidato ufficiale: trasportarla integralmente elimina il deficit RMS ma deteriora fortemente l'inviluppo.

La fase ledger e la regola V2 vengono interpolate circolarmente prima dell'unica deposizione complessa. Non esistono due segnali audio, due IFFT o un crossfade dry/wet.

### 5. Gate di risoluzione

Il ledger viene attivato in funzione dei bin per fondamentale, non dal nome della modalità:

- sotto circa `0.62 bin/F0`: V2 invariata, ridge non risolte;
- zona piena circa `0.78–1.35 bin/F0`: ledger attivo;
- rilascio fino a circa `1.65 bin/F0`: ritorno graduale alla V2, già coerente con ridge ben risolte.

Questo rende N=128 un no-op numerico e limita le modifiche a N=512. Il gate usa però l'F0 oracle: il tracker reale dovrà produrre una misura di risolvibilità equivalente.

---

## Risultati ufficiali — 48 kHz, 450 ms

### Live — N=256

| Caso | Spectral LSD V2→V3 | Envelope LSD V2→V3 | RMS V2→V3 | Old/target V3 | Risk V3 |
|---|---:|---:|---:|---:|---:|
| Vocale pulita | 9.541 → **7.964** | 4.596 → **4.342** | -0.206 → **+0.005 dB** | -21.09 dB | 0.008 |
| Vocale breathy | 9.199 → **8.818** | 3.398 → **3.256** | -3.233 → **+0.051 dB** | -11.96 dB | 0.060 |
| Consonante + vocale | 10.322 → **8.792** | 5.016 → **4.967** | +0.057 → **-0.033 dB** | -14.69 dB | 0.033 |
| Vibrato | 12.277 → **11.676** | 5.549 → 5.788 | -6.364 → **-0.112 dB** | -12.49 dB | 0.053 |
| Fondamentale mancante | 8.434 → 8.434 | 3.969 → 3.969 | +0.041 → +0.041 dB | -19.07 dB | 0.012 |
| Due voci | 9.292 → **8.618** | 3.255 → 3.428 | -1.607 → **-0.069 dB** | -6.27 dB | 0.191 |
| Aria aperiodica | 0.050 → 0.050 | 0.046 → 0.046 | 0.000 → 0.000 dB | — | 0.000 |

Il transiente resta nei gate: errore del picco `0.771 ms`, errore di larghezza `1.000 ms`. L'aria resta quasi identica all'oracle.

La coerenza di previsione del ledger è `1.0` per costruzione oracle; la coerenza della fase V2 misurata sulle stesse identità varia circa fra `0.33` e `0.62` nei casi tonali Live.

### Experimental — N=128

Il gate di risoluzione rende il percorso V3 un no-op entro il rumore numerico (`~10^-15` sul waveform). Tutte le metriche coincidono con la V2 nelle cifre registrate.

Questo evita regressioni, ma **non risolve** i limiti già presenti nella baseline corta:

- missing fundamental risk `0.427`;
- polifonia risk `0.257`;
- autorità della famiglia target ancora debole.

La V3-alpha supera quindi la domanda Live, non il gate produttivo completo in tutte le modalità.

### Quality — N=512

Il gate ritorna alla V2 nei casi già ben risolti. Restano due interventi misurabili:

- missing fundamental: Spectral LSD `7.751 -> 7.511`, risk `0.066 -> 0.005`, ma Envelope LSD `4.052 -> 4.534`;
- due voci: Spectral LSD `9.523 -> 9.326`, Envelope LSD `3.000 -> 2.853`, risk `0.096 -> 0.097`.

Il tradeoff sull'inviluppo del missing fundamental è accettato dal verificatore del laboratorio ma resta un limite da rimuovere nel tracker/phase-field successivo.

---

## Energia prima della compensazione globale

La V2 e la V3 usano la stessa compensazione globale finale già presente nel laboratorio. Per verificare che il risultato non sia soltanto un gain rescue, è stato misurato il gain richiesto **prima** di quella compensazione:

| Caso N=256 | V2 richiesto | V3-alpha richiesto | Riduzione |
|---|---:|---:|---:|
| Vocale pulita | 8.152 dB | 3.949 dB | 4.203 dB |
| Vocale breathy | 11.246 dB | 4.399 dB | 6.847 dB |
| Consonante + vocale | 6.715 dB | 2.397 dB | 4.318 dB |
| Vibrato | 14.454 dB | 3.156 dB | 11.298 dB |
| Fondamentale mancante | 6.819 dB | 6.819 dB | 0.000 dB |
| Due voci | 9.457 dB | 5.606 dB | 3.851 dB |
| Aria | 0.022 dB | 0.022 dB | 0.000 dB |

La cancellazione OLA è quindi realmente ridotta. Rimane però un gain globale residuo di circa `2.4–5.6 dB` nei casi trasformati: il laboratorio non deve presentarlo come un renderer già normalizzato per la produzione.

---

## Ablazioni e fallimenti utili

### Centro persistente soltanto

Un'unica fase complessa per regione migliora breath e vibrato, ma continua a perdere diversi dB e peggiora altri casi. La fase interna per bin di destinazione è necessaria.

### Trasporto integrale del campo di fase sorgente

L'inversa della mappa V2 applicata alla fase locale sorgente porta tutti i deficit RMS entro circa 1 dB, ma deteriora l'Envelope LSD:

- clean circa `7.6 dB`;
- vibrato circa `9.3 dB`;
- polifonia circa `4.2 dB` con perdita di autorità target.

La curvatura locale non può essere copiata senza regolarizzazione.

### Upper bound con fase target oracle per ogni bin

Mantenendo identiche magnitudini, ownership e destinazioni, ma usando la fase target ideale per ogni bin, tutti i deficit RMS Live scendono vicino a zero. Questo dimostra che il deposito V2 può sostenere il risultato e che il problema residuo è nella legge di fase, non nella quantità di energia o nelle coordinate.

L'upper bound mostra anche che una fase perfetta non rende automaticamente perfetto il timbro: vibrato e missing fundamental conservano differenze d'inviluppo. La V3-beta non dovrà ottimizzare soltanto il livello.

---

## Limiti dichiarati

1. **Oracle, non tracker.** Gli ID arrivano dalle traiettorie sintetiche e nel laboratorio codificano ancora lo slot voce/armonica; il codice non prova che magnitudine, phase advance e affidabilità osservate bastino a ricostruire identità indipendenti dal numero armonico.
2. **Prior empirico.** `1.35*pi rad/bin` funziona nel corpus ufficiale a 48 kHz, ma non è ancora una legge derivata dal ritardo di gruppo stimato.
3. **Dipendenza dal sample rate.** A 44.1 kHz il livello viene corretto, ma la breathy passa da Envelope LSD `2.920` a `4.057 dB`; a 96 kHz il gate ricade quasi interamente sulla V2.
4. **N=128 irrisolto.** Il no-op evita regressioni ma lascia missing fundamental e parte della polifonia fuori dai gate produttivi.
5. **Gain globale residuo.** La coerenza migliora molto, ma il laboratorio mantiene ancora la compensazione energetica V2.
6. **Nessuna transizione musicale reale.** Non sono testati salti di nota, scale lock, hysteresis, hard lock o traiettorie microtonali dinamiche oltre al vibrato sintetico.
7. **Nessun audio reale.** Mancano voce maschile, femminile, rap e cori.
8. **Nessun requisito realtime verificato.** Il Python usa dizionari, allocazioni e operazioni non trasferibili direttamente nel callback.
9. **Nessun porting C++.** `Source/` resta deliberatamente invariato.

---

## File

```text
OutputLab/v3_source/neumaton_output_lab_v3_oracle.py.gz.b64.part00..05
OutputLab/restore_v3.py              # verifica archivio e genera il sorgente Python
OutputLab/V3_CHECKSUMS.txt
OutputLab/verify_v3a.py
OutputLab/results/v3a_oracle_summary.csv
OutputLab/results/v3a_oracle_diagnostics.csv
OutputLab/results/v3a_raw_gain_N256.csv
OUTPUT_STAGE_LAB_V3_ALPHA.md
```

Il sorgente ripristinato è protetto da checksum SHA-256 sia dell'archivio Base64 concatenato sia del file Python decompresso. Il verificatore controlla poi il contratto stretto:

- no-op numerico a N=128;
- gate Live/N=256 su livello, aria, transiente, famiglia vecchia, missing fundamental, vibrato e polifonia;
- assenza di regressioni larghe a N=512.

---

## Prossima decisione

Il risultato autorizza lo sviluppo della **V3-beta nel laboratorio**, non ancora la sostituzione del renderer produttivo.

Il prossimo componente deve sostituire gli ID oracle con osservazioni reali mantenendo invariati:

- accumulatore e coast;
- traiettoria target fornita dal controller;
- campo di fase per bin di destinazione;
- singola deposizione;
- diagnostiche e corpus.

In parallelo si può progettare l'interfaccia C++ (`NeumatonRidgeLedger` / `NeumatonOutputRenderer`) e individuare i punti di innesto in `ModernPitchEngine`, ma il codice attivo in `Source/` non dovrebbe ancora essere modificato.
