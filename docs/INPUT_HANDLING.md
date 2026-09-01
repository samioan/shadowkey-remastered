# Key input handling

Started per the port-scoping discussion in `ROADMAP.md`: input handling was
never investigated in any earlier round, but `ROADMAP.md`'s own Phase 3
prioritization already named it as gating a playable port, alongside the
render loop/camera/world-update logic.

## How it was found

Symbian apps receive key events through the CONE/AVKON control framework
(`CCoeControl::OfferKeyEventL`), not directly from the window server —
confirmed by checking the resolved import names: there's no `WSERV` import
in `6r51.app` at all, only `CONE`/`AVKON` control-framework entries. Several
of those entries were already resolved to real names by the earlier SDK
import-name-resolution work but had never been followed up on:
`CONE!82`=`HandleKeyEventL`, `CONE!143`=`OfferKeyEventL`,
`CONE!101`=`InputCapabilities`.

The base-class `OfferKeyEventL` import (`CONE!143`) has exactly **one
caller** in the whole binary — and since a Symbian control's own override
typically falls back to the base-class implementation only for keys it
doesn't handle itself, that caller is the app's own key-handling override.
Found via `pyghidra_find_callers.py` on the import thunk address (resolved
from `shadowkey/extracted/import_thunks.json`), same technique used
throughout this project's import-tracing work.

## `AppUi_OfferKeyEventL` (renamed from `FUN_10021fc0`, 0x10021fc0)

`AppUi_OfferKeyEventL(this, TKeyEvent* aKeyEvent, TEventCode aType)` reads
the event's scan code (`aKeyEvent+4`) and maps it to one of **21 boolean
"game action" slots** in a flat array at `engine+0x488`, written through
**`InputState_SetButton`** (renamed from `FUN_1001a6b8`, 0x1001a6b8) — a
simple bounds-checked (`actionId < 0x15`) byte write. Any scan code not
recognized falls through to the real base-class `OfferKeyEventL` (standard
Symbian default-handling behavior).

### The scan-code → action-slot table

| scan code | action slot | notes |
|---|---|---|
| `0x0e` | 0 | |
| `0x0f` | 1 | |
| `0x10` | 2 | + Bluetooth-conditional side effect (see below) |
| `0x11` | 3 | + Bluetooth-conditional side effect |
| `0x31` ('1') | 4 | |
| `0x32` ('2') | 5 | |
| `0x33` ('3') | 6 | |
| `0x34` ('4') | 7 | |
| `0x35` ('5') | 8 | + Bluetooth-conditional side effect |
| `0x36` ('6') | 9 | |
| `0x37` ('7') | 10 | |
| `0x38` ('8') | 11 | |
| `0x39` ('9') | 12 | |
| `0x30` ('0') | 13 (`0xd`) | |
| `0x2a` ('*') or `0x85` | 14 (`0xe`) | |
| `0x7f` | 15 (`0xf`) | |
| `0xa4` | 16 (`0x10`) | only when `engine+0x260 != 0` |
| `0xa5` | 17 (`0x11`) | only when `engine+0x260 != 0` |
| `1` | 18 (`0x12`) | |
| `0xa7` | 20 (`0x14`) | only when `engine+0x260 == 0` |

**The numeric keypad digits (`0x30`-`0x39`, literally their own ASCII
values as Symbian scan codes) are the primary game controls** — each digit
key has its own dedicated action slot, no lookup table needed since the
scan code *is* the character. This matches the N-Gage's well-known design:
its unconventional "sideways" form factor and lack of a normal D-pad means
many N-Gage games use the numeric keypad as the primary control surface.

