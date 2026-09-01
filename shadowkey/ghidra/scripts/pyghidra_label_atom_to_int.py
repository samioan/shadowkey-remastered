"""
Rename SIMKIN ordinal 185 (523 call sites, the single busiest
unresolved ordinal per pyghidra_simkin_ordinal_stats.py) now that its
role is confirmed: extracts a native integer value out of a SimKin
value/argument object -- the read counterpart to SIMKIN_MakeIntAtom's
construction. Confirmed via two real property-setter dispatch chains
("SetSpellType", "SetSprite" -- see docs/SIMKIN_BRIDGE.md).

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_atom_to_int.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

COMMENT = (
    "SIMKIN_AtomToInt (was ordinal 185, 523 call sites / 34 callers -- "
    "by far the busiest SIMKIN ordinal). Extracts a native integer from "
    "a SimKin value/argument atom (the read counterpart to "
    "SIMKIN_MakeIntAtom's construction). Confirmed via two real native "
    "property-setter dispatch chains: a \"SetSpellType\" handler "
    "(0x1002e434, stores a 16-bit result) and a \"SetSprite\" handler "
    "(0x1007ef24, stores a 32-bit result) -- both follow the same "
    "pattern (compare an incoming member-name argument against a "
    "literal via wcscmp, and on match, call this ordinal on the value "
    "argument and store the result into an object field; on mismatch, "
    "delegate to the next handler in a chain). See docs/SIMKIN_BRIDGE.md."
)

SETTER_COMMENT = (
    "One handler in a chained SimKin native property-setter dispatch "
    "(compare the requested member name against a literal via wcscmp; "
    "on match, extract the argument's int value via SIMKIN_AtomToInt "
    "and store it; on mismatch, delegate to the next handler in the "
    "chain). This one handles \"{name}\". See docs/SIMKIN_BRIDGE.md."
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

    with program.openTransaction("label SIMKIN_AtomToInt"):
        addr = addr_factory.getDefaultAddressSpace().getAddress(0x100a0110)
        func = fm.getFunctionAt(addr)
        if func is None:
            print("WARNING: no function at 0x100a0110")
        else:
            func.setName("SIMKIN_AtomToInt", SourceType.USER_DEFINED)
            func.setComment(COMMENT)
            print("renamed 0x100a0110 -> SIMKIN_AtomToInt")

        for addr_int, name in [(0x1002e434, "SetSpellType"), (0x1007ef24, "SetSprite")]:
            a = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            f = fm.getFunctionAt(a)
            if f is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            f.setComment(SETTER_COMMENT.format(name=name))
            print(f"commented {hex(addr_int)} ({name} handler)")

print("done")
