# Vendored: stb_vorbis (single-file Ogg Vorbis decoder)

- Source: https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c
- Fetched: 2026-09-03
- License: dual MIT / public domain (see the license block at the end of
  `stb_vorbis.c` itself, unmodified). Copyright (c) 2017 Sean Barrett.
- Unmodified upstream source (v1.22). Header-only style single-file
  library -- one translation unit defines `STB_VORBIS_IMPLEMENTATION`
  before including it (`port/src/audio/vorbis_decoder.cpp`), every other
  includer just gets the declarations.
- Why this instead of a full libvorbis/libogg build: the port only ever
  needs to *decode* a handful of small, already-shipped `.ogg` ambient/
  battle music tracks (`docs/AUDIO_FORMAT.md`) into raw PCM once at load
  time (no encoding, no streaming-decode-while-playing needed at this
  port's scale) -- `stb_vorbis` is a well-tested, dependency-free single
  file for exactly that, same "vendor a real, focused open-source
  implementation instead of reinventing a codec" precedent
  `third_party/puff` and the vendored `simkin` interpreter already
  established for this project.
