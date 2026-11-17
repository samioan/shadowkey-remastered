# shadowkey-decomp

A decompilation project for **The Elder Scrolls Travels: Shadowkey**
(Vir2L Studios, 2004), targeting the retail Nokia N-Gage release — and a
playable PC port built from what it found.

> **Version 1.0.0 — complete.** The whole game is playable from the opening
> in Azra to the end of the Crypt of Hearts, on its original data and its
> original rules, with the PC extras below. More is possible and may come
> later, but this is a finished release.

## Playing it

Grab the latest zip from [Releases](../../releases), unzip it anywhere, and
run **Shadowkey.exe**. It will ask for your own copy of the N-Gage game
files; point it at the folder you extracted them to and press Play.

It does not include the game, and cannot — the game is Bethesda's and the
font it draws with is Nokia's. Both are yours to supply. There is no
installer and no Visual C++ redistributable to chase: everything stays in
the folder you unzipped, and uninstalling is deleting it. The launcher
updates itself from the releases above, and asks first.

### Controls

The N-Gage's own scheme mapped onto a keyboard — the original had a D-pad
and a numeric keypad, so W/A/S/D is the D-pad and the rest stand in for the
number keys. All of it is rebindable in **Options → Customize Controls**.

| | |
|---|---|
| `W` / `S` | walk forward / backward |
| `A` / `D` | turn left / right |
| `←` / `→` | sidestep |
| `↑` / `↓` | look up / down |
| `Space` | jump |
| `E` | use — doors, people, chests, pickups |
| Mouse | turn and look around (**Options → Mouse Look**); while it is on, `A` / `D` sidestep instead of turning |
| Left mouse | attack with your right hand |
| `Q` | attack with your left hand |
| `G` / `C` | cycle what is in your right / left hand |
| `Tab` | character manager — inventory, spells, stats |
| `M` | map |
| `Enter` | confirm |
| `Esc` | back / cancel; during play, opens the menu (Save Game is there) |
| `F11` / `Alt+Enter` | fullscreen on / off |

The game starts in borderless fullscreen; the launcher's display row picks
that or a window (2×, 3× or 4×).

**Controller** (any XInput pad — Xbox-style, or others through Steam Input):
left stick walks and sidesteps, right stick turns and looks, the d-pad is
the N-Gage d-pad; A uses (confirms in menus), B / Start open the menu (back
in menus), Y jumps, RT / LT attack with the right / left hand, RB / LB cycle
the hands, X is the character manager, Back is the map. The buttons follow
whatever you set in Customize Controls.

**Display settings** — the **Port Settings** page in **Options**, reached
from both the main menu and the in-game menu. All default to *Original*, which is the
game exactly as the N-Gage drew it:

- **Resolution** — the 3D view drawn at 2× to 6× the original 176×208, or
  *Auto* to match the window. The HUD, menus and sprites keep their original
  pixels.
- **Aspect Ratio** — widescreen: 4:3, 16:10, 16:9, 21:9, or *Auto* to fill
  the screen. The view widens at the same height, so nothing is stretched —
  you simply see more to each side. The compass banner stays centred, the
  vitals move to the left edge and a weapon to the right; menus keep their
  original shape.
- **Frame Rate** — 30 to 240, *V-Sync* or *Unlimited*. The game still runs
  at its original 25 ticks a second, so nothing plays differently; the
  frames between ticks are interpolated.
- **Fog** — thicker, thinner (which also draws further), or off.
- **Mouse Look** — mouse sensitivity, 50% to 300%, or *Off*. On at 100% by
  default: the mouse turns and tilts the view during play, with the pointer
  hidden and held in the window until a menu, the map or a conversation
  opens.
- **Renderer** — the original software rasterizer, *Direct3D*, or
  *D3D Smooth* (filtered textures) for high resolutions on the graphics card.

**Multiplayer** — the N-Gage's two-player Bluetooth co-op, carried over a
local network: one player hosts, the other finds the game or types its
address, and both play through the campaign together.

Building it yourself: `port\build.bat` for the everyday build (then
`port\run_tests.ps1` from the repository root), or `port\build_dist.bat` for
the shipping one. See [`docs/PORT_ROADMAP.md`](docs/PORT_ROADMAP.md) — the
port's authoritative record, milestone by milestone.

