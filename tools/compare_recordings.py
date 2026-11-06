#!/usr/bin/env python3
"""Compare complete TR-66 capture loops with the C sequencer and synth."""

import argparse
import ctypes as c
import csv
from dataclasses import dataclass
import json
from math import gcd
import os
from pathlib import Path
import struct
import subprocess
import tempfile

import numpy as np
from scipy import optimize, signal
import soundfile as sf

from calibrate import Engine, NAMES, Patch, RATE, ROOT, emit_patch, rms

REFS = ROOT / "work/references/complete/drumbase"
RENDERER = ROOT / "work/build/tr66-render"
LIBRARY = ROOT / "work/build/libtr66.so"
EDGES = (25, 125, 235, 340, 490, 710, 1050, 1900, 3400, 5700,
         7200, 8650, 9900, 12000, 15800)
WINDOW = 2048
HOP = 480
HANN = signal.windows.hann(WINDOW, sym=False)
BINS = np.searchsorted(np.fft.rfftfreq(WINDOW, 1 / RATE), EDGES)
HOLDOUT = {"BOSSANOVA", "CHA-CHA", "J-WALTZ", "SAMBA", "WALTZ", "S-ROCK", "ROCK2A",
           "ROCK4A", "HABANERA", "SWING1", "SWING3", "TANGO"}
AUDITION = ("ROCK2A", "BOSSANOVA", "HABANERA", "SWING1", "SAMBA", "MAMBO")


@dataclass(frozen=True)
class Groove:
    file: str
    rhythm: str
    variation: str = "off"
    arrange: str = "auto"
    bars: int = 4
    beats: int = 4
    skip_bars: int = 0


GROOVES = [
    Groove("BEGUINE", "beguine"),
    Groove("BOSSANOVA", "bossa-nova"),
    Groove("CHA-CHA", "cha-cha"),
    Groove("J-WALTZ", "jazz-waltz", beats=3),
    Groove("MAMBO", "mambo"),
    Groove("RHUMBA", "rhumba"),
    Groove("S-ROCK", "slow-rock", beats=3),
    # Reverb's capture has the A phrase. Drumbase has the B fill.
    Groove("SAMBA", "samba", arrange="b", bars=2),
    Groove("WALTZ", "waltz", beats=3),
    Groove("HABANERA", "off", "habanera", "a", 1),
    Groove("MARCH", "off", "march", "a", 1),
    Groove("PARADE", "off", "parade", "b", 2),
    # The CD names this A, but its kick on 2-and matches Roland's B chart.
    Groove("ROCK1A", "off", "rock-1", "b", 1),
    Groove("ROCK2A", "off", "rock-2", "a", 1),
    Groove("ROCK3A", "off", "rock-3", "a", 1),
    Groove("ROCK4A", "off", "rock-4", "a", 2),
    Groove("ROCK5A", "off", "rock-5", "a", 1),
    Groove("ROCK6", "off", "rock-6", "a", 2),
    Groove("SHUFFLE", "off", "shuffle", "b", 1, skip_bars=1),
    Groove("SWING1", "off", "swing-1", "a", 1),
    Groove("SWING2", "off", "swing-2", "a", 1),
    Groove("SWING3", "off", "swing-3", "a", 1),
    Groove("TANGO", "off", "tango", "b", 2),
]


def sampler_loop(path):
    with path.open("rb") as source:
        source.read(12)
        while header := source.read(8):
            chunk, size = struct.unpack("<4sI", header)
            if chunk == b"smpl":
                data = source.read(size)
                if struct.unpack_from("<I", data, 28)[0] == 1:
                    return struct.unpack_from("<II", data, 44)
                break
            source.seek(size + (size & 1), 1)
    raise ValueError(f"{path}: expected a single sampler loop")


def render(exe, directory, groove, tempo):
    wave = directory / "audio.wav"
    trace = directory / "events.csv"
    bars = groove.bars + groove.skip_bars
    subprocess.run([exe, wave, groove.rhythm, groove.variation, groove.arrange,
                    f"{tempo:.3f}", str(bars), trace], check=True)
    with trace.open(newline="") as events:
        rows = list(csv.DictReader(events))
    positions = np.array([int(row["frame"]) for row in rows], dtype=np.uint32)
    masks = np.array([int(row["voices"]) for row in rows], dtype=np.uint16)
    audio, rate = sf.read(wave)
    if rate != RATE or audio.ndim != 1:
        raise ValueError(f"{wave}: expected mono at {RATE} Hz")
    return audio[:-RATE], positions, masks


