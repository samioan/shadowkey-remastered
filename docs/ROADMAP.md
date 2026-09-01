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

- [x] Resolved `entities.txt`'s `thirdField`: traced all 17 callers of
      `EntityTypeDescriptor_Lookup`, found the handful that read
      descriptor `+0x10`, then verified the meaning **directly against
      the real `entities.txt` game data** (not just code inference) by
      tallying its third column across all ~760 lines and sampling names
      per bucket. It's an entity **category enum**: 1=prop, 2=monster,
      3=misc-loot, 4=weapon, 5=spell, 6=armor, 7=merchant, 8=container,
      9=consumable, 10=trap, 11=door, 12=trapped, 14=scroll, 15=shield,
      16=unique. Confirms the monster-death "loot bag" mechanic (spawns
      typeId 300 = `entities.txt` line `300 30 8 !bag_loot`, category
      8/container, then re-checks category==8 before flagging it as a
      container) and an inventory-search special case (category 5/spell
      queries also match category 14/scroll — spell scrolls are
      castable). Full writeup:
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md#thirdfield-resolved-its-an-entity-category-enum).

- [x] Fully decoded `.zon`'s room record (4×`u16` header fields at
      `0x00/0x02/0x04/0x06`, exact byte offsets pinned via the decompiler's
      declared local-variable sizes, not just guessed from stack deltas),
      partially decoded `.zmp`'s 132-byte header (first 32 bytes are the
      zone's SimKin level-script base name, used to build `<name>.s`,
      loaded via `SIMKIN_ord59` — the first concrete binary-to-script link
      found; a `u16` at `+0x82` is shared between `.zon`'s field conversion
      and `Bullseye_InitMap`), and corrected an earlier note that
      attributed two `Bullseye_InitMap` fields to `.sur` — they're actually
      `.zmp`'s. Also found an **8th per-zone file, `.stn`** ("skTreeNodes"),
      missed earlier because it loads conditionally rather than always:
      parsed 4 real `.stn` files directly and found every record binds a
      named lockable object (a door or container) to a slot in a global
      SimKin `resistDisarm[]` array — per-instance lockpick/trap-disarm
      difficulty. Full writeup:
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md#zons-room-record-fully-decoded).
- [x] Closed `.stn`'s last open question — where the object field it
      overwrites is actually *read*. Re-checked `FUN_100732c8`'s raw ARM
      disassembly (its decompiled C looked like it silently dropped its
      2nd parameter; the asm shows it's real — forwarded through an
      untouched register into a nested call) and then found the actual
      consumer directly in the readable SimKin scripts: every lockable
      object's class script (e.g. `chest_trapa.s`) declares its own
      `resistDisarm[N]` constant, and the lockpicking minigame
      (`menus\usepicks.s`) calls `CanDisarmTrap(GetOpener().resistDisarm)`.
      `.stn` overwrites a specific placed instance's field with a
      *reference* to a shared global slot instead of its class's constant
      — explaining why e.g. `crypt1.stn` points 5 different doors at the
      same `resistDisarm[28]`. No native-code mystery left here; the rest
      lived in already-readable script files.
- [x] Resolved `GameEngine_InitLevel`'s `param_3`. Traced its only call
      chain end to end (`FUN_10027d0c` thread entry ← `FUN_10027d44`
      thread-spawn ← `FUN_10019780`/`FUN_10069cac`, a per-tick load-state
      machine) and both of its use sites inside `GameEngine_InitLevel`
      itself: non-zero means a **full/fresh zone entry** — reset the
      player's position from the `.ent` player-start record *and* load
      `.stn`'s difficulty overrides — zero means a **lighter reload** that
      leaves the player's current position and any already-applied `.stn`
      overrides alone. Renamed/commented via
      `pyghidra_label_initlevel_param3.py`. Full writeup:
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md#gameengine_initlevels-param_3-full-entry-vs-partial-reload).

