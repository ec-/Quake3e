#!/usr/bin/env python3
"""
Audio test harness for Quake3e (audio track, stage 1).

Runs the engine headless and silent (SDL offscreen video, SDL dummy audio)
and checks:

  positional  s_spatialTest renders a sound at fixed positions around the
              listener through the spatial backend. Checks left/right
              behaviour, reports whether height is audible, and compares the
              WAV against a golden hash.
  occlusion   Loads q3dm1 and renders a sound with occlusion off and on, for a
              source in the open, one under the floor and one across the map
              (positions derived from the map's spawn points). Checks the
              occlusion amount, that clear sounds are untouched, that
              blocked ones are quieter and muffled, and that several walls
              block more (lows included) than one thin floor.
  reflection  With Steam Audio, loads q3dm1 and renders a footstep in two rooms
              as direct sound, direct plus reflections, and reflections only
              ("spatial-refl" variant). Checks the reverb level, decay time,
              that the tail is diffuse (ears decorrelated above 1 kHz) and that
              a second run measures the same. Steam Audio's ray tracing is
              randomised, so this compares measurements, not hashes; the other
              HRTF variants run with s_reflections 0 to stay bit-exact.
  demo        Plays a demo with video capture (fixed frame time, so audio is
              deterministic) through each backend and extracts the audio.
              Checks run-to-run determinism, golden hashes, and that the
              spatial backend matches the classic one in loudness and timing.

Golden hashes depend on the compiler and CPU (floating point), so they are
per environment: regenerate with --update-golden after an intended change.
The determinism and A/B checks are portable.

With --steamaudio (or STEAMAUDIO_LIB) pointing at libphonon.so, the
spatial backend is also tested with HRTF ("spatial-hrtf"), and height must
then be audible. Adding --hrtf-file (or HRTF_FILE) with a full-sphere SOFA
file (e.g. SADIE II D1_48K_24bit_256tap_FIR_SOFA.sofa) adds "spatial-sofa",
where directly below must also differ from 45 degrees below.

Needs: python3, numpy, retail Quake 3 data (pak0.pk3 ... pak8.pk3).

Example:
  tests/audio/run_audio_tests.py --basepath ~/Downloads/q3a
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
GOLDEN = os.path.join(HERE, "golden.json")

POSITIONAL_SOUND = "sound/player/footsteps/boot1.wav"
DEMO = "four"
OCCLUSION_MAP = "q3dm1"
REFL_VARIANT = "spatial-refl"
REFL_SPAWNS = (0, 3)                 # q3dm1 spawn points with open space 128 units ahead
REFL_LEVEL_DB = (-20.0, 3.0)         # reflections relative to the direct sound, default s_reflectionGain
REFL_T60 = (0.2, 3.0)                # seconds
REFL_MAX_HF_CORR = 0.75              # ear correlation of the tail above 1 kHz (diffuse field)
REFL_REPEAT_DB = 1.5                 # run-to-run tolerance (ray tracing is randomised)
REFL_REPEAT_T60 = 0.2                # relative
AB_REFL_MIN_ENVELOPE_CORR = 0.75
AB_REFL_MAX_LEVEL_DIFF_DB = 3.0
EYE_HEIGHT = 26

# name -> extra engine arguments. "spatial" is pinned to stereo panning with
# occlusion off, so it stays directly comparable with the classic mixer.
VARIANTS = {
    "base": ["+set", "s_backend", "base"],
    "spatial": ["+set", "s_backend", "spatial", "+set", "s_spatialHRTF", "0", "+set", "s_occlusion", "0"],
}

# A/B thresholds between the classic and spatial backends on the demo
AB_MIN_ENVELOPE_CORR = 0.93
AB_MAX_LEVEL_DIFF_DB = 0.5
AB_HRTF_MIN_ENVELOPE_CORR = 0.85
AB_HRTF_MAX_LEVEL_DIFF_DB = 2.0


# ---------------------------------------------------------------------------
# helpers

class Results:
    def __init__(self):
        self.rows = []

    def add(self, name, status, detail=""):
        self.rows.append((name, status, detail))
        print(f"  [{status}] {name}" + (f": {detail}" if detail else ""), flush=True)

    @property
    def failed(self):
        return [r for r in self.rows if r[1] == "FAIL"]


def md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def run_engine(args, home, extra):
    env = dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_AUDIODRIVER="dummy")
    cmd = [
        args.engine,
        "+set", "fs_basepath", args.basepath,
        "+set", "fs_homepath", home,
        "+set", "r_mode", "-1", "+set", "r_customwidth", "320", "+set", "r_customheight", "240",
        "+set", "r_fullscreen", "0", "+set", "in_nograb", "1", "+set", "com_skipIdLogo", "1",
    ] + extra
    p = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       text=True, errors="replace", timeout=args.timeout)
    return p.stdout


def read_wav(path):
    """16-bit PCM WAV -> (float array [frames, channels], rate)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError(f"{path}: not a WAV file")
    pos, fmt, pcm = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            pcm = body
        pos += 8 + size + (size & 1)
    tag, channels, rate, _, _, bits = fmt
    if tag != 1 or bits != 16:
        raise ValueError(f"{path}: expected 16-bit PCM")
    return np.frombuffer(pcm, "<i2").astype(np.float64).reshape(-1, channels) / 32768.0, rate


