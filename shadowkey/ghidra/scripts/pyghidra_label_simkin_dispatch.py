"""
Label the runtime half of the SimKin bridge: how a name that
SimKinNameTrie_Insert (0x1000de50) registered at startup actually gets
looked up and dispatched to native code when a SimKin script calls it.

Found by looking for a small sibling function compiled right after
SimKinNameTrie_Insert with the same 28-caller count: FUN_1000df3c walks
the identical trie structure (same char/firstChild/nextSibling/value
node layout) but read-only -- no node creation, returns false if any
node on the path is missing or the terminal node's value is still -1,
else writes the resolved index through an out-param and returns true.
This is SimKinNameTrie_Lookup, the read counterpart to
SimKinNameTrie_Insert.

Two of its 28 callers were decompiled and confirm the full runtime
mechanism, and they close out the "how does an index reach its
dispatcher" open question from docs/SIMKIN_BRIDGE.md:

  FUN_10046328 (already known as the wcscmp-miss fallthrough target of
  the "SetSpellType" property handler @ 0x1002e434):
    - pulls the member-name string out of the incoming name atom
      (param_2)
    - calls SimKinNameTrie_Lookup(*(this+0x44) + 0x14dc8, name, &idx)
      using ITS OWN class-specific trie root, not a shared global one
    - on lookup failure, falls through to FUN_1002c848 (next link in
      the chain)
    - on success, `switch(idx)` directly into 9 cases (0-8) of inline
      get/set logic, using SIMKIN_AtomToInt to pull argument values and
      SIMKIN_MakeIntAtom/SIMKIN_ord101/SIMKIN_ord38 to build results

  FUN_1002c848 (the fallthrough target above):
    - byte-identical pattern, own trie root at
      *(this+0x44) + 0x14d74 (a DIFFERENT offset -- its own separate
      trie, not the same one FUN_10046328 used)
    - falls through to FUN_1006ca90 on miss

So the ~28 SimKinNameTrie_Insert-calling registration functions found
earlier and this newly-found chain of ~28 "class member dispatcher"
functions are the same set, 1:1: each registration function builds ONE
class-specific trie (root stored at its own small offset within the
shared object reached via `*(this+0x44)`, e.g. 0x14cf0/0x14d74/0x14dc8),
and each has a matching dispatcher that looks a member name up in that
one trie and switches directly on the resolved index. The chain of
dispatchers (FUN_10046328 -> FUN_1002c848 -> FUN_1006ca90 -> ...) is
just "try my class's members, else ask the next class" -- exactly
mirroring the registration-function chain that built the tries in the
first place. `*(this+0x44)` -- a shared backpointer every per-class
object holds to a central registry -- looks like the same "engine"
object identity established elsewhere in this project (CMap /
render-loop / input-handling all key off one unified engine object).

Run from repo root:
  python shadowkey/ghidra/scripts/pyghidra_label_simkin_dispatch.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LOOKUP_COMMENT = (
    "SimKinNameTrie_Lookup: read-only counterpart to SimKinNameTrie_Insert\n"
    "(0x1000de50). Same node layout (char@+8/value@+0xc/firstChild@+0x14/\n"
    "nextSibling@+0x10), same case-insensitive per-character walk, but never\n"
    "creates nodes: returns false if any node on the path is missing, or if\n"
    "the terminal node's value is still -1 (unregistered); else writes the\n"
    "registered index through the out-param (param_3) and returns true.\n"
    "This is the runtime side of the SimKin bridge -- see docs/SIMKIN_BRIDGE.md,\n"
    "'The runtime dispatch mechanism' section, for the full chain-of-\n"
    "dispatchers architecture this feeds into."
)

DISPATCH_COMMENT_TEMPLATE = (
    "Confirmed example of a SimKin per-class member dispatcher (see\n"
    "docs/SIMKIN_BRIDGE.md, 'The runtime dispatch mechanism'). Pulls the\n"
    "member-name string out of the incoming name atom (param_2), calls\n"
    "SimKinNameTrie_Lookup against this class's OWN trie root at\n"
    "*(this+0x44) + {offset} (a small per-class slot in the shared registry\n"
    "object reached via the +0x44 backpointer -- likely the same unified\n"
    "'engine' object used throughout this project), then switches directly\n"
    "on the resolved index to run inline get/set logic (SIMKIN_AtomToInt to\n"
    "read argument atoms, SIMKIN_MakeIntAtom/SIMKIN_ord101/ord38 to build\n"
    "results). On lookup failure, falls through to {fallthrough} -- the next\n"
    "link in the per-class dispatcher chain, mirroring the ~28-function\n"
    "chain of SimKinNameTrie_Insert registration functions that built these\n"
    "tries at startup."
)

TARGETS = [
    (0x1000df3c, "SimKinNameTrie_Lookup", LOOKUP_COMMENT),
]

DISPATCH_COMMENTS = [
    (0x10046328, "0x14dc8", "FUN_1002c848"),
    (0x1002c848, "0x14d74", "FUN_1006ca90"),
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
    listing = program.getListing()
    addr_factory = program.getAddressFactory()

    from ghidra.program.model.symbol import SourceType

    with program.openTransaction("label SimKin runtime dispatch"):
        for addr_int, name, comment in TARGETS:
            addr = addr_factory.getAddress(hex(addr_int))
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"NO FUNCTION at {hex(addr_int)}")
                continue
            old_name = func.getName()
            func.setName(name, SourceType.USER_DEFINED)
            func.setComment(comment)
            print(f"renamed {old_name} -> {name} @ {hex(addr_int)}")

        for addr_int, offset, fallthrough in DISPATCH_COMMENTS:
            addr = addr_factory.getAddress(hex(addr_int))
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"NO FUNCTION at {hex(addr_int)}")
                continue
            comment = DISPATCH_COMMENT_TEMPLATE.format(offset=offset, fallthrough=fallthrough)
            func.setComment(comment)
            print(f"commented {func.getName()} @ {hex(addr_int)} (trie root offset {offset})")

print("done")
