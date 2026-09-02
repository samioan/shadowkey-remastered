"""
Decompile every function in the program once and grep the resulting C
text for one or more substrings -- for finding all consumers of an
offset/constant the decompiler folds from multi-instruction ARM
arithmetic (so a literal-pool word search, pyghidra_find_literal_pool_
offset.py, finds nothing) without guessing which function to check by
hand. Slow (~2000 functions, several minutes) -- meant to be run once
per investigation, not routinely.

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_grep_decompiled.py <substr> [<substr> ...]

Prints, per match: function address/name and the matching line(s).
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

PATTERNS = sys.argv[1:]
if not PATTERNS:
    print("usage: pyghidra_grep_decompiled.py <substr> [<substr> ...]")
    sys.exit(1)

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

    from ghidra.app.decompiler import DecompInterface
    decomp = DecompInterface()
    decomp.openProgram(program)

    funcs = list(fm.getFunctions(True))
    total = len(funcs)
    hits = 0
    for i, func in enumerate(funcs):
        if i % 200 == 0:
            print(f"... {i}/{total}", file=sys.stderr)
        res = decomp.decompileFunction(func, 60, None)
        if not res.decompileCompleted():
            continue
        code = res.getDecompiledFunction().getC()
        for pat in PATTERNS:
            if pat not in code:
                continue
            hits += 1
            print(f"\n=== {func.getEntryPoint()} {func.getName()}  (matched '{pat}') ===")
            for line in code.splitlines():
                if pat in line:
                    print("   ", line.strip())

    print(f"\n{hits} matching function(s) out of {total}")
