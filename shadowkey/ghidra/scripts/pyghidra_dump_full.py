import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

TARGET = int(sys.argv[1], 16)
OUT = REPO_ROOT / "shadowkey" / "extracted" / f"decomp_{sys.argv[1]}.c"

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

    addr = addr_factory.getDefaultAddressSpace().getAddress(TARGET)
    func = fm.getFunctionAt(addr)
    res = decomp.decompileFunction(func, 120, None)
    if res.decompileCompleted():
        code = res.getDecompiledFunction().getC()
        OUT.write_text(code, encoding="utf-8")
        print("wrote", OUT, "len", len(code))
    else:
        print("FAILED", res.getErrorMessage())
