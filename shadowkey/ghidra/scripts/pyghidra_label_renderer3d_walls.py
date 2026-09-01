"""
Apply the room/wall 3D geometry renderer findings as real labels. This is
the follow-up to pyghidra_label_renderer3d.py: it answers RENDERER_3D.md's
central open question (does static dungeon geometry use the same pipeline
as actors?). See docs/RENDERER_3D.md for the full writeup/evidence.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LABELS = {
    0x100166c8: (
        "Render3DScene",
        "Per-frame 3D scene render entry point, called from the screen "
        "state machine (FUN_10068e0c case 5) once per frame. Sequence: (1) "
        "if the current room (engine+0x62c) has a model resource "
        "(+0x54 != 0), calls RoomGeometry_TransformAndSort to render it into "
        "engine+0x5b4; otherwise flat-fills engine+0x5b4 (no room loaded "
        "yet / void). (2) Renders actors/entities via virtual dispatch "
        "(vtable+0x170, the same virtual slot Actor3D's FUN_10064ffc reads) "
        "-- also drawing into engine+0x5b4. (3) Calls "
        "CompositeSceneBufferToScreen to pack the intermediate buffer down "
        "into the real engine+0x480 176x208 screen buffer. See "
        "docs/RENDERER_3D.md.",
    ),
    0x10057890: (
        "RoomGeometry_TransformAndSort",
        "RoomGeometry_TransformAndSort(engine). Given the current room "
        "object (engine+0x62c) and its 3D model resource (room+0x54 -- the "
        "SAME vertex-list/face-list/texture-atlas format read by "
        "Actor3D_TransformAndSubmitModel at actor+0x54), builds a "
        "rotation+translation transform via BuildRotationMatrix3x4/"
        "ComposeTransform3x4, transforms and perspective-projects every "
        "model vertex in one pass (same 0x5800/0x6800 + EUSER____divsi3 "
        "formula as Poly3D_ClipAgainstPlane) into a fixed per-engine vertex "
        "buffer (engine+0x7738, stride 0x1c), then depth-sorts every face "
        "into 64 Z-buckets (engine+0xbd88, bucketed by -z>>5) and walks the "
        "buckets calling RoomFace_ClipAndDispatch per face in depth order "
        "(a painter's-algorithm sort) -- architecturally different from "
        "the actor pipeline's per-face immediate clip-and-dispatch. See "
        "docs/RENDERER_3D.md.",
    ),
    0x10056aa0: (
        "RoomFace_ClipAndDispatch",
        "RoomFace_ClipAndDispatch(engine, faceVerts[3-4], texturePtr). "
        "Room-geometry counterpart to Poly3D_ClipAndDispatch: clips a "
        "single room face against up to 4 planes by calling "
        "Poly3D_ClipAgainstPlane repeatedly (reusing the actor pipeline's "
        "clip routine), repacks surviving vertices' screen X/Y and "
        "texture U/V, then unconditionally calls "
        "RoomFace_RasterizeTextured with *(engine+0x480) as the fbPtr "
        "argument (unused by the rasterizer, which writes engine+0x5b4 "
        "directly -- same convention as the actor rasterizers). No "
        "multi-variant dispatch (rooms don't need the actor pipeline's "
        "~10 blend-mode variants). See docs/RENDERER_3D.md.",
    ),
    0x10055f38: (
        "RoomFace_RasterizeTextured",
        "Dedicated scanline rasterizer for room/wall faces (distinct from "
        "the ~10 Poly3D_RasterizeTextured_* actor variants). Same "
        "edge-walking / reciprocal-of-height-LUT technique as "
        "Poly3D_RasterizeTextured_v0 (DAT_10056310 here vs DAT_10050e34 "
        "there), but simpler affine (non-perspective-corrected) per-scanline "
        "UV stepping -- room faces are already perspective-projected "
        "per-vertex by RoomGeometry_TransformAndSort, so no additional "
        "1/z correction is applied here. Writes into engine+0x5b4 with the "
        "same 0x2c0 (704-byte) row stride as the actor rasterizers, "
        "confirming both actor and room rendering share one intermediate "
        "render target. See docs/RENDERER_3D.md.",
    ),
    0x1005dfe0: (
        "CompositeSceneBufferToScreen",
        "CompositeSceneBufferToScreen(engine). Packs the intermediate scene "
        "buffer at engine+0x5b4 (176x208, 4 bytes/pixel -- 16bpp color in "
        "the low halfword, a constant 0x7fff padding/marker in the high "
        "halfword written by every rasterizer -- 0x2c0=704 byte stride "
        "confirms 704/4=176 px/row) down into the real 16bpp "
        "engine+0x480 screen buffer, 2 source pixels packed per 32-bit "
        "destination write. When engine+0xbe0f is set, takes a second code "
        "path that runs each channel through a lookup table at "
        "engine+0x5c4 instead of a plain copy -- likely a fade/lighting "
        "post-process (e.g. torchlight falloff). Only caller: "
        "Render3DScene, at the end of each frame. See docs/RENDERER_3D.md.",
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

    with program.openTransaction("label room/wall 3D renderer functions"):
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
