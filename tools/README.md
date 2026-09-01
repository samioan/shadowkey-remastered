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

- `shadowkey/ghidra/scripts/pyghidra_find_zone_loader.py` — finds the
  zone/level loading function by locating xrefs to debug strings like
  `"InitLevel Pre load_models"` and the `"%s\%s.ent"`/`.zon`/`.zmp` format
  strings; see [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md).
- `shadowkey/ghidra/scripts/pyghidra_read_strings.py <hex-addr>...` —
  resolves one or more `DAT_xxxxxxxx` literal-pool constants (as the
  decompiler names them) to their string value, auto-following one level of
  pointer indirection; used to pin down the exact `sprintf`/`sscanf` format
  strings (e.g. `"%s\%s_models.txt"`, `"%d %d %d %d %s"`) behind the zone
  loader's `DAT_` operands.
- `shadowkey/ghidra/scripts/pyghidra_label_zone_loader.py` — labels
  `GameEngine_InitLevel`, `ZoneModelList_Load`, `ModelArchive_Open`,
  `ModelArchive_LoadByIndex`, and `EntityTypeDescriptor_Lookup`; see
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md).

- `shadowkey/ghidra/scripts/pyghidra_label_room_render_state.py` — labels
  `RoomRenderState_ctor` and appends an addendum to `GameEngine_ctor`
  documenting `engine+0x62c` (allocated once, never reassigned, fields
  overwritten in place on room transitions); see
  [`../docs/RENDERER_3D.md`](../docs/RENDERER_3D.md).

- `shadowkey/ghidra/scripts/pyghidra_label_compressed_zone_files.py` —
  labels `WholeFile_Load` (the zlib-compressed per-zone file loader),
  `Bullseye_Init`, `Bullseye_InitMap`; see
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md#compressed-per-zone-files-and-where-the-actual-room-geometry-comes-from).

- `shadowkey/ghidra/scripts/pyghidra_label_entity_category.py` — documents
  `EntityTypeDescriptor+0x10` (`entities.txt`'s third `%d`) as an entity
  category enum (verified against the real `entities.txt` game data, not
  just code) and comments the loot-bag-drop and spell/scroll-matching call
  sites that use it; see
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md#thirdfield-resolved-its-an-entity-category-enum).

- `shadowkey/ghidra/scripts/pyghidra_label_stn.py` — documents the newly
  found `.stn` per-zone file (loaded conditionally, binds named lockable
  objects to a global `resistDisarm[]` SimKin array slot) plus the fully
  decoded `.zon` room record and partially decoded `.zmp` header; see
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md#stn-per-instance-traplockpick-difficulty-bindings).

- `shadowkey/ghidra/scripts/pyghidra_dump_disasm.py <hex-addr>` — dumps a
  function's raw ARM disassembly plus Ghidra's inferred parameter list.
  Used when the decompiler's C view is misleading about a function's real
  signature — e.g. it showed `FUN_100732c8` taking only one parameter, but
  the call site clearly passed two; the raw asm proved the second argument
  *is* real (silently forwarded through an untouched register into a
  nested call, just never re-loaded/shown by the decompiler). See
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md#stn-per-instance-traplockpick-difficulty-bindings).

- `shadowkey/ghidra/scripts/pyghidra_label_stn_lookup.py` — renames
  `FUN_100732c8` to `SimKinObject_FindByName` and documents its real (but
  decompiler-obscured) 2-parameter signature; see
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md#stn-per-instance-traplockpick-difficulty-bindings).

- `shadowkey/ghidra/scripts/pyghidra_label_initlevel_param3.py` —
  documents `GameEngine_InitLevel`'s `param_3` (traced via its full call
  chain plus both of its use sites): non-zero means a full/fresh zone
  entry (reset the player's position, load `.stn`'s difficulty overrides),
  zero means a lighter reload that leaves both alone; see
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md#gameengine_initlevels-param_3-full-entry-vs-partial-reload).

- `shadowkey/ghidra/scripts/pyghidra_dump_batch.py <hex-addr> [<hex-addr>
  ...]` — like `pyghidra_dump_full.py` but decompiles several functions in
  one pyghidra session (one Ghidra startup instead of N); used to dump all
  9 unexamined `Poly3D_RasterizeTextured` variants at once for comparison.

