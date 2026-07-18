# Neumaton Output Laboratory

Laboratorio standalone per confrontare contratti d'uscita spettrali prima di modificare il VST.

## Requisiti

```bash
python -m pip install -r requirements.txt
```

## Baseline V2

Ripristino del candidato congelato:

```bash
python restore_v2.py
```

Corpus completo:

```bash
python neumaton_output_lab_v2_candidate.py --out results_v2 --frames 128 256 512 --duration 0.45
```

## V3-alpha oracle ledger

Ripristino verificato del sorgente multipart:

```bash
python restore_v2.py
python restore_v3.py
```

Esecuzione ufficiale a 48 kHz:

```bash
python neumaton_output_lab_v3_oracle.py \
  --out results_v3a \
  --sample-rate 48000 \
  --frames 128 256 512 \
  --duration 0.45
```

Verifica del contratto sperimentale:

```bash
python verify_v3a.py results_v3a/v3a_metrics_detail.csv
```

Il report completo è `../OUTPUT_STAGE_LAB_V3_ALPHA.md`. La V3-alpha usa ridge e traiettorie oracle: dimostra la legge di fase a N=256, ma non è ancora un tracker né un renderer produttivo.

## Laboratorio V1 e sottoinsieme critico

```bash
python restore_source.py
python neumaton_output_lab.py --out results_all --frames 128 256 512 --duration 0.45
python run_subset.py --out results_live --frame 256 --duration 0.45 --write-wavs
```

L'esecuzione Python a N=128 è intenzionalmente un laboratorio, non un benchmark realtime del futuro C++.

Gli oracle usano gli stessi formanti, rumori ed eventi della sorgente, ma una fondamentale target nota. Il detector del plug-in non viene eseguito: il test misura soltanto il renderer.