def extract_avi_audio(path):
    """Audio track of a Quake3e AVI capture -> (float array [frames, channels], rate)."""
    with open(path, "rb") as f:
        data = f.read()
    fmt, chunks = None, []
    stream_types, current = [], None

    def walk(start, end):
        nonlocal fmt, current
        pos = start
        while pos + 8 <= end:
            cid = data[pos:pos + 4]
            size = struct.unpack("<I", data[pos + 4:pos + 8])[0]
            body_start = pos + 8
            if cid in (b"RIFF", b"LIST"):
                walk(body_start + 4, min(body_start + size, end))
            elif cid == b"strh":
                current = data[body_start:body_start + 4]
                stream_types.append(current)
            elif cid == b"strf" and current == b"auds":
                fmt = struct.unpack("<HHIIHH", data[body_start:body_start + 16])
            elif cid[2:] == b"wb":
                chunks.append(data[body_start:body_start + size])
            pos = body_start + size + (size & 1)

    walk(0, len(data))
    if fmt is None:
        raise ValueError(f"{path}: no audio stream")
    tag, channels, rate, _, _, bits = fmt
    if tag != 1 or bits != 16:
        raise ValueError(f"{path}: expected 16-bit PCM audio")
    pcm = b"".join(chunks)
    return np.frombuffer(pcm, "<i2").astype(np.float64).reshape(-1, channels) / 32768.0, rate


def envelope(a, rate, win=0.02):
    n = int(rate * win)
    m = len(a) // n
    return np.sqrt((a[:m * n].reshape(m, n, a.shape[1]) ** 2).mean(axis=1))


def load_golden():
    if os.path.exists(GOLDEN):
        with open(GOLDEN) as f:
            return json.load(f)
    return {}


def check_golden(results, golden, key, digest, update):
    if update:
        golden[key] = digest
        results.add(f"golden {key}", "UPDATED", digest)
    elif key not in golden:
        results.add(f"golden {key}", "SKIP", "no golden yet (run with --update-golden)")
    elif golden[key] == digest:
        results.add(f"golden {key}", "PASS")
    else:
        results.add(f"golden {key}", "FAIL", f"got {digest}, expected {golden[key]}")


# ---------------------------------------------------------------------------
# tests

