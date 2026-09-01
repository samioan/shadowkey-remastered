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

## Phase 2 — decompilation strategy: DECIDED — behavioral RE, not byte-exact

**Decision: behavioral reverse-engineering** (ashen-decomp's model — recover
function purpose/structure, produce readable C/C++, *not* byte-identical
to the original), not byte-exact recompilation (`rac1`'s model). Not
provisional — this is the plan going forward. Rationale:

- **The project's actual goal is a PC port** (widescreen, uncapped/fixed
  frame rate — see the porting discussion this roadmap grew out of), not
  preservation-grade archival fidelity. A port needs *correct* logic it's
  free to modify, not a binary-identical recompile — byte-exactness would
  be solving a harder problem than the one that's actually wanted.
- **Two different compilers, not one** (`COMPILER_TOOLCHAIN.md`): GCC for
  ~98% of the binary, ARM RVCT/ADS for one small (~26KB) vendored library.
  Byte-exact would mean reproducing *both* exactly — a materially harder
  bar than `rac1`'s single, well-known PS2 GCC. The GCC side alone is
  already the obscure, semi-lost "gnupoc"/`arm-epoc-pe-gcc` cross
  toolchain (unlike PS2 EE-GCC, which is well-documented and has an
  active decomp community); the RVCT/ADS side is a commercial compiler
  building an unidentified third-party library, so even *finding* the
  right compiler version to match against is likely unknowable, not just
  hard.
- **Scale**: real function count from the analyzed Ghidra project is
  2,006, with a genuine long tail (17 functions over 4KB, one 19KB
  monster). Byte-exact, compiler-flag-matching decompilation at this
  scale is a months-long undertaking even with a single, well-known
  toolchain (`rac1` took ~7-8 weeks for a comparable function count with
  exactly that advantage) — behavioral RE is faster per function and,
  critically, doesn't block on solving the toolchain-identification
  problem above before any real progress can start.

This doesn't preclude ever attempting byte-exact matching for the ~98%
GCC-built portion specifically, if someone wants preservation-grade
fidelity later — it's just explicitly not what Phase 3 is organized
around, and shouldn't gate anything.

## Phase 3 — function-level work (behavioral RE)

Not a `splat`-style split/build/verify loop (that's the byte-exact
model) — instead, a per-function/per-subsystem loop of: disassemble in
Ghidra (imports already labeled with real names where known — 319/415,
see `IMPORT_NAMES.md`) → decompile → understand → write clean,
independently-compilable C/C++ that reproduces the *behavior*, named
using real Symbian class/method names wherever the mangled import names
give them away (e.g. `EIKCORE____15CEikApplication` → this function
constructs a `CEikApplication`) → move on. No requirement to match the
original binary byte-for-byte, no requirement to reproduce
compiler-specific codegen quirks.

Prioritization, driven by the port goal rather than raw coverage:

- **In scope early**: whatever the render loop, camera/projection setup,
  input handling, and world/entity update logic actually are — these
  gate a playable port and are exactly where the aspect-ratio and
  frame-timing fixes from the original porting discussion will need to
  land.
- **Deprioritized**: `GAMECOMMS`/`ESOCK`/`ETEL`-driven Bluetooth
  multiplayer code (~50 import ordinals of supporting infrastructure) —
  skippable or stubbable for a single-player-first port.
- **Mostly free**: SimKin-scripted game logic (dialogue, menus, a lot of
  AI) doesn't need decompiling at all — the `.s` files are already
  plaintext and SimKin itself is open source. Time here is better spent
  understanding *how* `6r51.app` drives the SimKin interpreter (the 64
  `SIMKIN` import ordinals, still unresolved to real names — see
  `IMPORT_NAMES.md`) than decompiling engine-side glue.
- The vendored ~26KB RVCT/ADS library (`COMPILER_TOOLCHAIN.md`) is
  behaviorally decompilable like everything else — the two-compiler
  finding only matters for the (now-moot) byte-exact question, not for
  reading what the code does.

- [x] Concrete next step, done: identify the render/game-loop entry
      point(s) by tracing outward from the `WS32`/`GDI`/`BITGDI`/`FBSCLI`
      import call sites. **Found**: a `CPeriodic` OS timer started with a
      **hardcoded 40ms interval (25Hz)** drives a single tick callback
      that does both simulation update *and* frame present (blit +
      smoothed-FPS bookkeeping) in one place. This is a strong, concrete
      lead for the porting discussion's "low fps" complaint — likely a
      deliberate fixed-tick-rate design decision from the N-Gage
      hardware, not a raw rendering performance ceiling, and the real
      port fix is probably decoupling update-tick-rate from
      present-rate rather than just changing the constant. Full chain,
      evidence, and labeled functions in
      [`RENDER_LOOP.md`](RENDER_LOOP.md). Also ruled out the binary's
      single largest function (19KB) as a loop candidate — it's a
      localized string-table lookup (121 cases, matches the release's 5
      bundled languages), not game logic.

Next: keep pulling on this thread — find what the two per-tick vtable
calls inside `GameTick_UpdateAndPresent` actually dispatch to (candidate
`Update()`/`Render()` split), and locate the actual 3D rasterization
code, likely reachable from there.

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
