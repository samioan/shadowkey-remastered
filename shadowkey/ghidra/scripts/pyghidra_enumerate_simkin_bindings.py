"""
Enumerate the full SimKin native-binding API surface: every
(class, name, index) triple registered by the ~28 sibling functions
that call SimKinNameTrie_Insert (0x1000de50) from GameEngine_ctor, and
correlate each registration function with its matching runtime
dispatcher (the sibling that calls SimKinNameTrie_Lookup, 0x1000df3c,
against the same trie-root offset) -- see docs/SIMKIN_BRIDGE.md.

Unlike the earlier one-off manual trace (which had to read raw
disassembly because a truncated single-call-site view dropped args),
the FULL decompiled C for these functions shows all 3 SimKinNameTrie_Insert
arguments directly, e.g.:

    FUN_1000de50(*(undefined4 *)(param_1 + 0x14cf0), DAT_100113f8, 0xb);

So this script decompiles each registration/dispatcher function in full,
regexes out the call arguments, resolves each DAT_xxx literal-pool
symbol to its UTF-16LE string (following pointer indirection as needed),
and writes the result to shadowkey/simkin_native_bindings.json --
committed on purpose (like shadowkey/import_names.json): this is a
derived table of compiled-in native API identifiers (method/property
names), not a copy of any copyrighted narrative/asset content.

Run from repo root (can take a few minutes -- ~28 large functions):
  python shadowkey/ghidra/scripts/pyghidra_enumerate_simkin_bindings.py
"""
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

INSERT_ADDR = 0x1000DE50
LOOKUP_ADDR = 0x1000DF3C
OUT_PATH = REPO_ROOT / "shadowkey" / "simkin_native_bindings.json"

CALL_RE = re.compile(
    r"(?:FUN_1000de50|SimKinNameTrie_Insert)\s*\(\s*([^,]+),\s*([^,]+),\s*([^)]+)\)"
)
OFFSET_RE = re.compile(r"\+\s*(0x[0-9a-fA-F]+)\)")
DAT_RE = re.compile(r"(DAT_[0-9a-fA-F]+|PTR_[A-Za-z0-9_]+)")


def resolve_string(listing, addr_factory, symbol_name, depth=4):
    """Follow a DAT_/PTR_ symbol through up to `depth` pointer
    indirections until it hits string data, decode as UTF-16LE."""
    st = program.getSymbolTable()
    syms = list(st.getSymbols(symbol_name))
    if not syms:
        return None
    addr = syms[0].getAddress()
    for _ in range(depth):
        data = listing.getDataAt(addr)
        if data is None:
            data = listing.getDefinedDataContaining(addr)
        if data is None:
            # try raw UTF-16LE decode directly from memory
            return read_raw_utf16(addr)
        dt = str(data.getDataType()).lower()
        if "unicode" in dt or "string" in dt:
            try:
                return str(data.getValue())
            except Exception:
                return read_raw_utf16(addr)
        try:
            val = data.getValue()
            if hasattr(val, "getUnsignedValue"):
                ival = val.getUnsignedValue()
            elif hasattr(val, "getOffset"):
                ival = val.getOffset()
            else:
                ival = int(val)
            addr2 = addr_factory.getAddress(hex(int(ival)))
            addr = addr2
        except Exception as e:
            return read_raw_utf16(addr)
    return read_raw_utf16(addr)


