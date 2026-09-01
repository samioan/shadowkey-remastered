"""
Parser for Shadowkey's localized string table format (real game asset
data, e.g. system/apps/6r51/stringtable.eng -- NOT extracted from the
binary, NOT committed to git per the project's scope rules).

Format (verified against a real stringtable.eng, 4082 entries, exact
byte-for-byte consumption of the file):

    u32 count
    repeat count times:
        u32 charCount        (includes a trailing UTF-16 NUL)
        charCount x uint16   (UTF-16LE, last one is 0x0000)

Index is 0-based and IS the resource-string ID used throughout the game's
native code and SimKin bindings (e.g. InputState's per-action-slot IDs,
0xced-0xd16, resolved via this table -- see docs/INPUT_HANDLING.md).

Usage:
    python tools/parse_string_table.py <stringtable.xxx> --range 0xced 0xd17
    python tools/parse_string_table.py <stringtable.xxx> --id 0xd05
    python tools/parse_string_table.py <stringtable.xxx> --dump-all
"""
import argparse
import struct


def parse(path):
    with open(path, "rb") as f:
        data = f.read()
    (count,) = struct.unpack_from("<I", data, 0)
    strings = []
    off = 4
    for _ in range(count):
        (n,) = struct.unpack_from("<I", data, off)
        off += 4
        raw = data[off : off + n * 2]
        off += n * 2
        strings.append(raw.decode("utf-16-le", errors="replace").rstrip("\x00"))
    assert off == len(data), f"size mismatch: consumed {off}, file is {len(data)}"
    return strings


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--id", type=lambda s: int(s, 0), help="print one string by index")
    ap.add_argument("--range", nargs=2, type=lambda s: int(s, 0), metavar=("LO", "HI"),
                     help="print strings [LO, HI)")
    ap.add_argument("--dump-all", action="store_true")
    args = ap.parse_args()

    strings = parse(args.path)
    print(f"{len(strings)} strings, {args.path}")

    if args.id is not None:
        print(f"{args.id:#x}: {strings[args.id]!r}")
    if args.range:
        lo, hi = args.range
        for i in range(lo, min(hi, len(strings))):
            print(f"{i:#x}: {strings[i]!r}")
    if args.dump_all:
        for i, s in enumerate(strings):
            print(f"{i:#x}: {s!r}")


if __name__ == "__main__":
    main()
