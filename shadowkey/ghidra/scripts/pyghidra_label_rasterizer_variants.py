"""
Rename and comment the ~10 Poly3D_RasterizeTextured rasterizer variants
dispatched from Poly3D_ClipAndDispatch (0x10056324), resolving the last open
item in RENDERER_3D.md.

Classification derived by reading Poly3D_ClipAndDispatch's dispatch tree
(selects on: bVar1 = "this poly needed near-plane clipping", the fade flag
byte at engine+0xbe0f, and two bits of the caller-supplied mode word
param_6) plus decompiling and comparing all 10 targets. See
docs/RENDERER_3D.md for the full writeup. Summary:

- near-clip (bVar1) doubles/triples code size in every family -- extra
  per-edge bookkeeping for polygons that got Sutherland-Hodgman-clipped.
- "fade" variants (engine+0xbe0f != 0) additionally interpolate a
  per-vertex value (vertex[+8] * engine[+0x5c8] >> 8, i.e. depth * a
  global scale, clamped to 0xffff) and OR its top nibble into the output
  color word; CompositeSceneBufferToScreen's fade branch then uses the
  FULL 16-bit color+nibble word as an index into a LUT at engine+0x5c4 --
  a torchlight/distance-fog effect, not a simple post-process filter.
- "stencil" variants (param_6 & 2, param_7 == -1, or the always-stencil
  v8) additionally stamp a literal byte (repurposing param_6 itself, now
  typed as a single byte) into a SECOND framebuffer plane at
  engine+0x5b8 (176x208 8bpp, 0xb0-byte stride) for every pixel actually
  drawn -- an object-ID/picking buffer, not alpha blending.
- v9 (param_6 & 1) is structurally distinct: writes straight into the
  real 16bpp screen buffer (engine+0x480, 0x160-byte stride) with an
  unconditional store (no depth test, no OR'd high bits), bypassing the
  padded intermediate-buffer/depth-test scheme every other variant uses.
- Every variant treats texel value 0x0f0f as a chroma-key "transparent,
  skip this pixel" -- texture cutouts, not real alpha blending.
- The "0x7fff constant in the high 16 bits" claim in an earlier version
  of RENDERER_3D.md was wrong: those bits hold a genuine per-pixel
  interpolated depth value (from the packed vertex's +8 field) used for
  a same-buffer occlusion test, then discarded (masked with & 0xffff) by
  CompositeSceneBufferToScreen.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_rasterizer_variants.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

VARIANTS = [
    (0x10051460, "Poly3D_RasterizeTextured_v1",
     "near-clip=Y, stencil=N, fade=Y. Plain family + near-clip bookkeeping "
     "+ per-vertex fog-nibble interpolation. See docs/RENDERER_3D.md."),
    (0x10052044, "Poly3D_RasterizeTextured_v2",
     "near-clip=N, stencil=N, fade=N. The baseline case: scanline "
     "edge-walk, 1/z-corrected UV, chroma-key (0x0f0f) transparent "
     "texels, per-pixel depth test against engine+0x5b4's high 16 bits. "
     "See docs/RENDERER_3D.md."),
    (0x10052504, "Poly3D_RasterizeTextured_v3",
     "near-clip=N, stencil=N, fade=Y. Same as v2 plus the fog-nibble "
     "interpolation consumed by CompositeSceneBufferToScreen's "
     "engine+0x5c4 LUT branch. See docs/RENDERER_3D.md."),
    (0x100536f8, "Poly3D_RasterizeTextured_v4",
     "near-clip=Y, stencil=Y, fade=N. Stencil family: repurposes the mode "
     "word (now a literal byte) to stamp engine+0x5b8, a second 176x208 "
     "8bpp object-ID/picking buffer, for every pixel actually drawn. See "
     "docs/RENDERER_3D.md."),
    (0x10052ab8, "Poly3D_RasterizeTextured_v5",
     "near-clip=Y, stencil=Y, fade=Y. Stencil family + fog-nibble "
     "interpolation. See docs/RENDERER_3D.md."),
    (0x1005420c, "Poly3D_RasterizeTextured_v6",
     "near-clip=N, stencil=Y, fade=N. Stencil family baseline: same as "
     "v2 but additionally writes param_6 (a literal byte, not a flags "
     "word here) into engine+0x5b8 at each drawn pixel. See "
     "docs/RENDERER_3D.md."),
    (0x10054704, "Poly3D_RasterizeTextured_v7",
     "near-clip=N, stencil=Y, fade=Y. Stencil family + fog-nibble "
     "interpolation. See docs/RENDERER_3D.md."),
    (0x10054d04, "Poly3D_RasterizeTextured_v8",
     "near-clip=Y, stencil=Y, fade=N, plus an extra forwarded parameter "
     "(Poly3D_ClipAndDispatch's own param_7, passed only when that value "
     "!= -1) not yet deciphered beyond being in the stencil family. See "
     "docs/RENDERER_3D.md."),
    (0x10055a4c, "Poly3D_RasterizeTextured_v9",
     "Selected by mode-word bit 0, independent of near-clip/stencil/fade. "
     "Structurally distinct: writes straight into the real 16bpp screen "
     "buffer (engine+0x480, 0x160-byte stride) with a plain unconditional "
     "store -- no depth test, no OR'd high bits -- bypassing the padded "
     "intermediate-buffer/depth-test scheme every other variant uses. "
     "Also unconditionally stores a raw interpolated accumulator into "
     "engine+0x5b4 at the same slot (purpose not pinned down). Likely an "
     "always-on-top / no-occlusion draw path. See docs/RENDERER_3D.md."),
]

DISPATCHER_COMMENT = (
    "Dispatch tree resolved (see docs/RENDERER_3D.md, 'The other ~9 "
    "Poly3D_RasterizeTextured variants' section):\n"
    "  bVar1 = true if any vertex needed near-plane clipping.\n"
    "  fade  = *(char*)(engine+0xbe0f) != 0 (torchlight/distance-fog LUT).\n"
    "  param_6 & 1      -> v9 (direct-to-screen, no depth test)\n"
    "  param_6 & 2 == 0 -> plain family: v0/v1/v2/v3 by (bVar1, fade)\n"
    "  param_6 & 2 != 0, param_7 == -1 -> stencil family: v4/v5/v6/v7\n"
    "  param_6 & 2 != 0, param_7 != -1 -> v8 (stencil family, near-clip, "
    "extra forwarded param_7)"
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

    with program.openTransaction("label rasterizer variants"):
        for addr_int, name, comment in VARIANTS:
            addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            func.setName(name, SourceType.USER_DEFINED)
            func.setComment(comment)
            print(f"renamed {hex(addr_int)} -> {name}")

        dispatcher_addr = addr_factory.getDefaultAddressSpace().getAddress(0x10056324)
        dispatcher = fm.getFunctionAt(dispatcher_addr)
        if dispatcher is not None:
            existing = dispatcher.getComment() or ""
            if DISPATCHER_COMMENT not in existing:
                new_comment = (existing + "\n\n" + DISPATCHER_COMMENT).strip() if existing else DISPATCHER_COMMENT
                dispatcher.setComment(new_comment)
                print("updated Poly3D_ClipAndDispatch comment")

print("done")
