"""
Rename and document the real SimKin native-name resolution mechanism:
a hand-rolled case-insensitive trie (prefix tree) mapping every
script-visible name (native methods, properties, pages -- ~700 of them)
to a small sequential integer index, built once at startup from
GameEngine_ctor. See docs/SIMKIN_BRIDGE.md for the full writeup --
this is what actually resolves names like "ConfigKeysMenu"/
"SetSpellType", not literal string matching at call time (an earlier
round's search for those names came up empty only because it searched
ASCII bytes; they're stored UTF-16LE).

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_simkin_trie.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

TRIE_INSERT_COMMENT = (
    "SimKinNameTrie_Insert(root, nameUtf16, index): inserts a "
    "case-insensitive string key into a hand-rolled trie (24-byte "
    "nodes: char@+8, value@+0xc [-1=unset], firstChild@+0x14, "
    "nextSibling@+0x10, vtable@+4), walking/creating one node per "
    "character. At the string's terminator, if the node's value is "
    "still -1 it gets set to `index` and the call returns true (newly "
    "registered); if already set, returns false (duplicate, not "
    "overwritten). 703 total call sites across ~28 sibling functions "
    "(0x10010804-0x1001537c) all ultimately called from "
    "GameEngine_ctor -- this is the real name-resolution mechanism for "
    "~700 SimKin-visible native bindings (methods/properties/pages), "
    "each mapped to a small sequential integer used for dispatch "
    "afterward. See docs/SIMKIN_BRIDGE.md."
)

REGISTER_COMMENT = (
    "One of ~28 sibling functions (address range "
    "0x10010804-0x1001537c, all called from GameEngine_ctor either "
    "directly or transitively) that register SimKin-visible native "
    "binding names into the shared trie via SimKinNameTrie_Insert -- "
    "confirmed via real examples: registers \"ConfigKeysDefault\" at "
    "index 0x11 and \"ConfigKeysMenu\" at index 0x12 (both UTF-16LE, "
    "found via pyghidra_find_string_and_refs.py). The other ~27 "
    "sibling functions weren't individually named this pass. See "
    "docs/SIMKIN_BRIDGE.md."
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

    with program.openTransaction("label SimKin name trie"):
        addr = addr_factory.getDefaultAddressSpace().getAddress(0x1000de50)
        func = fm.getFunctionAt(addr)
        if func is None:
            print("WARNING: no function at 0x1000de50")
        else:
            func.setName("SimKinNameTrie_Insert", SourceType.USER_DEFINED)
            func.setComment(TRIE_INSERT_COMMENT)
            print("renamed 0x1000de50 -> SimKinNameTrie_Insert")

        addr2 = addr_factory.getDefaultAddressSpace().getAddress(0x10011008)
        func2 = fm.getFunctionAt(addr2)
        if func2 is None:
            print("WARNING: no function at 0x10011008")
        else:
            func2.setName("SimKin_RegisterNativeBindings_1", SourceType.USER_DEFINED)
            func2.setComment(REGISTER_COMMENT)
            print("renamed 0x10011008 -> SimKin_RegisterNativeBindings_1")

print("done")
