"""
Phase 3 starting point: locate the render/game-loop entry point(s) by
tracing callers of the WS32/GDI/BITGDI import thunks outward.

Strategy:
  1. Find the labeled import functions for the render-adjacent DLLs
     (WS32, GDI, BITGDI, FBSCLI) -- these were named by
     pyghidra_label_imports.py from shadowkey/import_names.json.
  2. For each, list every caller (function containing a call to it).
  3. For the most interesting anchor (WS32::Flush, called once per frame
     in the conventional Symbian app-UI drawing model), walk up the
     caller graph a couple of levels and report call counts, to find a
     function that looks like "the" per-frame draw/update entry point
     (called from a tight loop or from a single high-level dispatcher).
"""
import sys
from pathlib import Path
from collections import defaultdict

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

RENDER_DLLS = ("WS32", "GDI", "BITGDI", "FBSCLI")

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
    listing = program.getListing()
    ref_mgr = program.getReferenceManager()

    all_funcs = list(fm.getFunctions(True))
    render_funcs = [
        f for f in all_funcs
        if any(f.getName().startswith(dll + "_") or f.getName().startswith(dll + "__")
               for dll in RENDER_DLLS)
    ]

    print(f"Found {len(render_funcs)} render-adjacent import functions:")
    for f in render_funcs:
        print(f"  {f.getEntryPoint()}  {f.getName()}")
    print()

    # caller -> set of (callee_name) it calls, plus count of call sites
    caller_call_sites = defaultdict(list)  # caller_func -> [(callee_name, call_addr)]

    for target in render_funcs:
        entry = target.getEntryPoint()
        refs = ref_mgr.getReferencesTo(entry)
        for ref in refs:
            if not ref.getReferenceType().isCall():
                continue
            from_addr = ref.getFromAddress()
            caller = fm.getFunctionContaining(from_addr)
            if caller is None:
                continue
            caller_call_sites[caller].append((target.getName(), from_addr))

    print(f"Found {len(caller_call_sites)} distinct caller functions:")
    for caller, calls in sorted(caller_call_sites.items(), key=lambda kv: -len(kv[1])):
        names = sorted(set(c[0] for c in calls))
        print(f"  {caller.getEntryPoint()}  {caller.getName()}  size={caller.getBody().getNumAddresses()}  calls={names}")

    print()
    print("=== Second-level: callers of the above caller functions ===")
    level2 = defaultdict(list)
    for caller in caller_call_sites:
        entry = caller.getEntryPoint()
        refs = ref_mgr.getReferencesTo(entry)
        for ref in refs:
            if not ref.getReferenceType().isCall():
                continue
            from_addr = ref.getFromAddress()
            gp = fm.getFunctionContaining(from_addr)
            if gp is None:
                continue
            level2[gp].append((caller.getName(), from_addr))

    for gp, calls in sorted(level2.items(), key=lambda kv: -len(kv[1])):
        names = sorted(set(c[0] for c in calls))
        print(f"  {gp.getEntryPoint()}  {gp.getName()}  size={gp.getBody().getNumAddresses()}  calls_into={names}")
