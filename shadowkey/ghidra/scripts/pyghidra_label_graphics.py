"""
Apply the low-level 2D graphics primitive findings as real labels. See
docs/GRAPHICS_FORMAT.md for the full writeup/evidence.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LABELS = {
    0x1006a488: (
        "Blit_RLESprite",
        "Blit(engine, fbPtr, dstX, dstY, imageData, srcXOffset, "
        "rowStart, rowEnd, clipRight, blendMode). Draws a paletted, "
        "run-length/segment-encoded sprite image (header at "
        "imageData+0: width, height; row data from imageData+0x204: "
        "per-row [startX,endX] segment pairs, then 1-byte-per-pixel "
        "palette indices into a 16-bit palette at imageData+4) into the "
        "176x208 (0xb0 x 0xd0) 16bpp backbuffer, stride 0x160 (352) "
        "bytes/row. Skips pixels equal to the colorkey 0x0f0f "
        "(magenta -- matches ScreenModeController's palette setup). "
        "blendMode 0 = opaque copy, 1 = 50% average blend with the "
        "existing framebuffer pixel (((src&0xeee)+(dst&0xeee))>>1) -- "
        "used for translucency/shadow effects. See "
        "docs/GRAPHICS_FORMAT.md.",
    ),
    0x1007ec80: (
        "DrawEntitySpriteWithOutline",
        "Draws the sprite for image index entity+0x98 (from the "
        "384-slot loaded-image cache at engine+0x4460) at screen "
        "position (entity+0x78, entity+0x7c) via Blit_RLESprite. If "
        "param_2 (highlight flag) is set, also draws an 8-direction "
        "1px-offset outline of the same sprite using "
        "FUN_1006bee8/FUN_1006be58 in a UI-palette color -- a "
        "selection/highlight effect. Called from FUN_1007f050.",
    ),
    0x1007f050: (
        "DrawListItemIconAndLabel",
        "UI widget draw: if entity+0x9c (next/pending image index) is "
        "-1 and a valid size is set, draws a plain colored placeholder "
        "box (FUN_1006bee8/FUN_1006be58) instead of an icon; otherwise "
        "draws the icon via DrawEntitySpriteWithOutline. Always finishes "
        "by drawing a text label (FUN_1007f49c) below/beside it. Looks "
        "like an inventory/menu/dialogue-choice list-item renderer, not "
        "the 3D world view. See docs/GRAPHICS_FORMAT.md.",
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

    with program.openTransaction("label graphics primitive functions"):
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
