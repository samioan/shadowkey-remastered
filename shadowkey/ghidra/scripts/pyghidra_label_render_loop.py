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
        "two vtable methods on ScreenModeController (via engine+0x30, "
        "+0x28) -- NOT a clean Update()/Render() split, see below -- then "
        "calls PresentFrame_BlitAndComputeFPS. The slow/startup path (else "
        "branch) memsets exactly 0x11e00 bytes (176*208*2 -- the N-Gage's "
        "exact screen buffer size) and draws via CCoeControl's window GC "
        "directly for the first ~61 ticks (splash?).",
    ),
    0x10023ad0: (
        "GameEngine_FirstTickBootstrap",
        "Called once, on the first real tick (engine+0x34==0 guard) from "
        "GameTick_UpdateAndPresent. `new`s the ~85.5KB (0x14e14 byte) main "
        "engine object via GameEngine_ctor, stores it at "
        "appview+0x30 -- this is THE central engine/world object every "
        "other offset chain in this file hangs off of. Also `new`s and "
        "binds a SimKin interpreter instance (SIMKIN_ord20) at engine+0x3a0, "
        "then registers ~50 named script bindings (SIMKIN_ord42/43/60 "
        "calls) -- see docs/RENDER_LOOP.md's SimKin-binding note. Also "
        "`new`s ScreenModeController at engine+0x28 via its ctor "
        "(FUN_1002958c) and calls its one-time init (vtable+4, offset "
        "0x10) if this is a truly fresh start.",
    ),
    0x1000fa7c: (
        "GameEngine_ctor",
        "C++ constructor for the ~85.5KB main engine/world object "
        "(GameEngine_FirstTickBootstrap `new`s param_1 then calls this). "
        "Initializes two fixed-capacity entity arrays: 40 slots x 132 "
        "bytes at engine+0x5464, and 32 slots x 264 bytes at engine+0xbe54 "
        "(each element's ctor called via FUN_1001b3a8 / FUN_1001b448 "
        "respectively) -- likely a scene/actor pool and a light/effect "
        "pool. Also registers 48 (0x2f+1) SimKin-visible slots at "
        "engine+0xdfa4 (SIMKIN_ord22 per slot) -- fits the roadmap's note "
        "that SimKin scripts drive most game-entity logic.",
    ),
    0x1002958c: (
        "ScreenModeController_ctor",
        "C++ constructor for a ~440-byte (0x1b8) subsystem object, `new`d "
        "at engine+0x28 by GameEngine_FirstTickBootstrap. Name is a "
        "medium-confidence guess: its vtable methods (see below) are "
        "driven by an internal state field at (this+0x78) with SimKin "
        "script calls interleaved, which reads like a screen/menu/dialog "
        "mode state machine rather than 3D simulation or rendering "
        "directly -- NOT confirmed to be a literal Update()/Render() pair, "
        "correcting an earlier guess. Sets up a 12-bit-RGB color palette "
        "(this+8/0xc/0x16c..0x17a) and a 2KB scratch buffer (this+0x18c), "
        "consistent with UI/overlay chrome rather than the 3D scene "
        "itself.",
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

# Plate-comment only (not confident enough to rename yet) -- the four
# ScreenModeController vtable slots actually observed being called.
# Vtable itself lives at 0x100fb908 (secondary vtable, object+4), read via
# pyghidra_read_vtable.py.
COMMENT_ONLY = {
    0x100298a4: (
        "ScreenModeController vtable slot +0x10 (secondary vtable @ "
        "0x100fb908). Called once from GameEngine_FirstTickBootstrap "
        "guarded by engine+0x34==0 -- one-time init/ConstructL-equivalent "
        "for this subsystem."
    ),
    0x10029cb0: (
        "ScreenModeController vtable slot +0x1c (secondary vtable @ "
        "0x100fb908). Called every tick from GameTick_UpdateAndPresent's "
        "fast path. Large (~400 line decompile) state-machine dispatcher "
        "keyed on this+0x78, with SimKin calls (SIMKIN_ord43) and further "
        "vtable dispatch through other objects -- reads like UI/dialog/menu "
        "mode handling, not confirmed as 'the' render call. See "
        "docs/RENDER_LOOP.md."
    ),
    0x1002a6d4: (
        "ScreenModeController vtable slot +0x24 (secondary vtable @ "
        "0x100fb908). Called every tick from GameTick_UpdateAndPresent's "
        "fast path, right after the +0x1c call above. Same "
        "state-machine-over-this+0x78 shape as the +0x1c method. See "
        "docs/RENDER_LOOP.md."
    ),
    0x1006c274: (
        "ScreenModeController vtable slot +0x38 (secondary vtable @ "
        "0x100fb908). Called conditionally on pause/resume-looking "
        "transitions in FUN_1002152c (engine+0x30 -> +0x28 state checks "
        "in range 0x14..0x1a). Lives in the 0x1006Bxxx-0x1006Dxxx code "
        "cluster along with several other vtable slots (0x2c, 0x30, 0x34, "
        "0x40) -- that cluster is the next place to look for the real "
        "3D-rendering / world-simulation code, per docs/RENDER_LOOP.md."
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

        from ghidra.program.model.listing import CodeUnit
        for addr_int, comment in COMMENT_ONLY.items():
            addr = addr_factory.getDefaultAddressSpace().getAddress(addr_int)
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"SKIP comment {hex(addr_int)}: not a function")
                continue
            listing.setComment(addr, CodeUnit.PLATE_COMMENT, comment)
            print(f"commented {hex(addr_int)} ({func.getName()})")

print("done")
