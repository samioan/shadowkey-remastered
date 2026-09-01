"""
Rename and document the key-input dispatch chain, found by tracing who
calls the base-class fallback CCoeControl::OfferKeyEventL (import
CONE!143) -- that caller is the app's own override, and it turned out to
be a full scan-code-to-game-action dispatcher.

See docs/INPUT_HANDLING.md for the full writeup: a flat 21-slot input
state array, the N-Gage numeric keypad (ASCII '0'-'9') as primary game
controls, 4 arrow-shaped scan codes gated through a shared 12-step
"secret sequence" detector, a menu-mode alternate key set, and a
Bluetooth-conditional side effect on 3 codes.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_input_handling.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

DISPATCH_COMMENT = (
    "AppUi_OfferKeyEventL: the app's own CCoeControl::OfferKeyEventL "
    "override -- found by tracing the only caller of the base-class "
    "import CONE!143 (called here as a fallback for unhandled scan "
    "codes). Reads a TKeyEvent's scan code (param_2+4) and maps it to "
    "one of 21 'game action' slots in a flat boolean array at "
    "engine+0x488 (set via InputState_SetButton), keyed by scan code:\n"
    "  0x0e/0x0f/0x10/0x11 -> slots 0/1/2/3 (4 consecutive codes, almost\n"
    "                          certainly the D-pad/arrow keys per\n"
    "                          Symbian's standard TStdScanCode enum --\n"
    "                          not re-verified against a primary-source\n"
    "                          SDK copy this pass, the project's earlier\n"
    "                          archived SDK was session-scratchpad-only\n"
    "                          and is gone)\n"
    "  0x30-0x39 (ASCII '0'-'9', the numeric keypad) -> slots 0xd,4,5,6,\n"
    "                          7,8,9,10,11,12 (i.e. digits are literal\n"
    "                          scan codes here, not looked up -- N-Gage's\n"
    "                          numeric keypad is the primary control set)\n"
    "  0x2a ('*') or 0x85    -> slot 0xe\n"
    "  0x37 ('7')            -> slot 10\n"
    "  0x38 ('8')            -> slot 0xb\n"
    "  0x39 ('9')            -> slot 0xc\n"
    "  0x7f                  -> slot 0xf\n"
    "  1                     -> slot 0x12\n"
    "  0xa7                  -> slot 0x14 (only when engine+0x260 == 0)\n"
    "  0xa4/0xa5             -> slots 0x10/0x11 (only when engine+0x260\n"
    "                          != 0 -- a 'menu open' or text-entry mode\n"
    "                          flag switching to a different key set)\n"
    "4 of these scan codes (0xe/0xf/0x10/0x11, slots 0-3) plus '5'/'7'\n"
    "(slots 8/10) are ALSO checked against a 12-step expected-sequence\n"
    "array at engine+0x414 (progress counter at engine+0x410) -- a\n"
    "hidden cheat-code/Easter-egg system: completing the sequence sets\n"
    "engine+0x14a7a=1 and plays a sound effect via SecretSequence_OnComplete.\n"
    "Slots for codes 0x10/0x11/0x35 ('5') also check\n"
    "*(engine+0x5cc)+0x31 (the GAMECOMMS/Bluetooth object) and fire an\n"
    "extra virtual call when set -- multiplayer-specific, tangential.\n"
    "See docs/INPUT_HANDLING.md."
)

SETSTATE_COMMENT = (
    "InputState_SetButton(inputStateArray /* engine+0x488 */, actionId "
    "/* 0-20 */, value /* pressed/released */): bounds-checked "
    "(actionId < 0x15) write into the flat 21-slot game-action input "
    "state array AppUi_OfferKeyEventL populates from scan codes. See "
    "docs/INPUT_HANDLING.md."
)

SEQUENCE_COMMENT = (
    "Completion handler for AppUi_OfferKeyEventL's hidden 12-step "
    "scan-code sequence detector: plays a sound effect (id 0x57) and "
    "sets a flag at engine+0x14a7a. See docs/INPUT_HANDLING.md."
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

    FUNCS = [
        (0x10021fc0, "AppUi_OfferKeyEventL", DISPATCH_COMMENT),
        (0x1001a6b8, "InputState_SetButton", SETSTATE_COMMENT),
        (0x1001b204, "SecretSequence_OnComplete", SEQUENCE_COMMENT),
    ]

    with program.openTransaction("label input handling"):
        for addr_int, name, comment in FUNCS:
            addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            func.setName(name, SourceType.USER_DEFINED)
            func.setComment(comment)
            print(f"renamed {hex(addr_int)} -> {name}")

print("done")
