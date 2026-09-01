"""
Decompile multiple functions in one pyghidra session (faster than invoking
pyghidra_dump_full.py once per address). Writes each to
shadowkey/extracted/decomp_0x<addr>.c

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_dump_batch.py <hex-addr> [<hex-addr> ...]
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
    fm = program.getFunctionManager()
    addr_factory = program.getAddressFactory()

    from ghidra.app.decompiler import DecompInterface
    decomp = DecompInterface()
    decomp.openProgram(program)

    for target in TARGETS:
        hexstr = hex(target)
        out = REPO_ROOT / "shadowkey" / "extracted" / f"decomp_{hexstr}.c"
        addr = addr_factory.getDefaultAddressSpace().getAddress(target)
        func = fm.getFunctionAt(addr)
        if func is None:
            print(f"NO FUNCTION at {hexstr}")
            continue
        res = decomp.decompileFunction(func, 120, None)
        if res.decompileCompleted():
            code = res.getDecompiledFunction().getC()
            out.write_text(code, encoding="utf-8")
            print("wrote", out, "len", len(code))
        else:
            print("FAILED", hexstr, res.getErrorMessage())
