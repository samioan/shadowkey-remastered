# itch.io page copy

M114. The text for the store page, kept in the repo so it can be revised
like anything else rather than living only in a web form. Paste into itch's
description field; it takes Markdown.

Page settings that matter:

- **Kind of project**: Downloadable. Not "HTML" — there is nothing to run
  in a browser.
- **Pricing**: free. This is someone else's game; charging for a port of it
  would be indefensible whatever the licence situation.
- **Platforms**: Windows only for now.
- **Upload**: the `ShadowkeyRemastered-<tag>-win64.zip` from the matching
  GitHub release, marked "Windows" and **not** "This file will be played in
  the browser".
- **Cover image**: `port/src/launcher/assets/banner_source.jpg`, cropped to
  itch's 630x500.
- Leave **"Generate itch.io app manifest"** off — the launcher already
  updates itself from GitHub (M115), and two update mechanisms fighting over
  the same folder is a bug waiting to happen.

---

## The Elder Scrolls Travels: Shadowkey — Remastered

**Shadowkey on PC, rebuilt from the original game's own code.**

In 2004 Bethesda put an Elder Scrolls game on the Nokia N-Gage. It ran at
176x208, at 25 frames a second, on a phone you held sideways against your
ear. It was also a real Elder Scrolls game — a first-person RPG with
spellcasting, a stat system, quests, shops and a world you could get lost
in — and it has been effectively unplayable for twenty years unless you
still own the hardware.

This is that game, running natively on Windows: not an emulator, but a
re-implementation written by reading the original ARM binary and rebuilding
what it does, piece by piece.

### What you need

**This download does not include the game.** It cannot — the game belongs to
Bethesda, and the font it draws its menus with belongs to Nokia. You supply
your own copy of both.

1. Download and unzip anywhere.
2. Run **Shadowkey.exe**.
3. Point it at your N-Gage game files. Pick the folder you extracted them
   to; it finds the right one inside.
4. Press **Play**.

Optionally, add `Ceurope.gdr` (the N-Gage's system font) for the real menu
lettering. Without it everything still works, just with stand-in letters.

### Notes

- **Portable.** No installer, no registry keys, nothing in AppData, no
  Visual C++ redistributable. Your game files are copied into `data\`, your
  saves and settings live in `user\`. Uninstalling is deleting the folder.
- **Pick your window size** — 2x, 3x or 4x the original 176x208.
- **It updates itself** from the GitHub releases, and asks first.

### This is a work in progress

It is playable, and it is not finished. Some things the original does are
not implemented yet; the game logs them as it goes, so
`user\shadowkey_port.log` is the right thing to attach to a bug report.

Source, the full milestone-by-milestone record of how it was reverse
engineered, and the issue tracker:
<https://github.com/samioan/shadowkey-remastered>

---

*The Elder Scrolls* and *Shadowkey* are trademarks of ZeniMax Media Inc.
This project is not affiliated with, endorsed by, or supported by Bethesda
Softworks, ZeniMax, Vir2L Studios or Nokia.
