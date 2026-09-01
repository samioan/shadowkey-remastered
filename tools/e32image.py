#!/usr/bin/env python
"""
Parser/extractor for the Symbian EKA1 E32Image executable format
(used by 6r51.app, the Shadowkey N-Gage binary).

There is no compression in this header revision (deflate/bytepair
compression was added later, in the EKA2 "E32ImageHeaderV" extension),
so file offsets in the header point directly at the real section data
-- no decompression pass is needed before extraction.

Struct layout below is transcribed from Symbian's own e32image.h
(the EKA1 / "petran"-era version, e32tools/INC/E32IMAGE.H) and verified
field-by-field against 6r51.app's actual header bytes -- see
docs/E32IMAGE_FORMAT.md for the annotated hex dump this was checked
against.

Usage:
    python e32image.py <path-to.app-or-.dll> [--extract-code out.bin]
"""
import argparse
import json
import struct
import sys

HEADER_FMT = "<4I I I 2I I q I 6i 3I i I i 5I I I"
# breakdown (all little-endian):
#   4I  : iUid1, iUid2, iUid3, iCheck
#   I   : iSignature ('EPOC')
#   I   : iCpu
#   2I  : iCheckSumCode, iCheckSumData
#   I   : iVersion (packed TVersion)
#   q   : iTime (TInt64)
#   I   : iFlags
#   6i  : iCodeSize, iDataSize, iHeapSizeMin, iHeapSizeMax, iStackSize, iBssSize
#   3I  : iEntryPoint, iCodeBase, iDataBase
#   i   : iDllRefTableCount
#   I   : iExportDirOffset
#   i   : iExportDirCount
#   5I  : iTextSize, iCodeOffset, iDataOffset, iImportOffset, iCodeRelocOffset
#   I   : iDataRelocOffset
#   -- iPriority (TProcessPriority, 4 bytes) appended separately below --
HEADER_SIZE = 0x7C

FIELDS = [
    "iUid1", "iUid2", "iUid3", "iCheck",
    "iSignature", "iCpu",
    "iCheckSumCode", "iCheckSumData",
    "iVersion", "iTime", "iFlags",
    "iCodeSize", "iDataSize", "iHeapSizeMin", "iHeapSizeMax", "iStackSize", "iBssSize",
    "iEntryPoint", "iCodeBase", "iDataBase",
    "iDllRefTableCount",
    "iExportDirOffset", "iExportDirCount",
    "iTextSize", "iCodeOffset", "iDataOffset", "iImportOffset", "iCodeRelocOffset",
    "iDataRelocOffset", "iPriority",
]

ECPU_NAMES = {0: "Unknown", 0x1000: "X86", 0x2000: "ARM", 0x4000: "MCore"}


assert struct.calcsize(HEADER_FMT) == HEADER_SIZE, "HEADER_FMT/HEADER_SIZE mismatch"


def parse_header(data: bytes) -> dict:
    if len(data) < HEADER_SIZE:
        raise ValueError("file too small to contain an E32Image header")
    vals = struct.unpack_from(HEADER_FMT, data, 0)
    h = dict(zip(FIELDS, vals))
    if h["iSignature"] != struct.unpack("<I", b"EPOC")[0]:
        raise ValueError(
            f"not an E32Image: expected 'EPOC' signature, got {h['iSignature']:#x}"
        )
    h["iCpuName"] = ECPU_NAMES.get(h["iCpu"], f"?{h['iCpu']:#x}")
    h["iIsDll"] = bool(h["iFlags"] & 0x01)
    h["iNoCallEntryPoint"] = bool(h["iFlags"] & 0x02)
    return h


def read_import_section(data: bytes, h: dict) -> list:
    """Returns [{"dll": name, "ordinals": [...]}] for each imported DLL."""
    off = h["iImportOffset"]
    if off == 0 or h["iDllRefTableCount"] == 0:
        return []
    (section_size,) = struct.unpack_from("<i", data, off)
    pos = off + 4
    imports = []
    for _ in range(h["iDllRefTableCount"]):
        name_off, n_imports = struct.unpack_from("<Ii", data, pos)
        pos += 8
        # iOffsetOfDllName is relative to the start of the import section.
        name_addr = off + name_off
        end = data.index(b"\x00", name_addr)
        dll_name = data[name_addr:end].decode("latin1")
        ordinals = list(struct.unpack_from(f"<{n_imports}I", data, pos))
        pos += 4 * n_imports
        imports.append({"dll": dll_name, "ordinals": ordinals})
    return imports


