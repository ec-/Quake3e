#!/usr/bin/env python3
"""
Bandwidth-extend Quake III sounds to 48 kHz with AudioSR.

The retail sounds are 22.05 kHz, so nothing above ~11 kHz exists, and much of
the HRTF height cue lives at 6-16 kHz. AudioSR (MIT, Liu et al. 2023,
https://github.com/haoheliu/versatile_audio_super_resolution) is a latent
diffusion model that predicts the missing high band.

AudioSR regenerates the whole spectrum, which can change a sound's character,
so only its new high band is used:

  result = original (resampled to 48 kHz, unchanged below the crossover)
         + AudioSR output above the crossover (just under the original's
           cutoff), level-matched to the original over the overlap band,
           and gated by the original's own high-frequency envelope so the
           model cannot add hiss where the original is silent.

The output is a pk3 to drop into baseq3 (it overrides the originals) plus a
CSV report. Processed id Software sounds are for personal use only.

Needs a Python environment with torch and audiosr (see tools/audio/README.md).

usage: bandwidth_extend.py <input dir of sound/...wav> <output.pk3> [--steps 50] [--seed 42]
"""

import argparse
import csv
import os
import sys
import time
import wave
import zipfile

import numpy as np
from scipy import signal

OUT_RATE = 48000
CHUNK = int(5.12 * OUT_RATE)    # AudioSR works in 5.12 s windows


def read_wav(path):
    with wave.open(path) as w:
        rate, ch, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if width != 2:
        raise ValueError(f"{path}: only 16-bit PCM supported")
    x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, rate


def write_wav(path, x, rate):
    pcm = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def cutoff_frequency(x, rate):
    """Highest frequency holding real content: where cumulative spectral energy reaches 99.9%."""
    f, p = signal.welch(x, rate, nperseg=min(2048, len(x)))
    c = np.cumsum(p) / max(p.sum(), 1e-20)
    return float(f[min(np.searchsorted(c, 0.999), len(f) - 1)])


def zero_phase(x, kind, freq, rate, order=8):
    sos = signal.butter(order, freq, btype=kind, fs=rate, output="sos")
    return signal.sosfiltfilt(sos, x)


def envelope(x, rate, ms=5.0):
    n = max(1, int(rate * ms / 1000))
    return np.sqrt(np.convolve(x * x, np.ones(n) / n, mode="same"))


