"""
Find consumers of a large (>0xfff, can't fit a single ARM LDR immediate)
object-field offset that gets built via a PC-relative literal-pool
constant instead of an immediate -- pyghidra_find_reads.py's simple
"[base,#imm]" text match can't see these, since the offset is computed
via a separate ADD from a loaded constant.

Strategy: scan the whole program's memory for 4-byte little-endian words
matching each target offset, then use the reference manager to find every
instruction that reads that literal-pool location (typically an
"ldr rX,[<addr>]" that Ghidra's disassembler already resolves to an
absolute target even without full analysis), and print a few instructions
of context around each hit so the consuming function/usage can be read
directly.

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_find_literal_pool_offset.py <hex-offset> [<hex-offset> ...]
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
    memory = program.getMemory()
    ref_mgr = program.getReferenceManager()

    import struct

    for target in TARGETS:
        pattern = struct.pack("<I", target)
        print(f"\n=== searching for literal-pool word 0x{target:08x} ===")
        found_any = False
        addr = memory.getMinAddress()
        max_addr = memory.getMaxAddress()
        while addr is not None and addr <= max_addr:
            found = memory.findBytes(addr, pattern, None, True, None)
            if found is None:
                break
            found_any = True
            func = fm.getFunctionContaining(found)
            print(f"  literal at {found}  (in function: {func.getName() if func else '???'})")
            refs = ref_mgr.getReferencesTo(found)
            for ref in refs:
                from_addr = ref.getFromAddress()
                rfunc = fm.getFunctionContaining(from_addr)
                instr = listing.getInstructionAt(from_addr)
                print(f"    referenced by {from_addr} in {rfunc.getName() if rfunc else '???'}: {instr}")
                # print a few instructions of context after the load
                nxt = instr
                for _ in range(4):
                    nxt = nxt.getNext() if nxt else None
                    if nxt is None:
                        break
                    print(f"      {nxt.getAddress()}: {nxt}")
            try:
                addr = found.add(4)
            except Exception:
                break
        if not found_any:
            print("  (no literal-pool word found with this value)")

print("\ndone")
