from __future__ import annotations
import argparse, csv, json, sys
from dataclasses import asdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import neumaton_output_lab as lab

RENDERERS = {
    r.name: r for r in [
        lab.UniformWideRenderer(),
        lab.SingleFieldOwnershipRenderer(),
        lab.PhaseCovariantOwnershipRenderer(),
        lab.EventAnchoredOwnershipRenderer(),
        lab.PeakRegionRenderer(),
    ]
}


def write_csv(path: Path, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        return
    with path.open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader(); w.writerows(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--frame', type=int, required=True)
    ap.add_argument('--duration', type=float, default=0.45)
    ap.add_argument('--renderers', nargs='+', default=list(RENDERERS))
    ap.add_argument('--cases', nargs='+', default=[
        'clean_vowel_7_6', 'consonant_vowel', 'missing_fundamental',
        'two_voice_polyphony', 'aperiodic_air'])
    ap.add_argument('--write-wavs', action='store_true')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    cases = [c for c in lab.make_cases(48000, args.duration) if c.name in args.cases]
    renderers = [RENDERERS[n] for n in args.renderers]
    metrics=[]
    for case in cases:
        if args.write_wavs:
            lab.write_wav(args.out/'wav'/case.name/'source.wav', case.source, 48000)
            lab.write_wav(args.out/'wav'/case.name/'oracle.wav', case.oracle, 48000)
        for renderer in renderers:
            y=lab.process_signal(case, renderer, args.frame, 48000)
            m=lab.evaluate(case, renderer, y, args.frame, 48000)
            metrics.append(m)
            if args.write_wavs:
                lab.write_wav(args.out/'wav'/case.name/f'{renderer.name}_N{args.frame}.wav', y, 48000)
            print(args.frame, case.name, renderer.name, f'LSD={m.spectral_lsd_db:.3f}', flush=True)
    detail=[asdict(m) for m in metrics]
    summary=lab.aggregate(metrics)
    write_csv(args.out/'metrics_detail.csv', detail)
    write_csv(args.out/'metrics_summary.csv', summary)
    (args.out/'metrics_summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')

if __name__ == '__main__':
    main()
