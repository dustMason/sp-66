#!/usr/bin/env python3
"""Fit TR-66 C voice parameters to local one-shots and report held-out metrics."""

import argparse
import ctypes as c
import json
import math
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy import fft, optimize, signal

RATE = 48_000
TONAL_MODES = 5  # Matches TR66_TONES in src/tr66.h.
SEEDS = (319, 1223, 5897, 8831, 15091, 19073)
ROOT = Path(__file__).resolve().parents[1]
NAMES = (
    "kick", "snare", "hat", "cymbal", "claves", "rim",
    "cowbell", "conga", "high-bongo", "low-bongo",
)

# Set 4 shares the same captures as 3/Dry, but retains more useful gain
# relationships between the hats and other voices. Avoid testing against 3/Dry.
# The long treble and 843 Hz hit are provisional assignments pending a voice tap.
FILES = {
    "kick": ("Kick.wav", "66-bd-03.wav", "66-bd-05.wav"),
    "snare": ("Snare.wav", "66-sd-03.wav", "66-sd-06.wav"),
    "hat": ("HH Cl.wav", "66-hh-02-or.wav", "66-hh-06.wav"),
    "cymbal": ("HH Op.wav", "66-hho-02-or.wav", "66-hho-04.wav"),
    "claves": ("Clave.wav", "66-per-11.wav"),
    "rim": ("Rim.wav", "66-rim-03.wav"),
    "cowbell": ("Conga Hi.wav", "66-per-25.wav"),
    "conga": ("Tom.wav", "66-per-33.wav"),
    "high-bongo": ("Conga Mi.wav", "66-per-23.wav"),
    "low-bongo": ("Conga Lo.wav", "66-per-09.wav"),
}
DRUMBASE_FILES = {
    "kick": "TR66 BD1.wav",
    "snare": "TR66 SD1.wav",
    "hat": "TR66 CHH1.wav",
    "cymbal": "TR66 OHH1.wav",
    "claves": "TR66 P4.wav",
    "rim": "TR66 P9.wav",
    "cowbell": "TR66 P5.wav",
    "conga": "TR66 P3.wav",
    "high-bongo": "TR66 P2.wav",
}
# Primary resonances from the calibration table and reference spectra.
TONES = {
    "kick": (62.5, 36, 0.165),
    "snare": (273, 19, 0.110),
    "claves": (2357, 5, 0.042),
    "rim": (1480, 2, 0.018),
    "cowbell": (843, 15, 0.097),
    "conga": (200, 42, 0.205),
    "high-bongo": (600, 23, 0.108),
    "low-bongo": (398, 14, 0.076),
}
NOISE = {
    "snare": (0.122, 6500, 1.2, 11300, 1.0, 0.4, 22),
    "hat": (0.100, 7900, 10.0, 8600, 2.5, 0.2, 17),
    "cymbal": (0.405, 8030, 10.0, 8900, 2.0, 0.6, 78),
    "rim": (0.018, 7700, 3.5, 10000, 2.0, 0.15, 1.7),
}
HP = signal.butter(2, 25, "highpass", fs=RATE, output="sos")
NOISE_HP = signal.butter(3, 1300, "highpass", fs=RATE, output="sos")
RIM_HP = signal.butter(5, 3100, "highpass", fs=RATE, output="sos")
BANDS = np.array([1300, 2400, 3600, 5000, 6500, 7250, 7700, 8100,
                  8500, 9000, 10000, 12000, 14500, 17500, 21000])


class Tone(c.Structure):
    _fields_ = [(name, c.c_float) for name in ("hz", "decay_ms", "cosine", "sine")]


class Band(c.Structure):
    _fields_ = [(name, c.c_float) for name in ("hz", "q", "gain")]


class Patch(c.Structure):
    _fields_ = [("tone", Tone * TONAL_MODES), ("band", Band * 2),
                ("noise_attack_ms", c.c_float), ("noise_decay_ms", c.c_float),
                ("noise_highpass_hz", c.c_float), ("noise_fast_ms", c.c_float),
                ("noise_fast_mix", c.c_float), ("noise_threshold", c.c_float),
                ("noise_sweep_hz", c.c_float), ("noise_fast_hold_ms", c.c_float),
                ("noise_sweep_ms", c.c_float), ("noise_start_q", c.c_float),
                ("noise_damping_ms", c.c_float), ("noise_gate_curve", c.c_float)]


