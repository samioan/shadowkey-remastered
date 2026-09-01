"""
Given a DAT_ label address from decompiler output (e.g. "DAT_100296e8"),
figure out whether it's a direct vtable address or a literal-pool slot
pointing to one, then dump the first N entries as (offset, target,
containing function name if any).
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

DAT_ADDR = int(sys.argv[1], 16)
N_ENTRIES = int(sys.argv[2]) if len(sys.argv) > 2 else 20

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
    mem = program.getMemory()
    af = program.getAddressFactory()

    addr = af.getDefaultAddressSpace().getAddress(DAT_ADDR)
    raw = mem.getInt(addr) & 0xFFFFFFFF
    print(f"value stored at {hex(DAT_ADDR)}: {hex(raw)}")

    # Heuristic: if `raw` itself lies in a mapped, executable-ish code range,
    # treat DAT_ADDR itself as the vtable base (common: DAT_x IS the vtable,
    # decompiler just shows "= DAT_x" for "= &vtable_array").
    def looks_like_code_ptr(v):
        a = af.getDefaultAddressSpace().getAddress(v & 0xFFFFFFFE)
        f = fm.getFunctionContaining(a)
        return f is not None

    candidates = []
    if looks_like_code_ptr(raw):
        candidates.append(("DAT_ADDR value points at code -> DAT_ADDR is a literal-pool slot, raw value is the vtable base", raw))
    # Also just try DAT_ADDR itself as the vtable base directly.
    candidates.append(("DAT_ADDR itself as vtable base", DAT_ADDR))

    for label, base in candidates:
        print(f"\n--- trying: {label} (base={hex(base)}) ---")
        base_addr = af.getDefaultAddressSpace().getAddress(base)
        ok = 0
        for i in range(N_ENTRIES):
            try:
                v = mem.getInt(base_addr.add(i * 4)) & 0xFFFFFFFF
            except Exception as e:
                print(f"  [{hex(i*4)}] read failed: {e}")
                continue
            f = fm.getFunctionContaining(af.getDefaultAddressSpace().getAddress(v & 0xFFFFFFFE))
            tag = f"-> {f.getEntryPoint()} {f.getName()}" if f else "(not a function)"
            if f:
                ok += 1
            print(f"  [{hex(i*4)}] = {hex(v)}  {tag}")
        print(f"  {ok}/{N_ENTRIES} entries resolve to functions")
