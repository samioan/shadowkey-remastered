"""
Rename and document the full "InputState" class discovered adjacent to
InputState_SetButton (0x1001a6b8) in the address space -- a whole
small object, much richer than the flat 21-byte array first assumed:
current + previous button-state buffers (edge detection), a 17-entry
remappable logical-action-to-byte-offset indirection table, and a
21-entry resource-string-ID + flag binding-registration table (the
game's default control bindings, each tagged with a Symbian resource
string ID -- likely UI labels for a controls/options menu).

See docs/INPUT_HANDLING.md for the full writeup.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_input_state_class.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LAYOUT_NOTE = (
    "InputState object layout (all offsets relative to this, i.e. "
    "engine+0x488): +0x00..0x14 (21 bytes) current button state; "
    "+0x15..0x29 (21 bytes) previous-frame button state (edge "
    "detection); +0x2c..0x6f (17x4 bytes) a remappable logical-action "
    "-> byte-offset indirection table; +0x6c..0xaf (21x4 bytes) "
    "resource-string-ID per action slot; +0xc0..0xd4 (21 bytes) a "
    "per-slot flag (1 for the 16 core/remappable actions, 0 for 4 "
    "fixed system actions that mostly share one resource ID). See "
    "docs/INPUT_HANDLING.md."
)

FUNCS = [
    (0x1001a690, "InputState_GetButton",
     "Direct getter: this[slot], bounds-checked < 0x15. " + LAYOUT_NOTE),
    (0x1001a6f0, "InputState_GetButton2",
     "Byte-identical logic to InputState_GetButton (0x1001a690) but a "
     "separate compiled function with different call sites -- exact "
     "distinction between the two not determined. " + LAYOUT_NOTE),
    (0x1001a6dc, "InputState_GetButtonPrev",
     "Getter for the previous-frame mirror array: this[0x15+slot]. " + LAYOUT_NOTE),
    (0x1001a67c, "InputState_ResolveBindingOffset",
     "Looks up the byte-offset a logical action (0-0x10) is currently "
     "bound to, via the 17-entry indirection table at this+0x2c. "
     "Returns 0x15 (out of range) if slot >= 0x11. " + LAYOUT_NOTE),
    (0x1001a6c4, "InputState_SetBoundButton",
     "Setter via the indirection table: this[this[0x2c+slot*4]] = "
     "value, for slot < 0x10. Called from GameTick_UpdateAndPresent. " + LAYOUT_NOTE),
    (0x1001a700, "InputState_GetBoundButtonPrev",
     "Indirected getter into the previous-frame array. " + LAYOUT_NOTE),
    (0x1001a724, "InputState_GetBoundButton",
     "Indirected getter into the current-frame array -- e.g. slot "
     "0xf (15) is checked by the screen-state machine "
     "(FUN_10068e0c) to skip a splash screen. " + LAYOUT_NOTE),
    (0x1001a744, "InputState_ClearAll",
     "memset's both the current and previous-frame 21-byte arrays to "
     "0. " + LAYOUT_NOTE),
    (0x1001a770, "InputState_RegisterBinding",
     "Registers one action slot's default resource-string ID "
     "(this+0x6c+slot*4) and a flag (this+0xc0+slot) -- called 21 "
     "times by InputState_InitDefaultBindings with sequential resource "
     "IDs 0xd05-0xd16. Likely UI labels for a controls/options menu; "
     "resolving the actual text needs the app's compiled Symbian "
     "resource file, not located this pass. " + LAYOUT_NOTE),
    (0x1001a064, "InputState_InitDefaultBindings",
     "Constructs the default control bindings: clears state "
     "(InputState_ClearAll) then registers 21 action slots (0-0x14) "
     "with sequential resource-string IDs 0xd05-0xd16. Slots 0-15 "
     "(0-0xf) get unique IDs and flag=1 (16 core/remappable gameplay "
     "actions); slots 0x10/0x11/0x12/0x14 get flag=0 and mostly reuse "
     "resource ID 0xd15 (4 fixed system actions -- matches "
     "AppUi_OfferKeyEventL's 'menu mode' scan codes 0xa4/0xa5/0xa7 "
     "mapping to these same slots). Slot 0x13 (19) is never "
     "registered -- a gap. See docs/INPUT_HANDLING.md."),
]

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

    with program.openTransaction("label InputState class"):
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
