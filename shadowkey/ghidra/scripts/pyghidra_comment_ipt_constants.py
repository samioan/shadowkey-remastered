"""
Addendum comment on GameEngine_FirstTickBootstrap documenting the
IPT_Weapon/IPT_Spell/IPT_Misc/IPT_Armor SIMKIN_RegisterConstant example
that established what ordinals 42/43/60 do. See docs/SIMKIN_BRIDGE.md.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_comment_ipt_constants.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

ADDENDUM = (
    "SimKin constant registration example (see docs/SIMKIN_BRIDGE.md): "
    "the block right after the player-start/.zon setup calls "
    "SIMKIN_MakeIntAtom + SIMKIN_MakeStringAtom + SIMKIN_RegisterConstant "
    "repeatedly to expose named integer constants to scripts -- "
    "confirmed real names by resolving the string literals: "
    "IPT_Weapon=1, IPT_Spell=2, IPT_Misc=0, IPT_Armor=3 (an item-category "
    "enum). This is the call site that established what ordinals "
    "42/43/60 do."
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

    with program.openTransaction("comment IPT constants example"):
        addr = addr_factory.getDefaultAddressSpace().getAddress(0x10023ad0)
        func = fm.getFunctionAt(addr)
        if func is None:
            print("WARNING: no function at 0x10023ad0")
        else:
            existing = func.getComment() or ""
            if ADDENDUM not in existing:
                new_comment = (existing + "\n\n" + ADDENDUM).strip() if existing else ADDENDUM
                func.setComment(new_comment)
                print("updated GameEngine_FirstTickBootstrap comment")

print("done")
