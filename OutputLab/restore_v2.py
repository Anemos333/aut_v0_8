#!/usr/bin/env python3
from __future__ import annotations

import base64
import gzip
from pathlib import Path

HERE = Path(__file__).resolve().parent
encoded_path = HERE / 'neumaton_output_lab_v2_candidate.py.gz.b64'
target_path = HERE / 'neumaton_output_lab_v2_candidate.py'

encoded = encoded_path.read_text(encoding='ascii')
source = gzip.decompress(base64.b64decode(encoded))
target_path.write_bytes(source)
print(f'Wrote {target_path} ({len(source)} bytes)')
