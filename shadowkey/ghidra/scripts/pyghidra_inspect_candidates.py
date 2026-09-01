import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

TARGETS = [0x10078de4, 0x10023ad0, 0x1000b634, 0x10018e70]

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
    addr_factory = program.getAddressFactory()

    from ghidra.app.decompiler import DecompInterface
    decomp = DecompInterface()
    decomp.openProgram(program)

    for t in TARGETS:
        addr = addr_factory.getDefaultAddressSpace().getAddress(t)
        func = fm.getFunctionAt(addr)
        print("=" * 80)
        print(f"{hex(t)}  {func.getName() if func else 'NOT A FUNCTION'}")
        if func is None:
            continue
        print(f"  size={func.getBody().getNumAddresses()}")

        # callers
        refs = ref_mgr.getReferencesTo(addr)
        callers = set()
        for ref in refs:
            if not ref.getReferenceType().isCall():
                continue
            c = fm.getFunctionContaining(ref.getFromAddress())
            if c:
                callers.add((c.getEntryPoint().toString(), c.getName()))
        print(f"  callers ({len(callers)}):")
        for ep, name in sorted(callers):
            print(f"    {ep}  {name}")

        # decompile snippet
        res = decomp.decompileFunction(func, 30, None)
        if res.decompileCompleted():
            code = res.getDecompiledFunction().getC()
            lines = code.splitlines()
            print("  decompiled (first 40 lines):")
            for line in lines[:40]:
                print("    " + line)
        else:
            print("  decompile FAILED:", res.getErrorMessage())
        print()
