"""
Find consumers of a large object-field offset (>0xfff, too big for a
single ARM LDR/STR immediate) that the compiler built by splitting it
into a big rotated-immediate ADD (e.g. "add r3,rBase,#0x6b00") followed
by a small immediate LDR/STR ("ldr r2,[r3,#0x1c]") -- rather than a
literal-pool constant (see pyghidra_find_literal_pool_offset.py, which
covers that other case and came up empty for these specific offsets).

Scans every instruction in the binary for an ADD/SUB with the given
immediate (e.g. 0x6b00), then prints the next few instructions so any
LDR/STR consuming the result register can be read directly, grouped by
containing function.

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_find_split_offset.py <hex-immediate> [<hex-immediate> ...]
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
    listing = program.getListing()

    from ghidra.program.model.scalar import Scalar

    targets_set = set(TARGETS)
    hits = 0
    for instr in listing.getInstructions(True):
        mnem = instr.getMnemonicString().lower()
        if mnem not in ("add", "sub"):
            continue
        matched = None
        for i in range(instr.getNumOperands()):
            for obj in instr.getOpObjects(i):
                if isinstance(obj, Scalar):
                    val = obj.getUnsignedValue()
                    if val in targets_set:
                        matched = val
        if matched is None:
            continue
        hits += 1
        func = fm.getFunctionContaining(instr.getAddress())
        print(f"\n{instr.getAddress()} ({func.getName() if func else '???'}): {instr}  [matches 0x{matched:x}]")
        nxt = instr
        for _ in range(5):
            nxt = nxt.getNext() if nxt else None
            if nxt is None:
                break
            print(f"    {nxt.getAddress()}: {nxt}")

    print(f"\n{hits} matching ADD/SUB instructions found")
