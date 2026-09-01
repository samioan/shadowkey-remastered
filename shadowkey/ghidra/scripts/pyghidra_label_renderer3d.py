"""
Apply the actor/entity 3D polygon renderer findings as real labels. See
docs/RENDERER_3D.md for the full writeup/evidence.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LABELS = {
    0x10056eb0: (
        "Actor3D_TransformAndSubmitModel",
        "Given an actor (param_2) and its 3D model resource (param_2+0x54: "
        "vertex list + face list + texture-atlas-cell table), computes "
        "position relative to the camera (actor+0x94/+0x9c - camera+0x94/"
        "+0x9c, 8.8 fixed point), builds a transform via "
        "BuildRotationMatrix3x4/ComposeTransform3x4, walks the model's "
        "vertex list applying that transform per-vertex, and submits each "
        "face to Poly3D_ClipAndDispatch. Called from FUN_10064ffc, "
        "FUN_10065f7c, FUN_10083490 -- all actor/entity model rendering, "
        "not tile-grid/wall geometry. See docs/RENDERER_3D.md.",
    ),
    0x10073a70: (
        "BuildRotationMatrix3x4",
        "BuildRotationMatrix3x4(dst12, yaw, pitch, roll, tx, ty, tz). "
        "Builds a 3x3 Euler rotation matrix (dst[0,1,2]/[4,5,6]/[8,9,10]) "
        "plus translation column (dst[3]/[7]/[0xb]) from a 2048-entry "
        "sin/cos LUT (DAT_10073bc8, angle & 0x7ff over a 0x2000-unit "
        "circle -- same table shape as the automap's rotated player-marker "
        "code). 8.8-ish fixed point (>>8 after each multiply). See "
        "docs/RENDERER_3D.md.",
    ),
    0x100738c4: (
        "ComposeTransform3x4",
        "ComposeTransform3x4(dst, matA, matB). Matrix-multiplies two 3x4 "
        "transforms (3x3 rotation part; ignores translation column in the "
        "multiply) -- combines an actor's local model-space rotation with "
        "its parent/attachment transform (e.g. weapon attached to a hand "
        "bone). See docs/RENDERER_3D.md.",
    ),
    0x10056324: (
        "Poly3D_ClipAndDispatch",
        "Dispatch(engine, vertices[], vertexCount, fbPtr, ..., flags, "
        "mode). Runs the polygon through Poly3D_ClipAgainstPlane (near-"
        "plane clip vs engine+0xbe10, plus further clip passes), does a "
        "2D cross-product backface/winding test on the result, repacks "
        "screen X/Y (+0x14/+0x18) and texture U/V (+0xc/+0x10) per vertex, "
        "then dispatches to one of ~10 rasterizer variants (FUN_100509a4, "
        "FUN_10051460, FUN_10052044, FUN_10052504, FUN_100536f8, "
        "FUN_10052ab8, FUN_1005420c, FUN_10054704, FUN_10054d04, "
        "FUN_10055a4c) chosen by blend/near-clip/shading flags. See "
        "docs/RENDERER_3D.md.",
    ),
    0x1005c8b4: (
        "Poly3D_ClipAgainstPlane",
        "Sutherland-Hodgman-style single-plane polygon clip: walks the "
        "vertex ring, interpolates a new vertex (position+U+V) at each "
        "edge that crosses the plane using reciprocal tables DAT_1005c968/"
        "DAT_1005cb94, allocating scratch vertices from a per-frame pool "
        "(engine+0xc5c counter, engine+0xc60 scratch). When the camera-"
        "space scale (engine+0x608) == 0x100 (1.0 in 8.8 fixed point), "
        "also does the actual perspective divide via EUSER____divsi3: "
        "screenX = 0x5800 +- 0x5800*x/z, screenY = 0x6800 -+ 0x6800*y/z "
        "(0x5800/0x6800 = half of 176/208 in 8.8 fixed point -- screen "
        "center). See docs/RENDERER_3D.md.",
    ),
    0x100509a4: (
        "Poly3D_RasterizeTextured_v0",
        "One of ~10 textured-polygon rasterizer variants dispatched from "
        "Poly3D_ClipAndDispatch. Scanline edge-walking fill (two edge "
        "walkers step around the clipped polygon as y advances 0..0xcf, "
        "the screen's last row). Per-scanline edge deltas via a "
        "reciprocal-of-height LUT (DAT_10050e34) instead of a divide. "
        "Per-span, perspective-correct U/V via a 1/z reciprocal LUT split "
        "into near/far precision bands (DAT_10051038 / DAT_10050f54). "
        "Writes into engine+0x5b4 with a 704-byte (0x2c0) row stride -- "
        "NOT the 352-byte/176px engine+0x480 screen buffer from "
        "GRAPHICS_FORMAT.md, so likely a separate/wider intermediate "
        "render target, unconfirmed. See docs/RENDERER_3D.md.",
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
    listing = program.getListing()

    with program.openTransaction("label actor 3D renderer functions"):
        from ghidra.program.model.symbol import SourceType
        from ghidra.program.model.listing import CodeUnit

        for addr_int, (name, comment) in LABELS.items():
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

print("done")
