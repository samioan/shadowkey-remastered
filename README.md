# shadowkey-decomp

A decompilation project for **The Elder Scrolls Travels: Shadowkey**
(Vir2L Studios, 2004), targeting the retail Nokia N-Gage release.

## The target

The install image (`The-Elder-Scrolls-Travels-Shadowkey_N-Gage_.../`, kept
out of git — see `.gitignore`) is a Symbian OS 6.1 filesystem dump, not a
disc/ROM image. Inside it, exactly one file is the actual game:

- **`system/apps/6r51/6r51.app`** — a Symbian **E32Image** ARM binary (not
  ELF, not PE). This is the whole decompilation target. See
  [`docs/E32IMAGE_FORMAT.md`](docs/E32IMAGE_FORMAT.md) for the file format
  itself (there's no public Symbian E32Image spec and no existing Ghidra
  loader for it, so this had to be worked out from the format's own
  `E32IMAGE.H` and verified against this binary's actual header bytes).

Everything else in the image is either:
- stock Symbian/N-Gage platform middleware (`system/libs/*.dll`:
  `ogles.dll`, `http.dll`, `ssl70.dll`, etc.) — out of scope, and
- [SimKin](http://simkin.co.uk/) script source (`system/apps/6r51/*.s`,
  imported by `6r51.app` via `simkin.dll`) — plaintext dialogue/menu/AI
  scripts, already human-readable, not a decompilation target.

## Structure

- `docs/` — format notes and the project roadmap. Start with
  [`docs/ROADMAP.md`](docs/ROADMAP.md) for current status, then
  [`docs/E32IMAGE_FORMAT.md`](docs/E32IMAGE_FORMAT.md),
  [`docs/COMPILER_TOOLCHAIN.md`](docs/COMPILER_TOOLCHAIN.md), and
  [`docs/IMPORT_NAMES.md`](docs/IMPORT_NAMES.md).
- `tools/` — `e32image.py` (header/import/export parsing + code
  extraction), `resolve_imports.py` (structural import call-site
  resolution), `parse_symbian_lib.py` + `resolve_import_names.py` (real
  import names from a period SDK), `fingerprint_compiler.py` (GCC vs.
  RVCT/ADS detection). See [`tools/README.md`](tools/README.md) for usage.
- `shadowkey/` — the actual decomp project: `ghidra/` (Ghidra project,
  regenerable, plus its `scripts/`), `extracted/` (code section +
  load-address metadata pulled from `6r51.app`, regenerable),
  `import_names.json` (tracked — small derived data, not a copy of any
  SDK; see `docs/IMPORT_NAMES.md`).

## Status

Phase 1 (static analysis) is done: the E32Image format is fully parsed,
the code section imports cleanly into Ghidra with clean auto-analysis,
every one of the 415 import call sites is resolved to its `(dll,
ordinal)` — 319 of those to real function names — and the compiler
toolchain is identified (GCC for the main codebase, one small
statically-linked ARM RVCT/ADS-built library). All of it is live in the
Ghidra project's decompiler output, not just written down in docs.

Phase 2 is decided: **behavioral reverse-engineering**, not byte-exact
recompilation — the project's actual goal is a PC port, and the compiler
finding (GCC main codebase + one vendored ARM RVCT/ADS library) makes
byte-exact matching meaningfully harder than a single-toolchain target
for no benefit that goal needs. See
[`docs/ROADMAP.md`](docs/ROADMAP.md#phase-2--decompilation-strategy-decided--behavioral-re-not-byte-exact)
for the full rationale.

Phase 3 has its first concrete finding: the main game loop is a
`CPeriodic` OS timer with a **hardcoded 40ms (25Hz) interval**, driving a
single tick callback that does both simulation update and frame present
together. This directly explains the porting discussion's "low fps"
symptom — likely a fixed-tick-rate design decision rather than a raw
performance ceiling, meaning the port fix is probably decoupling
update-rate from present-rate. See
[`docs/RENDER_LOOP.md`](docs/RENDER_LOOP.md) for the full call chain and
evidence.
