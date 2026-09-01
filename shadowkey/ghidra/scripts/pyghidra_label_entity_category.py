"""
Label the resolution of EntityTypeDescriptor's third field (entities.txt's
third %d, descriptor+0x10) as an entity-category enum -- see
docs/ZONE_FORMAT.md's "thirdField resolved" section. Verified directly
against the real entities.txt data (not just code inference): value 8 is
"container" (confirmed via typeId 300 = "!bag_loot"), 11 is "door", etc.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_entity_category.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

CATEGORY_COMMENT = (
    "descriptor+0x10 = entities.txt's third %d column = entity category enum:\n"
    "1=prop 2=monster 3=misc-loot 4=weapon 5=spell 6=armor 7=merchant\n"
    "8=container 9=consumable 10=trap 11=door 12=trapped 14=scroll\n"
    "15=shield 16=unique. Verified against real entities.txt (typeId 300 =\n"
    "'300 30 8 !bag_loot' = category 8/container). See docs/ZONE_FORMAT.md."
)

FUNC_COMMENTS = {
    0x1002C3A8: (
        None,
        "Monster on-death handler: spawns typeId 300 (entities.txt: '300 30 8\n"
        "!bag_loot') at the dying actor's position -- the loot-bag-drop\n"
        "mechanic. Re-derives category==8 from the descriptor before flagging\n"
        "the spawned object as a container (+0x180/+0x181=1). See\n"
        "docs/ZONE_FORMAT.md.",
    ),
    0x10084438: (
        None,
        "Generic version of FUN_1002c3a8: spawns whatever typeId is stored at\n"
        "the calling object's +0x2f0, flags it as a container (+0x180=1) if\n"
        "its entities.txt category is 8. See docs/ZONE_FORMAT.md.",
    ),
    0x1003F130: (
        None,
        "Inventory/item search: matches a wanted entities.txt category against\n"
        "a candidate's descriptor+0x10, with a special case -- category 5\n"
        "(spell) also matches category 0xe/14 (scroll), i.e. spell scrolls\n"
        "count as castable spells. See docs/ZONE_FORMAT.md.",
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
    from ghidra.program.model.symbol import SourceType

    with program.openTransaction("label entity category enum"):
        for addr_int, (name, comment) in FUNC_COMMENTS.items():
            addr = addr_factory.getAddress(hex(addr_int))
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            if name and func.getName() != name:
                func.setName(name, SourceType.USER_DEFINED)
            existing = func.getComment() or ""
            if comment not in existing:
                new_comment = (existing + "\n\n" + comment).strip() if existing else comment
                func.setComment(new_comment)
            print(f"labeled {hex(addr_int)} -> {func.getName()}")

        # also drop the category-enum note on EntityTypeDescriptor_Lookup itself
        lookup_addr = addr_factory.getAddress(hex(0x1008C1CC))
        lookup_func = fm.getFunctionAt(lookup_addr)
        if lookup_func is not None:
            existing = lookup_func.getComment() or ""
            if CATEGORY_COMMENT not in existing:
                new_comment = (existing + "\n\n" + CATEGORY_COMMENT).strip() if existing else CATEGORY_COMMENT
                lookup_func.setComment(new_comment)
            print(f"labeled {hex(0x1008C1CC)} -> {lookup_func.getName()} (category enum note)")

print("done")
