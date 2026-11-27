# Audio tools

## bandwidth_extend.py

Adds high-frequency content to 22 kHz sounds with
[AudioSR](https://github.com/haoheliu/versatile_audio_super_resolution) (MIT)
and writes a pk3 that overrides the originals. Below the crossover the
original is kept unchanged; only AudioSR's new top end is mixed in. Processed
id Software sounds are for personal use only.

Setup (outside the repo; Python 3.11, PyTorch for your GPU):

```
python3.11 -m venv venv
venv/bin/pip install torch torchaudio torchvision   # pick the right index for your GPU (ROCm/CUDA)
venv/bin/pip install --no-deps audiosr==0.0.7
venv/bin/pip install tqdm pyyaml einops chardet "numpy==1.26.4" soundfile "librosa==0.9.2" scipy pandas \
    unidecode "torchlibrosa>=0.0.9" "transformers==4.30.2" "huggingface_hub<0.26" progressbar ftfy timm \
    phonemizer matplotlib
```

AMD Strix Halo (gfx1151): PyTorch wheels from `https://rocm.nightlies.amd.com/v2/gfx1151/`.

Usage: extract the sounds to process into a directory (keeping `sound/...`
paths), then

```
venv/bin/python bandwidth_extend.py <dir> zz-hd-sounds.pk3 [--steps 50] [--keep outdir]
```

and copy the pk3 into `baseq3` (home path is fine). About 15 s per sound on a
Radeon 8060S. A CSV report is written next to the pk3.
