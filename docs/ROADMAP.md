# Roadmap

## Phase 0 — setup (done)

- [x] Identify the target: `6r51.app`, a Symbian EKA1 E32Image ARM binary
      (see [`E32IMAGE_FORMAT.md`](E32IMAGE_FORMAT.md)). Everything else in
      the install image is either stock Symbian platform middleware
      (`system/libs/*.dll`) or SimKin script source (`system/apps/6r51/*.s`,
      already plaintext) — not decompilation targets.
- [x] Write `tools/e32image.py` to parse the header/imports/exports and
      extract the raw code section + its load address.
- [x] Prove out the Ghidra side of the pipeline: headless-import the
      extracted code section as a raw ARM binary at `0x10000000`
      (`ARM:LE:32:v4t`, `apcs` compiler spec — see below for why), confirm
      auto-analysis runs cleanly and produces disassembly/functions.

## Phase 1 — static analysis in Ghidra (done)

- [x] Resolve every import call site structurally, no SDK needed: found
      the fixed-byte "IAT thunk" veneer pattern, matched all 415 thunks to
      their exact `(dll, ordinal)` via IAT slot order (verified: bijective,
      zero gaps/collisions). Automated end-to-end —
      `tools/resolve_imports.py` computes the table,
      `shadowkey/ghidra/scripts/pyghidra_label_imports.py` applies it as
      real labels/function names inside the Ghidra project (confirmed
      persisted and showing up in the decompiler output). See
      [`E32IMAGE_FORMAT.md`](E32IMAGE_FORMAT.md#resolving-import-calls-without-any-sdk-def-file)
      and [`tools/README.md`](../tools/README.md).
- [x] Found the entry point's real logic (`iEntryPoint` offset 0 -> code
      base): it's a dead `mov r0,#0; bx lr` stub, never called at runtime
      — matches the `iNoCallEntryPoint` flag bit decoded from the header.
      The exported factory function (ordinal 1, `0x1002841c`) is confirmed
      standard `NewApplication()` boilerplate — see the worked example in
      `E32IMAGE_FORMAT.md`.
- [x] Ghidra scripting is now real Python (not just Jython/headless
      postScript, which hit a "PyGhidra script provider claims .py but
      Python not started" wall under plain `analyzeHeadless`): installed
      Ghidra's bundled `pyghidra` pip package
      (`Ghidra/Features/PyGhidra/pypkg`), which opens/edits the existing
      project directly from a normal Python process. Gotchas hit and
      fixed: pass `nested_project_location=False` for a project created by
      `analyzeHeadless` (not PyGhidra itself); use
      `program.openTransaction(...)` (AutoCloseable) not
      `startTransaction`/`endTransaction` (leaves the transaction open,
      breaks the context manager's auto-save on exit).
- [x] Identify the compiler/toolchain that built `6r51.app`. **Answer:
      GCC** for essentially the whole binary (1,448 plain
      `push {reglist,lr}` prologues), except one self-contained ~26KB / 7
      function region built with ARM's own RVCT/ADS (`armcc` — genuine
      APCS frame-pointer-chained prologues), almost certainly a vendored
      third-party library rather than Vir2L's own code. Evidence, method,
      and the reusable `tools/fingerprint_compiler.py` check are in
      [`COMPILER_TOOLCHAIN.md`](COMPILER_TOOLCHAIN.md). This settles (for
      now) that byte-exact recompilation would mean reproducing *two*
      different period compilers, not one — another point toward
      behavioral RE over byte-exact matching for this project (see Phase 2
      below).
- [x] Get real function *names* (not just `DLLNAME_ord<N>`) for the
      imports. **319 of 415 resolved.** The EKA2L1 `epoc6.def` catalog
      lead from the first pass turned out to be a dead end (hash-keyed,
      needs a real ROM to match against) — what actually worked was
      fetching the **Series 60 v0.9 SDK** ("Nokia Edition", targets
      Symbian OS 6.1 — the exact OS `6r51.app` targets), extracting its
      MSI/CAB installer without running it, and parsing the real
      ARM-hardware-target import libraries (`EUSER.LIB`, `CONE.LIB`, etc.,
      *not* the Windows-emulator-target ones, which use an incompatible
      ordinal scheme). Full methodology, provenance, and verification in
      [`IMPORT_NAMES.md`](IMPORT_NAMES.md) — worth reading in full, it's a
      good example of how deep "download an SDK" actually went (MSI
      database parsing to disambiguate build targets, reverse-engineering
      the `.LIB`'s `ar`-archive-with-`ds<N>.o`-members layout, two
      independent verifications before trusting it). Remaining 96
      unresolved: `SIMKIN` (64, third-party, source is public but not yet
      attempted), `GAMECOMMS`/`NOKIAFC` (28, N-Gage-specific, no SDK
      covers these), `MEDIACLIENTAUDIOSTREAM` (1, not in this SDK
      revision), plus 3 individual ordinals missing from this SDK's
      tables for unknown reasons. `shadowkey/import_names.json` holds the
      resolved table (tracked in git — small, derived, not a copy of the
      SDK); `shadowkey/ghidra/scripts/pyghidra_label_imports.py` applies
      it, confirmed live in decompiler output.

## Phase 2 — decide the decompilation strategy

Two roads, not mutually exclusive:

- **Byte-exact recompilation** (rac-decomp's model): only viable once the
  exact compiler + linker + codegen flags are identified and reproducible.
  ARM/Symbian toolchains from this era (GCC "gnupoc"/`arm-epoc-pe-gcc`, or
  ARM ADS/RVCT `armcc`+`armlink`) are more obscure than the PS2 EE-GCC
  chain from the prior project — expect this to take real investigation
  before it's known to be tractable at all.
- **Behavioral reverse-engineering** (ashen-decomp's model, for the sibling
  N-Gage game *Ashen*): recover function purpose/structure and produce
  readable, *not necessarily byte-identical*, C — faster to get useful
  results, doesn't depend on finding the original toolchain.

Pick per-function/per-subsystem rather than committing globally up front;
revisit once Phase 1's toolchain findings are in.

## Phase 3 — function-level work

Not planned in detail yet — depends on Phase 2's outcome. If byte-exact
becomes viable, expect a `splat`-equivalent split/build/verify loop similar
to `rac1/` in the prior PS2 project, adapted for E32Image instead of ELF.

## Open questions

- Is `6r51.app`'s single `.app` really the *entire* game engine, or does it
  load additional overlay/plugin binaries at runtime that aren't visible as
  static `.dll`/`.app` files in the install image? (SimKin scripts drive
  a lot of game logic already, per the `.s` files — worth confirming how
  much of "the game" is actually inside the binary vs. interpreted script.)
- Would the **Series 60 v1.2 SDK** (June 2003, also cataloged by
  Symbian-Archive, also OS 6.1) resolve any of the 3 individual ordinals
  missing from the v0.9 SDK's tables, or add `MEDIACLIENTAUDIOSTREAM`?
  Not checked — v0.9 already got 319/415, diminishing-returns territory
  unless one of those specific ordinals turns out to matter.
