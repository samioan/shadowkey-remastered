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

The core architecture is now understood end to end, registration through
runtime dispatch (see "The real name-resolution mechanism" and "The
runtime dispatch mechanism" below) — this is still not a *complete*
inventory of the ~700-entry native API surface, but the "how does it
work" question is fully answered, both the write side (startup
registration) and the read side (a script call resolving to a handler).

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

## The runtime dispatch mechanism: how a trie index reaches its handler

The previous open question — "once a script call resolves to a trie
index, what happens next" — is now answered. Right after
`SimKinNameTrie_Insert` in the binary's address space sits a small
(136-byte) sibling with the *same* 28-distinct-caller count:
**`SimKinNameTrie_Lookup`** (renamed from `FUN_1000df3c`, 0x1000df3c).
It walks the identical node structure the same case-insensitive way, but
read-only — no node creation. It returns `false` if any node on the path
is missing, or if the terminal node's value is still `-1`
(unregistered); otherwise it writes the resolved index through an
out-param and returns `true`. This is the read counterpart to
`SimKinNameTrie_Insert`, and it's the missing link between "a name got
registered into a trie at startup" and "a script call by that name does
something."

Two of its 28 callers were decompiled and turn out to be **exactly the
"chained property-setter dispatcher" functions found earlier**
(`FUN_10046328`, the fallthrough target of the `SetSpellType` handler)
plus its own fallthrough target (`FUN_1002c848`):

```c
// FUN_10046328 — confirmed per-class member dispatcher
undefined4 FUN_10046328(void *this, Atom *nameAtom, ArgList *args, Atom *result, ...) {
    wchar_t *name = nameAtom->str ? *nameAtom->str : kDefaultName;
    int index;
    if (!SimKinNameTrie_Lookup(*(this->registry) + 0x14dc8, name, &index))
        return FUN_1002c848(this, nameAtom, args, result, ...);   // next class in the chain

    switch (index) {               // 9 cases (0-8), inline get/set logic
    case 2: case 6:                // SIMKIN_AtomToInt(args[0]) -> this+0x1d0 or 0x1d2
    case 3:                        // SIMKIN_AtomToInt(args[0]) -> this+0x1b0
    ...
    }
    return 1;
}
```

`FUN_1002c848` is byte-for-byte the same shape, using its *own* separate
trie root (`*(this->registry) + 0x14d74`, a different offset — its own
class, not shared with `FUN_10046328`'s), and falls through to
`FUN_1006ca90` on a miss.

This resolves the whole architecture: the ~28 sibling functions that
call `SimKinNameTrie_Insert` at startup are the *same* ~28 functions
(one-to-one) as a chain of "class member dispatcher" functions found
among `SimKinNameTrie_Lookup`'s callers. **Each of the ~28 classes gets
its own separate trie**, not one shared 700-entry trie — the "~700
total bindings" figure is a sum across ~28 small per-class tries, each
with its root pointer stored at its own small offset (0x14cf0, 0x14d74,
0x14dc8, ... — a tightly packed cluster, consistent with a table of
~28 pointer-sized slots) within a shared registry object every dispatcher
reaches via a `this+0x44` backpointer. A script call by name walks the
dispatcher chain class by class; each class's dispatcher does one trie
lookup scoped to just its own members, and on a match immediately
`switch`es on the resolved index straight into inline get/set logic —
no separate step in between. `this+0x44`, the shared backpointer every
per-class object holds, looks like the same unified "engine" object
established elsewhere in this project (`WORLD_MODEL.md`'s `CMap`/engine
identity), though that's not independently confirmed for this specific
offset — worth checking if the port work ever needs it.

The earlier "SetSpellType"/"SetSprite" `wcscmp`-based handlers
(`0x1002e434`, `0x1007ef24`) sit *ahead* of this trie-based dispatch in
the same chain — one hardcoded name check, then fall through to the
generic per-class trie dispatcher on a miss. Why a small number of
members get a hardcoded fast-path check instead of just being in the
trie like everything else is unclear (hottest properties? added after
the trie infrastructure existed?) — not resolved, low priority.

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

- Only 2 of ~28 SimKin native-binding-registration functions (and their
  matching 2 of ~28 dispatcher functions) were individually
  examined/named — the full ~700-entry API surface (every name, its
  assigned per-class trie index, and which of the ~28 classes it belongs
  to) hasn't been enumerated. Doing so is now mechanical (each
  registration function's `SimKinNameTrie_Insert` call sites give the
  name+index pairs directly, in raw disassembly), just a lot of them —
  the address clusters for both the registration functions
  (`0x10010804`-`0x1001537c`) and likely a parallel cluster for the
  dispatcher functions are already known.
- What object each of the ~28 classes actually *is* (spell? item? actor?
  UI menu?) — not identified for any of them beyond weak field-offset
  guessing (e.g. `FUN_10046328`'s class touches fields at `this+0x1b0`/
  `+0x1d0`/`+0x1d2`/`+0x1cc`/`+0x170`, consistent with something
  spell/equipment-related given `SetSpellType` chains into it, but not
  confirmed against a known struct).
- `SIMKIN_ord237`/`ord101`/`ord85` — all broadly used (74-91 call sites,
  26-34 distinct callers each) — still not confirmed, but `FUN_10046328`'s
  case 0 (a getter, gated on a "has string" flag at `this+0x1d4`) gives
  weak new hypotheses for two of them, from this line pair:
  `SIMKIN_ord101(&outAtom, wcharPtr, wcslen(wcharPtr)); SIMKIN_ord38(result, &outAtom);`
  — `ord101` looks like a `MakeStringAtom` variant that takes an explicit
  length instead of relying on a NUL terminator, and `ord38` looks like
  "store an already-built atom into the dispatcher's result out-param."
  Case 1 calls `SIMKIN_ord85` on an atom and gets back a single byte,
  consistent with an `AtomToBool`/`AtomToByte` reader alongside
  `SIMKIN_AtomToInt`. None of these three are renamed — one example each
  isn't enough confidence, and `ord237` wasn't seen in either dispatcher
  examined this pass.
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
- `SimKinNameTrie_Lookup` (0x1000df3c)
- `FUN_10046328`/`FUN_1002c848` — not renamed (class identity unknown),
  commented as confirmed per-class dispatcher chain links

Tools: `pyghidra_label_simkin_bridge.py`, `pyghidra_label_simkin_trie.py`,
`pyghidra_label_atom_to_int.py`, `pyghidra_label_simkin_dispatch.py`.
New general-purpose tool: `pyghidra_list_funcs_near.py <hex-lo> <hex-hi>`
(lists every function in an address range with its size and distinct
caller count — this is how `SimKinNameTrie_Lookup` was spotted next to
`SimKinNameTrie_Insert`, by matching caller counts). Survey tool:
`pyghidra_simkin_ordinal_stats.py` (one-shot call-site/caller-count
report for every SIMKIN ordinal — the source of the numbers throughout
this doc). `pyghidra_find_string_and_refs.py` now checks both ASCII and
UTF-16LE (an earlier ASCII-only version produced the false "dead end"
this doc opens with).