- `shadowkey/ghidra/scripts/pyghidra_label_rasterizer_variants.py` —
  renames and documents the remaining 9 `Poly3D_RasterizeTextured` variants
  (`_v1`–`_v9`, `_v0` was already named) plus the dispatch-tree logic in
  `Poly3D_ClipAndDispatch`, resolved by decompiling and diffing all 10
  against each other: near-clip code-size doubling, a depth-driven fog LUT,
  a "stencil"/object-ID buffer family (not blend modes as first guessed),
  and one structurally distinct direct-to-screen/no-depth-test variant; see
  [`../docs/RENDERER_3D.md`](../docs/RENDERER_3D.md#the-10-poly3d_rasterizetextured-variants).

- `shadowkey/ghidra/scripts/pyghidra_label_bullseye_lighting.py` — renames
  and documents the "bullseye" subsystem's three lighting-bake functions
  (`Bullseye_LoadZmpCells`, `Bullseye_BakeLighting`,
  `Bullseye_PropagateLight`), resolved by tracing `.zmp`'s bulk content and
  `.zcp` to the byte level — a one-shot per-zone light-propagation bake
  with wall-bounce, not AI pathfinding as first guessed; see
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md#the-bullseye-subsystem-a-load-time-light-propagation-bake-not-ai-pathfinding).

- `shadowkey/ghidra/scripts/pyghidra_find_literal_pool_offset.py
  <hex-offset> [<hex-offset> ...]` — finds consumers of an object field
  whose offset gets built from a PC-relative literal-pool constant (a
  32-bit word embedded in the code, `ldr rX,[<addr>]`) rather than a
  direct immediate; scans memory for the constant, then follows the
  reference manager to every instruction that loads it. Came up empty for
  the offsets chased in this pass — see the sibling tool below for why.
- `shadowkey/ghidra/scripts/pyghidra_find_split_offset.py <hex-immediate>
  [<hex-immediate> ...]` — finds consumers of a large (>0xfff) object
  offset built by splitting it into a big rotated-immediate `ADD`/`SUB`
  (e.g. `add r3,rBase,#0x6b00`) followed by a small immediate `LDR`/`STR`,
  which neither `pyghidra_find_reads.py` (single-instruction immediate
  match) nor the literal-pool tool above can see. Scans every `ADD`/`SUB`
  in the binary for a matching scalar immediate operand and prints
  surrounding instructions. This is what finally located `.ztx`/`.zlu`/
  `.sur`'s real consumer; see
  [`../docs/RENDERER_3D.md`](../docs/RENDERER_3D.md#the-tile-grid-wallsurface-face-renderer-a-third-pipeline).
- `shadowkey/ghidra/scripts/pyghidra_label_surface_renderer.py` — renames
  and documents the tile-grid wall/surface-face renderer (6 functions:
  `SurfaceFace_BuildAndProject`, `SurfaceFace_ClipAndDispatch`, and 4
  `SurfaceFace_RasterizeTextured_v0`–`_v3` rasterizer variants) found by
  the tools above; see
  [`../docs/RENDERER_3D.md`](../docs/RENDERER_3D.md#the-tile-grid-wallsurface-face-renderer-a-third-pipeline).

- `shadowkey/ghidra/scripts/pyghidra_label_tile_visibility.py` — renames
  `TileGrid_RaycastVisibility` (the per-frame fan-raycast that determines
  which tiles are visible, feeding the tile-grid wall/surface-face
  renderer) and adds addendum comments to `Map_GetTileAt` (the CMap/
  "engine" object-identity unification) and `Bullseye_LoadZmpCells` (the
  extended tile-record layout); see
  [`../docs/WORLD_MODEL.md`](../docs/WORLD_MODEL.md#per-frame-tile-visibility-raycasting-how-render3dscene-picks-which-faces-to-draw).

- `shadowkey/ghidra/scripts/pyghidra_label_zcp_face_map.py` — documents
  `Render3DScene`'s tile-grid traversal's full face-direction byte map
  (every wall direction × 2 height bands, both ceiling bands, floor —
  all 11 relevant bytes of the 36-byte `.zcp` type-table entry), found by
  reading the traversal's 5 near-identical per-direction blocks in full
  rather than sampling isolated call sites; see
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md#the-bullseye-subsystem-a-load-time-light-propagation-bake-not-ai-pathfinding).

- `shadowkey/ghidra/scripts/pyghidra_label_surface_rasterizer_variants.py`
  — adds full trace comments to `SurfaceFace_RasterizeTextured_v0`/`_v1`/
  `_v2` (`_v3` was already documented when renamed): confirms `_v2`
  matches `_v3` plus the fog-nibble scheme, and that `_v0`/`_v1`'s near
  variants skip the chroma-key/depth-test checks entirely — a genuine
  behavioral split from the far variants, not just extra clip-edge
  bookkeeping; see
  [`../docs/RENDERER_3D.md`](../docs/RENDERER_3D.md#the-tile-grid-wallsurface-face-renderer-a-third-pipeline).

- `shadowkey/ghidra/scripts/pyghidra_label_input_handling.py` — renames
  and documents the key-input dispatch chain (`AppUi_OfferKeyEventL`,
  `InputState_SetButton`, `SecretSequence_OnComplete`), found by tracing
  the one caller of the base-class `OfferKeyEventL` import; see
  [`../docs/INPUT_HANDLING.md`](../docs/INPUT_HANDLING.md).
- `shadowkey/ghidra/scripts/pyghidra_find_reads_range.py <hex-lo>
  <hex-hi>` — like `pyghidra_find_reads.py` but scans a whole contiguous
  range of byte offsets in one pyghidra session (one full-binary
  instruction pass instead of one per offset) by matching each `LDR`/
  `LDRB`/`LDRH` instruction's scalar operand directly rather than
  string-matching the disassembly text.
- `shadowkey/ghidra/scripts/pyghidra_label_input_state_class.py` —
  renames and documents the full `InputState` object at `engine+0x488`
  (10 functions: current/previous button-state accessors, a remappable
  17-action binding-indirection layer, and the default-bindings
  constructor that tags each slot with a Symbian resource-string ID),
  found by reading the functions adjacent to `InputState_SetButton` in
  the binary's address space rather than an offset search (the object's
  base offset doesn't fit a single ARM immediate, so neither existing
  offset-search tool could find its consumers); see
  [`../docs/INPUT_HANDLING.md`](../docs/INPUT_HANDLING.md).

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

- **`parse_zone_placement.py`** — parser/verifier for `azra.ent` and
  `azra.sta` (both real game asset data from the install image, not
  extracted from the binary). Confirms `.sta`'s records are a near-total
  subset of `.ent`'s own entity placements (same position/rotation/`unkA`
  field for 188/201 records) — the evidence behind concluding `.sta` is a
  leftover level-editor staging export, not a format the game reads. See
  [`../docs/ZONE_FORMAT.md`](../docs/ZONE_FORMAT.md#azrasta-a-leftover-level-editor-staging-file-not-a-game-format).

  ```
  python tools/parse_zone_placement.py <azra.ent> <azra.sta>
  ```
