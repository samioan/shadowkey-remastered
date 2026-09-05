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

**Every physical key name below is now confirmed directly from the game's
own data** (see "The `InputState` object" and "The full default control
scheme" further down) — not guessed from Symbian convention:

| scan code | action slot | key name (from game data) | notes |
|---|---|---|---|
| `0x0e` | 0 | Left | + Bluetooth-conditional side effect on `0x10`/`0x11` (see below) |
| `0x0f` | 1 | Right | |
| `0x10` | 2 | Up | + Bluetooth-conditional side effect |
| `0x11` | 3 | Down | + Bluetooth-conditional side effect |
| `0x31` ('1') | 4 | Key 1 | |
| `0x32` ('2') | 5 | Key 2 | |
| `0x33` ('3') | 6 | Key 3 | |
| `0x34` ('4') | 7 | Key 4 | |
| `0x35` ('5') | 8 | Key 5 | + Bluetooth-conditional side effect |
| `0x36` ('6') | 9 | Key 6 | |
| `0x37` ('7') | 10 | Key 7 | |
| `0x38` ('8') | 11 | Key 8 | |
| `0x39` ('9') | 12 | Key 9 | |
| `0x30` ('0') | 13 (`0xd`) | Key 0 | |
| `0x2a` ('*') or `0x85` | 14 (`0xe`) | Key * | |
| `0x7f` | 15 (`0xf`) | Key # | not the ASCII value of `#` (`0x23`) — N-Gage's own scan code for this key |
| `0xa4` | 16 (`0x10`) | Left Selection Key | only when `engine+0x260 != 0`; the left softkey |
| `0xa5` | 17 (`0x11`) | Right Selection Key | only when `engine+0x260 != 0`; the right softkey |
| `1` | 18 (`0x12`) | (reuses "Left Selection Key") | |
| `0xa7` | 20 (`0x14`) | (reuses "Left Selection Key") | only when `engine+0x260 == 0` |

**The numeric keypad digits (`0x30`-`0x39`, literally their own ASCII
values as Symbian scan codes) are the primary game controls** — each digit
key has its own dedicated action slot, no lookup table needed since the
scan code *is* the character. This matches the N-Gage's well-known design:
its unconventional "sideways" form factor and lack of a normal D-pad means
many N-Gage games use the numeric keypad as the primary control surface.

`0x0e`/`0x0f`/`0x10`/`0x11` (Left/Right/Up/Down) match Symbian's standard
`TStdScanCode` enum (`e32keys.h`) convention
`EStdKeyLeftArrow`/`EStdKeyRightArrow`/`EStdKeyUpArrow`/`EStdKeyDownArrow`
exactly, and `0xa4`/`0xa5`/`0xa7` fall in the range Symbian conventionally
reserves for `EStdKeyDevice0`-`EStdKeyDeviceF` (phone soft keys) —
**confirmed indirectly** by the key *names* pulled from the game's own
string table (below), even though the exact `EStdKey*` enum identifiers
themselves were never re-verified against a primary-source SDK this
session (the project's earlier-archived Series 60 SDK copy no longer
exists locally, and a public `e32keys.h` mirror search came up short —
worth firming up by re-downloading the SDK, same method as
`IMPORT_NAMES.md`, only if the exact enum *names* matter, since the actual
key identities are no longer in doubt).

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

## The `InputState` object: not a flat array, a whole small class

`engine+0x488` (the object `InputState_SetButton`/`AppUi_OfferKeyEventL`
write into) turned out to be the base of a **much richer structure** —
found by checking which functions sit next to `InputState_SetButton` in
the binary's address space (small, related C++ methods are typically
compiled contiguously) and confirmed by tracing their real callers.

This also explains why `pyghidra_find_reads.py 0x488` initially found
**zero** consumers, despite the object clearly being read every frame: the
value `0x488` doesn't fit a single ARM rotated immediate, so the compiler
builds `engine + 0x488` as a two-instruction split the existing offset-scan
tooling doesn't recognize (neither a literal-pool constant nor a
single-instruction `ADD`/`SUB` match) — a real, if narrow, tooling gap.
Found instead by reading the neighboring functions directly.

