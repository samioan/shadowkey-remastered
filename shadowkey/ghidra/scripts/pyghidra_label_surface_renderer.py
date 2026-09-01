"""
Rename and comment the tile-grid wall/surface-face renderer, discovered
while chasing .ztx/.zlu's consumers. This is a THIRD 3D rendering
pipeline (alongside the actor pipeline and the .zsk-baked room mesh),
driven by .sur (per-face material index + UV parameters), textured via
.ztx (a palettized wall-texture atlas), and colored via .zlu (4
selectable 256-color palettes). Reuses the shared Poly3D_ClipAgainstPlane
clip core and the same near-clip/fade-flag axes as the actor rasterizer
family. See docs/RENDERER_3D.md and docs/ZONE_FORMAT.md.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_surface_renderer.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

FUNCS = [
    (0x1005d784, "SurfaceFace_BuildAndProject",
     "Called from Render3DScene's tile-grid wall/floor/ceiling traversal "
     "(one call per exposed face). Looks up the face's .sur record "
     "(param_3, index into engine+0x6918's array; 0xff = no override, "
     "falls back to defaults), reads its uShift/vShift/uOffset/vOffset/"
     "flags fields, builds a quad's 4 corner UVs (also sampling the "
     "engine+0x6904 grid Bullseye_InitMap allocates), perspective-"
     "projects them (same 0x5800/0x6800/EUSER____divsi3 formula as "
     "Poly3D_ClipAgainstPlane, same engine+0xc5c/+0xc60 per-frame vertex "
     "scratch pool), then calls SurfaceFace_ClipAndDispatch once per "
     "triangle. See docs/ZONE_FORMAT.md for the decoded .sur record "
     "layout and docs/RENDERER_3D.md for this pipeline's role."),
    (0x1005d074, "SurfaceFace_ClipAndDispatch",
     "Wall/surface-face counterpart to Poly3D_ClipAndDispatch: clips via "
     "Poly3D_ClipAgainstPlane (the same routine the actor and room "
     "renderers use), then dispatches to one of 4 rasterizer variants by "
     "(near vs. far -- average vertex depth vs. engine+0xbe10 -- and the "
     "same engine+0xbe0f fade flag as the actor pipeline). The texture "
     "pointer passed through is *(engine+0x6b20) + surfaceIndex*0x4000 "
     "-- .ztx's per-face texture slot -- and the extra pointer forwarded "
     "alongside it is one of .zlu's 4 selected 256-color palette chunks "
     "(engine+0x6b24 + 2-bit selector*4, selector = bits 4-5 of the "
     "caller's per-face material byte). See docs/RENDERER_3D.md and "
     "docs/ZONE_FORMAT.md."),
    (0x1005a9e0, "SurfaceFace_RasterizeTextured_v0",
     "near + fade. One of SurfaceFace_ClipAndDispatch's 4 targets; see "
     "_v3 (0x1005bbc8) for the fully-traced sibling (indexed-texture + "
     "palette-LUT lookup). See docs/RENDERER_3D.md."),
    (0x10059970, "SurfaceFace_RasterizeTextured_v1",
     "near + no-fade. One of SurfaceFace_ClipAndDispatch's 4 targets; "
     "see _v3 (0x1005bbc8) for the fully-traced sibling. See "
     "docs/RENDERER_3D.md."),
    (0x1005c1a4, "SurfaceFace_RasterizeTextured_v2",
     "far + fade. One of SurfaceFace_ClipAndDispatch's 4 targets; see "
     "_v3 (0x1005bbc8) for the fully-traced sibling. See "
     "docs/RENDERER_3D.md."),
    (0x1005bbc8, "SurfaceFace_RasterizeTextured_v3",
     "far + no-fade. Fully traced: reads a texel as a single BYTE from "
     "the .ztx texture pointer (param_5, 8bpp-indexed, not raw 16bpp -- "
     "corrects the actor/room rasterizers' raw-16bpp assumption for this "
     "pipeline specifically), doubles it (*2) as an index, then reads "
     "the FINAL 16bpp color from the .zlu palette chunk (param_8) at "
     "that index -- i.e. .ztx holds palettized wall textures and .zlu "
     "supplies the palette(s) that convert indices to color, with a "
     "secondary per-scanline/per-pixel offset into adjacent 0x200-byte "
     "blocks not fully decoded (likely a further distance-driven palette "
     "blend, analogous to the actor pipeline's fade LUT). See "
     "docs/RENDERER_3D.md and docs/ZONE_FORMAT.md."),
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

    with program.openTransaction("label surface/wall-face renderer"):
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
