#!/usr/bin/env python3
"""
Bandwidth-extend Quake III sounds to 48 kHz with AudioSR, and add bass.

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

Bass (no ML): thin sounds get a subharmonic one octave below their lowest
strong band, like a hardware bass enhancer: that band is band-passed, its
frequency halved with a zero-crossing flip-flop, and the result shaped by
the band's own envelope, so it follows the sound and stays silent when the
sound is. How much depends on the kind of sound (see bass_amount): most
for footsteps, some for weapons and impacts, none for voices and clicks,
and none for sounds that already have plenty of sub-bass.

The output is a pk3 to drop into baseq3 (it overrides the originals) plus a
CSV report. Processed id Software sounds are for personal use only. The
AudioSR result for each sound is cached in the work directory, so an
interrupted run resumes and the bass can be retuned without the GPU.

Needs a Python environment with torch and audiosr (see tools/audio/README.md).

usage: bandwidth_extend.py <input dir of sound/...wav> <output.pk3> [--work dir] [--no-sr] [--no-bass] [--device cpu]
"""

import argparse
import csv
import json
import os
import re
import sys
import time
import wave
import zipfile

import numpy as np
from scipy import signal
from scipy.ndimage import minimum_filter1d

OUT_RATE = 48000
CHUNK = int(5.12 * OUT_RATE)    # AudioSR works in 5.12 s windows
OVERLAP = int(0.5 * OUT_RATE)   # crossfade between windows of long sounds


def read_wav(path):
    with wave.open(path) as w:
        rate, ch, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    elif width == 1:
        x = ( np.frombuffer(raw, "u1").astype(np.float64) - 128.0 ) / 128.0
    else:
        raise ValueError(f"{path}: only 8- and 16-bit PCM supported")
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    read_wav.bits = 8 * width
    return x, rate


def write_wav(path, x, rate):
    pcm = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(pcm.tobytes())


def cutoff_frequency(x, rate, bits=16):
    """Upper edge of the recorded band: the highest frequency whose level is
    clearly above the noise floor. (An energy percentile misjudges sounds
    whose energy is mostly bass.)"""
    f, p = signal.welch(x, rate, nperseg=min(2048, len(x)))
    # average power in 250 Hz bands, so stray bins don't count
    edges = np.arange(0, 0.5 * rate + 250, 250)
    bands = np.array([p[(f >= a) & (f < a + 250)].mean() if np.any((f >= a) & (f < a + 250)) else 0.0
                      for a in edges[:-1]])
    db = 10 * np.log10(bands + 1e-20)
    # quantisation noise density, the floor of anything recorded
    floor = 10 * np.log10((1.0 / 2 ** (bits - 1)) ** 2 / 12 / (0.5 * rate))
    threshold = max(floor + 20.0, db.max() - 90.0)
    above = np.nonzero(db > threshold)[0]
    edge = float(edges[above[-1] + 1]) if len(above) else 0.5 * rate
    return float(np.clip(edge, 3000.0, 0.97 * 0.5 * rate))


def zero_phase(x, kind, freq, rate, order=8):
    sos = signal.butter(order, freq, btype=kind, fs=rate, output="sos")
    return signal.sosfiltfilt(sos, x)


def envelope(x, rate, ms=5.0):
    n = max(1, int(rate * ms / 1000))
    return np.sqrt(np.convolve(x * x, np.ones(n) / n, mode="same"))


# ---------------------------------------------------------------------------
# bass

def bass_amount(rel):
    """Subharmonic level relative to the whole sound (dB), or None for none."""
    r = rel.lower()
    if r.startswith("sound/player/footsteps/"):
        return -8.0
    if r.startswith("sound/player/") and r.count("/") == 2:
        # land1, gibs, fry: impacts; talk, gurp, water: leave alone
        return -10.0 if re.search(r"/(land|gib|fry)", r) else None
    if r.startswith(("sound/player/", "sound/feedback/", "sound/teamplay/voc_", "sound/misc/menu")):
        return None     # voices, announcer, UI
    if r.startswith("sound/weapons/"):
        return None if re.search(r"/(change|noammo)\.wav$", r) else -10.0
    return -14.0        # world, movers, items, other