- [x] Resolved all 9 remaining `Poly3D_RasterizeTextured` variants (`_v1`
      through `_v9`) by decompiling and diffing each against the already-
      documented `_v0`. **Not** "opaque vs. blended" as guessed — the three
      real axes are near-clip (bVar1, ~2.5x code size), a depth-driven fog
      LUT (`engine+0xbe0f`/`+0x5c8`/`+0x5c4`, a torchlight/distance-fog
      effect confirmed structurally), and a "stencil" family that stamps a
      literal ID byte into a second 176x208 8bpp buffer at `engine+0x5b8`
      (picking/object-ID, not alpha blending). Also caught and corrected a
      wrong earlier claim in `RENDERER_3D.md`: the intermediate buffer's
      high 16 bits are a genuine per-pixel interpolated depth value used
      for occlusion testing, not a meaningless constant `0x7fff`. `_v9` is
      structurally distinct — writes straight to the real screen buffer
      with no depth test at all. Renamed/commented via
      `pyghidra_label_rasterizer_variants.py`. Full writeup:
      [`RENDERER_3D.md`](RENDERER_3D.md#the-10-poly3d_rasterizetextured-variants).

- [x] Decoded `.zmp`'s bulk content (everything after its 132-byte header)
      and `.zcp`, by tracing the "bullseye" subsystem's three lighting
      functions to the byte level. **Corrects** the earlier "AI
      navigation/pathfinding" guess for "bullseye": it's actually a
      one-shot **per-zone lighting bake** (every function in the chain has
      exactly one caller, none run per-tick) — `.zmp`'s post-header bytes
      are a `field80`×`zmpTotal` grid of 6-byte light/nav cells (light-source
      and wall/obstruction flags, a light level, an index into `.zcp`),
      `.zcp` is a small indexed table of per-cell light deltas, and a
      dedicated function does real 2D ray-cast light propagation with
      wall-bounce using the same sin/cos LUT as the rotation-matrix/automap
      code, all baked once at zone load. Also pinned `.sur`'s exact
      count/buffer field locations (forwarded into this same subsystem) and
      confirmed `.ztx`/`.zlu` get forwarded there too, though no consumer of
      their contents was found this pass. Renamed/commented via
      `pyghidra_label_bullseye_lighting.py`. Full writeup:
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md#the-bullseye-subsystem-a-load-time-light-propagation-bake-not-ai-pathfinding).

- [x] Resolved `.ztx`/`.zlu`/`.sur` by finding their real consumer — a
      previously-unknown **third 3D rendering pipeline**: a tile-grid
      wall/surface-face renderer inside `Render3DScene`, parallel to the
      actor pipeline and the `.zsk`-baked room mesh, reusing the same
      `Poly3D_ClipAgainstPlane` clip core and near-clip/fade dispatch axes.
      `.sur` is a per-face material record (UV scale/offset, flags, a
      texture index) — its 8-byte layout is now fully decoded. `.ztx` is a
      flat, **8bpp-palettized** wall-texture atlas (one `0x4000`-byte slot
      per `.sur` texture index). `.zlu` is **4 selectable 256-color
      palettes** that convert `.ztx`'s indexed texels into real 16bpp
      color — resolving the "unidentified 4-way LUT/table split" guess and
      matching the `"InitLevel Pre/Post LUA"` debug markers. Needed a new
      tool (`pyghidra_find_split_offset.py`) since the consuming code
      builds the large object-field offset via a split rotated-immediate
      ADD, invisible to the existing offset-grep tooling. Renamed/commented
      via `pyghidra_label_surface_renderer.py`. Full writeup:
      [`RENDERER_3D.md`](RENDERER_3D.md#the-tile-grid-wallsurface-face-renderer-a-third-pipeline).

- [x] Resolved the tile-grid traversal: `Render3DScene` calls a new
      function, `TileGrid_RaycastVisibility`, once per frame — a genuine
      fan-raycast (150-178 rays, same sin/cos LUT as the rotation-matrix
      code) that determines which tiles are actually visible this frame,
      stopping at wall tiles (the same `flags` bit `Bullseye_
      PropagateLight`'s light-bounce rays stop at) and deduplicating via a
      newly-decoded per-tile "last visible frame" byte. `Render3DScene`
      then walks *that* list, checking each tile's neighbors' `.zcp`
      type-table entries for a per-direction `.sur`-index byte (`0xff` =
      no face) to decide what to draw, gated by a camera-side check or an
      explicit per-tile override flag. This also resolved a long-standing
      `WORLD_MODEL.md` open item ("the actual first-person rasterizer/
      raycaster still not located") and an object-identity question: the
      "engine" object referenced throughout `ZONE_FORMAT.md`/
      `RENDERER_3D.md` **is** `WORLD_MODEL.md`'s `CMap` — there's no
      separate wrapper object. Also unified `WORLD_MODEL.md`'s
      independently-found "36-byte tile-type table at `map+0x690c`" with
      `ZONE_FORMAT.md`'s `.zcp` entries array — same table. Full writeup:
      [`WORLD_MODEL.md`](WORLD_MODEL.md#per-frame-tile-visibility-raycasting-how-render3dscene-picks-which-faces-to-draw)
      and
      [`RENDERER_3D.md`](RENDERER_3D.md#the-traversal-what-decides-which-faces-get-a-dynamic-draw).

- [x] Resolved `azra.sta` — not through binary RE (nothing in `6r51.app`
      reads `.sta` files, reconfirmed) but by directly parsing the real
      file and comparing it against `azra.ent`'s already-decoded entity
      placements: 188/201 (93.5%) of `.sta`'s records share a position
      with an `azra.ent` record, and of those, 100% also match on
      rotation and another field, meaning `.sta`'s records are a strict
      subset of `.ent`'s own fields. Conclusion: `azra.sta` is a leftover
      level-editor "staging" export of the same entity data now shipped
      as `azra.ent`, accidentally left in the install image for the
      `azra` zone only, never wired into any loader. Along the way, found
      and fixed a wrong byte-offset in this project's own `.ent` record
      struct (`typeId`/`name` were off by 8 bytes; two whole `int32`
      fields had been missed entirely). New tool:
      `tools/parse_zone_placement.py`. Full writeup:
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md#azrasta-a-leftover-level-editor-staging-file-not-a-game-format).

- [x] Fully mapped the 36-byte `.zcp` type-table entry's face-direction
      bytes by reading `Render3DScene`'s tile-grid traversal in full (5
      near-identical blocks) instead of sampling isolated call sites as
      an earlier pass had: `0x16`-`0x19` = east/west/south/north wall
      lower band, `0x1a`-`0x1d` = the same 4 directions' upper band (a
      second stacked wall segment for stepped floor/ceiling heights),
      `0x1e`/`0x1f` = two ceiling bands, `0x20` = floor. Also found a 5th
      meaningful tile `flags` bit (bit6, selects which of two height
      fields gates ceiling banding). Documented via
      `pyghidra_label_zcp_face_map.py`. Full writeup:
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md#the-bullseye-subsystem-a-load-time-light-propagation-bake-not-ai-pathfinding)'s
      `ZcpEntry` struct and
      [`RENDERER_3D.md`](RENDERER_3D.md#the-traversal-what-decides-which-faces-get-a-dynamic-draw).

- [x] Traced `SurfaceFace_RasterizeTextured_v0`/`_v1`/`_v2` in full
      (`_v3` already was). Confirmed `_v2` (far+fade) is `_v3`'s core plus
      the actor pipeline's fog-nibble scheme. Found the near/far split
      here is a genuine **behavioral** difference, not just extra
      clip-edge bookkeeping like the actor pipeline's near variants:
      `_v0`/`_v1` (near) write every pixel of a fixed 8-wide
      interpolation batch unconditionally — no chroma-key transparency
      check, no per-pixel depth test — while `_v2`/`_v3` (far) gate every
      pixel on both. Near wall segments are presumably always the
      frontmost thing drawn there, so the engine skips both checks for
      speed. Documented via
      `pyghidra_label_surface_rasterizer_variants.py`. Full writeup:
      [`RENDERER_3D.md`](RENDERER_3D.md#the-tile-grid-wallsurface-face-renderer-a-third-pipeline).

- [x] Started **input handling** (per the port-scoping discussion below —
      never touched by any earlier round, but already named in this
      section's own prioritization as gating a playable port). Found the
      app's own key-event override, `AppUi_OfferKeyEventL`, by tracing the
      one caller of the base-class `OfferKeyEventL` import: a flat
      21-slot boolean "game action" array, with the N-Gage's numeric
      keypad (`'0'`-`'9'`) as the primary control set — each digit key is
      its own scan code, no lookup needed — plus 4 likely D-pad codes and
      a handful of others. Also found a hidden 12-step scan-code-sequence
      Easter egg tucked into the same dispatcher. Exact `TStdScanCode`
      names weren't re-verified against a primary source (the project's
      earlier-archived SDK copy no longer exists locally, and a public
      mirror search came up short) — flagged clearly as convention, not
      confirmed. What each action slot actually *does* in gameplay is
      still open — `pyghidra_find_reads.py` found no direct consumer of
      the array's offset, meaning it's likely accessed through a cached
      pointer rather than a flat offset. Full writeup:
      [`INPUT_HANDLING.md`](INPUT_HANDLING.md).

Next: which physical key produces which scan code and what each of the 21
action slots does in gameplay (`INPUT_HANDLING.md`'s open items), the two
`heightA`/`heightB` fields' finer sub-structure, the `.zlu` chunk's
secondary per-scanline/per-pixel `0x200`-byte-block offset (seen in all 4
`SurfaceFace_RasterizeTextured` variants, not decoded), and `.sta`'s
remaining undecoded fields (`flags`, `unkA`/`unkB`). See `MODEL_FORMAT.md`'s,
`RENDERER_3D.md`'s, `WORLD_MODEL.md`'s, `ZONE_FORMAT.md`'s, and
`INPUT_HANDLING.md`'s open follow-ups.

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
