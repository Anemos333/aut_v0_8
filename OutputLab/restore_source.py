#!/usr/bin/env python3
from __future__ import annotations

import base64
import gzip
from pathlib import Path

HERE = Path(__file__).resolve().parent
encoded = (HERE / 'neumaton_output_lab.py.gz.b64').read_text(encoding='ascii')
source = gzip.decompress(base64.b64decode(encoded))
target = HERE / 'neumaton_output_lab.py'
target.write_bytes(source)
print(f'Wrote {target} ({len(source)} bytes)')
