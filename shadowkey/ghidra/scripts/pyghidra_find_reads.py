"""
Search the whole binary for LDR instructions reading a fixed immediate
byte offset from any base register -- used to find every function that
consumes a known object field (e.g. the raw framebuffer pointer cached
at engine+0x480), as opposed to pyghidra_find_field_writes.py which
finds where a field gets written.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

OFFSET = int(sys.argv[1], 16)

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

    ins_iter = listing.getInstructions(True)

    from collections import defaultdict
    hits_by_func = defaultdict(int)

    for ins in ins_iter:
        mnem = ins.getMnemonicString().lower()
        if mnem not in ("ldr", "ldrb", "ldrh"):
            continue
        text = ins.toString()
        if f"0x{OFFSET:x}]" in text and "sp" not in text.split(",")[1].lower():
            func = fm.getFunctionContaining(ins.getAddress())
            if func:
                hits_by_func[func] += 1

    for func, count in sorted(hits_by_func.items(), key=lambda kv: -kv[1]):
        print(f"{func.getEntryPoint()}  {func.getName()}  size={func.getBody().getNumAddresses()}  hits={count}")

    print(f"\n{len(hits_by_func)} functions read offset {hex(OFFSET)}")
