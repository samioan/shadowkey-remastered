import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

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
    ref_mgr = program.getReferenceManager()
    sym_table = program.getSymbolTable()

    start_func = None
    for f in fm.getFunctions(True):
        if "Start__9CPeriodic" in f.getName():
            start_func = f
            break
    print("CPeriodic::Start thunk:", start_func.getEntryPoint() if start_func else None)

    if start_func:
        refs = ref_mgr.getReferencesTo(start_func.getEntryPoint())
        for ref in refs:
            if not ref.getReferenceType().isCall():
                continue
            caller = fm.getFunctionContaining(ref.getFromAddress())
            print(f"  call site {ref.getFromAddress()} in {caller.getEntryPoint() if caller else '?'} {caller.getName() if caller else ''}")

    # also who calls FUN_10028200
    from ghidra.program.model.address import AddressSet
    target = program.getAddressFactory().getDefaultAddressSpace().getAddress(0x10028200)
    tf = fm.getFunctionAt(target)
    print()
    print("FUN_10028200 callers:")
    refs = ref_mgr.getReferencesTo(target)
    for ref in refs:
        if not ref.getReferenceType().isCall():
            continue
        caller = fm.getFunctionContaining(ref.getFromAddress())
        print(f"  {ref.getFromAddress()} in {caller.getEntryPoint() if caller else '?'} {caller.getName() if caller else ''}")

    # also references to the address as data (not call) -- callback function pointer usage
    print()
    print("FUN_10028200 non-call references (possible function-pointer/TCallBack usage):")
    for ref in ref_mgr.getReferencesTo(target):
        if ref.getReferenceType().isCall():
            continue
        caller = fm.getFunctionContaining(ref.getFromAddress())
        print(f"  {ref.getFromAddress()} type={ref.getReferenceType()} in {caller.getEntryPoint() if caller else '?'} {caller.getName() if caller else ''}")
