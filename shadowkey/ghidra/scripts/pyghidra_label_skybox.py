"""
Label the skybox: engine+0x62c is a Skybox object and <zone>.zsk is the
zone's sky, not its room geometry (M70).

The engine names it itself. Three debug strings in the code section --
"InitLevel Pre skybox load", "InitLevel Post skybox load" and "Bullseye
constructer Post newing Skybox" -- bracket, respectively, the WholeFile_Load
call whose result becomes (*(engine+0x62c))+0x54, and the line that
constructs the object. This script puts labels on those strings and appends
the finding to the four functions that make up the pipeline, without
renaming them: docs/RENDERER_3D.md deliberately keeps the older Room*
names for continuity with every earlier note, with the correction recorded
next to them.

See docs/ZONE_FORMAT.md's ".zsk is the zone's skybox" section for the full
writeup (the census over all 21 shipped files, the hardcoded 256x256 texel
addressing, and the loader's field writes).

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_skybox.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

# The debug strings, at their real addresses in the loaded code section.
STRING_LABELS = {
    0x100AD098: ("s_InitLevel_Pre_skybox_load", "InitLevel Pre skybox load"),
    0x100AD0C0: ("s_InitLevel_Post_skybox_load", "InitLevel Post skybox load"),
    0x100A6ACC: ("s_Bullseye_ctor_Post_newing_Skybox",
                 "Bullseye constructer Post newing Skybox"),
}

FUNCTION_ADDENDA = {
    # GameEngine_InitLevel
    0x10024DEC: (
        "SKYBOX LOAD (M70): the WholeFile_Load call bracketed by the debug\n"
        "markers 'InitLevel Pre skybox load' / 'InitLevel Post skybox load'\n"
        "(literal-pool refs at 0x10026618 / 0x10026620) loads <zone>.zsk and\n"
        "fills the Skybox object at engine+0x62c:\n"
        "    skybox+0x54 = model          the .zsk resource\n"
        "    skybox+0x5e = 0x200          scale, 8.8 -> 2x, EVERY load\n"
        "    skybox+0xa8 = +0xb2 = +0xb6 = 0    pitch/roll/yaw zeroed\n"
        "    engine+0xbe0e = 1            'there is a skybox'; cleared on a\n"
        "                                 failed load, which is what makes\n"
        "                                 Render3DScene flat-fill instead.\n"
        "So engine+0x62c is a SKYBOX, not a 'current room' -- see\n"
        "docs/ZONE_FORMAT.md. The +0x5e write also settles an open question\n"
        "in docs/RENDERER_3D.md: that scale is never identity."
    ),
    # Render3DScene
    0x100166C8: (
        "SKYBOX (M70): the RoomGeometry_TransformAndSort call here is not one\n"
        "more thing drawn into the scene -- it is THE FRAME CLEAR. Its else\n"
        "arm fills all 0x8f00 (176*208) scene-buffer words with\n"
        "engine[0x630] | 0x7fff0000, and the branch is taken on\n"
        "engine+0xbe0e (set by the .zsk load) and on the model pointer being\n"
        "non-null. Either way it runs before any wall or actor."
    ),
    # RoomGeometry_TransformAndSort -- read: Skybox_TransformAndSort
    0x10057890: (
        "SKYBOX (M70): this is the skybox transform, not room geometry.\n"
        "view = R_camera * (R_skybox * (vertex * scale) + (0, 270, 0)), with\n"
        "no camera POSITION term anywhere -- the mesh turns with the view and\n"
        "never translates with it. param_2 is the Skybox object\n"
        "(engine+0x62c): +0x5e/0x5f is the 0x200 (2x) scale the loader\n"
        "writes, +0xa8/+0xb2/+0xb6 the angles it zeroes, leaving the fixed\n"
        "-0x4000 quarter turn subtracted below as the only rotation."
    ),
    # RoomFace_RasterizeTextured -- read: SkyboxFace_RasterizeTextured
    0x10055F38: (
        "SKYBOX (M70): the inner loop is one store, `*dst = texel |\n"
        "0x7fff0000` -- the same far-depth word the flat background fill\n"
        "writes -- with no depth compare, no light term and no chroma-key\n"
        "cutout. It paints background.\n"
        "Its texel address, ((v & 0xff00) + ((u >> 8) & 0xff)) * 2, hardcodes\n"
        "a 256-wide stride, where the ACTOR pipeline takes a shift from the\n"
        "texture header's width (Actor3D_TransformAndSubmitModel's log2(width)\n"
        "size class). So a .zsk skin is always one 256x256 block at the fixed\n"
        "offset after the 8-byte header, whatever that header says -- which is\n"
        "why the nine interior zones' impossible 'skinCount=256, width=256,\n"
        "height=0' header does no harm on hardware: they draw a black dome."
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
    from ghidra.program.model.symbol import SourceType

    with program.openTransaction("label skybox"):
        for addr_int, (label, text) in STRING_LABELS.items():
            addr = addr_factory.getAddress(hex(addr_int))
            flat_api.createLabel(addr, label, True, SourceType.USER_DEFINED)
            flat_api.setEOLComment(addr, f'debug string: "{text}"')
            print(f"labeled string {hex(addr_int)} -> {label}")

        for addr_int, addendum in FUNCTION_ADDENDA.items():
            addr = addr_factory.getAddress(hex(addr_int))
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            existing = func.getComment() or ""
            if "SKYBOX" in existing:
                print(f"{func.getName()} already carries the skybox note")
                continue
            func.setComment((existing + "\n\n" + addendum).strip() if existing else addendum)
            print(f"appended skybox note to {func.getName()} at {hex(addr_int)}")

print("done")
