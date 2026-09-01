#!/usr/bin/env python
"""
Fingerprints which parts of 6r51.app's code section were built with a
frame-pointer-chaining ("APCS") compiler (ARM's own RVCT/ADS -- "armcc")
versus a non-chaining one (GCC's typical ARM defaults for this era).

The signature: a full APCS-compliant function prologue looks like

    mov  ip, sp
    stmfd sp!, {..., fp, ip, lr, pc}   ; push, WITH fp+ip in the reglist
    sub  fp, ip, #4

which threads a linked "frame pointer chain" through every call for stack
walking/debugging. ARM's compiler emitted this by default in APCS mode;
GCC for ARM in this era defaults to a leaner

    push {reglist, lr}
    ...
    pop  {reglist, pc}

with no fp/ip bookkeeping at all. Both conventions are ABI-compatible
(Symbian required exactly that, so GCC- and RVCT-built binaries could link
against each other) -- so this isn't about calling-convention correctness,
it's a codegen habit that survives as a compiler fingerprint.

Usage:
    python fingerprint_compiler.py <code.bin> [<load_address_hex>]
"""
import struct
import sys

MOV_IP_SP = bytes.fromhex("0dc0a0e1")  # E1A0C00D, little-endian


def find_frame_chain_prologues(code: bytes, base: int) -> list:
    """Returns load addresses of genuine 'mov ip,sp; push {...fp,ip,lr,pc}' prologues."""
    hits = []
    start = 0
    while True:
        i = code.find(MOV_IP_SP, start)
        if i == -1:
            break
        start = i + 1
        if i % 4 != 0:
            continue  # not on an instruction boundary -- byte coincidence
        if i + 8 > len(code):
            continue
        word = struct.unpack_from("<I", code, i + 4)[0]
        if (word & 0xFFFF0000) != 0xE92D0000:  # not STMFD sp!, {...}
            continue
        reglist = word & 0xFFFF
        fp, ip, lr, pc = 1 << 11, 1 << 12, 1 << 14, 1 << 15
        if reglist & fp and reglist & ip and reglist & lr and reglist & pc:
            hits.append(base + i)
    return hits


def count_plain_push_lr(code: bytes) -> int:
    """Counts ARM STMFD sp!, {reglist, lr} occurrences (GCC-style leaf prologue)."""
    count = 0
    for off in range(0, len(code) - 4, 4):
        word = struct.unpack_from("<I", code, off)[0]
        if (word & 0xFFFF0000) == 0xE92D0000 and (word & (1 << 14)):
            count += 1
    return count


def main():
    path = sys.argv[1]
    base = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x10000000
    code = open(path, "rb").read()

    chained = find_frame_chain_prologues(code, base)
    plain = count_plain_push_lr(code)

    print(f"genuine APCS frame-chain prologues (RVCT/ADS-style): {len(chained)}")
    for a in chained:
        print(f"  {a:#010x}")
    print(f"plain 'push {{reglist,lr}}' occurrences (GCC-style):   {plain}")

    if chained:
        span = max(chained) - min(chained)
        print(f"\nframe-chain hits span {span} bytes ({min(chained):#x} - {max(chained):#x})")
        print("-> if these cluster tightly while the rest of the binary doesn't use this "
              "convention, that's a strong signal of one statically-linked, differently-"
              "compiled library sitting inside an otherwise GCC-built binary.")


if __name__ == "__main__":
    sys.exit(main())
