#!/usr/bin/env python3
"""Verify the narrow V3-alpha laboratory contract.

This is intentionally not a production-readiness test. It verifies that the
oracle ledger solves the Live/N=256 phase-coherence failure without changing
V2 at N=128 and without broad regressions at N=512.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path


def load_rows(path: Path) -> dict[tuple[int, str], dict[str, str]]:
    with path.open(newline='', encoding='utf-8') as handle:
        rows = list(csv.DictReader(handle))
    return {(int(row['frame_size']), row['case']): row for row in rows}


def value(row: dict[str, str], key: str) -> float:
    return float(row[key])


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('summary', type=Path)
    args = parser.parse_args()
    rows = load_rows(args.summary)
    failures: list[str] = []

    expected_cases = {
        'clean_vowel_7_6', 'breathy_vowel_19edo', 'consonant_vowel',
        'vibrato_19edo', 'missing_fundamental', 'two_voice_polyphony',
        'aperiodic_air',
    }
    require(set(name for size, name in rows if size == 256) == expected_cases,
            'N=256 corpus is incomplete', failures)

    invariant_fields = (
        'spectral_lsd_db', 'envelope_lsd_db', 'rms_delta_db',
        'target_comb_db', 'old_family_leak_db', 'double_family_risk',
        'highband_flatness_error', 'transient_peak_error_ms',
        'transient_width_error_ms',
    )
    for case in expected_cases:
        row = rows[(128, case)]
        for field in invariant_fields:
            baseline = value(row, f'baseline_{field}') if f'baseline_{field}' in row else value(row, field)
            current = value(row, field)
            require(abs(current - baseline) <= 1.0e-9,
                    f'N=128 changed {case}/{field}: {baseline} -> {current}', failures)

    for case in expected_cases:
        row = rows[(256, case)]
        require(abs(value(row, 'rms_delta_db')) < 1.5,
                f'N=256 level gate failed for {case}', failures)

    air = rows[(256, 'aperiodic_air')]
    require(value(air, 'spectral_lsd_db') < 0.5, 'N=256 air LSD failed', failures)
    require(abs(value(air, 'rms_delta_db')) < 0.2, 'N=256 air RMS failed', failures)

    consonant = rows[(256, 'consonant_vowel')]
    require(value(consonant, 'transient_peak_error_ms') < 1.0,
            'N=256 consonant peak failed', failures)
    require(value(consonant, 'transient_width_error_ms') < 2.0,
            'N=256 consonant width failed', failures)

    require(value(rows[(256, 'clean_vowel_7_6')], 'old_family_leak_db') < 0.0,
            'N=256 clean old-family gate failed', failures)
    require(value(rows[(256, 'missing_fundamental')], 'double_family_risk') < 0.25,
            'N=256 missing-fundamental risk failed', failures)
    require(value(rows[(256, 'vibrato_19edo')], 'double_family_risk') < 0.25,
            'N=256 vibrato family gate failed', failures)
    poly = rows[(256, 'two_voice_polyphony')]
    require(value(poly, 'double_family_risk') < 0.25,
            'N=256 polyphony risk failed', failures)
    require(value(poly, 'old_family_leak_db') < 0.0,
            'N=256 polyphony old-family gate failed', failures)

    for case in expected_cases:
        row = rows[(512, case)]
        require(abs(value(row, 'rms_delta_db')) < 1.5,
                f'N=512 level gate failed for {case}', failures)
        envelope_delta = value(row, 'envelope_lsd_db') - value(row, 'baseline_envelope_lsd_db')
        require(envelope_delta <= 0.60,
                f'N=512 envelope regression too large for {case}: {envelope_delta:.3f} dB', failures)

    if failures:
        print('V3-alpha verification FAILED')
        for failure in failures:
            print(f'- {failure}')
        return 1

    print('V3-alpha verification passed: narrow oracle-ledger contract satisfied.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
