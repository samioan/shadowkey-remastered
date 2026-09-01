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

- [x] Followed the two per-tick vtable calls to their targets. **Not** a
      clean `Update()`/`Render()` split (correcting the guess above) —
      both dispatch into `ScreenModeController`, a ~440-byte subsystem
      object whose methods are state-machine dispatchers over screen/
      menu/dialog mode, interleaved with SimKin calls. Also found the
      real central engine object (`GameEngine_ctor`, ~85.5KB, holding two
      fixed-capacity entity pools individually exposed to SimKin) and how
      `6r51.app` binds its SimKin interpreter instance (in
      `GameEngine_FirstTickBootstrap`, ~50 named script bindings). Full
      writeup, corrected hypothesis, and labels in
      [`RENDER_LOOP.md`](RENDER_LOOP.md).

- [x] Explored the `0x1006Bxxx`–`0x1006Dxxx` cluster. It's save/load and
      level-transition management (not rendering) — but tracing
      `GameEngine_ctor` fully along the way found the actual World/Map
      object: **level layout/collision data is a 2D tile grid**
      (width/height/tile-array/tile-type-table fields, an 8-byte-per-cell
      layout, a 36-byte-per-type shared definition table), with a single
      ubiquitous accessor (`Map_GetTileAt`, 30+ call sites across the
      binary). This is about level *data*, not rendering — Shadowkey is
      a real-time first-person 3D dungeon crawler (like Ultima
      Underworld/Eye of the Beholder before it), and the 3D view is
      presumably generated *from* this grid each frame, not evidence the
      game itself is 2D (an earlier draft of this entry overclaimed
      that). Also incidentally confirmed `engine+0x5cc` is the GAMECOMMS
      (Bluetooth multiplayer) subsystem, not world-related. Full
      writeup: [`WORLD_MODEL.md`](WORLD_MODEL.md).

- [x] Followed the "turn tile data into pixels" lead. Found the actual
      low-level 2D graphics primitives (screen format: 176x208, 16bpp,
      352-byte stride; a paletted, run-length/segment-encoded sprite
      format with a magenta colorkey and an optional 50%-blend mode;
      the core `Blit_RLESprite` compositing function) — but the specific
      call sites traced (`DrawEntitySpriteWithOutline`,
      `DrawListItemIconAndLabel`) turned out to be HUD/menu/inventory
      icon rendering, not the first-person 3D view. Also found (and
      ruled out as renderer candidates) the binary's largest functions
      — huge SimKin native-function dispatch tables — and confirmed
      actor movement/collision against the tile grid (fixed-point 8.8
      positions, bounding radii, per-tile-type collision behavior). Full
      writeup: [`GRAPHICS_FORMAT.md`](GRAPHICS_FORMAT.md).

- [x] Checked `FUN_1006bee8`/`FUN_1006be58` (the outline/placeholder-box
      helpers from `GRAPHICS_FORMAT.md`) — both are trivial flat-color
      horizontal/vertical line fills (no texture, no per-column height).
      Ruled out as renderer candidates; moved to the other search angle
      (hunt for a division/reciprocal operation).

- [x] Found that angle: widened the `engine+0x480` framebuffer-pointer
      read-site search, which surfaced a **full perspective-correct
      textured 3D polygon pipeline** — Euler rotation matrix construction,
      matrix composition, Sutherland-Hodgman polygon clipping, a real
      `EUSER____divsi3` perspective divide, and scanline rasterizers using
      1/z reciprocal-table texture mapping. This is strong, direct
      confirmation that Shadowkey renders true 3D geometry (not sprites).
      **Caveat**: every caller traced renders an actor/entity 3D model
      (NPCs, monsters, attached items/weapons, keyframe animation) — none
      touch `Map_GetTileAt` or tile data, so it's still unconfirmed whether
      the static dungeon walls/floors/ceilings use this same pipeline or a
      separate one. Full writeup: [`RENDERER_3D.md`](RENDERER_3D.md). Also
      re-confirmed the earlier automap hypothesis for `FUN_1002b430` (2x2
      px/tile top-down map with a rotated player-direction marker driven by
      the same sin/cos LUT shape) while investigating.

