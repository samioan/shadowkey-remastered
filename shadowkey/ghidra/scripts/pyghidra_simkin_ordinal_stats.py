"""
One-shot survey of every SIMKIN import ordinal: how many distinct
functions call it, and total call-site count. Used to scope which
ordinals are worth investigating first for the SimKin native-function
bridge (see docs/SIMKIN_BRIDGE.md).

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_simkin_ordinal_stats.py
"""
import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

with open(REPO_ROOT / "shadowkey" / "extracted" / "import_thunks.json") as f:
    thunk_data = json.load(f)

simkin_thunks = {}
for addr_str, info in thunk_data["thunks"].items():
    if info["dll"].startswith("SIMKIN"):
        simkin_thunks[int(addr_str, 16)] = info["ordinal"]

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

    results = []
    for addr_int, ordinal in simkin_thunks.items():
        addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
        func = fm.getFunctionAt(addr)
        if func is None:
            continue
        refs = list(ref_mgr.getReferencesTo(addr))
        callers = set()
        for r in refs:
            cfunc = fm.getFunctionContaining(r.getFromAddress())
            if cfunc:
                callers.add(cfunc.getEntryPoint())
        results.append((ordinal, len(refs), len(callers)))

    results.sort(key=lambda t: -t[1])
    print(f"{'ordinal':>8} {'call sites':>11} {'distinct callers':>17}")
    for ordinal, nrefs, ncallers in results:
        print(f"{ordinal:>8} {nrefs:>11} {ncallers:>17}")
    print(f"\n{len(results)} SIMKIN ordinals with resolvable thunks")
