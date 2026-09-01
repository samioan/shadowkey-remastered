"""
Find the zone/level loading code by locating xrefs to debug strings like
"InitLevel Pre load_models" / "InitLevel Post load_models" and the
"%s\%s.ent" / "%s\%s.zon" / etc. format strings, then print the
containing function(s) + decompilation so we can trace how entity
placement records reference models.huge.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_find_zone_loader.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

NEEDLES = [
    "InitLevel Pre load_models",
    "InitLevel Post load_models",
    r"%s\%s.ent",
    r"%s\%s.zon",
    r"%s\%s.zmp",
]

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
    from ghidra.program.model.symbol import RefType
    from ghidra.util.task import ConsoleTaskMonitor
    from ghidra.app.decompiler import DecompInterface

    listing = program.getListing()
    fm = program.getFunctionManager()
    monitor = ConsoleTaskMonitor()

    di = DecompInterface()
    di.openProgram(program)

    seen_funcs = set()

    for needle in NEEDLES:
        print(f"\n=== searching for string data matching: {needle!r} ===")
        found_any = False
        data_iter = listing.getDefinedData(True)
        for data in data_iter:
            try:
                val = data.getValue()
            except Exception:
                continue
            if val is None:
                continue
            sval = str(val)
            if needle in sval:
                found_any = True
                addr = data.getAddress()
                print(f"  string at {addr}: {sval!r}")
                refs = program.getReferenceManager().getReferencesTo(addr)
                for ref in refs:
                    from_addr = ref.getFromAddress()
                    func = fm.getFunctionContaining(from_addr)
                    if func is None:
                        print(f"    ref from {from_addr} (no containing function)")
                        continue
                    print(f"    ref from {from_addr} in function {func.getName()} @ {func.getEntryPoint()}")
                    seen_funcs.add(func)
        if not found_any:
            print("  (not found as defined string data)")

    print(f"\n=== {len(seen_funcs)} distinct referencing functions ===")
    for func in seen_funcs:
        print(f"{func.getEntryPoint()}  {func.getName()}  size={func.getBody().getNumAddresses()}")
