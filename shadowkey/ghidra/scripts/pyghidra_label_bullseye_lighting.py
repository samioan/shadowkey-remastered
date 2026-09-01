"""
Rename and comment the "bullseye" subsystem's lighting-bake functions,
resolving what .zmp's bulk content (everything after its 132-byte header)
and .zcp actually are: a static per-zone light-propagation bake, computed
once at zone load (all three functions here have exactly one caller,
GameEngine_InitLevel or each other -- none are per-tick).

- Bullseye_LoadZmpCells (was FUN_1001b964, "calc_lights" per its debug
  marker): copies .zmp's per-cell grid (field80 x zmpTotal cells, 6 bytes
  each, starting right after the 132-byte header) into the per-cell 8-byte
  light/nav grid Bullseye_InitMap allocated at engine+0x6908.
- Bullseye_BakeLighting (was FUN_1000f130): re-zeroes each cell's light
  field, applies .zcp's per-cell light-delta lookup, then calls
  Bullseye_PropagateLight from every cell flagged as a light source.
- Bullseye_PropagateLight (was FUN_1000ef74): a 2D ray-cast light spread
  with wall-bounce, using the same 2048-entry sin/cos LUT as
  BuildRotationMatrix3x4/the automap.

See docs/ZONE_FORMAT.md for the full writeup and verified field layouts.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_bullseye_lighting.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

FUNCS = [
    (0x1001b964, "Bullseye_LoadZmpCells",
     "Copies .zmp's bulk content (offset 0x84 onward -- everything after "
     "the ZmpHeader) into the per-cell 8-byte light/nav grid at "
     "engine+0x6908: a field80 x zmpTotal grid of 6-byte cell records "
     "(flags, unknown, u16 lightLevel, u16 zcpIndex). See "
     "docs/ZONE_FORMAT.md."),
    (0x1000f130, "Bullseye_BakeLighting",
     "One-shot per-zone lighting bake (called once from "
     "GameEngine_InitLevel, not per-tick). Pass 1: zero every cell's "
     "lightLevel field (+2). Pass 2: for each cell with flags bit0 set "
     "(a light source), call Bullseye_PropagateLight at its world "
     "position (col*0x100+0x80, row*0x100+0x80 -- the same 256-unit "
     "tile scale as the collision/position code). Pass 3: apply .zcp's "
     "per-cell light-delta lookup (indexed by the cell's zcpIndex field, "
     "stride 0x24/36 bytes, only byte 0 -- a signed delta*0x100 -- "
     "decoded) to lightLevel, clamped to [0, 0x3eff]/saturating at "
     "0x3f00. See docs/ZONE_FORMAT.md."),
    (0x1000ef74, "Bullseye_PropagateLight",
     "2D ray-cast light propagation with wall-bounce: casts rays in 256 "
     "directions from a light-source cell using the same 2048-entry "
     "sin/cos LUT as BuildRotationMatrix3x4/the automap marker, stepping "
     "cell by cell and adding a flat +0x40 to each cell's lightLevel "
     "(clamped as above). A cell with flags bit1 set (wall/obstruction) "
     "makes the ray bounce (mirror its step direction) rather than pass "
     "through; the ray stops once it has bounced on both axes or leaves "
     "the grid. See docs/ZONE_FORMAT.md."),
]

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

    with program.openTransaction("label bullseye lighting functions"):
        for addr_int, name, comment in FUNCS:
            addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            func.setName(name, SourceType.USER_DEFINED)
            func.setComment(comment)
            print(f"renamed {hex(addr_int)} -> {name}")

print("done")