```c
struct InputState {                    // base = engine+0x488
    uint8  current[0x15];                // +0x00, this frame's 21 button states
    uint8  previous[0x15];                // +0x15, last frame's states (edge detection)
    int32  bindingOffset[0x11];            // +0x2c, 17-entry logical-action ->
                                             //        byte-offset indirection table
                                             //        (a REMAPPABLE key-binding scheme)
    int32  bindingResourceId[0x15];          // +0x6c, 21-entry resource-string ID per
                                               //        action slot (0xd05-0xd16,
                                               //        sequential) -- CONFIRMED UI key
                                               //        labels ("Left"/"Key 1"/etc.), see
                                               //        below
    uint8  bindingFlag[0x15];                  // +0xc0, 21-entry per-slot flag
};
```

- **`InputState_GetButton`**/**`InputState_GetButton2`** (renamed from
  `FUN_1001a690`/`FUN_1001a6f0`) — direct getters, byte-identical logic,
  different call sites (exact distinction not determined). Called from
  the already-known screen-state machine (`FUN_10068e0c`,
  `RENDERER_3D.md`'s "general screen-state machine" open item) and the
  per-tick load-state machine (`FUN_10069cac`, from Round D's
  `GameEngine_InitLevel` `param_3` investigation) among others.
- **`InputState_GetButtonPrev`** (was `FUN_1001a6dc`) — reads the
  previous-frame mirror array, enabling "just pressed" vs. "held" edge
  detection.
- **`InputState_ResolveBindingOffset`** (was `FUN_1001a67c`),
  **`InputState_SetBoundButton`** (was `FUN_1001a6c4`, called from
  `GameTick_UpdateAndPresent` — the already-known 25Hz per-tick
  function, `RENDER_LOOP.md`), **`InputState_GetBoundButton`**/
  **`InputState_GetBoundButtonPrev`** (was `FUN_1001a724`/`FUN_1001a700`)
  — a parallel set of accessors that go through the 17-entry
  `bindingOffset` indirection table first: `this[bindingOffset[logicalId]]`
  instead of `this[slot]` directly. This is a genuine **remappable
  control-binding layer** — 17 logical actions (0-0x10) whose actual
  storage slot can be reassigned at runtime, presumably backing an
  in-game "customize controls" option.
- **`InputState_InitDefaultBindings`** (was `FUN_1001a064`) — the
  constructor: clears both state arrays (`InputState_ClearAll`, was
  `FUN_1001a744`) then calls **`InputState_RegisterBinding`** (was
  `FUN_1001a770`) 21 times with **sequential resource-string IDs
  `0xd05`-`0xd16`** and a flag. Slots `0`-`0xf` (16 slots) each get a
  **unique** ID and flag `1`; slots `0x10`/`0x11`/`0x12`/`0x14` get flag
  `0` and mostly **reuse** ID `0xd15`; slot `0x13` (19) is never
  registered at all — a gap. The flag-`0` slots are exactly the same 4
  slots the scan-code table above marks as the alternate `engine+0x260`
  menu-mode keys (`0xa4`/`0xa5`) plus the Backspace-mapped slot 18 and
  the `0xa7`-mapped slot 20 — strong cross-confirmation that these are
  **fixed system actions** (back/menu/exit, sharing one generic label),
  while the 16 unique-ID slots are **core remappable gameplay actions**.

**Resolved**: each action slot's resource ID resolves to a real UI label,
found not in a Symbian `.rsc` resource file (`6r51.rsc`, right next to
`6r51.app`, turned out to be only 52 bytes — just app-registration data,
not a string table) but in a **separate, game-specific localized string
table** — `system/apps/6r51/stringtable.eng` (and five sibling files,
`.euk`/`.fre`/`.ger`/`.ita`/`.spa`, matching the release's bundled
languages). Format (verified byte-for-byte against the real 338078-byte
`.eng` file, 4082 entries):

```c
struct StringTable {
    uint32 count;
    struct { uint32 charCount; uint16 text[charCount]; } entries[count];
    // text[] is UTF-16LE, charCount includes a trailing 0x0000
};
```

