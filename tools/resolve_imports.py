#!/usr/bin/env python
"""
Resolves every imported-DLL call site in 6r51.app's code section to the
(dll, ordinal) it actually calls, using a structural trick rather than any
period Symbian SDK .def file:

Every call to an imported function goes through a fixed 12-byte "IAT
thunk" veneer placed in the code section:

    e59fc004   ldr  ip, [pc, #4]     ; load the *address* of this import's IAT slot
    e59cc000   ldr  ip, [ip]         ; load the *resolved function pointer* from that slot
    e12fff1c   bx   ip               ; tail-call it
    <4 bytes>  literal: absolute address of the IAT slot itself

The 12-byte instruction prefix is byte-for-byte identical across every
thunk (only the trailing literal differs), so thunks can be found with a
plain byte search -- no disassembly needed.

The literal in each thunk points into the Import Address Table, the flat
array of resolved-pointer slots living in [iTextSize, iCodeSize) of the
code section (confirmed: (iCodeSize - iTextSize) / 4 == 417 slots for 415
actual imports, 2 slots of padding). Slot index N corresponds exactly to
the Nth import in declaration order across the import section (DLL block
by DLL block, ordinal by ordinal within each block) -- verified against
6r51.app: all 415 indices 0..414 are hit by exactly one thunk each, no
gaps, no collisions.

So: thunk's literal -> IAT slot index -> (dll, ordinal) via the import
section's own declaration order. That's a full call-site resolution table
with zero external reference data.

Usage:
    python resolve_imports.py <path-to-6r51.app> [--json out.json]
"""
import argparse
import json
import struct
import sys

import e32image as e32

THUNK_PREFIX = bytes.fromhex("04c09fe500c09ce51cff2fe1")


def build_flat_import_list(imports):
    """[(dll, ordinal), ...] in exact IAT declaration order."""
    flat = []
    for imp in imports:
        for ordinal in imp["ordinals"]:
            flat.append((imp["dll"], ordinal))
    return flat


def find_thunks(data: bytes, h: dict, flat_imports: list) -> dict:
    """Returns {thunk_abs_addr: {"dll": ..., "ordinal": ..., "iat_slot_addr": ...}}."""
    code = data[h["iCodeOffset"]: h["iCodeOffset"] + h["iCodeSize"]]
    iat_base_abs = h["iCodeBase"] + h["iTextSize"]
    iat_end_abs = h["iCodeBase"] + h["iCodeSize"]

    thunks = {}
    start = 0
    while True:
        i = code.find(THUNK_PREFIX, start)
        if i == -1:
            break
        start = i + 1
        literal = struct.unpack_from("<I", code, i + 12)[0]
        if not (iat_base_abs <= literal < iat_end_abs) or (literal - iat_base_abs) % 4 != 0:
            continue  # not a real thunk (byte coincidence) -- none observed in practice
        idx = (literal - iat_base_abs) // 4
        if idx >= len(flat_imports):
            continue  # padding slot, not a real import
        dll, ordinal = flat_imports[idx]
        thunk_abs = h["iCodeBase"] + i
        thunks[thunk_abs] = {"dll": dll, "ordinal": ordinal, "iat_slot_addr": literal}
    return thunks


def resolve(path: str) -> dict:
    data = open(path, "rb").read()
    h = e32.parse_header(data)
    imports = e32.read_import_section(data, h)
    flat = build_flat_import_list(imports)
    thunks = find_thunks(data, h, flat)
    return {
        "path": path,
        "total_imports": len(flat),
        "thunks_found": len(thunks),
        "thunks": {f"{addr:#010x}": v for addr, v in sorted(thunks.items())},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", help="path to 6r51.app")
    ap.add_argument("--json", metavar="OUT.json", help="write the resolved thunk table as JSON")
    args = ap.parse_args()

    result = resolve(args.path)
    print(f"{result['total_imports']} imports declared, {result['thunks_found']} thunks found")
    for addr, info in result["thunks"].items():
        print(f"  {addr}: {info['dll']} ord {info['ordinal']}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump(result, f, indent=2)
        print(f"\nWrote {args.json}")


if __name__ == "__main__":
    sys.exit(main())
