import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

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
    funcs = list(fm.getFunctions(True))
    sizes = sorted((f.getBody().getNumAddresses() for f in funcs), reverse=True)

    n = len(sizes)
    total_bytes = sum(sizes)
    print("total functions:", n)
    print("total bytes covered by functions:", total_bytes)
    print("mean size:", total_bytes / n if n else 0)

    import statistics
    print("median size:", statistics.median(sizes))
    buckets = [(0, 16), (16, 32), (32, 64), (64, 128), (128, 256), (256, 512),
               (512, 1024), (1024, 4096), (4096, 10**9)]
    for lo, hi in buckets:
        c = sum(1 for s in sizes if lo <= s < hi)
        print(f"  [{lo:>5}, {hi if hi < 10**9 else 'inf':>5}) bytes: {c} functions")

    print("top 15 largest:")
    funcs_sorted = sorted(funcs, key=lambda f: f.getBody().getNumAddresses(), reverse=True)
    for f in funcs_sorted[:15]:
        print(f"  {f.getEntryPoint()}  size={f.getBody().getNumAddresses()}  name={f.getName()}")

    # how many are trivial (likely thunks/accessors/wrappers) vs "real" logic
    trivial = sum(1 for s in sizes if s <= 24)
    print(f"functions <=24 bytes (likely thunks/trivial wrappers): {trivial} ({trivial/n:.1%})")
