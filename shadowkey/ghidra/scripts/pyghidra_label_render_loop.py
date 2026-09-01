"""
Apply Phase 3's first concrete finding as real labels in the Ghidra
project: the main game loop is a CPeriodic timer firing every 40ms
(25Hz), and the per-tick callback drives update + present.

See docs/RENDER_LOOP.md for the full writeup/evidence this is based on.
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LABELS = {
    0x1002821c: (
        "InitGameLoopTimer_CPeriodic",
        "Starts the main CPeriodic loop timer: Start(delay=1us, "
        "interval=40000us, TCallBack=FUN_10028200). 40ms interval "
        "== hardcoded 25Hz tick rate for the whole game (update+render "
        "both happen inside the tick callback below). See RENDER_LOOP.md.",
    ),
    0x10028200: (
        "GameTick_TCallBackTrampoline",
        "TCallBack target registered with CPeriodic::Start in "
        "InitGameLoopTimer_CPeriodic. Trampoline into GameTick_UpdateAndPresent.",
    ),
    0x1002256c: (
        "GameTick_UpdateAndPresent",
        "Fires every 40ms (25Hz) from the CPeriodic timer. Grabs the raw "
        "backbuffer pointer (FBSCLI::DataAddress), on the fast path calls "
        "two vtable methods on an engine subsystem object (likely "
        "Update()/Render()), then calls PresentFrame_BlitAndComputeFPS. "
        "The slow/startup path (else branch) memsets exactly 0x11e00 bytes "
        "(176*208*2 -- the N-Gage's exact screen buffer size) and draws via "
        "CCoeControl's window GC directly for the first ~61 ticks (splash?).",
    ),
    0x10022898: (
        "PresentFrame_BlitAndComputeFPS",
        "Activates the window's graphics context, computes a smoothed FPS "
        "over a rolling 10-sample window (stored at engine+0xbe20), "
        "optionally draws a debug overlay, BitBlts the backbuffer bitmap "
        "into the window, deactivates the GC. This is where the game "
        "presents each frame to the screen.",
    ),
}

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
    listing = program.getListing()

    with program.openTransaction("label render loop functions"):
        for addr_int, (name, comment) in LABELS.items():
            addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"SKIP {hex(addr_int)}: not a function")
                continue
            try:
                from ghidra.program.model.symbol import SourceType
                func.setName(name, SourceType.USER_DEFINED)
            except Exception as e:
                print(f"FAILED to rename {hex(addr_int)}: {e}")
                continue
            from ghidra.program.model.listing import CodeUnit
            listing.setComment(addr, CodeUnit.PLATE_COMMENT, comment)
            print(f"labeled {hex(addr_int)} -> {name}")

print("done")
