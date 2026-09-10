"""
Scan a code range for instructions touching a given struct offset, e.g.
`ldr r3,[r7,#0xdc]`. Used to find where a field written by one function
is read by another.

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_scan_field_access.py <lo> <hi> <offset>
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

lo = int(sys.argv[1], 16)
hi = int(sys.argv[2], 16)
needle = sys.argv[3].lower()

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
    listing = program.getListing()
    af = program.getAddressFactory()
    fm = program.getFunctionManager()
    it = listing.getInstructions(af.getAddress(hex(lo)), True)
    for instr in it:
        a = int(str(instr.getAddress()), 16)
        if a >= hi:
            break
        text = str(instr).lower()
        if needle in text:
            f = fm.getFunctionContaining(instr.getAddress())
            name = f.getName() if f else "?"
            print(f"{instr.getAddress()}  {name:24s}  {instr}")

print("done")
