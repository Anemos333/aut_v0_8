#!/usr/bin/env python3
from __future__ import annotations

import base64
import gzip
import hashlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
encoded_path = HERE / 'neumaton_output_lab_v3_oracle.py.gz.b64'
target_path = HERE / 'neumaton_output_lab_v3_oracle.py'
expected_sha256 = '75cfcdfeb4f6e08220f378a9c9b788023f2d421a7113e35680e8518be661757f'

encoded = encoded_path.read_text(encoding='ascii')
source = gzip.decompress(base64.b64decode(encoded))
actual_sha256 = hashlib.sha256(source).hexdigest()
if actual_sha256 != expected_sha256:
    raise SystemExit(f'V3 source checksum mismatch: {actual_sha256}')
target_path.write_bytes(source)
print(f'Wrote {target_path} ({len(source)} bytes, sha256={actual_sha256})')