def test_positional(args, work, results, golden, variant):
    hrtf = variant in ("spatial-hrtf", "spatial-sofa")
    print(f"positional test ({variant})")
    home = os.path.join(work, f"positional-{variant}")
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(home)
    out = run_engine(args, home, VARIANTS[variant] + [
        "+s_spatialTest", POSITIONAL_SOUND, "positional.wav",
        "+quit",
    ])
    if hrtf and "HRTF)" not in out:
        results.add(f"[{variant}] HRTF active", "FAIL", "Steam Audio did not load; engine log:\n" + out[-2000:])
        return
    if variant == "spatial-sofa" and "built-in HRTF" in out:
        results.add(f"[{variant}] HRTF file loaded", "FAIL", "fell back to the built-in HRTF; engine log:\n" + out[-2000:])
        return
    rms = {}
    for line in out.splitlines():
        if line.startswith("spatialtest: ") and "rmsL=" in line:
            _, name, l, r = line.split()
            rms[name] = (float(l.split("=")[1]), float(r.split("=")[1]))
    wav = os.path.join(home, "baseq3", "positional.wav")
    if not rms or not os.path.exists(wav):
        results.add(f"[{variant}] positional render", "FAIL", "no output; engine log:\n" + out[-2000:])
        return
    results.add(f"[{variant}] positional render", "PASS", f"{len(rms)} placements")

    def ok(name, cond, detail):
        results.add(f"[{variant}] {name}", "PASS" if cond else "FAIL", detail)

    # HRTF separates the ears with level, spectrum and delay, so broadband
    # level differences are much smaller than with hard panning.
    ratio = 1.2 if hrtf else 2.0
    tol = 0.05 if hrtf else 1e-6
    ok("left/right: 'right' is louder in the right ear", rms["right"][1] > ratio * rms["right"][0],
       "L=%.4f R=%.4f" % rms["right"])
    ok("left/right: 'left' mirrors 'right'",
       abs(rms["left"][0] - rms["right"][1]) <= tol * rms["right"][1]
       and abs(rms["left"][1] - rms["right"][0]) <= tol * rms["right"][1] + 1e-9, "")
    ok("front is centred", abs(rms["front"][0] - rms["front"][1]) <= tol * rms["front"][0], "L=%.4f R=%.4f" % rms["front"])

    # Height: with only per-ear gains, sounds above/below/level are identical.
    # Compare the rendered audio itself, not just levels (HRTF changes spectra).
    audio, rate = read_wav(wav)
    seg = len(audio) // len(rms)
    names = list(rms.keys())
    seg_of = {n: audio[i * seg:(i + 1) * seg] for i, n in enumerate(names)}
    def differs(a, b):
        return float(np.sqrt(((seg_of[a] - seg_of[b]) ** 2).mean()))
    d_above = differs("above", "front")
    d_below = differs("below", "front")
    d_updown = differs("above", "below")
    detail = "rms diff above-front %.5f, below-front %.5f, above-below %.5f" % (d_above, d_below, d_updown)
    height_ok = min(d_above, d_below, d_updown) > 1e-4
    if hrtf:
        ok("height is audible (above/below/level differ)", height_ok, detail)
        ok("front/back differ", differs("front", "back") > 1e-4, "rms diff %.5f" % differs("front", "back"))
        # the built-in HRTF has no data below about -45 degrees
        d_low = differs("below", "front_down")
        if variant == "spatial-sofa":
            ok("directly below differs from 45 degrees below", d_low > 1e-4, "rms diff %.5f" % d_low)
        else:
            results.add(f"[{variant}] directly below differs from 45 degrees below",
                        "PASS" if d_low > 1e-4 else "XFAIL",
                        "rms diff %.6f" % d_low + ("" if d_low > 1e-4 else " (expected: built-in HRTF stops near -45 degrees)"))
    else:
        results.add(f"[{variant}] height is audible (above/below/level differ)",
                    "PASS" if height_ok else "XFAIL", detail + ("" if height_ok else " (expected: stereo panning)"))

    check_golden(results, golden, f"positional/{variant}", md5(wav), args.update_golden)


