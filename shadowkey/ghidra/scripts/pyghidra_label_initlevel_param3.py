"""
Rename GameEngine_InitLevel's 3rd parameter and document what it gates,
resolved by tracing its sole call chain (GameEngine_InitLevel <-
FUN_10027d0c <- FUN_10027d44 <- FUN_10019780 / FUN_10069cac) and both of
its use sites inside GameEngine_InitLevel itself: it selects between a
full/fresh zone entry (reset player position from the .ent player-start
record, load .stn's lock/trap-difficulty overrides) and a lighter reload
that leaves both alone. See docs/ZONE_FORMAT.md.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_initlevel_param3.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

COMMENT = (
    "param_3 = isFullEntry: gates two things inside this function, not\n"
    "the .stn loader alone as first suspected. Non-zero =\n"
    "(a) the .ent player-start record (typeId==1) writes the player's\n"
    "position/orientation, and (b) <zone>.stn loads at all. Zero skips\n"
    "both -- player keeps their current position, no .stn resistDisarm[]\n"
    "overrides are (re)applied. Traced end to end via its only call chain:\n"
    "FUN_10027d0c (thread entry) <- FUN_10027d44 (spawns the thread) <-\n"
    "FUN_10019780 / FUN_10069cac (a per-tick load-state machine). See\n"
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

    from ghidra.program.model.symbol import SourceType

    with program.openTransaction("rename GameEngine_InitLevel param_3"):
        addr = addr_factory.getAddress(hex(0x10024DEC))
        func = fm.getFunctionAt(addr)
        if func is None:
            print("WARNING: GameEngine_InitLevel not found at 0x10024dec")
        else:
            params = func.getParameters()
            renamed = False
            for p in params:
                if p.getName() == "param_3":
                    p.setName("isFullEntry", SourceType.USER_DEFINED)
                    p.setComment(COMMENT)
                    renamed = True
                    print(f"renamed param_3 -> isFullEntry on {func.getName()}")
            if not renamed:
                # No formal parameters are committed for this function (the
                # decompiler's param_1/param_2/param_3 names are inferred on
                # the fly, not real Parameter objects) -- fall back to
                # documenting the finding as a function-comment addendum.
                print("no formal 'param_3' Parameter object exists; adding a comment addendum instead")
                existing = func.getComment() or ""
                if COMMENT not in existing:
                    new_comment = (existing + "\n\n" + COMMENT).strip() if existing else COMMENT
                    func.setComment(new_comment)

print("done")
