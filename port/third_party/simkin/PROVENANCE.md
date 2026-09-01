# Vendored: Simkin for C++ 2.23

- Source: https://sourceforge.net/projects/simkincpp/files/Simkin%20for%20C%2B%2B/2.23/SimkinCpp223.zip
- Fetched: 2026-09-01
- License: LGPL v2 (or later) -- see `LICENSE.txt`, copied verbatim from
  the archive's `Docs/Simkin/LGPL.txt`. Copyright 1996-2004 Simon
  Whiteside. Unmodified upstream source; only a subset of files is kept
  (see below), each still carrying its original copyright header.
- `src/` is the archive's `Simkin/cpp/src/` directory, filtered to only
  `*.cpp`/`*.h`/`*.inl`/`*.y`/`*.l` (dropped: `.dsp`/`.dsw`/`.vcproj`/
  `.mmp`/`.def`/`bld.inf` IDE and Symbian-SDK build-system files, not
  needed and not useful outside their original toolchains).
- This port only builds the **TreeNode** flavor of the library (see
  `Src/Makefile`'s `treenode_objects` list upstream) -- the interpreter
  core plus the native `.s` script parser/executable-binding machinery,
  with no XML backend (Expat/Xerces/MSXML) linked in, since the game's
  `.s` scripts use Simkin's native script syntax, not its XML data
  format. The XML-only source files are still vendored (harmless, kept
  for upstream fidelity) but excluded from `port/CMakeLists.txt`.
- Not modified from upstream except where compiling on x64 MSVC made it
  impossible not to (recorded here, and marked `PORT PATCH` in-place):
  - `src/skInterpreter.cpp`, `setValue()`'s `Interpreter.debugBreak=1`
    handler: upstream's `_asm int 03h` is 32-bit-only inline assembly,
    which MSVC does not support at all on x64 (no inline asm on that
    target -- a hard compile error, not a warning). Replaced with
    `__debugbreak()`, MSVC's portable intrinsic for the same trap.
