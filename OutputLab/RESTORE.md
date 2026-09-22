# Ripristino del sorgente Python

Il sorgente completo del laboratorio è conservato compresso per evitare una patch GitHub molto rumorosa.

```bash
cd OutputLab
python restore_source.py
python -m pip install -r requirements.txt
python neumaton_output_lab.py --out results_quality --frames 512 --duration 0.45 --write-wavs
```

`restore_source.py` decodifica `neumaton_output_lab.py.gz.b64` e genera `neumaton_output_lab.py` senza accesso alla rete.
