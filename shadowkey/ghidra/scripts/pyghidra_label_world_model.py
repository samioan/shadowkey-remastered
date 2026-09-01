"""
Apply the "world is a 2D tile grid" finding as real labels. See
docs/WORLD_MODEL.md for the full writeup/evidence.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LABELS = {
    0x1001b004: (
        "Map_GetTileAt",
        "GetTileAt(CMap* map, TInt xFixed8_8, TInt yFixed8_8) -> tile "
        "record pointer or 0. Bounds-checks x/y (each an 8.8 fixed-point "
        "coordinate, >>8 to get the tile-integer index) against "
        "map+0x2c (width in tiles) / map+0x30 (height in tiles), computes "
        "a row-major index (width*y + x), and indexes into a per-cell "
        "tile array at map+0x6908 with an 8-byte stride. Called from 30+ "
        "call sites all over the binary -- this is the engine's central "
        "'what's at this world position' primitive. The world is a "
        "simple 2D tile grid, not open 3D geometry -- fits the 'Elder "
        "Scrolls Travels' series' simplified dungeon-crawler design "
        "(vs. mainline Elder Scrolls). See docs/WORLD_MODEL.md.",
    ),
    0x1000fa7c: None,  # already labeled GameEngine_ctor; see WORLD_MODEL.md addendum comment below
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
    listing = program.getListing()

    with program.openTransaction("label world model functions"):
        from ghidra.program.model.symbol import SourceType
        from ghidra.program.model.listing import CodeUnit

        for addr_int, entry in LABELS.items():
            if entry is None:
                continue
            name, comment = entry
            addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"SKIP {hex(addr_int)}: not a function")
                continue
            try:
                func.setName(name, SourceType.USER_DEFINED)
            except Exception as e:
                print(f"FAILED to rename {hex(addr_int)}: {e}")
                continue
            listing.setComment(addr, CodeUnit.PLATE_COMMENT, comment)
            print(f"labeled {hex(addr_int)} -> {name}")

        # engine+0x618 (the Map/World object) construction site addendum:
        # GameEngine_ctor is already labeled; append a note about +0x618
        # to its existing plate comment rather than overwrite it.
        ctor_addr = addr_factory.getDefaultAddressSpace().getAddress(0x1000fa7c)
        existing = listing.getComment(CodeUnit.PLATE_COMMENT, ctor_addr) or ""
        addendum = (
            "\n\nUPDATE: engine+0x618 is the CMap/World tile-grid object "
            "(see Map_GetTileAt, docs/WORLD_MODEL.md) -- zero-initialized "
            "here, actually constructed later via FUN_1000e3c4/FUN_1000e5c8 "
            "when no save data is loaded (see GameEngine_ctor's own body "
            "around the level-file-load call)."
        )
        if "engine+0x618 is the CMap" not in existing:
            listing.setComment(ctor_addr, CodeUnit.PLATE_COMMENT, existing + addendum)
            print("appended CMap addendum to GameEngine_ctor's comment")

print("done")
