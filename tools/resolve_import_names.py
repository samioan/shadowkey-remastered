#!/usr/bin/env python
"""
Produces the final ordinal -> real mangled name table for every import
6r51.app actually calls, by cross-referencing resolve_imports.py's
(dll, ordinal) list against a period Series 60 SDK's real ARM-hardware
import libraries (parse_symbian_lib.py).

The SDK itself is NOT part of this repo (third-party copyrighted install
media -- see docs/IMPORT_NAMES.md) and must be supplied externally: point
--sdk-libs-dir at a directory containing the plain-named .LIB files from
a period SDK's \\epoc32\\release\\armi\\urel\\ (or \\thumb\\urel\\ --
identical ordinals, just a different instruction-set target) directory,
e.g. EUSER.LIB, CONE.LIB, EIKCORE.LIB, etc. (NOT the \\wins\\/\\winsb\\
emulator-target ones, which use an incompatible ordinal scheme).

Output is small and derived (just the ~415 (dll, ordinal) -> name pairs
6r51.app actually references, not a redistribution of the SDK), safe to
commit.

Usage:
    python resolve_import_names.py <path-to-6r51.app> <sdk-libs-dir> --json shadowkey/import_names.json
"""
import argparse
import json
import sys

import resolve_imports
import parse_symbian_lib

# DLLs this can resolve names for -- core Symbian OS libraries with a
# real .LIB in a period SDK. SIMKIN (third-party), GAMECOMMS/NOKIAFC
# (N-Gage-platform-specific) are deliberately not in this list -- there's
# no SDK .LIB for them to resolve against.
RESOLVABLE_DLLS = {
    "EUSER", "CONE", "EIKCORE", "AVKON", "APPARC", "BITGDI", "GDI", "WS32",
    "ESOCK", "ETEL", "EFSRV", "ESTLIB", "FBSCLI", "MSGS", "HAL", "EZLIB",
}


def dll_short_name(dll_field: str) -> str:
    return dll_field.split("[")[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("app_path", help="path to 6r51.app")
    ap.add_argument("sdk_libs_dir", help="directory with EUSER.LIB, CONE.LIB, etc.")
    ap.add_argument("--json", metavar="OUT.json")
    args = ap.parse_args()

    result = resolve_imports.resolve(args.app_path)
    needed_by_dll = {}
    for info in result["thunks"].values():
        short = dll_short_name(info["dll"])
        needed_by_dll.setdefault(short, set()).add(info["ordinal"])

    resolved = {}  # "DLL!ordinal" -> name
    unresolved_dlls = []
    for short, ordinals in sorted(needed_by_dll.items()):
        if short not in RESOLVABLE_DLLS:
            unresolved_dlls.append((short, len(ordinals)))
            continue
        import os
        lib_path = os.path.join(args.sdk_libs_dir, f"{short}.LIB")
        if not os.path.exists(lib_path):
            print(f"WARNING: {lib_path} not found, skipping {short} ({len(ordinals)} ordinals)")
            continue
        table = parse_symbian_lib.parse_lib(lib_path)
        hits, misses = 0, 0
        for ordinal in sorted(ordinals):
            name = table.get(ordinal)
            if name:
                resolved[f"{short}!{ordinal}"] = name
                hits += 1
            else:
                misses += 1
        print(f"{short}: {hits}/{len(ordinals)} ordinals resolved" + (f" ({misses} missing from SDK table)" if misses else ""))

    if unresolved_dlls:
        print("\nNot resolvable (no SDK .LIB source for these):")
        for short, count in unresolved_dlls:
            print(f"  {short}: {count} ordinals")

    print(f"\ntotal resolved: {len(resolved)} / {sum(len(v) for v in needed_by_dll.values())} imported ordinals")

    if args.json:
        with open(args.json, "w") as f:
            json.dump(resolved, f, indent=2, sort_keys=True)
        print(f"Wrote {args.json}")


if __name__ == "__main__":
    sys.exit(main())
