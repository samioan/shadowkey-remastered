"""
Label the "current room" render-state object: engine+0x62c is a single
0x160-byte object, allocated once in GameEngine_ctor and never reassigned
-- room transitions overwrite its fields in place from <zone>.zon. See
docs/RENDERER_3D.md's "Open follow-ups" (engine+0x62c entry) and
docs/ZONE_FORMAT.md.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_room_render_state.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

CTOR_ADDENDUM = (
    "ROOM RENDER STATE (engine+0x62c): allocated here (new(0x160) +\n"
    "RoomRenderState_ctor call around this offset's store), never\n"
    "reassigned again. Room transitions overwrite ITS FIELDS in place from\n"
    "<zone>.zon in GameEngine_InitLevel -- not a pointer swap, not an index\n"
    "into the engine+0x5464 room-definition array. See docs/RENDERER_3D.md\n"
    "and docs/ZONE_FORMAT.md."
)

LABELS = {
    0x10067898: (
        "RoomRenderState_ctor",
        "Constructor for the engine+0x62c room render-state object (called\n"
        "once from GameEngine_ctor). Zeroes the model pointer (+0x54) and\n"
        "sets the default model-scale flag (+0x5e = 0x100) among other\n"
        "fields. See docs/RENDERER_3D.md.",
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

    with program.openTransaction("label room render state"):
        for addr_int, (name, comment) in LABELS.items():
            addr = addr_factory.getAddress(hex(addr_int))
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            if func.getName() != name:
                func.setName(name, SourceType.USER_DEFINED)
            existing = func.getComment() or ""
            if comment not in existing:
                new_comment = (existing + "\n\n" + comment).strip() if existing else comment
                func.setComment(new_comment)
            print(f"labeled {hex(addr_int)} -> {name}")

        ctor = fm.getFunctionAt(addr_factory.getAddress("0x1000fa7c"))
        if ctor is not None:
            existing = ctor.getComment() or ""
            if "ROOM RENDER STATE" not in existing:
                ctor.setComment((existing + "\n\n" + CTOR_ADDENDUM).strip())
                print(f"appended addendum to {ctor.getName()}")
            else:
                print(f"{ctor.getName()} already has the addendum")
        else:
            print("WARNING: GameEngine_ctor not found at 0x1000fa7c")

print("done")
