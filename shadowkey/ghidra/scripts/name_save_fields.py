"""
M52: put engine-given names on the save record's unidentified scalars.

M50 pinned every field in a save record by width and position (the
save/load pair agrees on both) but named only the ones a dispatcher or a
shipped script happened to name. This script closes the rest
mechanically, from two sources that are already in the repo:

  1. `shadowkey/simkin_native_bindings.json` -- 702 native bindings,
     each a (class dispatcher, case index, name) triple (M-simkin).
  2. `shadowkey/extracted/decomp_all.c` -- the whole program decompiled
     once by pyghidra_dump_program.py.

For each class dispatcher it splits the decompiled body into `case N:`
blocks, collects every `param_1 + 0xNNN` style field reference inside a
block, and joins the block back to its binding name. The result is an
offset -> {names} table: a field an `Is<X>`/`Set<X>` pair touches and
nothing else is that field, and one that ten cases touch is a false
positive to be discarded by hand.

Run from repo root, after pyghidra_dump_program.py:
  python shadowkey/ghidra/scripts/name_save_fields.py [offset ...]

With no arguments it prints the whole join for every offset the save
records use; with offsets (hex, e.g. 8c 1bc) it prints just those, with
the matching source lines.
"""
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
DECOMP = REPO_ROOT / "shadowkey" / "extracted" / "decomp_all.c"
BINDINGS = REPO_ROOT / "shadowkey" / "simkin_native_bindings.json"

# Every offset the M50 record layers carry, by layer, so the report reads
# in the same order as save_records.h.
SAVE_OFFSETS = {
    "Entity": [0x4A, 0x60, 0x6C, 0x6D, 0x70, 0x74, 0x78, 0x7C, 0x80, 0x86,
               0x8C, 0x8E, 0x90, 0x92, 0x93, 0x94, 0x9C, 0xA4, 0xA8, 0xB2,
               0xB6, 0xC4, 0xCA, 0xCB, 0xD5, 0xD8, 0xD9, 0xDC, 0xE0, 0xE2,
               0x10A, 0x10C, 0x110, 0x114, 0x115, 0x118, 0x11C],
    "Drawable": [0x5E, 0x12C, 0x130],
    "Item": [0x180, 0x19C, 0x1A0, 0x1A7],
    "Stackable": [0x1C4],
    "Wearable": [0x1D4],
    "Weapon": [0x1CC, 0x1CE, 0x1D0],
    "Spellbook": [0x160, 0x16C, 0x180, 0x181],
    "InventoryHolder": [0x1B9, 0x1BC, 0x1C0, 0x1C4, 0x1E1, 0x1E2, 0x1E4, 0x1E8],
    "Actor": [0x2A8, 0x2AC, 0x2EC],
    "LinkedActor": [0x224, 0x228],
    "Character": [0x22C, 0x358, 0x359, 0x35A, 0x35C, 0x360, 0x364, 0x368,
                  0x36C, 0x370, 0x374, 0x378, 0x37C, 0x380, 0x384, 0x398],
}

# Two dispatchers agree about what lives at `+0x8c` only when one of their
# classes descends from the other -- that is what makes the join
# trustworthy, and what makes scoping it necessary in two separate ways.
#
# The *other* sixteen dispatchers (menus, tables, the game engine, the
# stats block) have unrelated layouts and would contribute pure noise:
# unscoped, Entity+0x70 collects a dozen menu bindings that live at +0x70
# of a completely different object.
#
# And the chain is a **tree, not a line**, so two sibling branches reuse
# the same offsets for different fields: Stackable+0x1bc is an item's
# weight while InventoryHolder+0x1bc is a step toward a move target, and
# both branches independently use +0x180. A field is therefore only
# attributed from dispatchers on its own root-to-leaf path plus that
# path's descendants -- ANCESTORS_AND_BELOW, below.
#
# The chain itself is not assumed -- it is read off the dispatchers, each
# of which tail-calls its base's on a name it does not recognise:
#
#   10061a60 Entity
#    +- 10065a70 Drawable
#        +- 1006ca90 Item          (SetRange, SetPickupable, SetWeaponSprite)
#        |   +- 1002c848 Stackable (CanDrop, DestroyObject)
#        |       +- 1002d3c4 Weapon   (SetDamageMin/Max)
#        |       +- 1002da3c Armor    (SetArmorValue/Type/Constraint)
#        |       +- 10046328 Wearable (SetScroll, SetRefireRate, SetIcon)
#        +- 10028594 Spellbook     (GetDestroy/SetDestroy, Init)
#            +- 1002de24 Spell     (SetMagicDamage, SetSpellLevel)
#            +- 10003810 InventoryHolder
#                +- 10084924 Actor
#                +- 1001e2f8 Character
#                    +- 1003f130 Player
#
# and it matches the save chain in save_records.h layer for layer.
ENTITY_CHAIN = {
    "10061a60": "Entity", "10065a70": "Drawable", "1006ca90": "Item",
    "1002c848": "Stackable", "1002d3c4": "Weapon", "1002da3c": "Armor",
    "10046328": "Wearable", "10028594": "Spellbook", "1002de24": "Spell",
    "10003810": "InventoryHolder", "10084924": "Actor",
    "1001e2f8": "Character", "1003f130": "Player",
}

