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

The core architecture is now understood end to end (see "The real
name-resolution mechanism" below) — this is still not a *complete*
inventory of the ~700-entry native API surface, but the "how does it
work" question is answered.

## A false start, corrected: the "name-based dead end" was a search bug

`INPUT_HANDLING.md`'s work on the "Configure Keys" options menu
(`configkeys.s`, which calls native functions `ConfigKeysMenu(page)` and
`ConfigKeysDefault()`) reported one negative result: neither name
appeared to exist as a literal string anywhere in `6r51.app`'s memory
image, checked with `pyghidra_find_string_and_refs.py`. **That tool only
searched ASCII bytes.** Re-checking with UTF-16LE added (both names *do*
exist, just as UTF-16LE, like most of this binary's script-visible name
strings) immediately found both — see below. Worth remembering broadly:
**default to checking both encodings when searching this binary for a
literal string**, not just ASCII.

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

## The real name-resolution mechanism: a hand-rolled trie, ~700 entries

Both `"ConfigKeysMenu"` and `"ConfigKeysDefault"` (UTF-16LE) turned out
to be referenced from the exact same function, **`SimKin_
RegisterNativeBindings_1`** (renamed from `FUN_10011008`, 0x10011008 —
called once, from `GameEngine_ctor`). Its body is a long, repeating
pattern:

```c
r0 = context+8;
r1 = <literal UTF-16LE string, e.g. "ConfigKeysDefault">;
r2 = <sequential integer, e.g. 0x11>;
SimKinNameTrie_Insert(r0, r1, r2);
// ... repeated hundreds of times, r2 incrementing by 1 each time ...
```

(This call site is another instance of the "decompiler drops extra args"
issue this project keeps running into — the decompiled C for these calls
showed only one visible argument; the real 3-argument pattern only shows
up in raw disassembly.)

**`SimKinNameTrie_Insert`** (renamed from `FUN_1000de50`, 0x1000de50) is
a genuine hand-rolled **case-insensitive trie (prefix tree)**: 24-byte
nodes (`char@+8`, `value@+0xc` [`-1`=unset], `firstChild@+0x14`,
`nextSibling@+0x10`), walking or creating one node per input character;
at the string's end, if the node's value is still unset it gets set to
the caller's integer and the call returns "newly registered" — otherwise
it returns "duplicate, not overwritten."

**This is the real SimKin name-resolution mechanism**: every
script-visible native binding (method, property, page — `"ConfigKeysDefault"`
got index `0x11`, `"ConfigKeysMenu"` got `0x12`, both inserted back to
back) gets registered into one shared trie at startup, mapped to a small
sequential integer. **703 total call sites** to `SimKinNameTrie_Insert`
were found, across **~28 sibling registration functions**
(address range `0x10010804`-`0x1001537c`, all ultimately called from
`GameEngine_ctor`) — i.e. **the game's entire SimKin-visible native API
surface is on the order of ~700 bindings**, all resolved through this one
trie rather than by string comparison at call time. This also explains
why the earlier per-property dispatch chains (`SetSpellType`, `SetSprite`
— see below) *also* do their own `wcscmp` check: the trie resolves a
name to an index for one broad category (e.g. "which object class"), and
the linear compare-chain then finds the *specific* member within that
class — two related but distinct dispatch layers, not one.

Only 2 of the ~28 sibling registration functions were individually named
this pass (`SimKin_RegisterNativeBindings_1` plus the confirming example
above); the other ~27 are un-renamed but now easy to find (same address
cluster, same `SimKinNameTrie_Insert`-calling shape).

## `SIMKIN_AtomToInt` (resolved — was the dominant unknown ordinal)

**`SIMKIN_AtomToInt`** (renamed from `SIMKIN_ord185`, ordinal 185 — 523
call sites across 34 distinct callers, by far the busiest SIMKIN
ordinal) extracts a native integer from a SimKin value/argument atom —
the read counterpart to `SIMKIN_MakeIntAtom`'s construction. Confirmed
via two real, near-identical native property-setter handlers found among
its callers:

```c
// handles the "SetSpellType" member (0x1002e434):
if (memberName == L"SetSpellType") {
    if (args.count == 0) Leave(-2);
    short v = SIMKIN_AtomToInt(args[0]);
    this->spellType /* +200 */ = v;
    return handled;
}
return NextHandlerInChain(this, memberName, args, ...);   // e.g. FUN_10046328
```

The second example (`0x1007ef24`) is byte-for-byte the same shape for a
member called `"SetSprite"`, storing a 32-bit result instead of 16-bit.
Both delegate to another function in the same caller list on a mismatch
— i.e. these are **chained native property-setter dispatchers**, one
function per property, falling through to the next property's handler
until one matches or the chain is exhausted. This is the confirmed
mechanism for *script calls into native, per-object-class property
setters* — a second dispatch layer underneath the name trie above.

## What's still open

- Only 2 of ~28 SimKin native-binding-registration functions were
  individually examined/named — the full ~700-entry API surface (every
  name + its assigned trie index) hasn't been enumerated. Doing so would
  need either walking the trie's insert call sites across all ~28
  functions (mechanical, but a lot of them), or reconstructing it at
  runtime by instrumenting/tracing the real game (out of scope for static
  RE alone).
- The *dispatch* half of the trie: once a script reference resolves to a
  trie index, what actually happens next (how the index leads to the
  right chained property-setter dispatcher, or the right method call) —
  not traced. The property-setter chains (`SetSpellType`/`SetSprite`
  above) were found via `SIMKIN_AtomToInt`'s caller list, not via
  following the trie's output.
- `SIMKIN_ord237`/`ord101`/`ord85` — all broadly used (74-91 call sites,
  26-34 distinct callers each) — not characterized. Possibly related to
  the property-*getter* direction (the read counterpart to the setter
  chains above), not confirmed.
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
  `Quit`, and dozens more across the full `.s` corpus) against the ~700
  registered names — that cross-check (just reading the scripts, no RE
  needed) would usefully validate the ~700 count and identify which
  bindings actually matter for a minimal port.

## Labels applied

- `SIMKIN_MakeIntAtom` (ordinal 42, 0x100a0150)
- `SIMKIN_MakeStringAtom` (ordinal 43, 0x100a0310)
- `SIMKIN_RegisterConstant` (ordinal 60, 0x100a0320)
- `SIMKIN_AtomToInt` (ordinal 185, 0x100a0110)
- `SimKinNameTrie_Insert` (0x1000de50)
- `SimKin_RegisterNativeBindings_1` (0x10011008)

Tools: `pyghidra_label_simkin_bridge.py`, `pyghidra_label_simkin_trie.py`,
`pyghidra_label_atom_to_int.py`. Survey tool:
`pyghidra_simkin_ordinal_stats.py` (one-shot call-site/caller-count
report for every SIMKIN ordinal — the source of the numbers throughout
this doc). `pyghidra_find_string_and_refs.py` now checks both ASCII and
UTF-16LE (an earlier ASCII-only version produced the false "dead end"
this doc opens with).
