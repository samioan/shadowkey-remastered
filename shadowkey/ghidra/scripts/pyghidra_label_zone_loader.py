"""
Apply real names + plate comments for the zone-loading / model-index
resolution chain found by tracing debug strings like
"InitLevel Pre load_models" -- see docs/ZONE_FORMAT.md.

Run from repo root: python shadowkey/ghidra/scripts/pyghidra_label_zone_loader.py
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

LABELS = {
    0x10024DEC: (
        "GameEngine_InitLevel",
        "Zone/level loader -- the single monolithic function that loads all\n"
        "per-zone files for a level: <zone>_models.txt, .sur, .zon, .pth, .ent.\n"
        "Found via debug strings it references: 'InitLevel Pre load_models',\n"
        "'InitLevel Post load_models', 'InitLevel Post Zones',\n"
        "'InitLevel Pre Fog', 'InitLevel Pre Entities'.\n"
        "See docs/ZONE_FORMAT.md for the full per-file breakdown and how a\n"
        "placed .ent object's +0x54 model pointer gets resolved via the\n"
        "engine+0x6b38 model-index cache and the engine+0xbe34 type-descriptor\n"
        "BST (EntityTypeDescriptor_Lookup).",
    ),
    0x10060AC0: (
        "ZoneModelList_Load",
        "Loads <zone>_models.txt (format '%d %d %d %d %s' per line = archive\n"
        "index, 3 flags, model name; 'NULL.bin' = empty slot). For each real\n"
        "entry, calls ModelArchive_LoadByIndex(archiveObj, archiveIndex) and\n"
        "stores the result into the caller's 256-slot model-pointer cache\n"
        "(engine+0x6b38, param_2 here) at [archiveIndex], plus 3 flag fields\n"
        "into a parallel 8-byte-stride array (engine+0x6f38, param_3 here).\n"
        "See docs/ZONE_FORMAT.md.",
    ),
    0x10068948: (
        "ModelArchive_Open",
        "Opens the global models.idx + models.huge archive (path built from\n"
        "'z:\\system\\apps\\6R51\\' + 'models' + '.idx'/'.huge'). Reads the\n"
        "u32 count then count*8 bytes of raw (offset,size) pairs from\n"
        "models.idx into a count*32-byte in-memory array (extra per-entry\n"
        "bytes beyond the on-disk 8 are scratch/cache fields). See\n"
        "docs/MODEL_FORMAT.md for the verified on-disk format and\n"
        "docs/ZONE_FORMAT.md for this loader.",
    ),
    0x10068B68: (
        "ModelArchive_LoadByIndex",
        "Given an archive index (0..236, the same index documented in\n"
        "docs/MODEL_FORMAT.md), seeks models.huge to that entry's offset and\n"
        "reads its exact size into a freshly allocated buffer -- this is the\n"
        "single choke point that turns a models.idx index into a loaded\n"
        "model-resource pointer.",
    ),
    0x1008C1CC: (
        "EntityTypeDescriptor_Lookup",
        "Binary search tree lookup keyed by entity type ID (node layout:\n"
        "key@+8, data@+0xc, left@+0x10, right@+0x14). Tree root is\n"
        "engine+0xbe34. Returned data pointer's own +0xc field holds the\n"
        "models.idx archive index for that entity type -- see\n"
        "docs/ZONE_FORMAT.md for how a placed .ent object's +0x54 model\n"
        "pointer is resolved through this.",
    ),
    0x10068854: (
        "EntityTypeConfig_Load",
        "Loads the single global z:\\system\\apps\\6R51\\entities.txt (NOT\n"
        "per-zone -- only caller is GameEngine_FirstTickBootstrap, i.e. one-\n"
        "time engine startup). Each line 'typeId modelArchiveIndex thirdField\n"
        "name' (sscanf '%d %d %d %254s') becomes a 0x94-byte\n"
        "EntityTypeDescriptor, inserted into the engine+0xbe34 BST via\n"
        "EntityTypeDescriptor_Insert, keyed by typeId. See docs/ZONE_FORMAT.md.",
    ),
    0x1008C22C: (
        "EntityTypeDescriptor_Insert",
        "Ordered binary-search-tree insert keyed by typeId (allocates an\n"
        "0x18-byte node: tag@+4, key@+8, data@+0xc, left@+0x10, right@+0x14).\n"
        "This is entities.txt's insert side of the engine+0xbe34 tree that\n"
        "EntityTypeDescriptor_Lookup walks -- see docs/ZONE_FORMAT.md.",
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

    with program.openTransaction("label zone loader chain"):
        for addr_int, (name, comment) in LABELS.items():
            addr = addr_factory.getAddress(hex(addr_int))
            func = fm.getFunctionAt(addr)
            if func is None:
                print(f"WARNING: no function at {hex(addr_int)}")
                continue
            if func.getName() != name:
                func.setName(name, SourceType.USER_DEFINED)
            existing = func.getComment() or ""
            if comment not in existing:
                new_comment = (existing + "\n\n" + comment).strip() if existing else comment
                func.setComment(new_comment)
            print(f"labeled {hex(addr_int)} -> {name}")

print("done")
