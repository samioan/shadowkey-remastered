"""
Label the SIMKIN import ordinals confirmed this pass, and comment the
representative call sites that established each one's role. See
docs/SIMKIN_BRIDGE.md for the full writeup and confidence levels.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_simkin_bridge.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

# (thunk address, new name, comment)
ORDINALS = [
    (0x100a0150, "SIMKIN_MakeIntAtom",
     "Ordinal 42 (143 call sites, 24 callers). Constructs a SimKin "
     "value/'atom' object from a literal int (e.g. "
     "SIMKIN_MakeIntAtom(&tmp, 1)). Paired with SIMKIN_MakeStringAtom "
     "and SIMKIN_RegisterConstant to expose named integer constants to "
     "scripts (e.g. IPT_Weapon=1) -- but also used far more broadly "
     "than just constant registration (143 sites vs. only ~50 "
     "constant-registration sites), likely for any int value passed "
     "into the interpreter. See docs/SIMKIN_BRIDGE.md."),
    (0x100a0310, "SIMKIN_MakeStringAtom",
     "Ordinal 43 (102 call sites, 12 callers). Constructs a SimKin "
     "value/'atom' object from a literal string pointer. See "
     "SIMKIN_MakeIntAtom / docs/SIMKIN_BRIDGE.md."),
    (0x100a0320, "SIMKIN_RegisterConstant",
     "Ordinal 60 (119 call sites, but only 2 distinct callers -- both "
     "register many constants in a loop/sequence). "
     "SIMKIN_RegisterConstant(interp, &nameAtom, &valueAtom) binds a "
     "script-visible named constant. Confirmed via a real example: "
     "GameEngine_FirstTickBootstrap registers IPT_Weapon=1, "
     "IPT_Spell=2, IPT_Misc=0, IPT_Armor=3 (string literals resolved "
     "via pyghidra_read_strings.py). See docs/SIMKIN_BRIDGE.md."),
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

    with program.openTransaction("label SimKin bridge ordinals"):
        for addr_int, name, comment in ORDINALS:
            addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            func.setName(name, SourceType.USER_DEFINED)
            func.setComment(comment)
            print(f"renamed {hex(addr_int)} -> {name}")

print("done")
