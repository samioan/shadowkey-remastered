"""Print one function's decompiled C out of decomp_all.c, by address.

  python shadowkey/ghidra/scripts/show_func.py 10066614 [10066cc4 ...]

With `-g <substr>` instead of addresses, prints every function whose body
contains the substring, banner included.
"""
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
DECOMP = REPO_ROOT / "shadowkey" / "extracted" / "decomp_all.c"

text = DECOMP.read_text(encoding="utf-8", errors="replace")
marks = list(re.finditer(r"^// ==== ([0-9a-f]{8}) (\S+) ====$", text, re.M))
funcs = {}
for i, m in enumerate(marks):
    end = marks[i + 1].start() if i + 1 < len(marks) else len(text)
    funcs[m.group(1)] = (m.group(2), text[m.end():end])

args = sys.argv[1:]
if args[:1] == ["-g"]:
    for addr, (name, code) in sorted(funcs.items()):
        if args[1] in code:
            print(f"// ==== {addr} {name} ====")
            for line in code.splitlines():
                if args[1] in line:
                    print("   ", line.strip())
else:
    for a in args:
        a = a.lower().lstrip("0x").rjust(8, "0")
        if a not in funcs:
            print(f"// {a}: not found")
            continue
        print(f"// ==== {a} {funcs[a][0]} ====")
        print(funcs[a][1])
