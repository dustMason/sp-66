#!/usr/bin/env python3
"""Identify and compare the Reverb TR-66 loops with the C sequencer."""

import argparse
import json
from pathlib import Path
import tempfile

import numpy as np
import soundfile as sf

from calibrate import RATE, ROOT
from compare_recordings import (
    Groove, RENDERER, band_power, estimate_clock, render, spectral_error, write_audition,
)

REFS = ROOT / "work/references/complete/reverb"
HOLDOUT = {4, 16, 17, 19, 20}
MIX_FIT = {2, 13, 15, 18, 21, 22}
AUDITION = ("Loop24.wav", "Loop18.wav", "Loop7.wav", "Loop20.wav", "Loop21.wav", "Loop23.wav")
# Loop1 is two repeats of Rock 1 B's first bar, without the second-bar fill.
CAPTURE_REPEATS = {"Loop1": 2}
# File names in the pack are sequential. The labels come from matching
# voice onsets and fills on a circular beat grid. A or B describes the
# captured phrase, even if that phrase also occurs in Auto.
SELECTIONS = (
    (1, Groove("Loop1", "off", "rock-1", "b", 1), 6),
    (2, Groove("Loop2", "off", "rock-2", "a", 2), 0),
    (3, Groove("Loop3", "off", "rock-3", "a", 1), 0),
    (4, Groove("Loop4", "off", "rock-4", "a", 2), 0),
    (5, Groove("Loop5", "off", "rock-5", "a", 2), 6),
    (6, Groove("Loop6", "off", "rock-6", "a", 2), 1),
    (7, Groove("Loop7", "off", "foxtrot-1", "a", 2), 1),
    (8, Groove("Loop8", "off", "swing-1", "a", 2), 1),
    (9, Groove("Loop9", "off", "parade", "a", 2), 0),
    (10, Groove("Loop10", "off", "habanera", "a", 2), 5.5),
    (11, Groove("Loop11", "off", "shuffle", "a", 2), 0),
    (12, Groove("Loop12", "off", "tango", "a", 2), 6),
    (13, Groove("Loop13", "waltz", arrange="a", bars=2, beats=3), 0),
    (14, Groove("Loop14", "jazz-waltz", arrange="a", bars=2, beats=3), 0),
    (15, Groove("Loop15", "slow-rock", arrange="a", bars=2, beats=3), 0),
    (16, Groove("Loop16", "bossa-nova", arrange="a", bars=2), 0),
    (17, Groove("Loop17", "samba", arrange="a", bars=2), 0),
    (18, Groove("Loop18", "mambo", arrange="a", bars=2), 3),
    (19, Groove("Loop19", "cha-cha", arrange="a", bars=2), 2),
    (20, Groove("Loop20", "rhumba", arrange="a", bars=2), 4),
    (21, Groove("Loop21", "rhumba", arrange="b", bars=2), 4),
    (22, Groove("Loop22", "beguine", arrange="b", bars=2), 0),
    (23, Groove("Loop23", "mambo", arrange="b", bars=2), 6),
    (24, Groove("Loop24", "slow-rock", arrange="b", bars=2, beats=3), 3),
    (25, Groove("Loop25", "jazz-waltz", arrange="b", bars=2, beats=3), 3),
)


def evaluate(path, groove, rotation, workdir, baseline,
             baseline_slow_rock_scale=0.75, comparisons=None):
    capture, rate = sf.read(path, always_2d=True)
    if rate != RATE:
        raise ValueError(f"{path}: expected {RATE} Hz")
    capture = capture.mean(axis=1)
    capture_bars = groove.bars * CAPTURE_REPEATS.get(groove.file, 1)
    quarters = capture_bars * groove.beats
    nominal = RATE * 60 * quarters / len(capture)
    capture = np.roll(capture, round(len(capture) * rotation / quarters))
    _, events, _ = render(RENDERER, workdir, groove, 120)
    phase, tempo = estimate_clock(capture, rate, nominal, 0, events[1:] / 1000)
    model, _, _ = render(RENDERER, workdir, groove, tempo)
    length = min(len(model), len(capture))
    # Discard the first and last attacks around the edited loop boundary.
    interior = slice(RATE // 12, length - RATE // 12)
    source = np.interp(phase + np.arange(length), np.arange(len(capture)), capture)
    target = band_power(source[interior])
    result = {
        "selection": [groove.rhythm, groove.variation, groove.arrange],
        "capture_bars": capture_bars,
        "bars_compared": groove.bars,
        "rotation_quarters": rotation,
        "tempo": tempo,
        "spectral_error_db": spectral_error(target, band_power(model[interior])),
    }
    if baseline:
        old_tempo = tempo * (baseline_slow_rock_scale if groove.rhythm == "slow-rock" else 1.0)
        old, _, _ = render(baseline, workdir, groove, old_tempo)
        result["before_db"] = spectral_error(target, band_power(old[interior]))
    if comparisons is not None and path.name in AUDITION:
        comparisons[path.name] = (source, model[:length])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--references", type=Path, default=REFS)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--baseline-slow-rock-scale", type=float, default=0.75,
                        help="Pass 1 when the baseline already uses quarter-note tempo")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--audition", type=Path)
    args = parser.parse_args()
    loops = {}
    comparisons = {} if args.audition else None
    with tempfile.TemporaryDirectory() as directory:
        for i, groove, rotation in SELECTIONS:
            path, = args.references.rglob(f"Loop{i}.wav")
            metrics = evaluate(path, groove, rotation, Path(directory), args.baseline,
                               args.baseline_slow_rock_scale, comparisons)
            if i in MIX_FIT:
                metrics["fit_role"] = "mix fit"
            elif i in HOLDOUT:
                metrics["fit_role"] = "held out"
            else:
                metrics["fit_role"] = "new validation"
            loops[path.name] = metrics
            print(path.name, metrics, flush=True)
    measures = ("before_db", "spectral_error_db") if args.baseline else ("spectral_error_db",)
    result = {
        "source": "Reverb Roland Rhythm Arranger Sample Pack",
        "measure": "Per-loop level-matched RMS log band-power error in dB over 43 ms windows.",
        "selection_note": ("Pattern and phrase labels inferred from recorded onsets and fills. "
                           "The selected phrase can also occur in Auto."),
        "audition_order": AUDITION,
        "median": {metric: float(np.median([loop[metric] for loop in loops.values()]))
                   for metric in measures},
        "held_out_median": {
            metric: float(np.median([loop[metric] for loop in loops.values()
                                    if loop["fit_role"] == "held out"]))
            for metric in measures
        },
        "new_validation_median": {
            metric: float(np.median([loop[metric] for loop in loops.values()
                                    if loop["fit_role"] == "new validation"]))
            for metric in measures
        },
        "loops": loops,
    }
    args.report.write_text(json.dumps(result, indent=2) + "\n")
    if args.audition:
        write_audition(args.audition, comparisons, AUDITION)


if __name__ == "__main__":
    main()