def read_raw_utf16(addr):
    mem = program.getMemory()
    chars = []
    try:
        for i in range(256):
            lo = mem.getByte(addr.add(i * 2)) & 0xFF
            hi = mem.getByte(addr.add(i * 2 + 1)) & 0xFF
            code = lo | (hi << 8)
            if code == 0:
                break
            chars.append(chr(code))
    except Exception:
        pass
    s = "".join(chars)
    return s if s and s.isprintable() else None


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
    refman = program.getReferenceManager()

    from ghidra.app.decompiler import DecompInterface
    from ghidra.util.task import ConsoleTaskMonitor

    decomp = DecompInterface()
    decomp.openProgram(program)
    monitor = ConsoleTaskMonitor()

    def callers_of(addr_int):
        addr = addr_factory.getAddress(hex(addr_int))
        result = set()
        for ref in refman.getReferencesTo(addr):
            f = fm.getFunctionContaining(ref.getFromAddress())
            if f is not None:
                result.add(f)
        return result

    insert_callers = callers_of(INSERT_ADDR)
    lookup_callers = callers_of(LOOKUP_ADDR)
    print(f"{len(insert_callers)} insert callers, {len(lookup_callers)} lookup callers")

    def decompile_text(func):
        res = decomp.decompileFunction(func, 120, monitor)
        if not res.decompileCompleted():
            print(f"  DECOMPILE FAILED: {func.getName()} @ {func.getEntryPoint()}")
            return None
        return res.getDecompiledFunction().getC()

    # --- pass 1: registration functions -> (offset, [(name, index), ...]) ---
    registrations = {}  # func_entry_str -> {"offset": "0x...", "entries": [[name, idx], ...]}
    dat_cache = {}

    for func in sorted(insert_callers, key=lambda f: f.getEntryPoint().getOffset()):
        entry = str(func.getEntryPoint())
        print(f"decompiling registration fn {func.getName()} @ {entry} ...")
        code = decompile_text(func)
        if code is None:
            continue
        entries = []
        offset_seen = None
        for m in CALL_RE.finditer(code):
            arg1, arg2, arg3 = (g.strip() for g in m.groups())
            om = OFFSET_RE.findall(arg1)
            if om:
                offset_seen = om[-1]
            dm = DAT_RE.search(arg2)
            if not dm:
                continue
            sym = dm.group(1)
            if sym not in dat_cache:
                dat_cache[sym] = resolve_string(listing, addr_factory, sym)
            name = dat_cache[sym]
            if name is None:
                continue
            try:
                idx = int(arg3, 16) if arg3.lower().startswith("0x") else int(arg3)
            except ValueError:
                continue
            entries.append([name, idx])
        registrations[entry] = {
            "func": func.getName(),
            "offset": offset_seen,
            "entries": entries,
        }
        print(f"  -> offset={offset_seen} entries={len(entries)}")

    # --- pass 2: dispatcher functions -> offset (to correlate with pass 1) ---
    dispatch_offset_re = re.compile(r"(?:FUN_1000df3c|SimKinNameTrie_Lookup)\s*\(\s*([^,]+),")
    dispatchers = {}  # offset -> func name/entry
    for func in sorted(lookup_callers, key=lambda f: f.getEntryPoint().getOffset()):
        entry = str(func.getEntryPoint())
        code = decompile_text(func)
        if code is None:
            continue
        m = dispatch_offset_re.search(code)
        offset = None
        if m:
            om = OFFSET_RE.findall(m.group(1))
            if om:
                offset = om[-1]
        dispatchers[entry] = {"func": func.getName(), "offset": offset}
        print(f"dispatcher {func.getName()} @ {entry} -> offset={offset}")

    # --- correlate by offset ---
    offset_to_reg = {v["offset"]: k for k, v in registrations.items() if v["offset"]}
    offset_to_disp = {v["offset"]: k for k, v in dispatchers.items() if v["offset"]}
    classes = []
    all_offsets = set(offset_to_reg) | set(offset_to_disp)
    for off in sorted(all_offsets, key=lambda o: int(o, 16) if o else 0):
        reg_entry = offset_to_reg.get(off)
        disp_entry = offset_to_disp.get(off)
        classes.append({
            "trie_root_offset": off,
            "registration_func": registrations[reg_entry]["func"] if reg_entry else None,
            "registration_addr": reg_entry,
            "dispatcher_func": dispatchers[disp_entry]["func"] if disp_entry else None,
            "dispatcher_addr": disp_entry,
            "bindings": {name: idx for name, idx in registrations[reg_entry]["entries"]} if reg_entry else {},
        })

    total_bindings = sum(len(c["bindings"]) for c in classes)
    print(f"\n{len(classes)} classes, {total_bindings} total bindings resolved")

    OUT_PATH.write_text(json.dumps({"classes": classes}, indent=2), encoding="utf-8")
    print(f"wrote {OUT_PATH}")

print("done")
