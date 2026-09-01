"""
Rename/document TileGrid_RaycastVisibility, the fan-raycast visibility
scan Render3DScene runs once per frame to build the list of tiles whose
faces might need a dynamic SurfaceFace_BuildAndProject draw, and add
addendum comments documenting:
  - the CMap/"engine" object-identity unification (the object this
    project's ZONE_FORMAT.md/RENDERER_3D.md call "engine" throughout --
    holding +0x480/+0x5b4/+0x2c/+0x30/+0x6908/+0x690c/etc. -- IS
    WORLD_MODEL.md's CMap, i.e. *(the top-level GameEngine + 0x618))
  - the extended 8-byte tile record layout (byte 6 = a per-frame
    "last visible" stamp, newly decoded)
  - Render3DScene's per-tile/per-direction dynamic-face-draw gating logic

See docs/WORLD_MODEL.md and docs/RENDERER_3D.md for the full writeup.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_tile_visibility.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

RAYCAST_COMMENT = (
    "TileGrid_RaycastVisibility: called once per frame from Render3DScene. "
    "Casts a fan of rays (150/177/178, by a 3-tier quality setting read "
    "from engine+0x608) out from the camera position, using the same "
    "2048-entry sin/cos LUT as BuildRotationMatrix3x4/the automap, "
    "stepping tile by tile (DDA) up to a quality-tiered max range "
    "(25/93/172 tiles) or until a tile with flags bit1 set (wall/"
    "obstruction -- the SAME bit Bullseye_PropagateLight's light rays "
    "stop at) blocks the ray. Every tile a ray passes through gets "
    "appended (deduplicated via tile byte 6, a rotating 0-3 'last "
    "visible frame' stamp compared against engine+0x478) to a per-frame "
    "visible-tile list (max 512 entries) at engine+0x6c, which "
    "Render3DScene's main loop then iterates to decide which tiles' "
    "faces might need a dynamic SurfaceFace_BuildAndProject draw. Also "
    "opportunistically sets bits in a packed bitmap at "
    "*(engine+0x470)+0x45c) -- looks like a SimKin-exposed 'explored "
    "tiles' bitmap for automap reveal, not confirmed in depth. See "
    "docs/WORLD_MODEL.md."
)

CMAP_ADDENDUM = (
    "Object-identity note (see docs/WORLD_MODEL.md and "
    "docs/RENDERER_3D.md): the object this project's ZONE_FORMAT.md/ "
    "RENDERER_3D.md call 'engine' throughout (holding +0x480 framebuffer, "
    "+0x5b4 composite buffer, +0x2c/+0x30 grid width/height, +0x6904/"
    "+0x6908/+0x690c the tile grid + type table, +0xbe34 entity BST, "
    "etc., and passed as GameEngine_InitLevel's/Bullseye_*'s/"
    "Render3DScene's/SurfaceFace_*'s own first parameter) IS this "
    "function's CMap* argument -- i.e. WORLD_MODEL.md's '*(GameEngine+"
    "0x618), call it CMap'. Verified via TileGrid_RaycastVisibility "
    "(FUN_1000f694), which reaches this exact object both directly (as "
    "its own param_1) and by calling this function with it."
)

TILE_RECORD_ADDENDUM = (
    "Tile record layout extended (was: flags/unknown0/lightLevel/typeId, "
    "docs/ZONE_FORMAT.md's Bullseye_LoadZmpCells finding): byte 6 is a "
    "per-frame 'last visible' stamp (0-3, rotates via engine+0x478), set "
    "by TileGrid_RaycastVisibility to deduplicate its per-frame visible-"
    "tile list. flags bit1 (wall/obstruction) also stops visibility rays "
    "here, the same role it plays in Bullseye_PropagateLight's light-"
    "bounce simulation -- one bit governing both systems. Byte 7 still "
    "undecoded. See docs/WORLD_MODEL.md."
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

    from ghidra.program.model.symbol import SourceType

    with program.openTransaction("label tile visibility raycaster"):
        addr = addr_factory.getDefaultAddressSpace().getAddress(0x1000f694)
        func = fm.getFunctionAt(addr)
        if func is None:
            print("WARNING: no function at 0x1000f694")
        else:
            func.setName("TileGrid_RaycastVisibility", SourceType.USER_DEFINED)
            func.setComment(RAYCAST_COMMENT)
            print("renamed 0x1000f694 -> TileGrid_RaycastVisibility")

        map_addr = addr_factory.getDefaultAddressSpace().getAddress(0x1001b004)
        map_func = fm.getFunctionAt(map_addr)
        if map_func is not None:
            existing = map_func.getComment() or ""
            if CMAP_ADDENDUM not in existing:
                new_comment = (existing + "\n\n" + CMAP_ADDENDUM).strip() if existing else CMAP_ADDENDUM
                map_func.setComment(new_comment)
                print("updated Map_GetTileAt comment (CMap identity note)")

        bullseye_addr = addr_factory.getDefaultAddressSpace().getAddress(0x1001b964)
        bullseye_func = fm.getFunctionAt(bullseye_addr)
        if bullseye_func is not None:
            existing = bullseye_func.getComment() or ""
            if TILE_RECORD_ADDENDUM not in existing:
                new_comment = (existing + "\n\n" + TILE_RECORD_ADDENDUM).strip() if existing else TILE_RECORD_ADDENDUM
                bullseye_func.setComment(new_comment)
                print("updated Bullseye_LoadZmpCells comment (tile record addendum)")

print("done")
