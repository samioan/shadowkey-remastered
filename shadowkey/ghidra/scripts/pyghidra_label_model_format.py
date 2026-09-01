"""
Append the reconstructed on-disk 3D model resource format as plate-comment
addenda on the two functions whose field accesses it was derived from. See
docs/MODEL_FORMAT.md for the full writeup/evidence.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

ADDENDUM = (
    "\n\nMODEL RESOURCE FORMAT (see docs/MODEL_FORMAT.md): header @+0x0 is "
    "6x int16 (vertexTableBaseHW, frameCount, vertsPerFrame, uvEntryCount, "
    "faceCount, halfwordsPerFrame). Vertex table: frameCount frames x "
    "vertsPerFrame verts x 6 bytes (x,y,z int16), frame N at halfword "
    "(vertexTableBaseHW + N*halfwordsPerFrame). UV table (uvEntryCount x 4 "
    "bytes: U u16, V u16) starts right after all frames of vertex data. "
    "Face table (faceCount x 12 bytes: 3x vertex-index int16 + 3x "
    "UV-index int16 -- a textured triangle) starts right after the UV "
    "table. Texture header (4 halfwords: unknown, width-pow2, height, "
    "unknown) starts right after the face table; raw 16bpp pixel data "
    "(width*height texels per skin variant) starts 4 halfwords after that, "
    "indexed by a per-instance skin byte (actor+0xca) for actors, always "
    "variant 0 for rooms.",
)

TARGETS = {
    0x10056eb0: ADDENDUM[0],
    0x10057890: ADDENDUM[0],
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

    with program.openTransaction("append model-format addenda"):
        from ghidra.program.model.listing import CodeUnit

        for addr_int, addendum in TARGETS.items():
            addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"SKIP {hex(addr_int)}: not a function")
                continue
            existing = listing.getComment(CodeUnit.PLATE_COMMENT, addr) or ""
            if "MODEL RESOURCE FORMAT" in existing:
                print(f"SKIP {hex(addr_int)}: addendum already present")
                continue
            listing.setComment(addr, CodeUnit.PLATE_COMMENT, existing + addendum)
            print(f"updated {hex(addr_int)} ({func.getName()})")

print("done")
