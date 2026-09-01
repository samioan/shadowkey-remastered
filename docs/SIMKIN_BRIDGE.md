# The SimKin native bridge

Started per the port-scoping discussion in `ROADMAP.md`: this is the other
big unknown flagged alongside input handling (`INPUT_HANDLING.md`) — how
`6r51.app` drives the third-party SimKin scripting engine (`simkin.dll`,
source public at simkin.co.uk but never fetched — see `IMPORT_NAMES.md`).
Game logic (dialogue, menus, quests, item/monster behavior) lives in the
readable `.s` scripts throughout `system/apps/6r51/`; this doc is about
the native glue code that makes those scripts able to see and drive the
engine, not about decompiling the scripts themselves (which don't need
decompiling — they're already plaintext, in scope to read directly per
`shadowkey_target_scope`).

This is a **first-pass scoping**, not a full resolution — see "What's
still open" for the size of what's left, and the dominant ordinal
(`ord185`, by far the most-used) that the next round should start with.

## Starting point: a name-based dead end

`INPUT_HANDLING.md`'s work on the "Configure Keys" options menu
(`configkeys.s`, which calls native functions `ConfigKeysMenu(page)` and
`ConfigKeysDefault()`) already established one negative result:
**neither name exists as a literal string anywhere in `6r51.app`'s memory
image** (checked with the new `pyghidra_find_string_and_refs.py`,
validated against a known-good string first). So whatever mechanism
exposes native functions to scripts, it does **not** work by the native
side registering literal name strings that a naive search would catch —
either `simkin.dll` hashes names during script compilation and the native
side registers by hash/index, or the registration happens through an
indirect/table-driven mechanism this pass's direct-caller searches can't
see (see "The `ord22`/multi-vtable lead" below).

## What's confirmed: constructing and registering *values*

Reading real call sites (all initially showed the same "decompiler drops
extra args" ambiguity this project has hit repeatedly — `SIMKIN_ord42`'s
call sites looked like they took only 2 arguments; reading the
surrounding code, not just the one call line, made the real pattern
clear) found a clean three-ordinal group for exposing named **constants**
to scripts:

- **`SIMKIN_MakeIntAtom`** (renamed from `SIMKIN_ord42`, ordinal 42 — 143
  call sites across 24 distinct callers) — constructs a SimKin value
  ("atom") object from a literal `int`.
- **`SIMKIN_MakeStringAtom`** (renamed from `SIMKIN_ord43`, ordinal 43 —
  102 call sites across 12 callers) — same, from a literal string
  pointer.
- **`SIMKIN_RegisterConstant`** (renamed from `SIMKIN_ord60`, ordinal 60
  — 119 call sites but only **2** distinct callers, i.e. two functions
  each register many constants in a row) — binds a name atom to a value
  atom with the interpreter instance.

**Verified with a real example**: `GameEngine_FirstTickBootstrap`
(0x10023ad0, already known from `RENDER_LOOP.md`) calls this exact
sequence repeatedly; resolving its string-literal operands
(`pyghidra_read_strings.py`) gives real constant names:

```c
SIMKIN_MakeIntAtom(&val, 1);       SIMKIN_MakeStringAtom(&name, "IPT_Weapon");
SIMKIN_RegisterConstant(interp, &name, &val);   // IPT_Weapon = 1
SIMKIN_MakeIntAtom(&val, 2);       SIMKIN_MakeStringAtom(&name, "IPT_Spell");
SIMKIN_RegisterConstant(interp, &name, &val);   // IPT_Spell = 2
SIMKIN_MakeIntAtom(&val, 0);       SIMKIN_MakeStringAtom(&name, "IPT_Misc");
SIMKIN_RegisterConstant(interp, &name, &val);   // IPT_Misc = 0
SIMKIN_MakeIntAtom(&val, 3);       SIMKIN_MakeStringAtom(&name, "IPT_Armor");
SIMKIN_RegisterConstant(interp, &name, &val);   // IPT_Armor = 3
```

`IPT_*` reads as "item property type" — an item-category enum scripts can
compare against, distinct from (but conceptually similar to)
`ZONE_FORMAT.md`'s `entities.txt`-derived category enum. Not all 24
`SIMKIN_MakeIntAtom` callers were checked individually — this pattern
almost certainly repeats for other constant families elsewhere in the
engine, not surveyed exhaustively this pass.

