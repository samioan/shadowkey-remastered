"""
Print the string value of one or more defined-data addresses (e.g. the
DAT_xxxxxxxx literal-pool constants the decompiler shows for format
strings). Usage:

    python shadowkey/ghidra/scripts/pyghidra_read_strings.py 0x10060c58 0x10060d40 ...
"""
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import pyghidra  # noqa: E402

GHIDRA_INSTALL_DIR = Path(r"C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC")
PROJECT_DIR = REPO_ROOT / "shadowkey" / "ghidra"

addrs = sys.argv[1:]

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
    listing = program.getListing()
    addr_factory = program.getAddressFactory()

    for a in addrs:
        addr = addr_factory.getAddress(a)
        data = listing.getDataAt(addr)
        if data is None:
            data = listing.getDefinedDataContaining(addr)
        print(f"{a}: ", end="")
        if data is None:
            print("<no defined data>")
            continue
        try:
            val = data.getValue()
        except Exception as e:
            val = f"<error: {e}>"
        dt = str(data.getDataType())
        # follow one level of pointer indirection automatically
        if "undefined4" in dt or "pointer" in dt.lower():
            try:
                addr2 = addr_factory.getAddress(hex(int(val)))
                data2 = listing.getDataAt(addr2)
                if data2 is not None:
                    val2 = data2.getValue()
                    print(f"-> {addr2}: {val2!r}  dataType={data2.getDataType()}")
                    continue
            except Exception:
                pass
        print(repr(val), " dataType=", dt)
