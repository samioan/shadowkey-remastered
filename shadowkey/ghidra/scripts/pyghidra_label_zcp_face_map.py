"""
Document the fully-decoded byte map of the 36-byte .zcp type-table entry
(ZcpEntry), resolved by fully reading Render3DScene's tile-grid traversal
(5 near-identical blocks, one per face direction) instead of sampling
isolated call sites. See docs/ZONE_FORMAT.md's ZcpEntry struct and
docs/RENDERER_3D.md's "The traversal" section for the full writeup.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_zcp_face_map.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

RENDER3DSCENE_ADDENDUM = (
    "Tile-grid traversal face-direction map (36-byte .zcp type-table "
    "entry, see docs/ZONE_FORMAT.md's ZcpEntry struct): the ~500-line "
    "block after TileGrid_RaycastVisibility is 5 near-identical "
    "per-direction blocks, each gated by a camera-position-vs-tile-edge "
    "check OR the tile's flags bit3 (force-draw), reading its .sur "
    "index from the relevant neighbor's type entry (current tile's own "
    "entry for floor/ceiling):\n"
    "  east wall:  0x16 (lower band), 0x1a (upper band) -- east neighbor's entry\n"
    "  west wall:  0x17 (lower band), 0x1b (upper band) -- west neighbor's entry\n"
    "  south wall: 0x18 (lower band), 0x1c (upper band) -- south neighbor's entry\n"
    "  north wall: 0x19 (lower band), 0x1d (upper band) -- north neighbor's entry\n"
    "  ceiling:    0x1e or 0x1f (band picked by comparing entry+0x04/+0x14\n"
    "              against camera eye-height *(camera+0x618)+0x224,\n"
    "              tile flags bit6 selects which height field) -- own entry\n"
    "  floor:      0x20 (single band) -- own entry\n"
    "0xff in any of these = no face there (SurfaceFace_BuildAndProject's "
    "own param_3==0xff check is the ultimate no-op gate; the traversal's "
    "own pre-checks for east/west are just an optimization, not present "
    "for the other 3 directions). Orientation values passed to "
    "SurfaceFace_BuildAndProject: 0/1=east/west (flipped per a .sur "
    "flags-bit1 lookup), 2=south, 3=north, 4=ceiling, 5=floor. See "
    "docs/RENDERER_3D.md and docs/ZONE_FORMAT.md."
)

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

    with program.openTransaction("document zcp face-direction map"):
        addr = addr_factory.getDefaultAddressSpace().getAddress(0x100166c8)
        func = fm.getFunctionAt(addr)
        if func is None:
            print("WARNING: no function at 0x100166c8 (Render3DScene)")
        else:
            existing = func.getComment() or ""
            if RENDER3DSCENE_ADDENDUM not in existing:
                new_comment = (existing + "\n\n" + RENDER3DSCENE_ADDENDUM).strip() if existing else RENDER3DSCENE_ADDENDUM
                func.setComment(new_comment)
                print("updated Render3DScene comment (face-direction map)")

print("done")