# source bands for the subharmonic: the result lands an octave lower, so
# starting at 80 Hz keeps it at 40 Hz and up, where headphones still play
OCTAVES = ((80, 160), (160, 320), (320, 640), (640, 1280))


def add_bass(x, rate, level_db):
    """Adds a subharmonic an octave below the lowest strong band.
    Returns (signal, added dB relative to the whole sound, source band or "")."""
    if level_db is None or len(x) < rate // 20:
        return x, None, ""
    f, p = signal.welch(x, rate, nperseg=min(8192, len(x)))
    total = p.sum()
    if total <= 0:
        return x, None, ""
    if p[f < 60].sum() / total >= 0.20:
        return x, None, "has deep bass"
    energy = [p[(f >= lo) & (f < hi)].sum() for lo, hi in OCTAVES]
    strongest = max(energy)
    if strongest <= 0:
        return x, None, ""
    lo, hi = next(b for b, e in zip(OCTAVES, energy) if e >= strongest * 10 ** (-20 / 10))
    if lo >= 320:
        lo, hi = 160, 320               # no low end at all: work from 160-320 Hz

    band = zero_phase(x, "bandpass", (lo, hi), rate, order=4)
    env = envelope(band, rate, ms=15.0)
    # halve the frequency: flip at every upward zero crossing
    up = np.zeros(len(band))
    up[1:] = (band[:-1] < 0) & (band[1:] >= 0)
    square = np.where(np.cumsum(up) % 2 == 0, 1.0, -1.0)
    sub = zero_phase(square * env, "bandpass", (lo / 2, hi / 2), rate, order=4)
    # set the amount relative to the whole sound
    ratio = np.sqrt(np.sum(x ** 2) / max(np.sum(sub ** 2), 1e-20))
    sub *= ratio * 10 ** (level_db / 20)
    sub = zero_phase(sub, "highpass", 20.0, rate, order=2)
    # make room: turn the whole sound down by up to 2 dB if the bass needs it,
    # then turn only the bass down wherever it would still clip
    peak = np.max(np.abs(x + sub))
    if peak > 0.98:
        scale = max(0.98 / peak, 10 ** (-2 / 20))
        x = x * scale
        sub = sub * scale
    headroom = np.maximum(0.98 - np.abs(x), 0.0)
    g = np.where(np.abs(x + sub) > 0.98, headroom / (np.abs(sub) + 1e-9), 1.0)
    g = np.clip(g, 0.0, 1.0)
    w = max(1, int(rate * 0.01))
    g = minimum_filter1d(g, 2 * w + 1, mode="nearest")
    g = np.convolve(np.pad(g, w, mode="edge"), np.ones(w) / w, mode="same")[w:-w]
    sub *= g
    out = x + sub
    added = 10 * np.log10(max(np.mean(sub ** 2), 1e-20) / max(np.mean(x ** 2), 1e-20))
    return out, added, "%d-%d Hz" % (lo, hi)


def limit_peaks(x, rate, ceiling=0.98, ms=10.0):
    """Look-ahead peak limiter: turns down only around peaks above the
    ceiling, so the sound keeps its loudness. Many retail sounds are clipped
    at full scale; resampling and the new top end overshoot that by up to
    9 dB. Returns (signal, deepest gain reduction in dB)."""
    need = np.minimum(1.0, ceiling / np.maximum(np.abs(x), 1e-9))
    if need.min() >= 1.0:
        return x, 0.0
    w = max(1, int(rate * ms / 1000))
    # a box average inside a window twice its size never exceeds the window's minimum
    g = minimum_filter1d(need, 2 * w + 1, mode="nearest")
    g = np.convolve(np.pad(g, w, mode="edge"), np.ones(w) / w, mode="same")[w:-w]
    return x * g, 20 * np.log10(g.min())


# ---------------------------------------------------------------------------
# AudioSR