def spawn_points(basepath, mapname):
    """info_player_deathmatch origins from a map inside baseq3 pk3s."""
    for pk3 in sorted(os.listdir(os.path.join(basepath, "baseq3")), reverse=True):
        if not pk3.endswith(".pk3"):
            continue
        with zipfile.ZipFile(os.path.join(basepath, "baseq3", pk3)) as z:
            name = f"maps/{mapname}.bsp"
            if name not in z.namelist():
                continue
            d = z.read(name)
            ofs, ln = struct.unpack("<ii", d[8:16])
            ents = d[ofs:ofs + ln].decode("latin1")
            pts = []
            for block in re.findall(r"\{([^}]*)\}", ents):
                kv = dict(re.findall(r'"([^"]+)" "([^"]*)"', block))
                if kv.get("classname") == "info_player_deathmatch" and "origin" in kv:
                    pts.append(tuple(float(v) for v in kv["origin"].split()))
            return pts
    return []


def band_energy(x, rate, lo, hi):
    f = np.abs(np.fft.rfft(x)) ** 2
    fr = np.fft.rfftfreq(len(x), 1 / rate)
    return f[(fr >= lo) & (fr < hi)].sum()


def test_occlusion(args, work, results, golden, variant):
    print(f"occlusion test ({variant})")
    spawns = spawn_points(args.basepath, OCCLUSION_MAP)
    if len(spawns) < 3:
        results.add(f"[{variant}] occlusion setup", "FAIL", f"no spawn points found in {OCCLUSION_MAP}")
        return
    sx, sy, sz = spawns[0]
    listener = (sx, sy, sz + EYE_HEIGHT)
    far = spawns[2]
    cases = {
        # name: (source, expected occlusion range, expected overall level change in dB)
        "open": ((sx, sy, sz), (0.0, 0.0), None),
        "floor": ((sx, sy, sz - 256), (0.8, 1.0), (-24.0, -6.0)),      # one thin floor
        "across": ((far[0], far[1], far[2] + EYE_HEIGHT), (0.8, 1.0), (-30.0, -10.0)),     # several walls (capped by s_occlusionMaxLoss)
    }
    home = os.path.join(work, f"occlusion-{variant}")
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(home)
    cmds = []
    for name, (src, _, _) in cases.items():
        cmds += [f"+s_occlusionTest {POSITIONAL_SOUND} occl-{name}.wav "
                 + " ".join("%g" % v for v in listener + src)]
    out = run_engine(args, home, VARIANTS[variant] + ["+set", "s_occlusion", "1", "+map", OCCLUSION_MAP] + cmds + ["+quit"])
    found = {name: (float(occl), [float(lo), float(mid), float(hi)]) for occl, lo, mid, hi, name in
             re.findall(r"occlusiontest: occlusion=([0-9.]+) loss=([-0-9.]+)/([-0-9.]+)/([-0-9.]+) "
                        r"trace=[0-9.]+us wrote occl-(\w+)\.wav", out)}
    if len(found) != len(cases):
        results.add(f"[{variant}] occlusion render", "FAIL", "missing output; engine log:\n" + out[-2000:])
        return

    def ok(name, cond, detail):
        results.add(f"[{variant}] {name}", "PASS" if cond else "FAIL", detail)

    digest = hashlib.md5()
    levels = {}
    for name, (_, (lo, hi), expect) in cases.items():
        occl, loss = found[name]
        ok(f"occlusion amount '{name}'", lo <= occl <= hi, "%.2f (expected %.1f-%.1f), band losses %s dB"
           % (occl, lo, hi, "/".join("%.1f" % v for v in loss)))
        wav = os.path.join(home, "baseq3", f"occl-{name}.wav")
        digest.update(open(wav, "rb").read())
        audio, rate = read_wav(wav)
        seg = len(audio) // 2
        dry, wet = audio[:seg].mean(axis=1), audio[seg:].mean(axis=1)
        if hi == 0.0:
            ok(f"'{name}' is untouched by occlusion", np.array_equal(dry, wet), "")
        else:
            level = 20 * np.log10(np.sqrt((wet ** 2).mean()) / np.sqrt((dry ** 2).mean()))
            low = 10 * np.log10(band_energy(wet, rate, 0, 300) / band_energy(dry, rate, 0, 300))
            high = 10 * np.log10(band_energy(wet, rate, 3000, 11000) / band_energy(dry, rate, 3000, 11000))
            levels[name] = (level, low)
            ok(f"'{name}' is quieter", expect[0] <= level <= expect[1],
               "%+.1f dB (expected %+.0f to %+.0f)" % (level, expect[0], expect[1]))
            ok(f"'{name}' is muffled (highs drop more than lows)", high < low - 10.0,
               "below 300 Hz %+.1f dB, 3-11 kHz %+.1f dB" % (low, high))
    if "floor" in levels and "across" in levels:
        ok("more wall blocks more, lows included", levels["across"][1] < levels["floor"][1] - 4.0,
           "below 300 Hz: one floor %+.1f dB, several walls %+.1f dB" % (levels["floor"][1], levels["across"][1]))
    check_golden(results, golden, f"occlusion/{variant}", digest.hexdigest(), args.update_golden)


