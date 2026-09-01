"""
Add rich comments to the 3 SurfaceFace_RasterizeTextured variants not
individually traced when they were first renamed (v0/v1/v2 -- v3 was
fully traced already). Confirms all 4 share v3's core palette-indexed
texture lookup and fog-nibble mechanism, but finds a genuine behavioral
split along the near/far axis (not just near-clip bookkeeping size, like
the actor pipeline's variants): the near variants (v0/v1) write every
pixel of their fixed 8-wide interpolation batch unconditionally -- no
chroma-key (0x0f0f) transparency check, no per-pixel depth test against
the shared intermediate buffer -- while the far variants (v2/v3) gate
each pixel on both. See docs/RENDERER_3D.md for the full writeup.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_surface_rasterizer_variants.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

VARIANTS = [
    (0x1005a9e0, "near + fade. Traced: writes every pixel of its fixed "
     "8-wide interpolation batch unconditionally -- no chroma-key "
     "(0x0f0f) transparency check, no per-pixel depth test against "
     "engine+0x5b4 -- unlike the far variants (_v2/_v3). Applies the "
     "same engine+0x5c8 fog-nibble scheme as _v2/_v3. See "
     "docs/RENDERER_3D.md."),
    (0x10059970, "near + no-fade. Traced: same unconditional-write, "
     "8-wide-batch structure as _v0, no chroma-key/depth-test, no fog. "
     "The baseline 'near' case. See docs/RENDERER_3D.md."),
    (0x1005c1a4, "far + fade. Traced: same gated-write structure as _v3 "
     "(chroma-key + per-pixel depth test against engine+0x5b4) plus the "
     "engine+0x5c8 fog-nibble scheme. See docs/RENDERER_3D.md."),
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

    with program.openTransaction("comment surface rasterizer variants"):
        for addr_int, comment in VARIANTS:
            addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            func.setComment(comment)
            print(f"commented {hex(addr_int)} ({func.getName()})")

print("done")