def combine(orig48, sr_out, rate, cutoff):
    """Original below the crossover, AudioSR above it."""
    n = len(orig48)
    sr_out = sr_out[:n]
    if len(sr_out) < n:
        sr_out = np.pad(sr_out, (0, n - len(sr_out)))
    xover = 0.92 * cutoff

    # alignment: where every sound has energy
    oa = zero_phase(orig48, "bandpass", (200, min(4000, 0.9 * xover)), rate, order=4)
    sa = zero_phase(sr_out, "bandpass", (200, min(4000, 0.9 * xover)), rate, order=4)
    corr = float(np.dot(oa, sa) / np.sqrt(max(np.dot(oa, oa) * np.dot(sa, sa), 1e-30)))

    # level: the highest octave below the crossover that has real content
    total = float(np.dot(orig48, orig48))
    hi = 0.9 * xover
    while True:
        band = (hi / 2, hi)
        ob = zero_phase(orig48, "bandpass", band, rate, order=4)
        if np.dot(ob, ob) > total * 1e-5 or hi / 2 <= 500:
            break
        hi /= 2
    sb = zero_phase(sr_out, "bandpass", band, rate, order=4)
    denom = float(np.dot(sb, sb))
    gain = float(np.dot(ob, sb)) / denom if denom > 0 else 0.0

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
    added_db = 10 * np.log10(max(np.mean(high ** 2), 1e-20) / max(np.mean(orig48 ** 2), 1e-20))
    return out, gain, corr, added_db