def reflection_metrics(path):
    audio, rate = read_wav(path)
    seg = len(audio) // 3
    dry, refl = audio[:seg], audio[2 * seg:]
    e_dry, e_refl = (dry ** 2).sum(), (refl ** 2).sum()
    level = 10 * np.log10(e_refl / e_dry) if e_refl > 0 and e_dry > 0 else -999.0
    # Schroeder backward integration, T20 extrapolated to 60 dB
    m = (refl ** 2).sum(axis=1)
    sch = np.cumsum(m[::-1])[::-1]
    sch_db = 10 * np.log10(sch / max(sch[0], 1e-30) + 1e-30)
    def cross(db):
        i = int(np.argmax(sch_db < db))
        return i / rate if i > 0 else float("nan")
    t60 = (cross(-25) - cross(-5)) * 3
    # ear correlation of the late tail above 1 kHz
    tail = refl[int(0.15 * rate):int(0.8 * rate)]
    spec = np.fft.rfft(tail, axis=0)
    spec[np.fft.rfftfreq(len(tail), 1 / rate) < 1000] = 0
    hf = np.fft.irfft(spec, n=len(tail), axis=0)
    corr = float(np.corrcoef(hf[:, 0], hf[:, 1])[0, 1]) if (hf ** 2).sum() > 0 else 1.0
    return level, t60, corr


def test_reflections(args, work, results):
    variant = REFL_VARIANT
    print(f"reflection test ({variant})")
    spawns = spawn_points(args.basepath, OCCLUSION_MAP)
    if len(spawns) <= max(REFL_SPAWNS):
        results.add(f"[{variant}] reflection setup", "FAIL", f"not enough spawn points in {OCCLUSION_MAP}")
        return
    home = os.path.join(work, f"reflection-{variant}")
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(home)
    cmds = []
    for run in (1, 2):
        for i in REFL_SPAWNS:
            x, y, z = spawns[i]
            pos = (x, y, z + EYE_HEIGHT, x + 128, y, z + EYE_HEIGHT)
            cmds.append(f"+s_reflectionTest {POSITIONAL_SOUND} refl-{i}-{run}.wav " + " ".join("%g" % v for v in pos))
    out = run_engine(args, home, VARIANTS[variant] + ["+map", OCCLUSION_MAP] + cmds + ["+quit"])
    if out.count("reflectiontest: wrote") != len(cmds):
        results.add(f"[{variant}] reflection render", "FAIL", "missing output; engine log:\n" + out[-2000:])
        return

    def ok(name, cond, detail):
        results.add(f"[{variant}] {name}", "PASS" if cond else "FAIL", detail)

    for i in REFL_SPAWNS:
        m1 = reflection_metrics(os.path.join(home, "baseq3", f"refl-{i}-1.wav"))
        m2 = reflection_metrics(os.path.join(home, "baseq3", f"refl-{i}-2.wav"))
        level, t60, corr = m1
        where = f"spawn {i}"
        ok(f"{where}: reverb level", REFL_LEVEL_DB[0] <= level <= REFL_LEVEL_DB[1],
           "%+.1f dB vs direct (expected %+.0f to %+.0f)" % (level, *REFL_LEVEL_DB))
        ok(f"{where}: decay time", REFL_T60[0] <= t60 <= REFL_T60[1],
           "T60 %.2f s (expected %.1f-%.1f)" % (t60, *REFL_T60))
        ok(f"{where}: tail is diffuse", corr <= REFL_MAX_HF_CORR,
           "ear correlation above 1 kHz %.2f (max %.2f)" % (corr, REFL_MAX_HF_CORR))
        ok(f"{where}: repeatable", abs(m2[0] - level) <= REFL_REPEAT_DB and abs(m2[1] - t60) <= REFL_REPEAT_T60 * t60,
           "second run %+.1f dB, T60 %.2f s" % (m2[0], m2[1]))


