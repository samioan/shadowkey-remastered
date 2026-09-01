"""
Search a bounded address range for STR instructions writing a fixed
immediate byte offset (e.g. #0x30) to any base register -- used to find
where a not-yet-understood object field gets assigned, when we know the
offset but not which function sets it.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LO = int(sys.argv[1], 16)
HI = int(sys.argv[2], 16)
OFFSET = int(sys.argv[3], 16)

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
    listing = program.getListing()
    af = program.getAddressFactory()

    lo = af.getDefaultAddressSpace().getAddress(LO)
    hi = af.getDefaultAddressSpace().getAddress(HI)

    from ghidra.program.model.address import AddressSet
    aset = AddressSet(lo, hi)
    ins_iter = listing.getInstructions(aset, True)

    hits = 0
    for ins in ins_iter:
        mnem = ins.getMnemonicString().lower()
        if mnem not in ("str", "strb", "strh"):
            continue
        text = ins.toString()
        # crude match on the immediate offset appearing as #0x.. or plain hex
        if f"0x{OFFSET:x}]" in text or f"#0x{OFFSET:x}" in text:
            func = fm.getFunctionContaining(ins.getAddress())
            print(f"{ins.getAddress()}  {text}   in {func.getEntryPoint() if func else '?'} {func.getName() if func else ''}")
            hits += 1
    print(f"\n{hits} hits for offset {hex(OFFSET)} in [{hex(LO)}, {hex(HI)})")