def estimate_clock(samples, rate, nominal_bpm, start, ticks):
    # Use broadband onset flux to find a grid common to every model iteration.
    # Relative tempo comes from the sampler loop and may differ from the edits.
    bands = ((45, 165), (165, 500), (500, 1100), (1100, 3400), (3400, 9900))
    filtered = np.stack([signal.sosfiltfilt(signal.butter(
        3, band, "bandpass", fs=rate, output="sos"), samples) for band in bands])
    width = round(0.003 * rate)
    power = signal.fftconvolve(filtered ** 2, np.ones((1, width)) / width,
                               mode="same", axes=-1)
    envelope = np.log1p(25 * np.sqrt(np.maximum(0, power)))
    offset = round(0.008 * rate)
    flux = np.maximum(0, envelope[:, offset:] - envelope[:, :-offset]).max(axis=0)
    # The two windows straddle the estimated transient position.
    coordinates = np.arange(len(flux)) + offset // 2
    pulse = 60 * rate / (24 * nominal_bpm)

    def loss(params):
        phase, ratio = params
        expected = start + phase + ticks * pulse * ratio
        return -np.mean(np.interp(expected, coordinates, flux, left=0, right=0))

    fit = optimize.differential_evolution(
        loss, [(-0.035 * rate, 0.035 * rate), (0.98, 1.02)],
        seed=11, tol=0.002, popsize=10,
    )
    phase, ratio = fit.x
    return float(start + phase), round(float(nominal_bpm / ratio), 3)


def band_power(samples):
    windows = np.lib.stride_tricks.sliding_window_view(samples, WINDOW)[::HOP]
    bins = np.abs(np.fft.rfft(windows * HANN, axis=-1)) ** 2
    return np.stack([bins[:, a:b].sum(axis=1) for a, b in zip(BINS, BINS[1:])], axis=1)


def spectral_residual(target, model):
    # Match broadband level per clip. Weight spectral differences by the
    # recorded energy so quiet and stochastic tails do not drive the result.
    target = target / target.sum()
    model = model / model.sum()
    floor = target.max() * 1e-5
    weight = (target / target.max()) ** 0.30
    delta = 10 * np.log10((model + floor) / (target + floor))
    return delta * np.sqrt(weight / weight.sum())


def spectral_error(target, model):
    return round(float(np.linalg.norm(spectral_residual(target, model))), 2)


def stem_renderer():
    lib = c.CDLL(str(LIBRARY))
    function = lib.tr66_render_stem
    function.argtypes = [
        c.c_uint, np.ctypeslib.ndpointer(dtype=np.uint32, flags="C_CONTIGUOUS"),
        np.ctypeslib.ndpointer(dtype=np.uint16, flags="C_CONTIGUOUS"), c.c_size_t,
        np.ctypeslib.ndpointer(dtype=np.float32, flags="C_CONTIGUOUS"), c.c_size_t,
    ]
    function.restype = None
    return function, c.c_float.in_dll(lib, "tr66_line_gain").value


