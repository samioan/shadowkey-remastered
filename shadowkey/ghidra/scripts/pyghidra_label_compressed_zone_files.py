"""
Label the zlib-compressed per-zone file loader chain, found while tracing
GameEngine_InitLevel further -- see docs/ZONE_FORMAT.md's "Compressed
per-zone files" section. Corrects an earlier claim that engine+0x62c's
model pointer came from <zone>.zon -- it's actually <zone>.zsk, loaded
through this chain.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_compressed_zone_files.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LABELS = {
    0x1002778C: (
        "WholeFile_Load",
        "Generic per-zone compressed-file loader (.ztx/.zmp/.zlu/.zfg/.zcp/\n"
        ".zsk all go through this). On-disk format: first 4 bytes = u32 LE\n"
        "decompressed size, rest = a raw zlib stream (EZLIB__uncompress).\n"
        "Verified against a real azra.zsk: header decodes to 132624, and\n"
        "Python zlib.decompress() on the remaining bytes produces exactly\n"
        "132624 bytes. See docs/ZONE_FORMAT.md.",
    ),
    0x1001B8D4: (
        "Bullseye_Init",
        "First step of the 'bullseye' subsystem (likely AI navigation/\n"
        "pathfinding) init sequence in GameEngine_InitLevel -- bracketed by\n"
        "the 'InitLevel Pre bullseye init' debug marker. Consumes the\n"
        ".sur-loaded data. See docs/ZONE_FORMAT.md.",
    ),
    0x1000E840: (
        "Bullseye_InitMap",
        "Second step of the 'bullseye' subsystem init sequence (after\n"
        "Bullseye_Init) -- bracketed by 'InitLevel Pre bullseye init_map'.\n"
        "Consumes .sur header fields. Followed by loading <zone>.zcp\n"
        "('bullseye load_map') and a calc_lights step. See docs/ZONE_FORMAT.md.",
    ),
}

pyghidra.start(install_dir=GHIDRA_INSTALL_DIR)

with pyghidra.open_program(
    binary_path=None,
    project_location=str(PROJECT_DIR),
    project_name="ShadowkeyProject",
    program_name="6r51_code.bin",
    analyze=False,
    nested_project_location=False,
) as flat_api:
    program = flat_api.getCurrentProgram()
    fm = program.getFunctionManager()
    addr_factory = program.getAddressFactory()
    from ghidra.program.model.symbol import SourceType

    with program.openTransaction("label compressed zone file chain"):
        for addr_int, (name, comment) in LABELS.items():
            addr = addr_factory.getAddress(hex(addr_int))
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            if func.getName() != name:
                func.setName(name, SourceType.USER_DEFINED)
            existing = func.getComment() or ""
            if comment not in existing:
                new_comment = (existing + "\n\n" + comment).strip() if existing else comment
                func.setComment(new_comment)
            print(f"labeled {hex(addr_int)} -> {name}")

print("done")