## The `ord22`/multi-vtable lead (native *function* registration — not confirmed)

Ordinal 22 (10 call sites, 10 distinct callers — a "called exactly once
per caller" signature, unlike the broadly-reused atom constructors above)
is called from small constructor-shaped functions
(e.g. `FUN_1001b300`) that: store 4 caller-supplied values into an
object, write what look like **two or three vtable pointers** into the
same object (the classic GCC-old-ABI multiple-inheritance setup pattern —
several `DAT_` constants written into overlapping small offsets), call
`SIMKIN_ord22` on an embedded sub-object, then `strcpy` a
caller-supplied **name string** into the object. This is structurally
exactly what a "native function/object wrapper exposed to a scripting
engine" usually looks like (SimKin's C++ API most likely requires
implementing one or more abstract interfaces per exposed
function/object, hence the multi-vtable setup) — but **not confirmed**:
these constructor functions have **zero direct callers** findable by the
reference manager, meaning whatever actually invokes them does so
indirectly (through a table of constructor pointers, matching a
per-subsystem "here's my native binding" registration pattern) — the
concrete next step, not chased down this pass.

## What's still open

- **`SIMKIN_ord185` — by far the single most important next target.**
  523 call sites across 34 distinct callers, dwarfing every other SIMKIN
  ordinal (the next-busiest confirmed one, `SIMKIN_MakeIntAtom`, has 143).
  Not characterized at all this pass. Two leading hypotheses: (a) the
  core *native-calls-into-script* dispatcher — every `.s` file defines
  event-handler methods like `Init[...]`/`OnDisplay[...]` (seen
  throughout `configkeys.s` and every other script) that native code must
  invoke by name at runtime, which could easily account for hundreds of
  call sites across dozens of subsystems; or (b) the core
  *script-calls-into-native* dispatcher (the reverse direction, and the
  mechanism that would finally explain how `OpenMenu`/
  `AddFloatingTextJustify`/etc. reach native code despite the name-string
  dead end above). Resolving this ordinal is the clear next step.
- `SIMKIN_ord237`/`ord101`/`ord85` — all broadly used (74-91 call sites,
  26-34 distinct callers each) — not characterized.
- The `ord22`/multi-vtable native-function-registration lead above — not
  confirmed, needs tracing the indirect callers of the constructor
  functions (e.g. `FUN_1001b300`), not a simple reference-manager search.
- `SIMKIN_ord9` — a weak, unconfirmed lead: seen called from a
  destructor-shaped function (`~Class(uint aFlags)`, the classic
  GCC-old-ABI virtual-destructor signature, conditionally `delete`s
  `this`) on what looks like a value/atom object — plausibly "release a
  SimKin atom" but not verified, not renamed.
- Whether every remaining unresolved SIMKIN ordinal name (`IMPORT_NAMES.md`
  never attempted the public SimKin source) is worth resolving before
  going further, or whether behavioral tracing (as done here) is
  sufficient for port purposes without needing the real names.
- No attempt yet to map which specific native functions a real script
  like `configkeys.s` actually needs (`SetPrevMenu`, `MenuBackground`,
  `AddFloatingTextJustify`, `CreatePopupMenu`, `OpenMenu`, `ClearMenu`,
  `Quit`, and dozens more across the full `.s` corpus) — that inventory
  (just reading the scripts, no RE needed) would usefully bound how large
  the bridge's native-function surface actually is for a port.

## Labels applied

- `SIMKIN_MakeIntAtom` (ordinal 42, 0x100a0150)
- `SIMKIN_MakeStringAtom` (ordinal 43, 0x100a0310)
- `SIMKIN_RegisterConstant` (ordinal 60, 0x100a0320)

Tool: `pyghidra_label_simkin_bridge.py`. New survey tool:
`pyghidra_simkin_ordinal_stats.py` (one-shot call-site/caller-count
report for every SIMKIN ordinal — the source of the numbers throughout
this doc, and the fastest way to re-prioritize the "What's still open"
list if more ordinals get characterized later).