## The target

The install image (`The-Elder-Scrolls-Travels-Shadowkey_N-Gage_.../`, kept
out of git — see `.gitignore`) is a Symbian OS 6.1 filesystem dump, not a
disc/ROM image. Inside it, exactly one file is the actual game:

- **`system/apps/6r51/6r51.app`** — a Symbian **E32Image** ARM binary (not
  ELF, not PE). This is the whole decompilation target. See
  [`docs/E32IMAGE_FORMAT.md`](docs/E32IMAGE_FORMAT.md) for the file format
  itself (there's no public Symbian E32Image spec and no existing Ghidra
  loader for it, so this had to be worked out from the format's own
  `E32IMAGE.H` and verified against this binary's actual header bytes).

Everything else in the image is either:
- stock Symbian/N-Gage platform middleware (`system/libs/*.dll`:
  `ogles.dll`, `http.dll`, `ssl70.dll`, etc.) — out of scope, and
- [SimKin](http://simkin.co.uk/) script source (`system/apps/6r51/*.s`,
  imported by `6r51.app` via `simkin.dll`) — plaintext dialogue/menu/AI
  scripts, already human-readable, not a decompilation target.

## Structure

- `docs/` — format notes and the project roadmap. Start with
  [`docs/ROADMAP.md`](docs/ROADMAP.md) for current status, then
  [`docs/E32IMAGE_FORMAT.md`](docs/E32IMAGE_FORMAT.md),
  [`docs/COMPILER_TOOLCHAIN.md`](docs/COMPILER_TOOLCHAIN.md), and
  [`docs/IMPORT_NAMES.md`](docs/IMPORT_NAMES.md).
- `tools/` — `e32image.py` (header/import/export parsing + code
  extraction), `resolve_imports.py` (structural import call-site
  resolution), `parse_symbian_lib.py` + `resolve_import_names.py` (real
  import names from a period SDK), `fingerprint_compiler.py` (GCC vs.
  RVCT/ADS detection). See [`tools/README.md`](tools/README.md) for usage.
- `shadowkey/` — the actual decomp project: `ghidra/` (Ghidra project,
  regenerable, plus its `scripts/`), `extracted/` (code section +
  load-address metadata pulled from `6r51.app`, regenerable),
  `import_names.json` (tracked — small derived data, not a copy of any
  SDK; see `docs/IMPORT_NAMES.md`).

## Status

**v1.0.0 — complete.** The port runs the whole game: every zone, every
quest, the shops, guilds and trainers, the traps and their saving throws,
the boss fight and the ending, character creation through to the credits,
saving and loading in the original save format, and two-player co-op. It
was built over 140 milestones, each recorded in
[`docs/PORT_ROADMAP.md`](docs/PORT_ROADMAP.md) with the decompiled function
it came from; [`docs/ROADMAP.md`](docs/ROADMAP.md) tracks the
reverse-engineering behind it.

How it got here:

- **Phase 1 — static analysis.** The E32Image format fully parsed, the code
  section in Ghidra with clean auto-analysis, all 415 import call sites
  resolved (319 to real names), and the toolchain identified (GCC, plus one
  vendored ARM RVCT/ADS library).
- **Phase 2 — decided: behavioral reverse-engineering**, not byte-exact
  recompilation. The goal is a PC port, and matching two period compilers
  byte for byte buys that goal nothing. See
  [`docs/ROADMAP.md`](docs/ROADMAP.md#phase-2--decompilation-strategy-decided--behavioral-re-not-byte-exact).
- **Phase 3 — the port.** Starting from the 40ms `CPeriodic` game loop
  ([`docs/RENDER_LOOP.md`](docs/RENDER_LOOP.md)), every system the game uses
  was decompiled and rebuilt as ordinary C++ around the real SimKin scripts,
  checked against the shipped data by a suite of 118 tests.

**What could still come.** The game is finished as the N-Gage shipped it;
further PC extras (more renderers, platforms beyond Windows, mod support)
are possible, but nothing is required for a full playthrough.
