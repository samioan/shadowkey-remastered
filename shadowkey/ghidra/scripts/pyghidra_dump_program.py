"""
Decompile every function in the program once and write the whole lot to a
single file, so later questions ("who else touches +0x8c?") are a local
grep instead of another multi-minute Ghidra pass.

pyghidra_grep_decompiled.py answers one question per run; this answers
all of them, at the same cost. Run it once per investigation that needs
more than about three offsets.

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_dump_program.py [out.c]

Default output: shadowkey/extracted/decomp_all.c (gitignored). Each
function is preceded by a `// ==== <addr> <name> ====` banner, so a grep
with -B on the banner recovers which function a line belongs to.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO_ROOT / "shadowkey" / "extracted" / "decomp_all.c"

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
    failed = 0
    with OUT.open("w", encoding="utf-8") as fh:
        for i, func in enumerate(funcs):
            if i % 200 == 0:
                print(f"... {i}/{total}", file=sys.stderr)
            addr = func.getEntryPoint()
            fh.write(f"\n// ==== {addr} {func.getName()} ====\n")
            res = decomp.decompileFunction(func, 60, None)
            if not res.decompileCompleted():
                failed += 1
                fh.write("// (decompilation failed)\n")
                continue
            fh.write(res.getDecompiledFunction().getC())

    print(f"wrote {OUT} -- {total} functions, {failed} failed")
