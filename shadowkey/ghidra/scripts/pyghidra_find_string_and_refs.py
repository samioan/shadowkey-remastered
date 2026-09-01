"""
Search memory for one or more ASCII string literals, print their
addresses, and list every reference to each found string's address
plus the containing function.

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_find_string_and_refs.py "Str1" "Str2" ...
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

TARGETS = sys.argv[1:]

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
    memory = program.getMemory()
    ref_mgr = program.getReferenceManager()

    for target in TARGETS:
        pattern = target.encode("ascii")
        print(f"\n=== searching for ASCII {target!r} ===")
        found_any = False
        addr = memory.getMinAddress()
        max_addr = memory.getMaxAddress()
        while addr is not None and addr <= max_addr:
            found = memory.findBytes(addr, pattern, None, True, None)
            if found is None:
                break
            found_any = True
            print(f"  string at {found}")
            refs = ref_mgr.getReferencesTo(found)
            for ref in refs:
                from_addr = ref.getFromAddress()
                rfunc = fm.getFunctionContaining(from_addr)
                print(f"    ref from {from_addr} in {rfunc.getName() if rfunc else '???'}")
            try:
                addr = found.add(1)
            except Exception:
                break
        if not found_any:
            print("  (not found)")

print("\ndone")
