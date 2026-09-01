# tools/

Third-party/shared tooling. Not project source; this directory is
gitignored except for the files below.

- **`e32image.py`** — parser/extractor for the Symbian E32Image format (see
  [`../docs/E32IMAGE_FORMAT.md`](../docs/E32IMAGE_FORMAT.md)). Tracked in
  git because it *is* project code, just living in `tools/` alongside
  everything else needed to work on this binary.

  ```
  python tools/e32image.py <path-to-6r51.app>
  python tools/e32image.py <path-to-6r51.app> --extract-code shadowkey/extracted/6r51_code.bin
  ```

  The second form also writes `6r51_code.bin.json` with the load address
  Ghidra needs (see below).

- **`resolve_imports.py`** — resolves every import-thunk call site in the
  code section to the `(dll, ordinal)` it actually calls, structurally (no
  SDK `.def` file needed — see
  [`../docs/E32IMAGE_FORMAT.md`](../docs/E32IMAGE_FORMAT.md#resolving-import-calls-without-any-sdk-def-file)
  for how). Also usable as a library (`import resolve_imports`) — that's
  how `shadowkey/ghidra/scripts/pyghidra_label_imports.py` gets its data.

  ```
  python tools/resolve_imports.py <path-to-6r51.app> --json out.json
  ```

- **`parse_symbian_lib.py`** — parses a period Series 60 SDK's real
  ARM-hardware-target Symbian import library (`EUSER.LIB`, `CONE.LIB`,
  etc. from `\epoc32\release\armi\urel\` — *not* the `\wins\`/`\winsb\`
  emulator-target ones, incompatible ordinal scheme) into an exact
  `ordinal -> mangled name` table. See
  [`../docs/IMPORT_NAMES.md`](../docs/IMPORT_NAMES.md) for the full story
  (getting the SDK, disambiguating build targets via the MSI database,
  the `ar`-archive-with-`ds<N>.o`-members format, verification).

  ```
  python tools/parse_symbian_lib.py <path-to-EUSER.LIB> --json out.json
  ```

- **`resolve_import_names.py`** — cross-references `resolve_imports.py`'s
  `(dll, ordinal)` list against a directory of SDK `.LIB` files
  (`parse_symbian_lib.py`) to produce `shadowkey/import_names.json`, the
  small derived table this repo actually tracks (see that file's header
  comment and [`../docs/IMPORT_NAMES.md`](../docs/IMPORT_NAMES.md) — the
  SDK itself is never committed).

  ```
  python tools/resolve_import_names.py <path-to-6r51.app> <sdk-armi-urel-dir> --json shadowkey/import_names.json
  ```

- **`fingerprint_compiler.py`** — detects ARM RVCT/ADS-style
  frame-pointer-chained prologues vs. GCC-style lean ones, to fingerprint
  which compiler built which part of the binary (see
  [`../docs/COMPILER_TOOLCHAIN.md`](../docs/COMPILER_TOOLCHAIN.md)).

  ```
  python tools/fingerprint_compiler.py shadowkey/extracted/6r51_code.bin
  ```

- **Ghidra** — not installed under this repo. Reused from the sibling
  `rac-decomp` project's shared install:
  `C:\Users\Admin\rac-decomp\tools\ghidra_12.0.4_PUBLIC`. If that project
  ever moves/removes it, re-fetch Ghidra 12.x and update this note (and
  the paths below) with the new location.

- **`pyghidra`** (pip package) — installed into the system Python from
  Ghidra's own bundled copy:
  `pip install "<GHIDRA_INSTALL_DIR>/Ghidra/Features/PyGhidra/pypkg"`.
  This is what lets `shadowkey/ghidra/scripts/*.py` be run as plain
  `python foo.py` instead of through `analyzeHeadless -postScript` (which,
  on this Ghidra install, refuses `.py` scripts headless with "PyGhidra
  script provider claims .py but Python not started" — `pyghidra` sidesteps
  that entirely by driving Ghidra from a normal Python process).

## Headless-importing `6r51.app` into Ghidra

E32Image isn't ELF/PE, so Ghidra has no native loader for it. The working
approach is: extract the raw code section with `e32image.py`, then import
it as a raw ARM binary at its real link address, with the ARM Procedure
Call Standard compiler spec (what Symbian's own EKA1-era toolchains
targeted):

```
GHIDRA=/c/Users/Admin/rac-decomp/tools/ghidra_12.0.4_PUBLIC
cd shadowkey-decomp
"$GHIDRA/support/analyzeHeadless.bat" \
  shadowkey/ghidra ShadowkeyProject \
  -import shadowkey/extracted/6r51_code.bin \
  -processor "ARM:LE:32:v4t" \
  -cspec apcs \
  -loader BinaryLoader \
  -loader-baseAddr 0x10000000
```

This has been run once already (see `shadowkey/ghidra/ShadowkeyProject.gpr`,
gitignored/regenerable) and auto-analysis completes cleanly: disassembly
picks up mixed ARM/Thumb code without errors, which is itself a decent
sanity check that `v4t` is the right ISA variant for this CPU.

Open the resulting project in the normal Ghidra GUI
(`"$GHIDRA/ghidraRun.bat"`) to continue analysis interactively — headless
mode is just for the initial import/auto-analysis pass.

## Labeling import calls in the Ghidra project

Once the project above exists, `shadowkey/ghidra/scripts/pyghidra_label_imports.py`
applies `resolve_imports.py`'s table (plus `shadowkey/import_names.json`'s
real names, where resolved — 319/415) as real labels (and function
renames, where the "Create Function" analyzer already turned a thunk into
a Function) inside it — turns `CALL 0x1009eef0` into
`CALL EUSER____nw__5CBaseUi` (`CBase::operator new(TUint)`) everywhere,
including in decompiler output, or `CALL EUSER_ord1234`-style for the 96
still-unresolved ordinals. Run from the repo root:

```
python shadowkey/ghidra/scripts/pyghidra_label_imports.py
```

It opens the existing project directly (no GUI, no `analyzeHeadless`) via
`pyghidra.open_program(..., analyze=False, nested_project_location=False)`
— `nested_project_location=False` matters because the project was created
by `analyzeHeadless`, not by PyGhidra itself, so it doesn't have PyGhidra's
usual nested-folder layout. Wrap edits in `program.openTransaction(...)`
(a `with`-compatible `db.Transaction`) rather than the older
`startTransaction`/`endTransaction` pair — the latter left a transaction
open in one attempt here, which then broke `open_program`'s automatic
save-on-exit with "Unable to lock due to active transaction".

Still needs real names for the 96 unresolved ordinals (SimKin, GameComms,
NokiaFC, one MediaClientAudioStream ordinal) — see
[`../docs/IMPORT_NAMES.md`](../docs/IMPORT_NAMES.md) and
[`../docs/ROADMAP.md`](../docs/ROADMAP.md).

## Finding the render/game-loop (Phase 3's first result)

Same pattern, applied to a real Phase 3 question instead of imports —
see [`../docs/RENDER_LOOP.md`](../docs/RENDER_LOOP.md) for what was
found (a hardcoded 40ms/25Hz `CPeriodic` tick driving both update and
present).

- `shadowkey/ghidra/scripts/pyghidra_find_render_loop.py` — starting from
  the labeled `WS32`/`GDI`/`BITGDI`/`FBSCLI` import functions, walks
  callers two levels up and prints the graph, to spot candidate
  loop-driver functions.
- `shadowkey/ghidra/scripts/pyghidra_inspect_candidates.py` — given a
  list of hardcoded addresses, prints their callers plus a short
  decompiled snippet; used to eyeball candidates from the graph above.
- `shadowkey/ghidra/scripts/pyghidra_dump_full.py <hex-addr>` — dumps one
  function's full decompilation to
  `shadowkey/extracted/decomp_<addr>.c` (gitignored, regenerable) for
  closer reading than a snippet allows.
- `shadowkey/ghidra/scripts/pyghidra_find_periodic.py` — finds every
  call site of `CPeriodic::Start` and, for a given candidate callback
  address, its `TCallBack`-style (data/param, not call) references — how
  the 40ms/25Hz timer registration was actually located.
- `shadowkey/ghidra/scripts/pyghidra_label_render_loop.py` — applies the
  finding as real function names + plate comments in the Ghidra project
  (same idempotent pattern as `pyghidra_label_imports.py`).
- `shadowkey/ghidra/scripts/pyghidra_find_field_writes.py <lo-addr>
  <hi-addr> <hex-offset>` — scans a bounded address range for `STR`
  instructions writing a fixed immediate byte offset to any base
  register; used to find where an object field gets *set* when you know
  the offset but not the setter.
- `shadowkey/ghidra/scripts/pyghidra_find_reads.py <hex-offset>` — same
  idea, whole-binary, for `LDR` instead of `STR`; finds every function
  that *consumes* a known field.
- `shadowkey/ghidra/scripts/pyghidra_read_vtable.py <DAT_addr> [N]` —
  resolves a decompiler `DAT_x` literal-pool reference to the actual
  vtable it points at and lists the first N entries as (offset, target,
  function name if Ghidra already knows it) — for reading GCC-old-ABI
  virtual call targets straight out of the binary's data section.

See [`../docs/WORLD_MODEL.md`](../docs/WORLD_MODEL.md) for what these
found: level layout/collision data is a 2D tile grid, with a single
ubiquitous `Map_GetTileAt` accessor — this is about level *data*, not
evidence that the game is rendered in 2D (see that doc's correction note).

- `shadowkey/ghidra/scripts/pyghidra_label_world_model.py` — labels
  `Map_GetTileAt` and appends an addendum comment to `GameEngine_ctor`.
- `shadowkey/ghidra/scripts/pyghidra_label_graphics.py` — labels
  `Blit_RLESprite` and its two traced call sites; see
  [`../docs/GRAPHICS_FORMAT.md`](../docs/GRAPHICS_FORMAT.md).
- `shadowkey/ghidra/scripts/pyghidra_find_callers.py <hex-addr>` — prints
  every distinct function that calls a given address (name + size);
  general-purpose caller-graph tracing, used to walk up from the 3D
  polygon pipeline to its callers.
- `shadowkey/ghidra/scripts/pyghidra_label_renderer3d.py` — labels the
  actor/entity 3D polygon rendering pipeline (rotation matrix, clip,
  dispatch, one rasterizer variant); see
  [`../docs/RENDERER_3D.md`](../docs/RENDERER_3D.md).
- `shadowkey/ghidra/scripts/pyghidra_label_renderer3d_walls.py` — labels the
  room/wall geometry renderer (shares the actor pipeline's clip/perspective
  core but has its own top-level transform+depth-sort, face dispatch, and
  rasterizer) plus the per-frame `Render3DScene` entry point and the
  `engine+0x5b4` intermediate-buffer compositor; see
  [`../docs/RENDERER_3D.md`](../docs/RENDERER_3D.md#the-roomwall-geometry-renderer).
- `shadowkey/ghidra/scripts/pyghidra_label_model_format.py` — appends the
  reconstructed on-disk 3D model resource format (header layout, vertex/UV/
  face table strides, texture block) as plate-comment addenda on
  `Actor3D_TransformAndSubmitModel` and `RoomGeometry_TransformAndSort`; see
  [`../docs/MODEL_FORMAT.md`](../docs/MODEL_FORMAT.md).

- **`parse_model_resource.py`** — parser/verifier for the actual on-disk 3D
  model archive, `system/apps/6r51/models.idx` + `models.huge` in the game
  install tree (found separately from `6r51.app`'s code section — this is
  real game asset data, not something extracted from the binary). Decodes
  every entry and checks the format reconstructed in
  [`../docs/MODEL_FORMAT.md`](../docs/MODEL_FORMAT.md) against real bytes.

  ```
  python tools/parse_model_resource.py <models.idx> <models.huge> --verify
  python tools/parse_model_resource.py <models.idx> <models.huge> --dump <entryIndex>
  ```
