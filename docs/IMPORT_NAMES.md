# Real import names

`shadowkey/import_names.json` maps `"DLL!ordinal"` (e.g. `"EUSER!1582"`)
to the real mangled Symbian symbol name (e.g. `"__nw__5CBaseUi"`, i.e.
`CBase::operator new(TUint)`) for every import `6r51.app` actually calls,
where a name is known — **319 of the 415 total** (see
[`ROADMAP.md`](ROADMAP.md) for the ones that aren't, and why). Combined
with [`tools/resolve_imports.py`](../tools/resolve_imports.py)'s
structural `(dll, ordinal)` resolution (see
[`E32IMAGE_FORMAT.md`](E32IMAGE_FORMAT.md#resolving-import-calls-without-any-sdk-def-file)),
this gets real function names into Ghidra's decompiler output — e.g. the
exported `NewApplication` factory now decompiles to:

```c
undefined4 * FUN_1002841c(void)
{
  undefined4 *puVar1;
  puVar1 = (undefined4 *)EUSER____nw__5CBaseUi(0x22c);
  if (puVar1 != (undefined4 *)0x0) {
    EIKCORE____15CEikApplication(puVar1);
    *puVar1 = DAT_10028448;
  }
  return puVar1;
}
```

`EIKCORE____15CEikApplication` (`CEikApplication::CEikApplication()`)
confirms, from a real source this time, what
[`E32IMAGE_FORMAT.md`](E32IMAGE_FORMAT.md)'s original structural read of
this function already suspected: `CShadowkeyApplication` has no
explicit constructor of its own — it just inherits `CEikApplication`'s,
with the derived vtable patched in by the caller.

## Where this data came from, and why it's trustworthy

This was the item [`ROADMAP.md`](ROADMAP.md) previously listed as
blocked: EKA2L1's `epoc6.def` catalog turned out to be hash-keyed (needs
a real device ROM to hash-match against, which we don't have), and a
period Symbian 6.1 SDK existed but hadn't been fetched. This time it was:

1. Downloaded the **Series 60 SDK v0.9, "Nokia Edition"** (2003, targets
   Symbian OS 6.1 — the exact OS `6r51.app` targets) via the link
   cataloged by [Symbian-Archive](https://github.com/mrRosset/Symbian-Archive)
   (`mediafire.com/download/18n6wo75k0svknt/_s60+0.9+sdk.zip` — the
   direct mediafire.com download page's real CDN link, extracted from the
   page's `downloadX.mediafire.com` anchor since the plain `/download/`
   URL just serves an HTML landing page now).
2. It's an MSI installer + Cabinet (`.cab`) files, not a plain archive.
   Extracted the `.cab` files directly with Windows' built-in
   `expand.exe` — no need to actually run the installer.
3. **Critical, easy to get wrong**: a Symbian SDK ships import libraries
   for *multiple* build targets — the real ARM hardware target
   (`\epoc32\release\armi\urel\`, also `\thumb\urel\` — same ordinals,
   different instruction-set codegen) and the Windows/x86 **emulator**
   target (`\epoc32\release\wins\` / `\winsb\`), which is a real Win32
   DLL with a *completely different, incompatible* ordinal numbering
   scheme for the same-named functions. The installer's `.cab` files
   extract flat (no directory structure, colliding filenames disambiguated
   with numeric suffixes like `EUSER.LIB`, `EUSER.LIB1`, `EUSER.LIB2`...),
   so which numbered file is which build target isn't obvious from the
   extracted tree alone. Resolved this rigorously, not by guessing: parsed
   the **MSI database itself** (`File`/`Component`/`Directory` tables,
   via Python's stdlib `msilib`) to recover each file's real installed
   path and confirm which extracted copy came from `armi\urel\`.
4. `EUSER.LIB` (and the rest) turned out to be a plain **GNU `ar`
   archive** (`!<arch>\n` magic — consistent with
   [`COMPILER_TOOLCHAIN.md`](COMPILER_TOOLCHAIN.md)'s finding that the
   main codebase is GCC-built) containing one member per exported
   ordinal, named exactly `ds<N>.o` where **N is the literal decimal
   ordinal number** — no need to trust archive *position* as a proxy for
   ordinal (unlike EKA2L1's alphabetized catalog). Each member's content
   is a tiny COFF-ish object with the mangled symbol name present as a
   plain ASCII string. `tools/parse_symbian_lib.py` implements this.
5. Verified before trusting it, two independent ways:
   - `EUSER` ordinal 7 → `AddBefore__15TDblQueLinkBaseP15TDblQueLinkBase`,
     exactly matching the name (though not the ordinal) EKA2L1's
     `epoc6.def` catalog already had.
   - `EUSER` ordinal 1582 → `__nw__5CBaseUi` (`CBase::operator new(TUint)`)
     — matches, from a real source, what pure call-site *behavior*
     analysis had already inferred for this exact ordinal in `6r51.app`
     (see `E32IMAGE_FORMAT.md`'s original `NewApplication` writeup, done
     *before* this SDK was ever fetched).
   - All 1,679 `EUSER` ordinals present, zero gaps.

## Regenerating or extending this table

The SDK itself is **not** in this repo (large, copyrighted, third-party
install media — same policy as the game image itself, see the root
`.gitignore`). To regenerate `shadowkey/import_names.json` or resolve
more of the 96 still-missing ordinals:

```
python tools/resolve_import_names.py \
    "<path to 6r51.app>" \
    "<dir containing EUSER.LIB, CONE.LIB, ... from a period SDK's armi\urel\>" \
    --json shadowkey/import_names.json
```

## What's still unresolved (96 ordinals)

- **`SIMKIN`** (64 ordinals): third-party, not a core Symbian OS library
  — no `.LIB` for it in this SDK. Source is public
  ([simkin.co.uk](http://www.simkin.co.uk/)); not attempted yet.
- **`GAMECOMMS`** (27) / **`NOKIAFC`** (1): N-Gage-platform-specific, not
  generic Symbian OS — wouldn't be in any Series 60 SDK regardless of
  version. See [`ROADMAP.md`](ROADMAP.md) for the (inconclusive so far)
  GameComms reverse-engineering lead.
- **`MEDIACLIENTAUDIOSTREAM`** (1): not present in this v0.9 SDK — might
  be in a later Series 60 SDK revision; low priority for just one ordinal.
- A handful of individual ordinals (1 each in `BITGDI`, `CONE`, `FBSCLI`)
  that this exact SDK's `.LIB` doesn't have an entry for, for reasons not
  yet investigated.