def combine(orig48, sr_out, rate, cutoff):
    """Original below the crossover, AudioSR above it."""
    n = len(orig48)
    sr_out = sr_out[:n]
    if len(sr_out) < n:
        sr_out = np.pad(sr_out, (0, n - len(sr_out)))
    xover = 0.92 * cutoff

    # level and polarity from the band both signals share
    band = (0.45 * cutoff, 0.9 * cutoff)
    ob = zero_phase(orig48, "bandpass", band, rate, order=4)
    sb = zero_phase(sr_out, "bandpass", band, rate, order=4)
    denom = float(np.dot(sb, sb))
    gain = float(np.dot(ob, sb)) / denom if denom > 0 else 0.0
    corr = float(np.dot(ob, sb) / np.sqrt(max(np.dot(ob, ob) * denom, 1e-30)))

    low = zero_phase(orig48, "lowpass", xover, rate)
    high = gain * (sr_out - zero_phase(sr_out, "lowpass", xover, rate))

    # gate: the new band may not be louder than the original's top octave,
    # so silence stays silent and attacks don't smear
    ref = envelope(zero_phase(orig48, "bandpass", (0.5 * cutoff, xover), rate, order=4), rate)
    env = envelope(high, rate)
    limit = np.minimum(1.0, ref / np.maximum(env, 1e-9))
    limit = np.convolve(limit, np.ones(int(rate * 0.002)) / int(rate * 0.002), mode="same")
    high *= limit

    out = low + high
    peak = np.max(np.abs(out))
    if peak > 0.999:
        out *= 0.999 / peak
    added_db = 10 * np.log10(max(np.mean(high ** 2), 1e-20) / max(np.mean(orig48 ** 2), 1e-20))
    return out, gain, corr, added_db


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("indir")
    ap.add_argument("pk3")
    ap.add_argument("--steps", type=int, default=50, help="DDIM steps (AudioSR default 200)")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--guidance", type=float, default=3.5)
    ap.add_argument("--keep", help="also write the raw AudioSR outputs and results to this directory")
    args = ap.parse_args()

    # AudioSR pins librosa 0.9.2, which still uses aliases removed from NumPy
    for alias, real in (("float", float), ("int", int), ("complex", complex), ("bool", bool)):
        if not hasattr(np, alias):
            setattr(np, alias, real)

    import torch
    import audiosr
    import soundfile

    # torchaudio >= 2.9 needs torchcodec for load(); AudioSR only reads plain WAVs
    def _load(path, *a, **k):
        data, rate = soundfile.read(path, dtype="float32", always_2d=True)
        return torch.from_numpy(data.T.copy()), rate
    audiosr.utils.torchaudio.load = _load

    files = []
    for root, _, names in os.walk(args.indir):
        for name in names:
            if name.lower().endswith(".wav"):
                files.append(os.path.relpath(os.path.join(root, name), args.indir).replace(os.sep, "/"))
    files.sort()
    if not files:
        sys.exit("no .wav files found")

    model = audiosr.build_model(model_name="basic", device="auto")
    report = []
    start = time.time()
    with zipfile.ZipFile(args.pk3 + ".tmp", "w", zipfile.ZIP_DEFLATED) as z:
        for i, rel in enumerate(files):
            src = os.path.join(args.indir, rel)
            x, rate = read_wav(src)
            orig48 = signal.resample_poly(x, OUT_RATE, rate)
            cutoff = min(cutoff_frequency(x, rate), 0.5 * rate * 0.97)
            t0 = time.time()
            # hand AudioSR a 48 kHz input padded to whole windows: its own
            # resampling and padding are off by a frame for some lengths
            tmp = args.pk3 + ".wav"
            padded = np.pad(orig48, (0, -len(orig48) % CHUNK))
            write_wav(tmp, padded * (0.5 / max(np.max(np.abs(padded)), 1e-9)), OUT_RATE)
            try:
                sr = audiosr.super_resolution(model, tmp, seed=args.seed, ddim_steps=args.steps,
                                              guidance_scale=args.guidance)
                sr = np.asarray(sr, dtype=np.float64).reshape(-1)
                out, gain, corr, added = combine(orig48, sr, OUT_RATE, cutoff)
                if corr < 0.9:
                    raise RuntimeError("AudioSR output doesn't line up with the original (corr %.2f)" % corr)
            except Exception as e:
                print("[%d/%d] %s  FAILED, kept the original: %s" % (i + 1, len(files), rel, e), flush=True)
                report.append(dict(file=rel, seconds="%.2f" % (len(x) / rate), cutoff_hz="%.0f" % cutoff,
                                   match_corr="", gain="", added_db="failed"))
                continue
            write_wav(tmp, out, OUT_RATE)
            z.write(tmp, rel)
            if args.keep:
                for sub, data in (("audiosr", sr), ("result", out), ("original", orig48)):
                    p = os.path.join(args.keep, sub, rel)
                    os.makedirs(os.path.dirname(p), exist_ok=True)
                    write_wav(p, data[:len(orig48)] if sub == "audiosr" else data, OUT_RATE)
            report.append(dict(file=rel, seconds="%.2f" % (len(x) / rate), cutoff_hz="%.0f" % cutoff,
                               match_corr="%.3f" % corr, gain="%.3f" % gain, added_db="%.1f" % added))
            print("[%d/%d] %s  cutoff %.0f Hz  corr %.2f  added %.1f dB  (%.1f s)"
                  % (i + 1, len(files), rel, cutoff, corr, added, time.time() - t0), flush=True)
    os.remove(args.pk3 + ".wav")
    os.replace(args.pk3 + ".tmp", args.pk3)
    with open(os.path.splitext(args.pk3)[0] + ".csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(report[0]))
        w.writeheader()
        w.writerows(report)
    print("wrote %s (%d sounds) in %.0f s" % (args.pk3, len(files), time.time() - start))


if __name__ == "__main__":
    main()