def read_export_dir(data: bytes, h: dict) -> list:
    off = h["iExportDirOffset"]
    count = h["iExportDirCount"]
    if off == 0 or count <= 0:
        return []
    return list(struct.unpack_from(f"<{count}I", data, off))


def summarize(path: str) -> dict:
    data = open(path, "rb").read()
    h = parse_header(data)
    imports = read_import_section(data, h)
    exports = read_export_dir(data, h)
    return {
        "path": path,
        "file_size": len(data),
        "header": h,
        "imports": imports,
        "exports_ordinal1_based_code_offsets": exports,
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("path", help="path to the .app/.dll E32Image file")
    ap.add_argument(
        "--extract-code",
        metavar="OUT.bin",
        help="dump the raw code section to OUT.bin, plus an OUT.bin.json "
        "sidecar with the load address (iCodeBase) Ghidra should import it at",
    )
    ap.add_argument("--json", action="store_true", help="print full summary as JSON")
    args = ap.parse_args()

    data = open(args.path, "rb").read()
    h = parse_header(data)
    imports = read_import_section(data, h)
    exports = read_export_dir(data, h)

    if args.json:
        print(json.dumps(summarize(args.path), indent=2))
        return

    print(f"{args.path}  ({len(data)} bytes)")
    print(f"  UID1/2/3      : {h['iUid1']:#010x} / {h['iUid2']:#010x} / {h['iUid3']:#010x}")
    print(f"  type          : {'DLL' if h['iIsDll'] else 'EXE'}  cpu={h['iCpuName']}  priority={h['iPriority']}")
    print(f"  code          : base={h['iCodeBase']:#010x} size={h['iCodeSize']:#x} fileOffset={h['iCodeOffset']:#x}")
    print(f"  data          : base={h['iDataBase']:#010x} size={h['iDataSize']:#x} fileOffset={h['iDataOffset']:#x}")
    print(f"  bss           : size={h['iBssSize']:#x}")
    print(f"  entry point   : code-relative offset {h['iEntryPoint']:#x} -> abs {h['iCodeBase'] + h['iEntryPoint']:#010x}")
    print(f"  heap          : min={h['iHeapSizeMin']:#x} max={h['iHeapSizeMax']:#x}  stack={h['iStackSize']:#x}")
    print(f"  export dir    : fileOffset={h['iExportDirOffset']:#x} count={h['iExportDirCount']}")
    for i, code_rel in enumerate(exports, start=1):
        print(f"      ordinal {i}: code-relative {code_rel:#x} -> abs {h['iCodeBase'] + code_rel:#010x}")
    print(f"  imports       : {h['iDllRefTableCount']} DLL(s), section at fileOffset={h['iImportOffset']:#x}")
    for imp in imports:
        print(f"      {imp['dll']}: {len(imp['ordinals'])} ordinal(s) {imp['ordinals']}")

    if args.extract_code:
        code = data[h["iCodeOffset"]: h["iCodeOffset"] + h["iCodeSize"]]
        with open(args.extract_code, "wb") as f:
            f.write(code)
        sidecar = {
            "load_address": h["iCodeBase"],
            "entry_point_abs": h["iCodeBase"] + h["iEntryPoint"],
            "size": len(code),
            "language": "ARM:LE:32:v4t:default" if h["iCpu"] == 0x2000 else "unknown",
            "source_file": args.path,
        }
        with open(args.extract_code + ".json", "w") as f:
            json.dump(sidecar, f, indent=2)
        print(f"\nWrote code section ({len(code)} bytes) to {args.extract_code}")
        print(f"Wrote load metadata to {args.extract_code}.json -- import into Ghidra as raw "
              f"binary at base {h['iCodeBase']:#010x}")


if __name__ == "__main__":
    sys.exit(main())
