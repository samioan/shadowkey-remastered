#!/usr/bin/env python
"""
Opens the existing ShadowkeyProject/6r51_code.bin program via pyghidra (no
re-analysis) and creates a label + plate comment at every import-thunk
address, using tools/resolve_imports.py's structural resolution plus
shadowkey/import_names.json's real names (from resolve_import_names.py --
see docs/IMPORT_NAMES.md) where available. Turns "CALL 0x1009eef0" into
"CALL EUSER____nw__5CBaseUi" (CBase::operator new(TUint)) throughout the
whole binary -- or "CALL EUSER_ord1582" for the ~96 ordinals (SIMKIN,
GAMECOMMS, NOKIAFC, MEDIACLIENTAUDIOSTREAM) with no real-name source yet.

Run from the shadowkey-decomp repo root:
    python shadowkey/ghidra/scripts/pyghidra_label_imports.py
"""
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402  (must import after any env setup, before touching ghidra.*)

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")

APP_PATH = REPO_ROOT / (
    "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-EnFrDeEsIt-26102004"
    "/system/apps/6r51/6r51.app"
)
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"
PROJECT_NAME = "ShadowkeyProject"
PROGRAM_NAME = "6r51_code.bin"
IMPORT_NAMES_PATH = REPO_ROOT / "shadowkey" / "import_names.json"

NOT_IDENT = re.compile(r"[^A-Za-z0-9_]")


def label_for(dll_short: str, ordinal: int, real_names: dict) -> str:
    real = real_names.get(f"{dll_short}!{ordinal}")
    if real:
        return f"{dll_short}__{NOT_IDENT.sub('_', real)}"
    return f"{dll_short}_ord{ordinal}"


def main():
    import resolve_imports  # local module in tools/, needs sys.path set up above

    result = resolve_imports.resolve(str(APP_PATH))
    print(f"resolved {result['thunks_found']} / {result['total_imports']} import thunks")

    real_names = {}
    if IMPORT_NAMES_PATH.exists():
        real_names = json.loads(IMPORT_NAMES_PATH.read_text())
        print(f"loaded {len(real_names)} real names from {IMPORT_NAMES_PATH}")
    else:
        print(f"no {IMPORT_NAMES_PATH} found -- labeling with DLLNAME_ordN only")

    pyghidra.start(install_dir=GHIDRA_INSTALL_DIR)

    with pyghidra.open_program(
        binary_path=None,
        project_location=str(PROJECT_DIR),
        project_name=PROJECT_NAME,
        program_name=PROGRAM_NAME,
        analyze=False,
        nested_project_location=False,
    ) as flat_api:
        from ghidra.program.model.symbol import SourceType

        program = flat_api.getCurrentProgram()
        symtab = program.getSymbolTable()
        listing = program.getListing()

        applied = 0
        with program.openTransaction("label import thunks"):
            for addr_str, info in result["thunks"].items():
                addr = flat_api.toAddr(addr_str)
                dll_short = info["dll"].split("[")[0]
                real = real_names.get(f"{dll_short}!{info['ordinal']}")
                label = label_for(dll_short, info["ordinal"], real_names)
                # idempotency: drop any label(s) a previous run of this script
                # left at this address before adding the current one, so
                # re-running doesn't pile up stale ordinal-only aliases
                # alongside newly-resolved real names.
                for sym in list(symtab.getSymbols(addr)):
                    if sym.getSource() == SourceType.USER_DEFINED:
                        symtab.removeSymbolSpecial(sym)
                try:
                    symtab.createLabel(addr, label, SourceType.USER_DEFINED)
                except Exception as e:
                    print(f"  label failed at {addr_str} ({label}): {e} -- falling back to ordinal-only name")
                    label = f"{dll_short}_ord{info['ordinal']}"
                    symtab.createLabel(addr, label, SourceType.USER_DEFINED)
                code_unit = listing.getCodeUnitAt(addr)
                if code_unit is not None:
                    comment = f"import thunk: {info['dll']} ordinal {info['ordinal']}"
                    if real:
                        comment += f"\nmangled name: {real}"
                    code_unit.setComment(code_unit.PLATE_COMMENT, comment)
                # Also rename the underlying function, if the "Create Function"
                # analyzer already turned this thunk into a Function.
                func = flat_api.getFunctionAt(addr)
                if func is not None:
                    try:
                        func.setName(label, SourceType.USER_DEFINED)
                    except Exception as e:
                        print(f"  function rename failed at {addr_str} ({label}): {e}")
                applied += 1

        print(f"applied {applied} labels")


if __name__ == "__main__":
    main()