def capture_demo(args, work, backend, run):
    home = os.path.join(work, f"demo-{backend}-{run}")
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(home)
    out = run_engine(args, home, VARIANTS[backend] + [
        "+set", "cl_aviFrameRate", "50",
        "+set", "cl_aviMotionJpeg", "1",
        "+set", "nextdemo", "quit",
        "+demo", DEMO, "+video",
    ])
    avi = os.path.join(home, "baseq3", "videos", "video0000.avi")
    if not os.path.exists(avi):
        raise RuntimeError(f"no capture for {backend}; engine log:\n" + out[-2000:])
    audio, rate = extract_avi_audio(avi)
    digest = hashlib.md5((audio * 32768).astype("<i2").tobytes()).hexdigest()
    if not args.keep:
        os.remove(avi)
    return audio, rate, digest


def test_demo(args, work, results, golden):
    captures = {}
    for backend in VARIANTS:
        print(f"demo test ({backend})")
        try:
            a1, rate, d1 = capture_demo(args, work, backend, 1)
            results.add(f"demo capture {backend}", "PASS", "%.1f s at %d Hz" % (len(a1) / rate, rate))
            if backend == REFL_VARIANT:
                # randomised ray tracing: neither bit-exact nor golden
                captures[backend] = (a1, rate)
                continue
            if args.repeat:
                _, _, d2 = capture_demo(args, work, backend, 2)
                results.add(f"demo determinism {backend}", "PASS" if d1 == d2 else "FAIL",
                            "" if d1 == d2 else f"{d1} != {d2}")
            check_golden(results, golden, f"demo/{backend}", d1, args.update_golden)
            captures[backend] = (a1, rate)
        except Exception as e:  # noqa: BLE001 - report and continue
            results.add(f"demo capture {backend}", "FAIL", str(e))

    if "base" not in captures:
        return
    a, ra = captures["base"]
    for other in captures:
        if other == "base":
            continue
        b, rb = captures[other]
        # HRTF legitimately changes per-ear levels and spectra; only the
        # stereo-pan variant is held to the tight A/B limits
        corr_min, level_max = (AB_HRTF_MIN_ENVELOPE_CORR, AB_HRTF_MAX_LEVEL_DIFF_DB) if other != "spatial" \
            else (AB_MIN_ENVELOPE_CORR, AB_MAX_LEVEL_DIFF_DB)
        if other == REFL_VARIANT:
            corr_min, level_max = AB_REFL_MIN_ENVELOPE_CORR, AB_REFL_MAX_LEVEL_DIFF_DB
        ea, eb = envelope(a, ra), envelope(b, rb)
        m = min(len(ea), len(eb))
        corr = min(np.corrcoef(ea[:m, c], eb[:m, c])[0, 1] for c in range(2))
        results.add(f"A/B base vs {other}: envelope correlation (timing and dynamics)",
                    "PASS" if corr >= corr_min else "FAIL", "%.3f (min %.2f)" % (corr, corr_min))
        diffs = [20 * np.log10(np.sqrt((b[:, c] ** 2).mean()) / np.sqrt((a[:, c] ** 2).mean())) for c in range(2)]
        worst = max(abs(d) for d in diffs)
        results.add(f"A/B base vs {other}: loudness", "PASS" if worst <= level_max else "FAIL",
                    "L %+.2f dB, R %+.2f dB (max %.1f)" % (diffs[0], diffs[1], level_max))
        dur = abs(len(a) / ra - len(b) / rb)
        results.add(f"A/B base vs {other}: duration", "PASS" if dur < 0.05 else "FAIL", "%.3f s apart" % dur)


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", default=os.path.join(REPO, "build", "release-linux-x86_64", "quake3e.x64"))
    ap.add_argument("--basepath", default=os.environ.get("Q3_BASEPATH", os.path.expanduser("~/Downloads/q3a")),
                    help="directory containing baseq3/ with retail pak files (env Q3_BASEPATH)")
    ap.add_argument("--workdir", help="where to put engine home dirs and captures (default: temporary)")
    ap.add_argument("--only", choices=("positional", "occlusion", "reflection", "demo"), help="run one test group")
    ap.add_argument("--repeat", action="store_true", help="capture each demo twice to check determinism")
    ap.add_argument("--update-golden", action="store_true", help="record current outputs as golden")
    ap.add_argument("--steamaudio", default=os.environ.get("STEAMAUDIO_LIB"),
                    help="path to libphonon.so; adds the spatial-hrtf variant (env STEAMAUDIO_LIB)")
    ap.add_argument("--hrtf-file", default=os.environ.get("HRTF_FILE"),
                    help="full-sphere SOFA file; adds the spatial-sofa variant (needs --steamaudio, env HRTF_FILE)")
    ap.add_argument("--keep", action="store_true", help="keep AVI captures")
    ap.add_argument("--timeout", type=int, default=600)
    args = ap.parse_args()

    if not os.path.exists(args.engine):
        sys.exit(f"engine not found: {args.engine} (build it with make)")
    if not os.path.exists(os.path.join(args.basepath, "baseq3", "pak0.pk3")):
        sys.exit(f"no baseq3/pak0.pk3 under {args.basepath}")

    work = args.workdir or tempfile.mkdtemp(prefix="q3e-audio-")
    os.makedirs(work, exist_ok=True)
    print(f"work dir: {work}")

    if args.steamaudio:
        if not os.path.exists(args.steamaudio):
            sys.exit(f"Steam Audio library not found: {args.steamaudio}")
        def hrtf_variant(sofa, reflections):
            return ["+set", "s_backend", "spatial", "+set", "s_spatialHRTF", "1",
                    "+set", "s_steamAudioLib", os.path.abspath(args.steamaudio),
                    "+set", "s_hrtfFile", sofa, "+set", "s_reflections", str(reflections)]
        VARIANTS["spatial-hrtf"] = hrtf_variant("", 0)
        sofa = ""
        if args.hrtf_file:
            if not os.path.exists(args.hrtf_file):
                sys.exit(f"HRTF file not found: {args.hrtf_file}")
            sofa = os.path.abspath(args.hrtf_file)
            VARIANTS["spatial-sofa"] = hrtf_variant(sofa, 0)
        VARIANTS[REFL_VARIANT] = hrtf_variant(sofa, 1)
    else:
        print("note: no --steamaudio given, HRTF not tested")

    golden = load_golden()
    results = Results()
    # reflections don't change the direct path these test
    direct_variants = [v for v in VARIANTS if v not in ("base", REFL_VARIANT)]
    if args.only in (None, "positional"):
        for variant in direct_variants:
            test_positional(args, work, results, golden, variant)
    if args.only in (None, "occlusion"):
        for variant in direct_variants:
            test_occlusion(args, work, results, golden, variant)
    if args.only in (None, "reflection") and REFL_VARIANT in VARIANTS:
        test_reflections(args, work, results)
    if args.only in (None, "demo"):
        test_demo(args, work, results, golden)

    if args.update_golden:
        with open(GOLDEN, "w") as f:
            json.dump(golden, f, indent=2, sort_keys=True)
            f.write("\n")

    failed = results.failed
    print(f"\n{len(results.rows) - len(failed)} ok, {len(failed)} failed")
    if not args.workdir:
        shutil.rmtree(work, ignore_errors=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
