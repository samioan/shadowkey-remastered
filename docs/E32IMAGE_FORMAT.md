# E32Image format notes (6r51.app)

`6r51.app` is a Symbian OS **E32Image** — the native executable format for
EPOC/Symbian, not ELF or PE. It uses the *old* (EKA1, pre-Symbian-8) header
revision: no compression, no security/capability block, fixed 124-byte
(`0x7C`) header. This is the version documented in Symbian's own
`e32tools/INC/E32IMAGE.H` (`E32ImageHeader`, copyright 1996-2001) — later
Symbian OS versions (EKA2, Symbian 8+) extended this with an
`E32ImageHeaderV` carrying deflate/bytepair compression and capability data,
which does **not** apply here.

Every field offset below was cross-checked against the real header bytes of
`6r51.app` (see the annotated dump at the bottom) — not just transcribed from
the header file blind.

## Header layout (`E32ImageHeader`, 124 bytes)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0x00 | 4 | `iUid1` | `0x1000007A`=EXE, `0x10000079`=DLL |
| 0x04 | 4 | `iUid2` | app-framework recognizer UID (`0x100039CE` = "this is an app") |
| 0x08 | 4 | `iUid3` | the app's own unique UID |
| 0x0C | 4 | `iCheck` | checksum of the 3 UIDs |
| 0x10 | 4 | `iSignature` | ASCII `"EPOC"` |
| 0x14 | 4 | `iCpu` | `0x1000`=X86, `0x2000`=ARM, `0x4000`=M\*Core |
| 0x18 | 4 | `iCheckSumCode` | sum of all 32-bit words in .text |
| 0x1C | 4 | `iCheckSumData` | sum of all 32-bit words in .data |
| 0x20 | 4 | `iVersion` | packed `TVersion` |
| 0x24 | 8 | `iTime` | `TInt64` link timestamp |
| 0x2C | 4 | `iFlags` | bit0=DLL, bit1=no-call-entry-point, bit2=fixed-address-exe (more bits seen in practice than these 3 constants) |
| 0x30 | 4 | `iCodeSize` | code + IAT + const data + export dir |
| 0x34 | 4 | `iDataSize` | initialised data |
| 0x38 | 4 | `iHeapSizeMin` | |
| 0x3C | 4 | `iHeapSizeMax` | |
| 0x40 | 4 | `iStackSize` | |
| 0x44 | 4 | `iBssSize` | |
| 0x48 | 4 | `iEntryPoint` | **offset into code**, not absolute |
| 0x4C | 4 | `iCodeBase` | link-time base address of the code segment |
| 0x50 | 4 | `iDataBase` | link-time base address of the data segment |
| 0x54 | 4 | `iDllRefTableCount` | number of imported DLLs |
| 0x58 | 4 | `iExportDirOffset` | **file** offset of the export table |
| 0x5C | 4 | `iExportDirCount` | number of exports |
| 0x60 | 4 | `iTextSize` | size of just .text (also the IAT's offset within the code section) |
| 0x64 | 4 | `iCodeOffset` | file offset of the code section |
| 0x68 | 4 | `iDataOffset` | file offset of the data section |
| 0x6C | 4 | `iImportOffset` | file offset of the import section |
| 0x70 | 4 | `iCodeRelocOffset` | file offset of code relocations |
| 0x74 | 4 | `iDataRelocOffset` | file offset of data relocations |
| 0x78 | 4 | `iPriority` | `TProcessPriority` (350 = foreground) |

No compression field exists in this revision — **file offsets point directly
at real section bytes**, no deflate/bytepair pass needed.

### Export table addresses are code-relative, not absolute

The export directory (at `iExportDirOffset`, `iExportDirCount` entries, 4
bytes each) stores **offsets relative to `iCodeBase`**, the same convention
`iEntryPoint` itself documents in its own comment ("offset into code of
entry point"). This was confirmed empirically: `6r51.app`'s single export
entry holds the raw value `0x0002841c`, far smaller than `iCodeBase`
(`0x10000000`) — it only makes sense as `iCodeBase + 0x2841c =
0x1002841c`. Don't assume these are pre-relocated absolute VAs the way
later (EKA2/armcc-linked) E32Images sometimes store them.

### Import section layout

At `iImportOffset`: a 4-byte section size, then `iDllRefTableCount` blocks of:
- `iOffsetOfDllName` (u32, **relative to the start of the import section**)
  pointing at a NUL-terminated DLL name
- `iNumberOfImports` (i32)
- that many u32 ordinals imported from that DLL

## Worked example: `6r51.app`

```
UID1/2/3   : 0x10000079 (DLL) / 0x100039ce (KUidApp) / 0x101fd3f6 (Shadowkey's app UID)
type       : DLL, cpu=ARM, priority=350 (foreground)
code       : base=0x10000000 size=0x1009c4 fileOffset=0x7c
data       : base=0x00000000 size=0x0        (no initialised data section)
entry point: code-relative 0x0 -> abs 0x10000000
export dir : fileOffset=0x100a3c count=1
    ordinal 1: code-relative 0x2841c -> abs 0x1002841c
imports    : 20 DLLs (EUSER, CONE, EIKCORE, AVKON, APPARC, BITGDI, GDI, WS32,
             ESOCK, ETEL, EFSRV, ESTLIB, FBSCLI, MSGS, HAL, EZLIB,
             MEDIACLIENTAUDIOSTREAM, GAMECOMMS, NOKIAFC, SIMKIN)
```

Everything else in the install image's `system/libs/` (`ogles.dll`,
`http.dll`, `ssl70.dll`, etc.) is stock Symbian/N-Gage platform middleware,
not game code — out of scope. `simkin.dll` alongside `6r51.app` is
[SimKin](http://simkin.co.uk/), an open-source embeddable scripting
language; the `.s` files scattered through `system/apps/6r51/` are its
plaintext script source (dialogue, menus, AI — see e.g. `action_queue.s`),
already human-readable and not part of the binary decompilation target.
**`6r51.app` is the entire decompilation target.**

## Resolving import calls without any SDK .def file

Every call to an imported DLL function goes through a fixed 12-byte "IAT
thunk" veneer placed in the code section, byte-identical except for a
trailing literal:

```
e59fc004   ldr  ip, [pc, #4]     ; load the *address* of this import's IAT slot
e59cc000   ldr  ip, [ip]         ; load the *resolved function pointer* from that slot
e12fff1c   bx   ip               ; tail-call it
<4 bytes>  literal: absolute address of the IAT slot itself
```

Because the 12-byte prefix is identical across every thunk, they can all be
found with a plain byte search (`tools/resolve_imports.py`) — no
disassembly needed. Each thunk's trailing literal points into the Import
Address Table, the flat pointer-slot array living in
`[iTextSize, iCodeSize)` of the code section (confirmed for `6r51.app`:
`(iCodeSize - iTextSize) / 4 == 417` slots for 415 actual imports, 2 slots
of alignment padding). **Slot index N corresponds exactly to the Nth
import in declaration order** across the import section (DLL block by DLL
block, ordinal by ordinal within each block) — verified exhaustively: all
415 indices 0..414 are hit by exactly one thunk each, zero gaps, zero
collisions.

So: `thunk literal -> IAT slot index -> (dll, ordinal)`, purely
structurally, with no period Symbian SDK `.def` files needed. This gets
every call site to a `DLLNAME ordinal N` label; getting from there to the
*real* function name/signature still needs external reference data (a
`.def` file, or — for SimKin specifically — its public source), but the
call-site resolution itself is now solved and automatable. See
[`tools/README.md`](../tools/README.md) for the Ghidra-labeling script
that applies this across the whole project.

As a worked example, `6r51.app`'s exported ordinal 1 (`NewApplication`,
`0x1002841c`) decompiles — after running the labeling script — to:

```c
undefined4 * FUN_1002841c(void)
{
  undefined4 *puVar1;
  puVar1 = (undefined4 *)EUSER_ord1582(0x22c);
  if (puVar1 != (undefined4 *)0x0) {
    EIKCORE_ord222(puVar1);
    *puVar1 = DAT_10028448;
  }
  return puVar1;
}
```

Structurally this is `return new CShadowkeyApplication;`: allocate
`0x22c` bytes (`EUSER ord 1582`, almost certainly `operator new(TUint)`),
call a base-class constructor imported from `EIKCORE` (`ord 222`) with the
allocated pointer, then install the derived class's own vtable pointer
(`DAT_10028448`) at the call site — exactly what you'd expect if
`CShadowkeyApplication` has no explicit constructor body of its own (the
compiler installs the *derived* vtable at the outer call site precisely
because the imported base constructor only installs the *base* vtable).
This is standard, AppWizard-generated Symbian application boilerplate.

## Reference

- [BinaryBacktrace: Nokia N-Gage and Symbian File Formats](https://binarybacktrace.com/formats/n-gage)
- [Symbian `e32tools/INC/E32IMAGE.H`](https://github.com/loociano/symbian-build-tools/blob/master/e32tools/INC/E32IMAGE.H) (EKA1-era header, matches this binary)
- [`E32Explorer`](https://github.com/mrRosset/E32Explorer) — GUI E32Image viewer, useful for cross-checking `tools/e32image.py`'s output
- [EKA2L1](https://github.com/EKA2L1/EKA2L1) — Symbian OS emulator; its loader is a working reference implementation if the parser above ever disagrees with reality
