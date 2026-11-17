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

**Shadowkey on PC, rebuilt from the original game's own code. Version 1.0.0 —
the complete game.**

In 2004 Bethesda put an Elder Scrolls game on the Nokia N-Gage. It ran at
176x208, at 25 frames a second, on a phone you held sideways against your
ear. It was also a real Elder Scrolls game — a first-person RPG with
spellcasting, a stat system, quests, guilds, shops and a world you could
get lost in — and it has been effectively unplayable for twenty years unless
you still own the hardware.

This is that game, running natively on Windows: not an emulator, but a
re-implementation written by reading the original ARM binary and rebuilding
what it does, piece by piece, against the game's own scripts and data.

### The whole game

Every zone from Azra to the Crypt of Hearts, every quest, the Fighters',
Mages' and Thieves' guilds and their trainers, the merchants, the arena, the
traps and their saving throws, the spells and scrolls, the boss and the
ending — with the original's rules, numbers and quirks, not approximations of
them. Character creation is the original's, down to the on-screen letter
grid for your name (or just type it). Saves use the N-Gage's own format.

### What the PC adds

All of it optional — every setting starts on *Original*, which is the game
exactly as the N-Gage drew it.

- **Widescreen without stretching** — 4:3, 16:10, 16:9, 21:9 or fill the
  screen. The view widens at the same height, so you see more to each side;
  the HUD keeps its original pixels and its original places.
- **Sharper 3D** — the world drawn at up to 6x the original resolution, or
  matched to your window, while menus and sprites keep their pixel art.
- **Smooth frame rates** — 30 to 240 fps, V-Sync or unlimited. The game
  still runs at its original 25 steps a second, so nothing plays
  differently; the frames in between are interpolated.
- **Direct3D renderer** — high resolutions on the graphics card, with an
  optional texture-filtered look; the original software rasterizer is still
  there.
- **Mouse look** — turn and look up and down with the mouse, with the
  sensitivity you like, on top of the N-Gage's own keyboard-mapped controls.
- **Controller support** — any XInput pad, Xbox-style or others through
  Steam Input.
- **Rebindable controls** — the original's own Customize Controls screen.
- **Fog** — thicker, thinner (which also draws further), or off.
- **Two-player co-op** — the N-Gage's Bluetooth multiplayer, over your local
  network: host, join by discovery or by address, and play the campaign
  together.
- **Borderless fullscreen or a window** — 2x, 3x or 4x, switched any time
  with F11 or Alt+Enter.
- **Five languages** — English, French, German, Spanish and Italian, from
  the original's own string tables.

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
- **It updates itself** from the GitHub releases, and asks first.
- If anything goes wrong, `user\shadowkey_port.log` records what the game
  did — attach it to a bug report.

### 1.0.0

This is a complete release: the game as the N-Gage shipped it, start to
finish, plus the PC options above. More PC features are possible later, but
nothing is missing for a full playthrough.

Source, the full milestone-by-milestone record of how it was reverse
engineered, and the issue tracker:
<https://github.com/samioan/shadowkey-remastered>

---

*The Elder Scrolls* and *Shadowkey* are trademarks of ZeniMax Media Inc.
This project is not affiliated with, endorsed by, or supported by Bethesda
Softworks, ZeniMax, Vir2L Studios or Nokia.
