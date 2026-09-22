# Neumaton Output Laboratory

Laboratorio standalone per confrontare contratti d'uscita spettrali prima di modificare il VST.

## Requisiti

```bash
python -m pip install -r requirements.txt
```

## Corpus completo Quality

```bash
python neumaton_output_lab.py --out results_quality --frames 512 --duration 0.45 --write-wavs
```

## Tutte le modalità

```bash
python neumaton_output_lab.py --out results_all --frames 128 256 512 --duration 0.45
```

L'esecuzione Python a N=128 è intenzionalmente un laboratorio, non un benchmark realtime del futuro C++.

## Sottoinsieme critico

```bash
python run_subset.py --out results_live --frame 256 --duration 0.45 --write-wavs
```

Gli oracle usano gli stessi formanti, rumori ed eventi della sorgente, ma una fondamentale target nota. Il detector del plug-in non viene eseguito: il test misura soltanto il renderer.
