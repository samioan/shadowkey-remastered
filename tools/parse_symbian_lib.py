#!/usr/bin/env python
"""
Parses a GNU `ar`-format Symbian import library (.LIB, e.g. EUSER.LIB from
a period Series 60 SDK's \\epoc32\\release\\armi\\urel\\ directory -- the
REAL ARM-hardware-target import library; NOT the \\epoc32\\release\\wins\\
or \\winsb\\ one, which is a real Win32 DLL/import-lib for the emulator
target and uses a completely different, incompatible ordinal numbering
scheme for the same named functions) into a complete, exact ordinal ->
mangled-symbol-name table.

Format, reverse-engineered from S60 SDK v0.9's EUSER.LIB (verified against
independent evidence -- see below): standard GNU `ar` archive
("!<arch>\\n" magic, 60-byte member headers). The first couple of members
are metadata (named "dt.o", "dh.o" -- DLL table/header) and the special
"/" symbol-index member; skip those. Every other member is named exactly
"ds<N>.o" where **N is decimal ordinal number itself**, e.g. "ds01582.o"
is the entire content of the import stub for ordinal 1582 -- no need to
rely on archive *position* as a proxy for ordinal at all. Each such
member is a tiny COFF-ish object containing the mangled symbol name as a
plain ASCII string 3 times (bare, "__imp_"-prefixed, "_imp__"-prefixed);
this extracts the bare one.

Verified two ways against independent evidence before trusting this:
  - EUSER ordinal 7 (member "ds00007.o") -> "AddBefore__15TDblQueLinkBaseP15TDblQueLinkBase",
    exactly matching the name (just not the ordinal) already known from
    EKA2L1's epoc6.def catalog (see resolve_imports memory/docs).
  - EUSER ordinal 1582 (member "ds01582.o") -> "__nw__5CBaseUi" (Symbian
    mangling for CBase::operator new(TUint)) -- confirms, from a real
    source this time, what call-site *behavior* alone had already
    suggested for EUSER_ord1582 in 6r51.app (see resolve_imports.py /
    docs/E32IMAGE_FORMAT.md's NewApplication worked example).
  - All 1679 EUSER ordinals present, zero gaps in [1, 1679].

Usage:
    python parse_symbian_lib.py <EUSER.LIB> [--json out.json]
"""
import argparse
import json
import re
import sys

AR_MAGIC = b"!<arch>\n"
HEADER_LEN = 60
MEMBER_RE = re.compile(r"ds(\d+)\.o/?$")
STRING_RE = re.compile(rb"[\x20-\x7e]{3,}")
NOISE = re.compile(rb"\.(text|data|bss|idata\$)|_head_")


def iter_ordinal_members(data: bytes):
    """Yields (ordinal, content) for every 'ds<N>.o' member."""
    assert data[:8] == AR_MAGIC, "not a GNU ar archive"
    pos = 8
    while pos + HEADER_LEN <= len(data):
        header = data[pos:pos + HEADER_LEN]
        name = header[0:16].decode("ascii").strip()
        size = int(header[48:58].decode("ascii").strip())
        if header[58:60] != b"`\n":
            break  # not a valid header -- stop
        content = data[pos + HEADER_LEN: pos + HEADER_LEN + size]
        pos += HEADER_LEN + size
        if pos % 2:
            pos += 1  # members are 2-byte aligned
        m = MEMBER_RE.match(name)
        if m:
            yield int(m.group(1)), content


def extract_symbol_name(content: bytes) -> str:
    """The bare mangled name: a printable string that isn't a COFF section
    name and isn't one of the '_imp'-prefixed import-address variants."""
    candidates = [
        s for s in STRING_RE.findall(content)
        if not NOISE.search(s) and b"imp_" not in s[:8]
    ]
    if not candidates:
        return ""
    return min(candidates, key=len).decode("ascii")  # bare name is the shortest of the 3 variants


def parse_lib(path: str) -> dict:
    """Returns {ordinal: symbol_name}."""
    data = open(path, "rb").read()
    return {ordinal: extract_symbol_name(content) for ordinal, content in iter_ordinal_members(data)}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path")
    ap.add_argument("--json", metavar="OUT.json")
    args = ap.parse_args()

    table = parse_lib(args.path)
    ordinals = sorted(table)
    print(f"{len(table)} ordinals, range [{ordinals[0]}, {ordinals[-1]}]")
    missing = [o for o in range(ordinals[0], ordinals[-1] + 1) if o not in table]
    if missing:
        print(f"WARNING: {len(missing)} missing ordinals in range: {missing[:20]}")
    for o in ordinals[:10]:
        print(f"  {o:5d}  {table[o]}")
    print("  ...")
    for o in ordinals[-5:]:
        print(f"  {o:5d}  {table[o]}")

    if args.json:
        with open(args.json, "w") as f:
            json.dump(table, f, indent=2)
        print(f"\nWrote {args.json}")


if __name__ == "__main__":
    sys.exit(main())
