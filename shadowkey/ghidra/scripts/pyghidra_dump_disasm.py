"""
Dump raw disassembly for a given function address, to check details the
decompiler's C output can obscure (e.g. whether a register argument is
actually read).

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_dump_disasm.py <hex_addr>
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

target = int(sys.argv[1], 16)

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
    addr_factory = program.getAddressFactory()

    addr = addr_factory.getAddress(hex(target))
    func = fm.getFunctionAt(addr)
    if func is None:
        print(f"WARNING: no function at {hex(target)}")
    else:
        body = func.getBody()
        print(f"function {func.getName()} @ {hex(target)}, param count = {len(func.getParameters())}")
        for p in func.getParameters():
            print(f"  param: {p.getName()} {p.getDataType()} @ {p.getVariableStorage()}")
        instrs = listing.getInstructions(body, True)
        for instr in instrs:
            print(f"{instr.getAddress()}: {instr}")

print("done")
