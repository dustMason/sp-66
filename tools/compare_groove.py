#!/usr/bin/env python3
"""Compare sample-backed playback of a trigger trace with the C model."""

import argparse
import csv
from pathlib import Path

import numpy as np
import soundfile as sf

from calibrate import FILES, NAMES, RATE, ROOT, read, rms

ALTERNATE_HITS = {
    "kick": ((1 << 10, 0.50), (1 << 14, 0.43)),
    "conga": ((1 << 11, 0.66),),
    "high-bongo": ((1 << 12, 0.53), (1 << 13, 0.20)),
    "hat": ((1 << 15, 1.0),),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("trace", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--references", type=Path, default=ROOT / "work/references/drum-abuse")
    args = parser.parse_args()

    model, rate = sf.read(args.model)
    if rate != RATE or model.ndim != 1:
        parser.error("expected mono model render at 48 kHz")
    if np.max(np.abs(model)) >= 0.9999:
        parser.error("model render clipped")

    reference = np.zeros_like(model)
    hits = {}
    with args.trace.open(newline="") as trace:
        for event in csv.DictReader(trace):
            start = int(event["frame"])
            mask = int(event["voices"])
            if not (0 <= start < len(model) and 0 < mask < (1 << 16)):
                parser.error(f"invalid event {event}")
            for voice, name in enumerate(NAMES):
                gain = 1.0 if mask & (1 << voice) else next(
                    (level for bit, level in ALTERNATE_HITS.get(name, ()) if mask & bit), 0.0)
                if gain == 0.0:
                    continue
                if name not in hits:
                    hits[name] = read(args.references / "4" / FILES[name][0], RATE // 2)
                length = min(len(hits[name]), len(model) - start)
                reference[start:start + length] += gain * hits[name][:length]

    if rms(reference) == 0.0 or rms(model) == 0.0:
        parser.error("both sides must contain audio")
    reference *= rms(model) / rms(reference)
    comparison = np.concatenate((reference, np.zeros(RATE // 2), model))
    comparison *= 0.9 / np.max(np.abs(comparison))
    sf.write(args.output, comparison, RATE, subtype="PCM_24")


if __name__ == "__main__":
    main()
