"""
Scan the ENTIRE program memory (not just code literal pools) for a 4-byte
little-endian word matching a target value, and print every hit address
plus which defined data symbol / vtable-like region it falls in, if any.

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_scan_word_everywhere.py <hex-value> [<hex-value> ...]
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
    mem = program.getMemory()
    fm = program.getFunctionManager()
    listing = program.getListing()

    for target in TARGETS:
        print(f"\n=== scanning for word {hex(target)} ===")
        target_bytes = bytes([target & 0xff, (target >> 8) & 0xff, (target >> 16) & 0xff, (target >> 24) & 0xff])
        count = 0
        for block in mem.getBlocks():
            if not block.isInitialized():
                continue
            start = block.getStart()
            size = block.getSize()
            try:
                data = bytes(flat_api.getBytes(start, int(size)))
            except Exception as e:
                print(f"  (failed to read block {block.getName()}: {e})")
                continue
            idx = 0
            while True:
                idx = data.find(target_bytes, idx)
                if idx == -1:
                    break
                hit_addr = start.add(idx)
                f = fm.getFunctionContaining(hit_addr)
                containing = f"in function {f.getName()}" if f else ""
                print(f"  {hit_addr} (block {block.getName()}) {containing}")
                count += 1
                idx += 1
        print(f"  {count} hit(s)")
