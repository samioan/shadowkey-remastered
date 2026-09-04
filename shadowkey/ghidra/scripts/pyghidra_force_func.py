"""
Disassemble and decompile code at an address Ghidra never marked as a
function -- vtable slots routinely point at these, and
pyghidra_dump_disasm.py just says "no function at ...".

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_force_func.py <hexaddr> [...]

Prints the decompiled C for each. Does not save the program.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

TARGETS = [int(a, 16) for a in sys.argv[1:]]

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
    space = program.getAddressFactory().getDefaultAddressSpace()

    from ghidra.app.decompiler import DecompInterface
    decomp = DecompInterface()
    decomp.openProgram(program)

    for t in TARGETS:
        addr = space.getAddress(t)
        func = flat_api.getFunctionAt(addr)
        if func is None:
            flat_api.disassemble(addr)
            func = flat_api.createFunction(addr, f"FORCED_{t:08x}")
        if func is None:
            print(f"// {t:08x}: could not create a function here")
            continue
        res = decomp.decompileFunction(func, 60, None)
        print(f"// ==== {t:08x} {func.getName()} ====")
        print(res.getDecompiledFunction().getC() if res.decompileCompleted()
              else "// (decompilation failed)")