The four codes `0x0e`/`0x0f`/`0x10`/`0x11` are four **consecutive** values
mapped to four consecutive action slots (0-3) — a strong structural signal
they're the four D-pad/arrow directions, which in Symbian's standard
`TStdScanCode` enum (`e32keys.h`) are conventionally
`EStdKeyLeftArrow`/`EStdKeyRightArrow`/`EStdKeyUpArrow`/`EStdKeyDownArrow`
in exactly that order. **This specific enum mapping was not re-verified
against a primary-source SDK this pass** — the Series 60 v0.9 SDK this
project extracted earlier (`IMPORT_NAMES.md`) was downloaded to a
session-scratchpad temp directory and no longer exists locally; a web
search for a public mirror of `e32keys.h` didn't turn up the exact enum
listing either. Treat the `EStdKey*` names in this doc as **well-known
Symbian platform convention, not confirmed against this project's own
archived source** — worth firming up by re-downloading the SDK (same
method as `IMPORT_NAMES.md`) if exact key names matter for the port. The
`0xa4`/`0xa5`/`0xa7` codes (only live in the alternate `engine+0x260 != 0`
mode) fall in the range Symbian conventionally reserves for
`EStdKeyDevice0`-`EStdKeyDeviceF` (phone-specific keys: soft keys,
send/end-call, camera button, etc.) — same caveat applies.

### A hidden cheat-code / Easter-egg system

Four of the mapped codes (`0x0e`/`0x0f`/`0x10`/`0x11`, action slots 0-3)
plus two of the digit keys (`'5'`→slot 8, `'7'`→slot 10) are **also**
checked, on key-up (`aType == 3`), against a 12-step expected-sequence
array at `engine+0x414` (current progress tracked at `engine+0x410`): if
the pressed key's action slot doesn't match the next expected step, the
progress counter resets to 0; if it matches and all 12 steps complete,
`engine+0x14a7a` gets set to `1` and **`SecretSequence_OnComplete`**
(renamed from `FUN_1001b204`, 0x1001b204) plays a sound effect (id
`0x57`). This is a genuine hidden input-sequence unlock — a
Konami-code-style Easter egg — mixing D-pad directions and specific
number keys. Not pursued further (what `engine+0x14a7a` actually unlocks
is unknown); noted here since a straight scan-code remap for a port should
preserve it if the developers intended it to be discoverable.

### The Bluetooth-conditional side effect

Codes `0x10`, `0x11`, and `0x35` ('5') each additionally check
`*(char*)(*(engine+0x5cc)+0x31)` — `engine+0x5cc` is the already-identified
GAMECOMMS (Bluetooth multiplayer) subsystem object (`WORLD_MODEL.md`) — and
if set, make an extra virtual call. Multiplayer-specific and tangential;
`GAMECOMMS`/Bluetooth is already deprioritized for the port per
`ROADMAP.md`.

## What's still open

- **Which physical N-Gage key produces which scan code**, and **what each
  of the 21 action slots actually *does* in gameplay** (move forward?
  strafe? attack? open menu?) — not traced. `pyghidra_find_reads.py 0x488`
  found zero direct consumers, meaning whatever reads the action-state
  array likely caches `engine+0x488` in a local/member pointer first
  rather than re-deriving the offset each time — the next step would be
  tracing that cache point (probably in the game's per-tick update
  function) rather than a flat offset search.
- Exact Symbian `TStdScanCode` enum values for `0x0e`/`0x0f`/`0x10`/`0x11`,
  `0x2a`, `0x85`, `0x7f`, `0xa4`/`0xa5`/`0xa7` — plausible by convention
  and structural evidence, not confirmed against a primary source this
  pass (see above).
- What `0x260` (the mode flag switching between the two `0xa4`/`0xa5`/
  `0xa7`-vs-`0xa7`-alone key sets) actually represents (menu open? text
  entry? a different screen state?) — not traced.
- What `engine+0x14a7a` unlocks when the hidden sequence completes.
- Pointer/touch input (`CONE!84`, `HandlePointerBufferReadyL`) — the
  N-Gage has no touchscreen, so this import is very likely dead weight for
  this device, but wasn't checked for callers to confirm.

## Labels applied

- `AppUi_OfferKeyEventL` (0x10021fc0)
- `InputState_SetButton` (0x1001a6b8)
- `SecretSequence_OnComplete` (0x1001b204)

Tool: `pyghidra_label_input_handling.py`.