# Per save layer: the dispatchers whose class shares that layer's slice of
# the object -- the layer's own class, its ancestors (which allocated the
# field) and its descendants (which inherit it). Anything else is a
# different branch and a different field.
_ITEM_BRANCH = ["1006ca90", "1002c848", "1002d3c4", "1002da3c", "10046328"]
_ACTOR_BRANCH = ["10028594", "1002de24", "10003810", "10084924",
                 "1001e2f8", "1003f130"]
_ALL = ["10061a60", "10065a70"] + _ITEM_BRANCH + _ACTOR_BRANCH
LAYER_SCOPE = {
    "Entity": _ALL,
    "Drawable": _ALL,
    "Item": _ITEM_BRANCH,
    "Stackable": _ITEM_BRANCH,
    "Wearable": _ITEM_BRANCH,
    "Weapon": _ITEM_BRANCH,
    "Spellbook": _ACTOR_BRANCH,
    "InventoryHolder": _ACTOR_BRANCH,
    "Actor": _ACTOR_BRANCH,
    # LinkedActor has no bindings of its own; it sits beside Actor under
    # InventoryHolder, so it shares that branch's slice.
    "LinkedActor": _ACTOR_BRANCH,
    "Character": _ACTOR_BRANCH,
}

FUNC_BANNER = re.compile(r"^// ==== ([0-9a-f]{8}) (\S+) ====$", re.M)
# `param_1 + 0x8c`, `*(int *)(param_1 + 0x8c)`, `piVar3[0x23]` is not
# matched on purpose -- an index is only a byte offset after scaling, and
# guessing the scale produces noise.
OFFSET_REF = re.compile(r"\+ (0x[0-9a-f]+)\b")
# `(**(code **)(vt + 0x8c))(...)` is a *vtable slot*, not a field, and it
# is the single biggest source of false names -- Entity+0x8c "means"
# SetUseText only because SetUseText makes a virtual call through slot
# +0x8c. Offsets inside one of these are discarded.
VTABLE_CALL = re.compile(r"\(code \*\*\)\(([^)]*)\)")


def load_functions():
    text = DECOMP.read_text(encoding="utf-8", errors="replace")
    out = {}
    marks = list(FUNC_BANNER.finditer(text))
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
        out[m.group(1)] = (m.group(2), text[m.end():end])
    return out


def split_cases(code):
    """`case N:` .. next `case`/`default`/end, as {index: body}."""
    out = {}
    marks = list(re.finditer(r"^\s*(?:case (0x[0-9a-f]+|\d+)|default):", code, re.M))
    for i, m in enumerate(marks):
        if m.group(1) is None:
            continue
        end = marks[i + 1].start() if i + 1 < len(marks) else len(code)
        idx = int(m.group(1), 0)
        out.setdefault(idx, "")
        out[idx] += code[m.end():end]
    return out


def main():
    funcs = load_functions()
    bindings = json.load(BINDINGS.open())

    # offset -> {name -> [(dispatcher, line), ...]}
    hits = defaultdict(lambda: defaultdict(list))
    for cls in bindings["classes"]:
        addr = cls["dispatcher_addr"]
        if addr not in ENTITY_CHAIN:
            continue
        if addr not in funcs:
            print(f"!! dispatcher {addr} not in the dump", file=sys.stderr)
            continue
        name, code = funcs[addr]
        cases = split_cases(code)
        by_index = defaultdict(list)
        for bname, idx in cls["bindings"].items():
            by_index[idx].append(bname)
        for idx, body in cases.items():
            names = by_index.get(idx)
            if not names:
                continue
            for line in body.splitlines():
                virtual = {o for grp in VTABLE_CALL.findall(line)
                           for o in OFFSET_REF.findall(grp)}
                for off in set(OFFSET_REF.findall(line)) - virtual:
                    for bname in names:
                        hits[int(off, 16)][bname].append(
                            (addr, ENTITY_CHAIN[addr], line.strip()))

    wanted = [int(a, 16) for a in sys.argv[1:]]
    if wanted:
        for off in wanted:
            print(f"=== +0x{off:x}  (every branch) ===")
            for bname, lines in sorted(hits.get(off, {}).items()):
                print(f"  {bname}")
                for _, layer, line in lines[:4]:
                    print(f"      {layer:<16}{line}")
        return

    for layer, offsets in SAVE_OFFSETS.items():
        scope = set(LAYER_SCOPE[layer])
        print(f"\n########## {layer} ##########")
        for off in offsets:
            names = sorted(n for n, ls in hits.get(off, {}).items()
                           if any(d in scope for d, _, _ in ls))
            flag = "" if names else "   (no binding on this branch touches it)"
            print(f"  +0x{off:<5x} {len(names):>2} {', '.join(names[:12])}{flag}")


if __name__ == "__main__":
    main()
