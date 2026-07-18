#!/usr/bin/env python3
from __future__ import annotations

import base64
import gzip
import hashlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
parts_dir = HERE / 'v3_source'
target_path = HERE / 'neumaton_output_lab_v3_oracle.py'
expected_part_sizes = [2000, 2000, 2000, 2000, 2000, 1448]
expected_encoded_sha256 = '75b24b463cf30ebea1dd17ad6f3e19a3e2664b6a15aaea4e327c9692c094d301'
expected_source_sha256 = '75cfcdfeb4f6e08220f378a9c9b788023f2d421a7113e35680e8518be661757f'
expected_source_size = 35896

parts = sorted(parts_dir.glob('neumaton_output_lab_v3_oracle.py.gz.b64.part*'))
if len(parts) != len(expected_part_sizes):
    raise SystemExit(f'Expected {len(expected_part_sizes)} V3 source parts, found {len(parts)}')

chunks: list[str] = []
for path, expected_size in zip(parts, expected_part_sizes, strict=True):
    chunk = path.read_text(encoding='ascii').strip()
    if len(chunk) != expected_size:
        raise SystemExit(f'Unexpected size for {path.name}: {len(chunk)} != {expected_size}')
    chunks.append(chunk)

encoded = ''.join(chunks)
actual_encoded_sha256 = hashlib.sha256(encoded.encode('ascii')).hexdigest()
if actual_encoded_sha256 != expected_encoded_sha256:
    raise SystemExit(f'V3 encoded checksum mismatch: {actual_encoded_sha256}')

try:
    source = gzip.decompress(base64.b64decode(encoded, validate=True))
except (ValueError, gzip.BadGzipFile) as exc:
    raise SystemExit(f'V3 source archive is invalid: {exc}') from exc

actual_source_sha256 = hashlib.sha256(source).hexdigest()
if actual_source_sha256 != expected_source_sha256:
    raise SystemExit(f'V3 source checksum mismatch: {actual_source_sha256}')
if len(source) != expected_source_size:
    raise SystemExit(f'V3 source size mismatch: {len(source)} != {expected_source_size}')

target_path.write_bytes(source)
print(
    f'Wrote {target_path} ({len(source)} bytes, '
    f'archive_sha256={actual_encoded_sha256}, source_sha256={actual_source_sha256})'
)