The entry index **is** the resource-string ID used throughout the game's
native code and SimKin bindings — confirmed by reading indices
`0xd05`-`0xd16` (`InputState`'s per-action-slot labels) directly out of a
real file and getting back exactly the key names in the table above
("Left", "Right", "Key 1", "Left Selection Key", etc.). This is a
**new, independently reusable format finding** (the game's UI/dialogue
text in general almost certainly goes through this same table, well
beyond just controls) — not the same mechanism as the giant 121-case
string dispatcher `RENDER_LOOP.md` already found and ruled out as a
render-loop candidate (`0x10078de4`, small sequential case indices 0-120,
not raw resource IDs like these). Tool: `tools/parse_string_table.py`.

### The full default control scheme

`FUN_1001a220` (called right after `InputState_InitDefaultBindings`
finishes tagging all 21 slots with key-name labels) sets up the default
**logical action → physical key** bindings for the 16 remappable core
actions, each *also* tagged with its own resource ID
(`0xced`-`0xd04`, sequential) — resolved the same way:

| Action | Default key |
|---|---|
| Move Forward | Up |
| Move Backward | Down |
| Turn Left | Left |
| Turn Right | Right |
| Jump | Key 1 |
| Look Up | Key 2 |
| Use | Key 3 |
| Side Step Left | Key 4 |
| Use Right Action | Key 5 |
| Side Step Right | Key 6 |
| Use Left Action | Key 7 |
| Look Down | Key 8 |
| Map Toggle | Key 9 |
| Cycle Right Queue | Key 0 |
| Cycle Left Queue | Key * |
| Character Manager | Key # |

**M57 addendum: the action *index* is not the row order above.** That
table lists the actions in the order `FUN_1001a220` calls the binder,
which is (almost) resource-id order; the integer each action actually
*is* comes from that same call's second argument:

```c
FUN_1001a784(input, actionIndex, keySlot, nameResourceId);
```

The (action, default key) pairs above are all correct. The indices are:

| index | action | resource | default key |
|---|---|---|---|
| 0 | Move Forward | `0xced` | Up |
| 1 | Move Backward | `0xcee` | Down |
| 2 | Turn Left | `0xcef` | Left |
| 3 | Turn Right | `0xcf0` | Right |
| 4 | Look Up | `0xcf1` | Key 2 |
| 5 | Look Down | `0xcf4` | Key 8 |
| 6 | Side Step Left | `0xcf2` | Key 4 |
| 7 | Side Step Right | `0xcf3` | Key 6 |
| 8 | Jump | `0xcf6` | Key 1 |
| **9** | **Map Toggle** | `0xd04` | **Key 9** |
| 10 | Cycle Right Queue | `0xcfd` | Key 0 |
| 11 | Cycle Left Queue | `0xcfe` | Key * |
| 12 | Character Manager | `0xcff` | Key # |
| 13 | Use | `0xd01` | Key 3 |
| 14 | Use Left Action | `0xd02` | Key 7 |
| 15 | Use Right Action | `0xd03` | Key 5 |

This matters as soon as something has to be read out of the binary *by
index*: `FUN_1001c9c0`, the player's per-frame input poll, toggles the
map on literal action `9`, which under the row-order numbering would read
as "Side Step Right". Cross-checked against every branch of that same
poll — 0/1 and 6/7 are the four translation moves, 2/3 the two turns
sharing one release call, 4/5 the two looks sharing another, and 8..15
the edge-triggered actions, each landing on exactly the handler its name
predicts.

This is a complete, sensible first-person-RPG control scheme for the
N-Gage's numeric-keypad-primary layout — D-pad for movement/turning, and
the 0-9/*/# keys for combat, item-queue cycling (quick-swap spell/weapon
slots), and menus.

**8 more logical actions exist in the same resource-ID range but are
*not* wired into this default-binding table** (`0xcf5` Bash, `0xcf7`
Shoot, `0xcf8` Toggle Zoom, `0xcf9` Reload, `0xcfa` Stand/Crouch/Prone,
`0xcfb` Next Weapon, `0xcfc` Toggle Compass, `0xd00` Select) — either
handled through a different, not-yet-traced mechanism (fixed/
non-remappable, context-sensitive), or genuinely unused on this
platform/version (some read like a shooter's control set — "Shoot",
"Reload", "Toggle Zoom" — that may not apply to Shadowkey's melee/magic
combat, suggesting this string range might be shared with, or copied
from, a different Vir2L N-Gage title's control scheme).

Confirmed connected to a real, readable in-game UI: `configkeys.s` and
`configkeys2.s` (SimKin scripts, `system/apps/6r51/`) implement the
"Configure Keys" options-menu screen, calling native functions
`ConfigKeysMenu(page)` and `ConfigKeysDefault()` that are almost
certainly `InputState`'s binding-indirection accessors and
`InputState_InitDefaultBindings`/`FUN_1001a220` respectively. **A first
check of this (searching `6r51.app` for either name as a literal ASCII
string) reported a false "dead end"** — both names exist, just as
UTF-16LE, which an ASCII-only search silently misses. Re-checked with
both encodings and found both, registered into a shared name-resolution
trie right next to each other (`"ConfigKeysDefault"`→index `0x11`,
`"ConfigKeysMenu"`→index `0x12`) — **this fully resolved the SimKin
native-function bridge's real mechanism**, see
[`SIMKIN_BRIDGE.md`](SIMKIN_BRIDGE.md) for the complete writeup rather
than duplicating it here.

## What's still open

- The exact native-code identity of `ConfigKeysMenu`/`ConfigKeysDefault`
  themselves (as opposed to the registration mechanism that names them,
  now resolved in `SIMKIN_BRIDGE.md`) — inferred by role, not
  independently confirmed by tracing the trie's dispatch output to a
  specific handler function.
- The exact distinction between `InputState_GetButton` and
  `InputState_GetButton2` (byte-identical logic, different callers).
- Why 8 logical actions (Bash/Shoot/Toggle Zoom/Reload/Stand-Crouch-Prone/
  Next Weapon/Toggle Compass/Select) aren't in the default-binding table —
  a different binding mechanism, or genuinely dead/reused-from-elsewhere
  content.
- What `0x260` (the mode flag switching between the two `0xa4`/`0xa5`/
  `0xa7`-vs-`0xa7`-alone key sets) actually represents (menu open? text
  entry? a different screen state?) — not traced.
- What `engine+0x14a7a` unlocks when the hidden sequence completes.
- Pointer/touch input (`CONE!84`, `HandlePointerBufferReadyL`) — the
  N-Gage has no touchscreen, so this import is very likely dead weight for
  this device, but wasn't checked for callers to confirm.
- Whether the `stringtable.*` format generalizes to all of the game's UI/
  dialogue text (very likely, given SimKin dialogue scripts reference
  numeric-looking IDs in similar ranges elsewhere) — not surveyed broadly
  this pass, only the specific IDs needed for controls.

## Labels applied

- `AppUi_OfferKeyEventL` (0x10021fc0)
- `InputState_SetButton` (0x1001a6b8)
- `SecretSequence_OnComplete` (0x1001b204)
- `InputState_GetButton` (0x1001a690)
- `InputState_GetButton2` (0x1001a6f0)
- `InputState_GetButtonPrev` (0x1001a6dc)
- `InputState_ResolveBindingOffset` (0x1001a67c)
- `InputState_SetBoundButton` (0x1001a6c4)
- `InputState_GetBoundButton` (0x1001a724)
- `InputState_GetBoundButtonPrev` (0x1001a700)
- `InputState_ClearAll` (0x1001a744)
- `InputState_RegisterBinding` (0x1001a770)
- `InputState_InitDefaultBindings` (0x1001a064)

Tools: `pyghidra_label_input_handling.py`, `pyghidra_label_input_state_class.py`,
`pyghidra_find_reads_range.py` (scans a whole contiguous offset range in one
pyghidra session instead of one offset per invocation),
`tools/parse_string_table.py` (parses the `stringtable.*` localized
string-table format, real game asset data).