def match_capture(path, directory, groove, comparison, baseline, fit_gains):
    samples, rate = sf.read(path)
    start, end = sampler_loop(path)
    nominal = 60 * rate * groove.beats * groove.bars / (end + 1 - start)
    _, positions, _ = render(RENDERER, directory, groove, 120)
    first_tick = groove.skip_bars * groove.beats * 24
    ticks = positions.astype(float) / 1000 - first_tick
    phase, tempo = estimate_clock(samples, rate, nominal, start, ticks[ticks > 0])
    model, positions, masks = render(RENDERER, directory, groove, tempo)
    skip = round(RATE * 60 * groove.beats * groove.skip_bars / tempo)
    model = model[skip:]
    points = phase + np.arange(len(model)) * rate / RATE
    analysis_rate = 4 * RATE
    divisor = gcd(rate, analysis_rate)
    oversampled = signal.resample_poly(samples, analysis_rate // divisor, rate // divisor)
    reference = np.interp(points * analysis_rate / rate, np.arange(len(oversampled)),
                          oversampled, left=0, right=0)

    # Exclude the spliced first attack and the tail outside the archived WAV.
    end = min(len(model), int((len(samples) - phase) * RATE / rate))
    interior = slice(RATE // 25, end - RATE // 50)
    target = band_power(reference[interior])
    modeled = band_power(model[interior])
    limited = np.abs(samples) >= 32767 / 32768
    clipped = limited[np.clip(np.rint(points[interior]).astype(int), 0, len(samples) - 1)]
    clean = np.lib.stride_tricks.sliding_window_view(clipped, WINDOW)[::HOP].sum(axis=1) < 3
    result = {
        "selection": [groove.rhythm, groove.variation, groove.arrange],
        "bars": groove.bars,
        "skip_bars": groove.skip_bars,
        "fit_role": "held out" if groove.file in HOLDOUT else "mix fit",
        "tempo": tempo,
        "aligned_frame_in_capture": round(phase, 2),
        "capture_rate": rate,
        "capture_samples_at_pcm_limit": int(limited.sum()),
        "model_samples_at_pcm_limit": int(np.count_nonzero(np.abs(model) >= 32767 / 32768)),
        "clean_analysis_windows": int(clean.sum()),
        "spectral_error_db": spectral_error(target, modeled),
        "unclipped_error_db": spectral_error(target[clean], modeled[clean]),
    }
    if baseline:
        old, _, _ = render(baseline, directory, groove,
                           tempo * (0.75 if groove.rhythm == "slow-rock" else 1.0))
        length = min(len(model), len(old) - skip)
        if interior.stop <= length:
            old_power = band_power(old[skip:skip + length][interior])
            result["before_db"] = spectral_error(target, old_power)
            result["before_unclipped_db"] = spectral_error(target[clean], old_power[clean])

    if groove.file in AUDITION:
        comparison[groove.file] = (reference[:end].copy(), model[:end].copy())

    powers = None
    if fit_gains and groove.file not in HOLDOUT:
        render_stem, line_gain = stem_renderer()
        balance = np.float32(os.environ.get("TR66_RENDER_BALANCE", "0"))
        scales = np.ones(len(NAMES), dtype=np.float32)
        scales[NAMES.index("kick")] -= balance
        scales[[NAMES.index("hat"), NAMES.index("cymbal")]] += balance
        scratch = np.empty(skip + len(model), dtype=np.float32)
        mix = np.zeros_like(model)
        stems = []
        for voice, scale in enumerate(scales):
            render_stem(voice, positions, masks, len(positions), scratch, len(scratch))
            stem = scratch[skip:] * scale
            mix += stem
            stems.append(band_power(stem[interior])[clean])
        expected = np.rint(32767 * np.clip(line_gain * mix, -1, 1)) / 32768
        difference = np.max(np.abs(model - expected))
        if difference > 1 / 32768:
            raise ValueError(f"{groove.file}: C stems differ from the render by {difference:g}")
        powers = np.stack(stems)
    return result, target[clean], powers


def fit_voice_gains(training, destination):
    present = np.any([p.any(axis=(1, 2)) for _, p in training], axis=0)
    present[0] = False

    def residual(params):
        # Keep the kick as the reference level. Each other gain is shared
        # across every groove. There is no equalization of individual loops.
        squared_gains = np.ones(len(NAMES))
        squared_gains[present] = np.exp(2 * params)
        return np.concatenate([
            spectral_residual(target, np.einsum("v,vij->ij", squared_gains, powers)).ravel()
            for target, powers in training
        ])

    solution = optimize.least_squares(
        residual, np.zeros(present.sum()), bounds=(-1.2, 1.2),
        max_nfev=100, loss="soft_l1", f_scale=0.1,
    )
    gains = np.ones(len(NAMES))
    gains[present] = np.exp(solution.x)
    engine = Engine(LIBRARY)
    table = ["/* Voice models from calibrate.py, with levels fitted to full Drumbase loops. */"]
    for name, factory, gain in zip(NAMES, engine.patches, gains):
        patch = Patch.from_buffer_copy(factory)
        for tone in patch.tone:
            tone.cosine *= gain
            tone.sine *= gain
        for band in patch.band:
            band.gain *= gain
        table.append(emit_patch(name, patch))
    destination.write_text("\n".join(table) + "\n")
    return dict(zip(NAMES, map(float, gains)))


def write_audition(path, comparisons, order=AUDITION):
    segments = []
    for key in order:
        reference, model = comparisons[key]
        fade = np.linspace(0, 1, RATE // 400)
        for take in (reference, model):
            take[:len(fade)] *= fade
            take[-len(fade):] *= fade[::-1]
        reference *= rms(model) / rms(reference)
        segments.extend((reference, np.zeros(RATE // 3), model, np.zeros(RATE // 2)))
    comparison = np.concatenate(segments)
    comparison *= 0.9 / np.max(np.abs(comparison))
    sf.write(path, comparison, RATE, subtype="PCM_24")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--references", type=Path, default=REFS)
    parser.add_argument("--baseline", type=Path, help="Optional previous C renderer with decimal BPM")
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--audition", type=Path)
    parser.add_argument("--fit-gains", type=Path, help="Write candidate C patches with new mix gains")
    args = parser.parse_args()
    comparisons, report, training = {}, {}, []
    with tempfile.TemporaryDirectory() as temp:
        directory = Path(temp)
        for groove in GROOVES:
            path, = args.references.rglob(f"66 {groove.file}.wav")
            metrics, target, powers = match_capture(
                path, directory, groove, comparisons, args.baseline, args.fit_gains)
            report[groove.file] = metrics
            print(groove.file, metrics, flush=True)
            if powers is not None:
                training.append((target, powers))
    outcome = {"audition_order": AUDITION, "loops": report}
    if args.fit_gains:
        outcome["fit_gains"] = fit_voice_gains(training, args.fit_gains)
    args.report.write_text(json.dumps(outcome, indent=2) + "\n")
    if args.audition:
        write_audition(args.audition, comparisons)


if __name__ == "__main__":
    main()
