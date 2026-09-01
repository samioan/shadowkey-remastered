# Vendored: puff (zlib's reference inflate)

- Source: https://raw.githubusercontent.com/madler/zlib/master/contrib/puff/{puff.c,puff.h}
- Fetched: 2026-09-01
- License: zlib License (see `LICENSE.txt`, copied from the zlib project's
  own top-level LICENSE, which the same terms appear inline in puff.h's
  header). Copyright Mark Adler.
- Unmodified upstream source. `puff()` decompresses **raw DEFLATE** data
  only (not the zlib-wrapped stream format) -- the port's own loader code
  skips the 2-byte zlib header and doesn't validate the trailing 4-byte
  Adler-32 before calling into this, since `GameEngine_InitLevel`'s
  compressed per-zone files (`.ztx`/`.zmp`/`.zlu`/`.zfg`/`.zcp`/`.zsk`,
  see docs/ZONE_FORMAT.md) are a 4-byte LE decompressed-size header
  followed by an ordinary zlib stream (`EZLIB__uncompress`), verified
  directly against real game data this session.
- Why this instead of vendoring full zlib: the port only ever needs to
  *decompress* these specific files, never compress anything -- `puff` is
  the zlib project's own ~800-line single-purpose reference decoder for
  exactly this (no build system, no huge API surface), a much smaller
  footprint for a real, well-tested implementation of the same algorithm
  simkin's own vendoring precedent (real open-source code, not
  reinvented) already established for this project.
