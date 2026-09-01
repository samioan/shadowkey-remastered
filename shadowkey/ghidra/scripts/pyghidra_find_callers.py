"""
Find all callers of a given function address, with function name + size.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

TARGET = int(sys.argv[1], 16)

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
    target = addr_factory.getDefaultAddressSpace().getAddress(TARGET)
    func = fm.getFunctionAt(target)
    print("function:", func)
    refs = program.getReferenceManager().getReferencesTo(target)
    callers = set()
    for r in refs:
        caller_func = fm.getFunctionContaining(r.getFromAddress())
        if caller_func:
            callers.add((caller_func.getEntryPoint(), caller_func.getName(), caller_func.getBody().getNumAddresses()))
    for c in sorted(callers):
        print(c)
    print(f"\n{len(callers)} distinct callers")
