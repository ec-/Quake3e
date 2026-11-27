# Audio tools

## bandwidth_extend.py

Adds high-frequency content to 22 kHz sounds with
[AudioSR](https://github.com/haoheliu/versatile_audio_super_resolution) (MIT),
adds bass to thin sounds, and writes a 48 kHz pk3 that overrides the
originals. Processed id Software sounds are for personal use only.

- **Top end:** below the crossover (the top of the original's recorded band,
  found against the 8/16-bit quantisation floor) the original is kept; only
  AudioSR's new band above it is mixed in, level-matched over the highest
  octave the original has and gated by the original's envelope.
- **Bass:** a subharmonic synthesiser (no ML): the lowest strong octave
  below 1.3 kHz is band-passed, halved in frequency with a zero-crossing
  flip-flop, shaped by that band's envelope and mixed back in. The amount
  depends on the sound's path (`bass_amount`): footsteps −8 dB, weapons
  and impacts −10 dB, world/items −14 dB, none for voices, announcer and UI,
  and none for sounds already holding ≥20% of their energy below 60 Hz.
  Where the extra bass would clip, the sound is turned down by up to 2 dB
  and then only the bass is limited.
- **Peaks:** many retail sounds (announcer especially) are clipped at full
  scale, and resampling plus the new top end overshoot that by up to 9 dB,
  so a 10 ms look-ahead limiter turns down just the peaks (ceiling −0.2 dB)
  instead of the whole sound. Loudness stays within ±3 dB of the original
  (median −0.1 dB).

## Generating the higher-quality sounds

You need your own copy of Quake III Arena (`baseq3/pak0.pk3` to `pak8.pk3`)
and a GPU that PyTorch supports (CUDA or ROCm). Without a GPU, `--no-sr`
still adds bass in seconds, but the top end is not extended.

**1. Set up AudioSR** in its own environment, outside the repo (Python 3.11):

```
python3.11 -m venv ~/audiosr-venv
~/audiosr-venv/bin/pip install torch torchaudio torchvision   # use the index for your GPU, see pytorch.org
~/audiosr-venv/bin/pip install --no-deps audiosr==0.0.7
~/audiosr-venv/bin/pip install tqdm pyyaml einops chardet "numpy==1.26.4" soundfile "librosa==0.9.2" scipy pandas \
    unidecode "torchlibrosa>=0.0.9" "transformers==4.30.2" "huggingface_hub<0.26" progressbar ftfy timm \
    phonemizer matplotlib
```

AMD Strix Halo (gfx1151): PyTorch wheels from `https://rocm.nightlies.amd.com/v2/gfx1151/`.
The first run downloads the AudioSR model weights from Hugging Face.

**2. Extract the game's sounds.** Later pk3s override earlier ones, as in
the game. By default this includes sounds from any custom maps in that
directory; `--retail` takes only id's pak0–pak8 (598 sounds):

```
python3 tools/audio/extract_sounds.py --retail /path/to/quake3/baseq3 ~/q3-sounds
```

**3. Process them.** About 15 s per sound on a Radeon 8060S, so ≈2.5 h for
all ~600; it can be stopped and restarted, and picks up where it left off:

```
~/audiosr-venv/bin/python tools/audio/bandwidth_extend.py ~/q3-sounds ~/zz-hd-sounds.pk3
```

**4. Install** the pack into your home `baseq3` (it sorts after the retail
paks, so it overrides their sounds):

```
cp ~/zz-hd-sounds.pk3 ~/.q3a/baseq3/          # Linux; on Windows, your Quake3e home or install baseq3
```

Then play with the spatial backend, e.g. `+set s_steamAudioLib <path to libphonon> +set s_hrtfFile <SOFA file> +s_profile reflections`.
Remove the pk3 to go back to the original sounds. Use the pack on servers with
`sv_pure 0` (pure servers only allow their own paks). The processed sounds are
derived from id Software's assets: keep them for personal use and don't
redistribute them.

## Options and details

```
bandwidth_extend.py <dir> zz-hd-sounds.pk3 [--work dir] [--no-sr] [--no-bass] [--steps 50]
```

Each result is cached in the
work directory (default `<pk3 name>-work`), so an interrupted run picks up
where it stopped; delete it to reprocess. Rerunning with a warm cache only
redoes bass and peaks (seconds). `--no-sr` skips AudioSR entirely. A CSV report is written next to the pk3.