- [x] Answered that question: static room geometry does **not** go through
      `Actor3D_TransformAndSubmitModel`/`Poly3D_ClipAndDispatch` (both have
      exactly one caller each, all actor-shaped, re-confirmed). Instead
      rooms have their own top-level path (`RoomGeometry_TransformAndSort` →
      `RoomFace_ClipAndDispatch` → `RoomFace_RasterizeTextured`) built on
      the *same* model format (`room+0x54`, identical layout to
      `actor+0x54`) and reusing `BuildRotationMatrix3x4`/
      `ComposeTransform3x4`/`Poly3D_ClipAgainstPlane` directly, but with a
      whole-model transform + 64-bucket depth sort (painter's algorithm)
      instead of per-face immediate dispatch. Also found the per-frame
      master entry point `Render3DScene` (rooms, then actors via virtual
      dispatch, then composite) and resolved the `engine+0x5b4` mystery
      buffer: a 176×208, 4-bytes/pixel intermediate target both renderers
      share, packed down to the real 16bpp `engine+0x480` screen by
      `CompositeSceneBufferToScreen` once per frame (with an optional
      fade/lighting LUT pass). Full writeup:
      [`RENDERER_3D.md`](RENDERER_3D.md#the-roomwall-geometry-renderer).

- [x] Reconstructed the on-disk 3D model resource format (`actor+0x54`/
      `room+0x54`) by reading every byte-level field access in
      `Actor3D_TransformAndSubmitModel` and `RoomGeometry_TransformAndSort`:
      a 12-byte header (frame count, verts/frame, UV count, face count),
      then a multi-frame vertex-position table (Quake-MD2-style vertex
      animation — face/UV topology shared across frames, only positions
      vary), a UV-coordinate table, a 12-byte/6-`int16` textured-triangle
      face table, and a raw (uncompressed) 16bpp texture block with
      power-of-two dimensions and multiple "skin" color variants selected
      per-actor-instance (rooms always use variant 0). The two independent
      call sites compute the identical texture-offset formula from the same
      header byte, cross-validating the reconstruction. Full writeup:
      [`MODEL_FORMAT.md`](MODEL_FORMAT.md).

- [x] Found the actual on-disk 3D model archive and verified
      `MODEL_FORMAT.md`'s reconstruction against it byte-for-byte:
      `system/apps/6r51/models.idx` + `models.huge` in the game install
      tree (found separately from `6r51.app` — real asset data, not
      extracted from the binary). `models.idx` is a flat `(offset, size)`
      index into `models.huge`'s 237 model resources; decoded and
      cross-checked every invariant (header field roles, `H5==H2*3`, every
      vertex/UV index in every face table, the texture-block offset
      formula) against all 226 non-empty entries with **zero failures**.
      Also found (not yet pursued) `azra.sta` and 21-per-zone files
      (`.zon`/`.zmp`/`.pal`/etc.) that likely reference these models by
      the same index for level/entity placement. Full writeup + new tool
      (`tools/parse_model_resource.py`):
      [`MODEL_FORMAT.md`](MODEL_FORMAT.md).

- [x] Traced the zone-loading pipeline (`GameEngine_InitLevel`,
      0x10024dec, found via debug strings like "InitLevel Pre
      load_models") and answered the standing question of how a placed
      object's `+0x54` model pointer gets populated: each zone's
      `<zone>_models.txt` lists which `models.idx` archive indices it
      uses, loaded by `ZoneModelList_Load` into a flat 256-slot
      `engine+0x6b38` cache keyed directly by archive index (backed by
      `ModelArchive_Open`/`ModelArchive_LoadByIndex`, independently
      confirming `MODEL_FORMAT.md`'s verified `models.idx`/`.huge` layout);
      `<zone>.ent` placement records create typed game objects whose model
      pointer is a cache read keyed by an index carried on a per-type
      descriptor (`EntityTypeDescriptor_Lookup`, a BST at `engine+0xbe34`).
      Also identified the outer framing of `<zone>.sur`, `.zon`, `.pth`
      along the way (not decoded field-by-field) and corrected an earlier
      `RENDERER_3D.md` guess (`actor+0x2c2` is the model archive index, not
      a literal animation-frame counter). Full writeup:
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md).

- [x] Found where `engine+0xbe34`'s type-descriptor BST itself comes from:
      a single **global** file, `z:\system\apps\6R51\entities.txt`, loaded
      exactly once at boot by `EntityTypeConfig_Load` (0x10068854, whose
      only caller is `GameEngine_FirstTickBootstrap` — not per-zone).
      Each line (`typeId modelArchiveIndex thirdField name`) becomes a
      148-byte descriptor inserted into the BST by
      `EntityTypeDescriptor_Insert` (0x1008c22c), whose node layout
      matches `EntityTypeDescriptor_Lookup` exactly — closing the entity
      → model resolution chain end-to-end: `entities.txt` → `engine+0xbe34`
      BST → `.ent` record's `typeId` → descriptor's `modelArchiveIndex` →
      `engine+0x6b38[modelArchiveIndex]` (per-zone `_models.txt`) →
      object's `+0x54`. Full writeup:
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md#where-the-type-descriptor-tree-itself-comes-from-entitiestxt).

- [x] Resolved what sets `engine+0x62c` on room transitions: **nothing**
      does — it's a single 0x160-byte room-render-state object allocated
      exactly once in `GameEngine_ctor` (`RoomRenderState_ctor`) and never
      reassigned; `GameEngine_InitLevel` overwrites its fields in place on
      every level load, rather than swapping the pointer or indexing into
      the separate `engine+0x5464` room-list array.

- [x] Found a **second, zlib-compressed per-zone file loader**
      (`WholeFile_Load`, 0x1002778c — 4-byte decompressed-size header + a
      raw zlib stream, verified against a real file) feeding 6 more
      per-zone extensions, and used it to **correct** the `engine+0x62c`
      entry above: the room-render object's model pointer comes from
      **`<zone>.zsk`**, not `.zon` as first written — verified by
      decompressing a real `azra.zsk` and confirming its header decodes
      exactly per `MODEL_FORMAT.md`'s spec (`H0=7`, `H6=1`, `H5==H2*3`).
      So the zone's actual walkable dungeon geometry is an ordinary
      `MODEL_FORMAT.md`-format model, just zlib-compressed and loaded
      directly per-zone instead of through the `models.idx` archive. Also
      found `.zfg` (→ `engine+0x5c4`, resolving the fade-LUT open item) and
      traced a "bullseye" AI-navigation-looking subsystem
      (`Bullseye_Init`/`Bullseye_InitMap`/`.zcp`/`calc_lights`) along the
      way. This also fully explains the `"InitLevel Pre/Post LUA"` debug
      markers noted earlier: they bracket the `.zlu` file's load — "LUA" is
      short for `.zlu`, not a Lua scripting layer (a real dead end, just
      now correctly attributed instead of merely ruled out). Full writeup:
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md#compressed-per-zone-files-and-where-the-actual-room-geometry-comes-from).

Next: the ~9 unexamined `Poly3D_RasterizeTextured` blend-mode variants, the
`.sur`/`.zon`/`.pth`/`.ztx`/`.zmp`/`.zlu`/`.zcp` record/content field
layouts, `entities.txt`'s unidentified `thirdField`, and `azra.sta`'s
still-unidentified format (confirmed not the `.zcp`/"bullseye" file). See
`RENDERER_3D.md`'s, `MODEL_FORMAT.md`'s, and `ZONE_FORMAT.md`'s open
follow-ups.

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
