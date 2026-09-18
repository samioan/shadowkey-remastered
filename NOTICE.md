# Notices and attribution

This project is a from-scratch, behavioural re-implementation of *The Elder
Scrolls Travels: Shadowkey* (N-Gage, 2004), written by reading the original
binary. It is not affiliated with, endorsed by, or supported by Bethesda
Softworks, ZeniMax, Vir2L Studios or Nokia.

## What this project does not contain, and never will

**The game.** No script, texture, model, sound, string table or byte of the
original `6r51.app` is committed here, and none is distributed with any
release. The launcher asks for your own copy and will not run without it.
See `.gitignore`.

**The N-Gage system font.** `Ceurope.gdr` is Nokia device firmware. It is
never committed and never shipped; the launcher asks for it separately, and
the game runs (with stand-in letters) if you do not have it.

## Artwork

The launcher's background is this project's own key art, built on the
original game's key art. The full-resolution master is committed at
`port/src/launcher/assets/banner_source.jpg`; the downscaled copy the
launcher actually embeds is `banner.jpg`, and the application icon is a
crop of the same image (see `tools/make_banner.py` and `tools/make_icon.py`).

"The Elder Scrolls" and "Shadowkey" are trademarks of ZeniMax Media Inc.
The wordmark visible in that artwork is theirs, not this project's.

## Third-party code

| Component | Licence | Where |
|---|---|---|
| Simkin (C++ interpreter, TreeNode flavour) | LGPL | `port/third_party/simkin/`, see its `PROVENANCE.md` |
| `puff` (zlib's reference inflate) | zlib | `port/third_party/puff/`, see its `PROVENANCE.md` |
| `stb_vorbis` | public domain / MIT | `port/third_party/stb_vorbis/` |

The launcher vendors nothing of its own. It decodes its artwork through
WIC (`windowscodecs.dll`), a Windows system component, so there is nothing
extra to redistribute and no Visual C++ runtime to install — release builds
link the static CRT.
