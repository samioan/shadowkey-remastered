"""
Census of the vtables that carry a `Save`/`Load` pair, and which of the
thirteen save functions each one names.

M50 established the pair lives at vtable slots +0x130/+0x134; this
enumerates the vtables themselves so the count in SAVE_FORMAT.md is
reproducible rather than remembered. A vtable here is two zero words
followed by a run of >= 40 code pointers, with slot offsets counted from
the *first* of those two zero words -- which is where the object's own
`+0x04` points.

The obvious next step, joining a vtable to its SimKin dispatcher, does
**not** work: no dispatcher address appears in any vtable (only one of the
28 appears as a word anywhere in the image). Dispatchers are reached
through a wrapper instead, so the class hierarchy has to come from the
dispatchers' own tail calls -- see name_save_fields.py's ENTITY_CHAIN.

Run from repo root, after pyghidra_dump_program.py:
  python shadowkey/ghidra/scripts/list_save_vtables.py
"""
import bisect
import struct
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
BIN = REPO_ROOT / "shadowkey" / "extracted" / "6r51_code.bin"
DECOMP = REPO_ROOT / "shadowkey" / "extracted" / "decomp_all.c"

BASE = 0x10000000
# Where the code stops and the string/table data begins. The image's single
# section carries both, so a bare in-range test matches half the strings.
CODE_END = 0x100A5000
SAVE_SLOT = 0x130
LOAD_SLOT = 0x134

SAVE_FUNCS = {
    0x10066614: "Entity", 0x10067DB0: "Drawable", 0x1006D35C: "Item",
    0x1002ED3C: "Stackable", 0x10047584: "Wearable", 0x1002EAA0: "Weapon",
    0x10028A60: "Spellbook", 0x10005164: "InventoryHolder",
    0x10086514: "Actor", 0x10006F90: "LinkedActor",
    0x1001E754: "Character", 0x10043308: "Player",
}

data = BIN.read_bytes()
funcs = sorted(int(line.split()[2], 16)
               for line in DECOMP.read_text(encoding="utf-8", errors="replace").splitlines()
               if line.startswith("// ==== "))


def word(addr):
    return struct.unpack_from("<I", data, addr - BASE)[0]


def is_code(v):
    return BASE <= v < CODE_END and (v & 3) == 0


def owner(addr):
    i = bisect.bisect_right(funcs, addr) - 1
    return f"{funcs[i]:08x}" if i >= 0 else "?"


vtables = []
i = 0
while i < len(data) - 8:
    if struct.unpack_from("<II", data, i) == (0, 0):
        start = BASE + i
        n = 0
        while start + 8 + n * 4 < BASE + len(data) - 4 and is_code(word(start + 8 + n * 4)):
            n += 1
        if n >= 40:
            vtables.append((start, n))
            i += 8 + n * 4
            continue
    i += 4

print(f"{len(vtables)} vtables\n")
counts = {}
for start, n in vtables:
    if n * 4 + 8 <= SAVE_SLOT:
        continue
    sv = word(start + SAVE_SLOT)
    ld = word(start + LOAD_SLOT)
    name = SAVE_FUNCS.get(sv)
    if not name:
        continue
    counts[name] = counts.get(name, 0) + 1
    print(f"  {start:08x}  {name:<16} save {sv:08x}  load {ld:08x}")

print("\nvtables per save class:")
for name, c in sorted(counts.items(), key=lambda kv: -kv[1]):
    print(f"  {name:<16} {c}")
print(f"  total {sum(counts.values())}")
