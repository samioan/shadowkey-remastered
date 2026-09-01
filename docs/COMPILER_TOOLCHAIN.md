# Compiler/toolchain identification

## Conclusion

`6r51.app`'s own game/engine code (the vast majority of the ~2,006
functions) was almost certainly built with **GCC** (the free "GCC-E"
toolchain Nokia bundled with the Series 60 SDK), not ARM's commercial
RVCT/ADS (`armcc`). One small, self-contained ~26KB region (7 functions)
was built with the *other* convention — almost certainly a statically
linked, separately-compiled third-party library, not part of Vir2L's own
codebase.

No compiler banner strings are embedded anywhere in the binary (checked
directly — a stripped release build), so this had to come from codegen
pattern evidence instead of a citation.

## The evidence: frame-pointer chaining

A full APCS-compliant function prologue looks like:

```
mov  ip, sp
stmfd sp!, {..., fp, ip, lr, pc}    ; push, WITH fp+ip in the reglist
sub  fp, ip, #4
```

This threads a linked "frame pointer chain" through every call, for
stack-walking/debugging. **ARM's own compiler (RVCT/ADS, `armcc`) emitted
this by default** in APCS mode. GCC for ARM in this era instead defaults
to a leaner:

```
push {reglist, lr}
...
pop  {reglist, pc}
```

with no `fp`/`ip` bookkeeping at all. Both conventions are equally
ABI-correct — Symbian *required* exactly that, so GCC- and RVCT-built
binaries could freely link against each other (same C++ name mangling,
same vtable layout, same calling convention). So this isn't a correctness
difference, it's a codegen *habit* that survives as a compiler
fingerprint, and it's a well-known way to tell period ARM/RVCT and
ARM/GCC binaries apart.

`tools/fingerprint_compiler.py` implements this check as a byte scan (no
disassembly needed, same style as `resolve_imports.py`'s thunk search):
search for the 4-byte `mov ip, sp` encoding on an instruction boundary,
confirm the next word is really an `STMFD sp!, {...}` with `fp`, `ip`,
`lr`, and `pc` all in the register list (to rule out `mov ip, sp` showing
up for unrelated reasons, e.g. stack-pointer juggling mid-function), then
report where the hits land.

Result for `6r51.app`:

- **7** genuine frame-chain prologues, all falling in a tight
  **26,004-byte span** (`0x100961c4` – `0x1009c758`)
- **1,448** plain `push {reglist, lr}` occurrences everywhere else across
  the ~1MB code section

That's about as clean a signal as this kind of fingerprint ever gives: one
compiler dominates essentially the entire binary, and a single contiguous
region uses the other one exclusively.

## What's in that 26KB region?

Not identified with confidence, but the code shape is a specific,
recognizable kind: disassembling one of the seven functions
(`0x1009c758`) shows dynamically-stack-allocated arrays sized by an
entity/element count (`rsb sp, r3, sp`), indexed adjacency-list-style
traversal over pairs of elements (`ldr r3, [r6, #0x48c]` / `#0x490` /
`#0x890` fields, checked pairwise), and an indirect call through a vtable
slot (`ldr ip, [ip, #0x10]; mov lr, pc; bx ip`) with a `this` pointer and
computed arguments. That shape — graph/adjacency traversal over indexed
elements, not raw rendering or simple utility code — is consistent with a
**licensed pathfinding or level-connectivity library** (plausible given
the game ships `.pth` (path) and `.ent` (entity) asset files), vendored as
a precompiled, RVCT-built static library rather than written in-house.
This is a plausible read of the evidence, not a confirmed identification —
worth revisiting once more of the surrounding code (what calls into this
region, and with what data) is understood.

## Why this matters

This directly informs the Phase 2 strategic decision
([`ROADMAP.md`](ROADMAP.md)) between byte-exact recompilation and
behavioral reverse-engineering: byte-exact matching would need to
reproduce **two** different compilers' exact codegen for the same binary
(free GCC-E for ~98% of it, plus whatever specific RVCT/ADS version built
the vendored ~26KB chunk) — meaningfully harder than a single-toolchain
target, and another reason behavioral RE (matching `port/`'s model in
`rac-decomp`, not `rac1/`'s) is the more tractable path for this project.