def run_audiosr(audiosr, model, x, tmp, args):
    """AudioSR over x (48 kHz) in single 5.12 s windows that overlap by
    OVERLAP and are crossfaded. AudioSR's cost grows much faster than the
    length (attention over the whole clip), and on an integrated GPU one
    huge job starves the desktop's graphics queue, so long sounds are never
    given to it whole. Each window is normalised for the model and scaled
    back, so levels line up across windows."""
    hop = CHUNK - OVERLAP
    starts = [0] if len(x) <= CHUNK else list(range(0, len(x) - OVERLAP, hop))
    out = np.zeros(starts[-1] + CHUNK)
    fade = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, OVERLAP))
    for k, start in enumerate(starts):
        seg = x[start:start + CHUNK]
        # hand AudioSR a 48 kHz input of exactly one window: its own
        # resampling and padding are off by a frame for some lengths
        seg = np.pad(seg, (0, CHUNK - len(seg)))
        peak = np.max(np.abs(seg))
        if peak < 1e-5:
            continue                    # silence stays silence
        write_wav(tmp, seg * (0.5 / peak), OUT_RATE)
        try:
            sr = audiosr.super_resolution(model, tmp, seed=args.seed, ddim_steps=args.steps,
                                          guidance_scale=args.guidance)
            sr = np.asarray(sr, dtype=np.float64).reshape(-1)[:CHUNK] * (peak / 0.5)
            sr = np.pad(sr, (0, CHUNK - len(sr)))
        except Exception:               # noqa: BLE001
            if len(starts) == 1:
                raise
            # AudioSR rejects windows with almost nothing above a few hundred
            # Hz; keep the original there and extend the rest
            sr = seg.copy()
        if k > 0:
            sr[:OVERLAP] *= fade
        if k < len(starts) - 1:
            sr[-OVERLAP:] *= fade[::-1]
        out[start:start + CHUNK] += sr
    return out[:len(x)]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("indir")
    ap.add_argument("pk3")
    ap.add_argument("--work", help="cache directory for per-sound results (resumes an interrupted run)")
    ap.add_argument("--no-sr", action="store_true", help="don't run AudioSR: use results cached by earlier runs, plain 48 kHz for the rest")
    ap.add_argument("--no-bass", action="store_true", help="skip bass extension")
    ap.add_argument("--steps", type=int, default=50, help="DDIM steps (AudioSR default 200)")
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--guidance", type=float, default=3.5)
    ap.add_argument("--device", default="auto",
                    help="auto, cpu or cuda (ROCm GPUs appear as cuda). On an integrated GPU, cpu keeps "
                         "the desktop responsive: long GPU jobs can starve the compositor")
    args = ap.parse_args()

    files = []
    for root, _, names in os.walk(args.indir):
        for name in names:
            if name.lower().endswith(".wav"):
                files.append(os.path.relpath(os.path.join(root, name), args.indir).replace(os.sep, "/"))
    files.sort()
    if not files:
        sys.exit("no .wav files found")
    work = args.work or os.path.splitext(args.pk3)[0] + "-work"
    os.makedirs(work, exist_ok=True)

    model = None
    if not args.no_sr:
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

    start = time.time()
    tmp = os.path.join(work, "_input.wav")
    srdir = os.path.join(work, "sr")

    # pass 1: top end (slow, cached per sound); --no-sr skips it and uses
    # whatever an earlier run cached
    for i, rel in enumerate([] if args.no_sr else files):
        cached = os.path.join(srdir, rel + ".npy")
        cached_row = os.path.join(srdir, rel + ".json")
        if os.path.exists(cached) and os.path.exists(cached_row):
            continue
        t0 = time.time()
        try:
            x, rate = read_wav(os.path.join(args.indir, rel))
        except Exception as e:      # noqa: BLE001
            print("[%d/%d] %s  SKIPPED: %s" % (i + 1, len(files), rel, e), flush=True)
            continue
        orig48 = signal.resample_poly(x, OUT_RATE, rate)
        cutoff = cutoff_frequency(x, rate, read_wav.bits)
        row = dict(file=rel, seconds="%.2f" % (len(x) / rate), cutoff_hz="%.0f" % cutoff,
                   match_corr="", sr_added_db="", note="")
        out = orig48
        if not args.no_sr:
            if model is None:
                model = audiosr.build_model(model_name="basic", device=args.device)
            try:
                sr = run_audiosr(audiosr, model, orig48, tmp, args)
                result, gain, corr, added = combine(orig48, sr, OUT_RATE, cutoff)
                row["match_corr"] = "%.3f" % corr
                if corr < 0.9:
                    row["note"] = "AudioSR output didn't line up (corr %.2f), top end not extended" % corr
                else:
                    out = result
                    row["sr_added_db"] = "%.1f" % added
            except Exception as e:      # noqa: BLE001
                row["note"] = "AudioSR failed, top end not extended: %s" % str(e)[:120]
        os.makedirs(os.path.dirname(cached), exist_ok=True)
        np.save(cached, out.astype(np.float32))    # unclipped
        json.dump(row, open(cached_row, "w"))
        print("[%d/%d] %s  cutoff %.0f Hz  top %s dB %s (%.1f s)"
              % (i + 1, len(files), rel, cutoff, row["sr_added_db"] or "-", row["note"], time.time() - t0),
              flush=True)

    # pass 2: bass and levels (fast), written straight into the pk3
    report = []
    with zipfile.ZipFile(args.pk3 + ".tmp", "w", zipfile.ZIP_DEFLATED) as z:
        for rel in files:
            cached = os.path.join(srdir, rel)
            if os.path.exists(cached + ".json") and os.path.exists(cached + ".npy"):
                row = dict(json.load(open(cached + ".json")), bass_band="", bass_added_db="", peak_limited_db="")
                out = np.load(cached + ".npy").astype(np.float64)
            else:
                # no AudioSR result (--no-sr, or it failed to read): original, resampled
                try:
                    x, rate = read_wav(os.path.join(args.indir, rel))
                except Exception as e:      # noqa: BLE001
                    print("%s  SKIPPED: %s" % (rel, e), flush=True)
                    continue
                out = signal.resample_poly(x, OUT_RATE, rate)
                row = dict(file=rel, seconds="%.2f" % (len(x) / rate), cutoff_hz="", match_corr="",
                           sr_added_db="", note="top end not extended (no AudioSR result)",
                           bass_band="", bass_added_db="", peak_limited_db="")
            if not args.no_bass:
                out, bass, band = add_bass(out, OUT_RATE, bass_amount(rel))
                row["bass_band"] = band
                if bass is not None:
                    row["bass_added_db"] = "%.1f" % bass
            out, reduction = limit_peaks(out, OUT_RATE)
            row["peak_limited_db"] = "%.1f" % reduction if reduction < -0.05 else ""
            write_wav(tmp, out, OUT_RATE)
            z.write(tmp, rel)
            report.append(row)
    if os.path.exists(tmp):
        os.remove(tmp)
    os.replace(args.pk3 + ".tmp", args.pk3)
    with open(os.path.splitext(args.pk3)[0] + ".csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(report[0]))
        w.writeheader()
        w.writerows(report)
    print("wrote %s (%d sounds) in %.0f s" % (args.pk3, len(report), time.time() - start), flush=True)


if __name__ == "__main__":
    main()
