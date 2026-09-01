"""
Document the newly-found .stn per-zone file (an 8th zone file, missed
earlier because it's loaded conditionally on GameEngine_InitLevel's third
parameter) plus corrections to the .zon room record and .zmp header notes.
See docs/ZONE_FORMAT.md.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_stn.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

ADDENDUM = (
    "ADDENDUM: an 8th per-zone file, <zone>.stn ('skTreeNodes' per its\n"
    "debug-marker bracket), loads conditionally on param_3 during the\n"
    "'Init scripts'/'Done init scripts' phase -- u16 count + (varRef,\n"
    "objectName) Pascal-string pairs, binds named lockable objects\n"
    "(doors/containers) into a global SimKin resistDisarm[] array via\n"
    "FUN_100732c8 name lookup, overwriting the found object's +0x3c field.\n"
    "Also: .zon's room record is now fully decoded (4 u16 header fields at\n"
    "+0x00/+0x02/+0x04/+0x06 + 64-byte name at +0x08), and .zmp's header\n"
    "first 32 bytes turned out to be the zone's SimKin level-script base\n"
    "name (used to build <name>.s, loaded via SIMKIN_ord59). See\n"
    "docs/ZONE_FORMAT.md."
)

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
    addr_factory = program.getAddressFactory()

    with program.openTransaction("document .stn / .zon / .zmp findings"):
        addr = addr_factory.getAddress(hex(0x10024DEC))
        func = fm.getFunctionAt(addr)
        if func is None:
            print("WARNING: GameEngine_InitLevel not found at 0x10024dec")
        else:
            existing = func.getComment() or ""
            if ADDENDUM not in existing:
                new_comment = (existing + "\n\n" + ADDENDUM).strip() if existing else ADDENDUM
                func.setComment(new_comment)
            print(f"updated comment on {func.getName()} @ {hex(0x10024DEC)}")

print("done")
