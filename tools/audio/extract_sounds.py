#!/usr/bin/env python3
"""
Extract every sound/*.wav from a game directory's pk3 files, as the game
sees them: pk3s are read in name order and later ones override earlier ones.
Includes sounds from custom maps and mods in that directory; --retail
reads only id's pak0-pak8. The result is the input directory for
bandwidth_extend.py.

usage: extract_sounds.py [--retail] <game dir, e.g. .../quake3/baseq3> <output dir>
"""

import glob
import os
import sys
import zipfile


def main():
    args = sys.argv[1:]
    retail = "--retail" in args
    args = [a for a in args if a != "--retail"]
    if len(args) != 2:
        sys.exit(__doc__.strip())
    gamedir, outdir = args
    pk3s = sorted(glob.glob(os.path.join(gamedir, "pak[0-8].pk3" if retail else "*.pk3")))
    if not pk3s:
        sys.exit("no .pk3 files in " + gamedir)

    # the engine compares paths case-insensitively; the last pk3 wins
    found = {}
    for pk3 in pk3s:
        with zipfile.ZipFile(pk3) as z:
            for name in z.namelist():
                low = name.lower()
                if low.startswith("sound/") and low.endswith(".wav"):
                    found[low] = (pk3, name)

    root = os.path.realpath(outdir)
    for pk3, name in found.values():
        dest = os.path.realpath(os.path.join(outdir, name))
        if not dest.startswith(root + os.sep):
            print("skipping unsafe path %s in %s" % (name, pk3))
            continue
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with zipfile.ZipFile(pk3) as z, open(dest, "wb") as f:
            f.write(z.read(name))
    print("extracted %d sounds from %d pk3 files to %s" % (len(found), len(pk3s), outdir))


if __name__ == "__main__":
    main()
