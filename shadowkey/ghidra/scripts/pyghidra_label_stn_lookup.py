"""
Label FUN_100732c8, the "look up a named SimKin object" helper called by
.stn processing in GameEngine_InitLevel. Its decompiled C looked like it
dropped its 2nd parameter (object name) entirely -- pyghidra_dump_disasm.py
proved the raw ARM disassembly forwards it, untouched, straight into a
nested call (FUN_1008fc04), which is why the decompiler's C view never
showed it as used. See docs/ZONE_FORMAT.md's `.stn` section.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_stn_lookup.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

COMMENT = (
    "SimKinObject_FindByName(registry, name): looks up a named SimKin\n"
    "object in the object registry at registry+0x5c (registry here is\n"
    "engine+0x30's +0x470 field). Decompiled C shows only 1 formal param --\n"
    "misleading. The raw disassembly (see pyghidra_dump_disasm.py) proves\n"
    "the 2nd arg (name) IS used: it's left untouched in r1 across this\n"
    "function's prologue and forwarded straight into FUN_1008fc04, which\n"
    "does the real name-based search/cleanup-push. Returns the found\n"
    "object pointer, or 0.\n"
    "Called from .stn processing in GameEngine_InitLevel to find the named\n"
    "door/container object each .stn record refers to, before overwriting\n"
    "its +0x3c field with a resistDisarm[] array reference. See\n"
    "docs/ZONE_FORMAT.md."
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
    from ghidra.program.model.symbol import SourceType

    program = flat_api.getCurrentProgram()
    fm = program.getFunctionManager()
    addr_factory = program.getAddressFactory()

    with program.openTransaction("label SimKinObject_FindByName"):
        addr = addr_factory.getAddress(hex(0x100732C8))
        func = fm.getFunctionAt(addr)
        if func is None:
            print("WARNING: no function at 0x100732c8")
        else:
            func.setName("SimKinObject_FindByName", SourceType.USER_DEFINED)
            existing = func.getComment() or ""
            if COMMENT not in existing:
                new_comment = (existing + "\n\n" + COMMENT).strip() if existing else COMMENT
                func.setComment(new_comment)
            print(f"labeled {func.getName()} @ {hex(0x100732C8)}")

print("done")
