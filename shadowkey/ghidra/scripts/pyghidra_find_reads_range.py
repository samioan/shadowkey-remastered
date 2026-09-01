"""
Like pyghidra_find_reads.py but scans a whole contiguous range of
byte offsets in one pass (avoids restarting pyghidra once per offset).
Reports, per offset, which functions read it via LDR/LDRB/LDRH with a
literal (non-sp) base+immediate addressing form.

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_find_reads_range.py <hex-lo> <hex-hi>
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LO = int(sys.argv[1], 16)
HI = int(sys.argv[2], 16)

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

    from collections import defaultdict
    from ghidra.program.model.scalar import Scalar

    hits = defaultdict(set)  # offset -> set of function names
    total = 0

    for ins in listing.getInstructions(True):
        mnem = ins.getMnemonicString().lower()
        if mnem not in ("ldr", "ldrb", "ldrh"):
            continue
        text = ins.toString()
        if "[sp" in text.lower():
            continue
        for i in range(ins.getNumOperands()):
            for obj in ins.getOpObjects(i):
                if isinstance(obj, Scalar):
                    val = obj.getUnsignedValue()
                    if LO <= val <= HI:
                        func = fm.getFunctionContaining(ins.getAddress())
                        if func:
                            hits[val].add(func.getName())
        total += 1

    print(f"scanned {total} load instructions")
    for offset in range(LO, HI + 1):
        names = hits.get(offset)
        if names:
            print(f"0x{offset:x}: {sorted(names)}")
        else:
            print(f"0x{offset:x}: (none)")
