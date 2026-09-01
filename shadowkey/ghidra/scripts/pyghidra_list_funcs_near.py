"""
List functions in a given address range with their size and caller count,
to spot small sibling helper functions compiled next to a known function
of interest (e.g. looking for a trie *lookup* counterpart next to the
known trie *insert* function, SimKinNameTrie_Insert @ 0x1000de50).

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_list_funcs_near.py <hex-lo> <hex-hi>
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

lo = int(sys.argv[1], 16)
hi = int(sys.argv[2], 16)

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
    refman = program.getReferenceManager()
    addr_factory = program.getAddressFactory()

    lo_addr = addr_factory.getAddress(hex(lo))
    hi_addr = addr_factory.getAddress(hex(hi))

    func = fm.getFunctionContaining(lo_addr)
    if func is None:
        func = fm.getFunctionAfter(lo_addr) if hasattr(fm, "getFunctionAfter") else None

    it = fm.getFunctions(lo_addr, True)
    for f in it:
        entry = f.getEntryPoint()
        if entry.getOffset() > hi:
            break
        body_size = f.getBody().getNumAddresses()
        callers = set()
        for ref in refman.getReferencesTo(entry):
            caller_func = fm.getFunctionContaining(ref.getFromAddress())
            if caller_func is not None:
                callers.add(caller_func.getEntryPoint())
        print(f"{entry}  size={body_size:4d}  callers={len(callers):3d}  name={f.getName()}")

print("done")