def read_baseline(path):
    by_name = json.loads(path.read_text())
    patches = []
    for name in NAMES:
        patch = by_name[name]
        tones = (Tone * TONAL_MODES)(*(Tone(*tone) for tone in patch["tone"]))
        bands = (Band * 2)(*(Band(*band) for band in patch["band"]))
        patches.append(Patch(tones, bands, *patch["noise"]))
    return patches


class Engine:
    def __init__(self, library):
        lib = c.CDLL(str(library.resolve()))
        self.patches = (Patch * len(NAMES)).in_dll(lib, "tr66_factory_voices")
        self.render_hit = lib.tr66_render_hit
        self.render_hit.argtypes = [
            c.POINTER(Patch), c.c_float, c.c_uint32,
            np.ctypeslib.ndpointer(dtype=np.float32, flags="C_CONTIGUOUS"), c.c_size_t,
        ]
        self.render_hit.restype = None

    def render(self, patch, length, seed=319):
        output = np.empty(length, dtype=np.float32)
        self.render_hit(c.byref(patch), RATE, seed, output, length)
        return output.astype(np.float64)


def rms(samples):
    return np.sqrt(np.mean(np.square(samples)))


def read(path, length):
    x, rate = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    if rate != RATE:
        divisor = math.gcd(rate, RATE)
        x = signal.resample_poly(x, RATE // divisor, rate // divisor)
    x -= np.median(x[-min(len(x), RATE // 20):])
    # Preserve the onset in already-trimmed hits. Remove obvious leading silence.
    detected = np.flatnonzero(np.abs(x) > np.max(np.abs(x)) * 0.018)
    if detected.size:
        x = x[max(0, detected[0] - RATE // 3000):]
    return np.pad(x[:length], (0, max(0, length - len(x))))


def windowed(samples, max_lag_ms=4):
    width = round(RATE * max_lag_ms / 1000)
    return np.pad(samples, (width, width)), width


def align(source, target):
    padded, width = windowed(source)
    correlation = signal.correlate(padded, target, mode="valid", method="fft")
    offset = int(np.argmax(np.abs(correlation)))
    return padded[offset:offset + len(target)] * np.sign(correlation[offset]), offset - width


def similarity(left, right, band=None):
    if band is not None:
        left, right = (signal.sosfiltfilt(band, x) for x in (left, right))
    aligned, _ = align(left, right)
    return float(np.dot(aligned, right) / max(np.linalg.norm(aligned) * np.linalg.norm(right), 1e-20))


def fit_tone(engine, name, reference):
    hz, tau, seconds = TONES[name]
    length = round(seconds * RATE)
    reference = reference[:length]
    focus = signal.butter(2, (max(25, 0.45 * hz), 2.1 * hz),
                          "bandpass", fs=RATE, output="sos")
    y = signal.sosfiltfilt(focus, reference)
    t = np.arange(length) / RATE
    mode = np.exp(-1000 * t / tau)
    coefficients = np.linalg.lstsq(
        np.stack((mode * np.cos(2 * np.pi * hz * t), mode * np.sin(2 * np.pi * hz * t)), axis=1),
        y, rcond=None,
    )[0]
    patch = Patch()

    def render(params):
        patch.tone[0] = Tone(*params)
        return signal.sosfiltfilt(focus, engine.render(patch, length))

    # Frequencies and decay are fitted within the neighborhood of each circuit
    # resonance. The C recurrence also determines the finite-precision result.
    initial = np.r_[hz, tau, coefficients]
    bounds = ([0.91 * hz, 0.22 * tau, -2, -2], [1.09 * hz, 3.0 * tau, 2, 2])
    fit = optimize.least_squares(
        lambda p: (render(p) - y)[::3], initial, bounds=bounds,
        diff_step=0.001, x_scale=[hz, tau, 0.4, 0.4], max_nfev=100,
    )

    # A short, decaying transient models the trigger coupling capacitor.
    # Frequency zero is a real pole and costs the same as another resonator.
    primary = fit.x.copy()
    transient_bounds = ([*bounds[0], 0.09, -1.6], [*bounds[1], 17.0, 1.6])
    target = signal.sosfiltfilt(HP, reference)
    if name in ("snare", "rim"):
        low = signal.butter(2, 2.1 * hz, "lowpass", fs=RATE, output="sos")
        target = signal.sosfiltfilt(low, target)
    else:
        low = None

    def residual(params):
        patch.tone[0] = Tone(*params[:4])
        patch.tone[1] = Tone(0.0, params[4], params[5], 0.0)
        x = signal.sosfiltfilt(HP, engine.render(patch, length))
        if low is not None:
            x = signal.sosfiltfilt(low, x)
        return (x - target)[::3]

    second = optimize.least_squares(
        residual, np.r_[primary, 1.5, 0.012], bounds=transient_bounds,
        diff_step=0.001, x_scale=[hz, tau, 0.4, 0.4, 2, 0.15], max_nfev=110,
    )
    residual(second.x)
    return patch


def noise_features(samples, seconds):
    nfft = fft.next_fast_len(samples.shape[1])
    power = np.abs(fft.rfft(samples, n=nfft, axis=1)) ** 2
    power = np.mean(power, axis=0)
    edges = np.searchsorted(fft.rfftfreq(nfft, 1.0 / RATE), BANDS)
    band_energy = np.array([np.sum(power[a:b]) for a, b in zip(edges[:-1], edges[1:])])
    energy = np.cumsum(np.mean(samples * samples, axis=0))
    positions = np.interp((0.20, 0.40, 0.65, 0.82, 0.94), energy / energy[-1],
                          np.arange(len(energy)) / RATE)
    return np.r_[10.0 * np.log10(np.maximum(1e-6, band_energy / band_energy.sum())),
                 26.0 * positions / seconds]


def fit_noise(engine, name, patch, reference):
    seconds, center1, q1, center2, q2, attack, decay = NOISE[name]
    n = round(seconds * RATE)
    focus = RIM_HP if name == "rim" else NOISE_HP
    target = signal.sosfiltfilt(focus, reference[:n] - engine.render(patch, n))
    band_start = 2 if name == "rim" else 0
    noise = Patch()
    target_features = noise_features(target[np.newaxis, :], seconds)[band_start:]
    sharp_treble = name in ("hat", "cymbal")

    def make_noise(params, seed_list=SEEDS[:4]):
        f1, q1, f2, q2, ratio, attack, decay = params[:7]
        noise.band[0] = Band(f1, q1, 1.0)
        noise.band[1] = Band(f2, q2, ratio)
        noise.noise_attack_ms = attack
        noise.noise_decay_ms = decay
        if sharp_treble:
            noise.noise_highpass_hz = params[7]
        if name == "cymbal":
            noise.noise_fast_ms, noise.noise_fast_mix = params[8:]
        generated = np.stack([engine.render(noise, n, seed) for seed in seed_list])
        return signal.sosfiltfilt(focus, generated, axis=-1)

    def loss(params):
        difference = noise_features(make_noise(params), seconds)[band_start:] - target_features
        return np.mean(difference ** 2)

    limits = [(4300, 12500), (0.38, 22), (3200, 16300), (0.38, 12),
              (0.02, 2.5), (0.035, min(3.0, decay * 0.3)), (0.50 * decay, 2.3 * decay)]
    initial = [center1, q1, center2, q2, 0.28, attack, decay]
    if sharp_treble:
        limits.append((4200, 9700))
        initial.append(6900)
    if name == "cymbal":
        limits.extend(((3, 38), (0.0, 0.96)))
        initial.extend((12, 0.68))
    result = optimize.differential_evolution(
        loss, limits, x0=initial, seed=1059, popsize=8, maxiter=45, polish=True, tol=0.003,
    )
    samples = make_noise(result.x, SEEDS)
    level = rms(target) / rms(samples)
    for i, band in enumerate(noise.band):
        patch.band[i] = Band(band.hz, band.q, band.gain * level)
    for field, _ in Patch._fields_[2:]:
        setattr(patch, field, getattr(noise, field))


def evaluate(engine, name, patch, reference):
    if name in NOISE:
        seconds = NOISE[name][0]
        n = round(seconds * RATE)
        # Evaluation seeds are separate from those used to fit the filters.
        rendered = np.stack([engine.render(patch, n, s) for s in (28979, 37061, 51461, 55351)])
        reference = reference[:n]
        dry = signal.sosfiltfilt(NOISE_HP, reference)
        modeled = signal.sosfiltfilt(NOISE_HP, rendered, axis=-1)
        d = noise_features(dry[np.newaxis, :], seconds) - noise_features(modeled, seconds)
        return float(rms(d[:len(BANDS) - 1]))
    n = round(TONES[name][2] * RATE)
    x, y = (signal.sosfiltfilt(HP, s) for s in (engine.render(patch, n), reference[:n]))
    return similarity(x, y)


def fmt(value):
    return f"{value:.8f}f"


def emit_patch(name, patch):
    fields = []
    tones = [tone for tone in patch.tone if tone.decay_ms > 0]
    bands = [band for band in patch.band if band.gain != 0]
    if tones:
        fields.append(".tone = {" + ", ".join(
            "{" + ", ".join(fmt(getattr(t, k)) for k, _ in Tone._fields_) + "}" for t in tones
        ) + "}")
    if bands:
        fields.append(".band = {" + ", ".join(
            "{" + ", ".join(fmt(getattr(b, k)) for k, _ in Band._fields_) + "}" for b in bands
        ) + "}")
        fields.append(f".noise_attack_ms = {fmt(patch.noise_attack_ms)}")
        fields.append(f".noise_decay_ms = {fmt(patch.noise_decay_ms)}")
        for field, _ in Patch._fields_[4:]:
            value = getattr(patch, field)
            if value != 0:
                fields.append(f".{field} = {fmt(value)}")
    index = "TR66_" + name.upper().replace("-", "_")
    return f"    [{index}] = {{ " + ",\n        ".join(fields) + " },"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, default=ROOT / "work/build/libtr66.so")
    parser.add_argument("--baseline", type=Path, default=ROOT / "tools/baseline_patches.json")
    parser.add_argument("--references", type=Path, default=ROOT / "work/references/drum-abuse")
    parser.add_argument("--drumbase", type=Path,
                        help="Fit against the Drumbase TR-66 #2 directory where available")
    parser.add_argument("--output", type=Path, required=True, help="Destination for the fitted C table")
    parser.add_argument("--report", type=Path, required=True, help="Destination for the JSON report")
    parser.add_argument("--audition", type=Path, help="Optional level-matched reference/model WAV")
    args = parser.parse_args()

    engine = Engine(args.library)
    baseline = read_baseline(args.baseline)
    audition = []
    patches = []
    report = {}
    for i, name in enumerate(NAMES):
        first, *others = FILES[name]
        using_drumbase = args.drumbase is not None and name in DRUMBASE_FILES
        fit_file = args.references / "4" / first
        assignment = "provisional" if name in ("cymbal", "cowbell") else "working"
        if using_drumbase:
            fit_file = args.drumbase / DRUMBASE_FILES[name]
            assignment = "circuit-matched"
        reference = read(fit_file, RATE // 2)
        patch = fit_tone(engine, name, reference) if name in TONES else Patch()
        if name in NOISE:
            fit_noise(engine, name, patch, reference)
        patches.append(emit_patch(name, patch))
        train = evaluate(engine, name, patch, reference)
        comparisons = []
        for filename in others:
            second = read(args.references / "1" / filename, RATE // 2)
            duplicate = similarity(reference, second, NOISE_HP if name in NOISE else HP)
            comparisons.append({
                "file": f"1/{filename}",
                "reference_correlation": round(duplicate, 4),
                "baseline": round(evaluate(engine, name, baseline[i], second), 3),
                "fitted": round(evaluate(engine, name, patch, second), 3),
            })
        report[name] = {
            "fit_file": str(fit_file),
            "assignment": assignment,
            "measure": "RMS band-energy error in dB" if name in NOISE else "aligned waveform correlation",
            "baseline_fit_file": round(evaluate(engine, name, baseline[i], reference), 3),
            "fitted_fit_file": round(train, 3),
            "comparison_hits": comparisons,
        }
        print(name, json.dumps(report[name]), flush=True)
        if args.audition:
            fitted = engine.render(patch, len(reference), seed=67751)
            # Equal RMS per A/B pair for this isolated-voice audition only.
            # Absolute instrument levels remain intact in the firmware table.
            reference *= rms(fitted) / max(rms(reference), 1e-12)
            for samples in (reference, fitted):
                audition.extend((samples, np.zeros(RATE // 7)))

    args.output.write_text(
        "/* Circuit-model parameters generated by tools/calibrate.py.\n"
        " * Hi-hat also covers the maracas trigger in Roland's scores. */\n"
        + "\n".join(patches) + "\n"
    )
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    if audition:
        combined = np.concatenate(audition)
        combined *= 0.9 / np.max(np.abs(combined))
        sf.write(args.audition, combined, RATE, subtype="PCM_24")


if __name__ == "__main__":
    main()
