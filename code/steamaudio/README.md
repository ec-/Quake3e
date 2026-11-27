# Steam Audio headers

`include/phonon.h` and `include/phonon_version.h` are copied unmodified from the
Steam Audio C SDK **4.8.1** (https://github.com/ValveSoftware/steam-audio,
release asset `steamaudio_4.8.1.zip`). They are Copyright Valve Corporation and
licensed under the Apache License 2.0 (see the header comments).

Only the headers are vendored. The engine loads the library (`libphonon.so`,
`phonon.dll`, `libphonon.dylib`) at runtime from the `s_steamAudioLib` cvar, so
the engine builds and runs without it and falls back to stereo panning.

The prebuilt library bundles third-party components with their own terms (see
`THIRDPARTY.md` in the SDK); in particular Intel IPP may only be redistributed
unmodified and with its notices. Check this before shipping the library with a
game.

To update: replace both headers from the new SDK, bump the version above, and
check `code/client/snd_steamaudio.c` against the API changes.
