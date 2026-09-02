# Port Roadmap

Tracks `port/`, the actual PC port -- as opposed to [`ROADMAP.md`](ROADMAP.md),
which tracks the reverse-engineering work that feeds it. Phase 2's decision
there (behavioral RE, not byte-exact recompilation) applies here too: the
port reproduces *behavior* against the real game data, written as ordinary
modern C++, not a translation of the original's fixed-point/ARM-specific
algorithms.

## Decisions carried through every milestone

- **C++17, CMake + Ninja, MSVC.** Raw Win32 + GDI for windowing/present
  (`StretchDIBits`), no SDL2 -- the original is a 176x208 16bpp software
  rasterizer (see [`GRAPHICS_FORMAT.md`](GRAPHICS_FORMAT.md)), so a Win32
  window blitting a manually-computed backbuffer reproduces that
  architecture directly with zero external dependencies.
- **SimKin is vendored, real, LGPL C++ source** (`port/third_party/simkin/`)
  -- not a from-scratch interpreter. The game's `.s` files are ordinary
  SimKin script source, already plaintext; native binding glue
  (`port/src/simkin_bindings/`) is the actual porting work.
- **Soft-fail native bindings.** Any binding not yet implemented logs and
  returns a benign default rather than throwing, so the engine can boot
  and run real scripts well before 100% API coverage exists. Coverage
  grows milestone by milestone rather than blocking on completeness.
- **Assets stay out of the repo.** The port reads the real install image
  (`.s`/`stringtable.*`/zone files/model archives) from a configurable
  root path at runtime; nothing copyrighted is copied into git.
- Original scaffold plan (M0-M4): `C:\Users\Admin\.claude\plans\vast-wandering-summit.md`.

## Milestones done

- [x] **M0/M1 -- scaffold** (`9ef3ef2`). CMake/MSVC/Ninja project boots a
      Win32 window, presents a 176x208-scaled backbuffer, runs a fixed
      40ms/25Hz tick loop (matching the real engine's `CPeriodic` timer,
      see [`RENDER_LOOP.md`](RENDER_LOOP.md)), and maps keyboard input to
      the real 21-slot `InputState`/17-action scheme
      ([`INPUT_HANDLING.md`](INPUT_HANDLING.md)).
- [x] **M2 -- SimKin embedded, one script round-trips** (`58b980a`).
      Vendored SimKin C++ 2.23 (LGPL) under `port/third_party/simkin/`,
      one deliberate patch (`_asm int 03h` -> `__debugbreak()` for x64
      MSVC). Proved the integration against a real, simple `.s` file with
      a native test-object binding logging every call it makes.
- [x] **M3 -- native API for the main-menu chain** (`1fe298a`). Enough of
      `simkin_native_bindings.json`'s surface for `mainmenu.s` and its 19
      `CreateMenu`-referenced sibling scripts to run `Init()` without
      throwing: Menu-stack manager, Menu (generic), Widget-family classes
      (popup, combo box, floating text, player). Found and fixed a real
      bug in `skTreeNodeObject`'s default field storage along the way
      (native object references were being silently collapsed to their
      `.str()` text).
- [x] **M4 -- main menu renders and is navigable** (`13794f5`).
      `stringtable.eng`-backed text, a stand-in bitmap font (the real
      glyph format was never RE'd), and edge-triggered D-pad/softkey
      input driving real item selection and script callbacks
      (`MenuNewGame`, etc.).
- [x] **M5 -- deepen the menu chain** (`ebc38ca`). New Game -> full
      character creation (race/portrait/name entry), Load/Save/Delete
      (simulated 4-slot in-memory save system -- no real save-file format
      RE'd), Credits (native-only screen, no script-side handler exists
      in the corpus), and Quit's confirmation popup actually closing the
      app.
- [x] **M6 -- a real, playable 3D zone renderer** (`7c4f8a0`). New
      Game/Load Game now drop the player into an actual textured
      first-person view of the real dungeon data: a `puff`-based zlib
      decompressor for the compressed per-zone files, a `Zone` loader for
      `.zmp`/`.zcp`/`.sur`/`.ztx`/`.zlu`/`.ent`
      ([`ZONE_FORMAT.md`](ZONE_FORMAT.md)), and a from-scratch
      floating-point perspective/barycentric-rasterizer renderer (not a
      port of the original's fixed-point scanline pipeline). Two real RE
      corrections came out of building this: `.zcp`'s entry count is
      `u32` not `u8`, and `.zlu` palette entries are 4-bit-per-channel
      (`0x0RGB`), not RGB565 -- both fixed in `ZONE_FORMAT.md`. Known
      simplifications: fixed-radius tile scan instead of
      `TileGrid_RaycastVisibility`, no near-plane clipping (whole-quad
      cull), one wall band per direction, always ceiling band A, no
      Bullseye lighting bake.
- [x] **M7 -- tile-grid collision** (this session). Two things fixed:
    - **Turn direction was reversed.** `render3d/zone_renderer.cpp`'s
      camera basis (`forward = (cosYaw, sinYaw)`, `right = (sinYaw,
      -cosYaw)`) rotates `forward` *away* from `right` as yaw increases
      -- i.e. increasing yaw turns the camera left, not right. `main.cpp`
      had Left/Right bound to `yaw -=`/`yaw +=`, exactly backwards; now
      Left is `yaw +=` and Right is `yaw -=`.
    - **Collision**: `Zone::CircleHitsWall(worldX, worldY, radius)`
      (`world/zone.h`/`.cpp`) -- a closest-point-on-square circle test
      against every wall tile (and the grid boundary, treated as
      blocking to match the renderer's `neighborBlocks()` convention)
      the player's bounding circle's tile-range overlaps. `main.cpp`'s
      movement now tries the X and Y axes independently against it
      (simple wall-sliding) instead of moving unconditionally.
      Explicitly **not** a port of the real engine's actor
      bounding-radius mechanics (mentioned but never traced to the byte
      level in [`WORLD_MODEL.md`](WORLD_MODEL.md)) -- good enough to stop
      walking through walls, not guaranteed to feel identical. New smoke
      test: `port/src/tests/m7_collision_smoke.cpp`.

- [x] **M8 -- actors/entities in the 3D view** (this session). Every
      non-player `.ent` placement now resolves through the real
      `entities.txt` -> `models.idx`/`.huge` chain
      ([`ZONE_FORMAT.md`](ZONE_FORMAT.md),
      [`MODEL_FORMAT.md`](MODEL_FORMAT.md)) and renders as real textured
      geometry in the 3D view: `world/entity_types.h` (a global
      `entities.txt` loader), `world/model_archive.h` (lazy-parsing
      `models.idx`/`.huge` reader), `Zone::entities()` (all non-
      player-start `.ent` records, not just the player start), and
      `ZoneRenderer`'s new `PlacedEntity` submission path. All 280 of
      `azra`'s placed entities resolve to a real model with zero
      failures (`port/src/tests/m8_entity_smoke.cpp`).
    - **Real finding along the way**: model resources are **Y-up**, not a
      direct X/Y/Z passthrough into world space -- MODEL_FORMAT.md never
      pinned this down. Caught by comparing a real barrel model's
      bounding box (round in local X/Z, tall in local Y -- a barrel's
      actual shape only makes sense with local Y as "up") and a real
      door model's (thin in local Z, wide in X, tall in Y). Fixed in
      `zone_renderer.cpp`'s entity vertex transform; visually confirmed
      with a close-up render showing a recognizable, upright, correctly
      textured barrel (`port/src/tests/m8_closeup_smoke.cpp`).
    - **Known open items, not resolved this pass**: no confirmed vertex-
      to-world scale factor (entities render a bit large -- a barrel is
      roughly one full floor tile wide; `RENDERER_3D.md`'s
      `ComposeTransform3x4` writeup hints at a real per-instance "scale"
      field on the actor transform block that this port isn't reading
      yet), no orientation (`.ent`'s `rotOrScale[0..2]` fields' exact
      meaning is still unconfirmed, so entities render in their default
      facing), frame 0/skin 0 only, no distance culling. Also surfaced a
      question (not fixed here, followed up on below): real `.zcp`
      floor/ceiling height data near the player start implies
      ~19-tile-tall rooms under the port's single `kTileScale` divisor,
      which looked implausible at the time.

- [x] **World Z-scale investigation** (this session). Decompiled
      `SurfaceFace_BuildAndProject` (0x1005d784) directly: it feeds
      camera-relative X, Y, and height deltas into one shared
      rotation-matrix weighted-sum-then-`>>8` formula, with no separate
      scale factor for height -- confirming X/Y/Z genuinely share one
      raw-unit scale in the real engine. **No bug**: the port's existing
      single `kTileScale` divisor (unchanged since M6) was already
      correct. Cross-checked against real `azra.zcp` data across all
      15,659 open cells: floor-to-ceiling height has a *median* of ~6800
      raw units (~27 tile-widths), with values landing on exact/near-exact
      tile-height fractions -- real, intentional (if unusually tall)
      level data, not a units mismatch. A "floor + nearby wall, ceiling
      out of frame" render is what a normal eye-level view of a room that
      tall actually looks like. What *did* get fixed: the player
      eye-height offset above the floor was a bare, too-small guess (128
      raw units); recalibrated to 800 using two independent real
      model-height measurements (a door's local-space height ~1036, a
      barrel's ~373 -- both consistent with a ~800-900-unit-tall person).
    - **Follow-up (this session): traced the real constant's write site
      as far as it goes, still unrecovered.** Confirmed `GameEngine_
      InitLevel`'s eye-height read really does target `CMap`'s own field
      (fingerprinted the pointer against known-`CMap`-only offsets used
      elsewhere in the same function), then decompiled `CMap`'s ~85KB
      constructor (`GameEngine_ctor`, 0x1000fa7c) in full and searched
      every `strh`-to-offset-`0x1a` instruction in the whole binary
      against known `CMap` fields -- **found zero writes to it anywhere**.
      Working (unconfirmed) hypothesis: `CMap`'s allocation is never
      explicitly zeroed but also never explicitly sets this field, so if
      EKA1 hands out zeroed heap pages, the real constant may simply be
      **0** -- the real game's camera might render from literal floor
      height, not human eye height. Port's `kEyeHeightOffset = 800`
      (`render3d/camera.h`) is kept as a **deliberate, documented
      deviation** for a conventional playable camera, not a recovered
      value. Full writeup: [`RENDERER_3D.md`](RENDERER_3D.md#open-follow-ups)
      and `render3d/camera.h`.

## Next milestones (not yet started)

Roughly in priority order for reaching "actually playable," not commitments:

- **M9 -- lighting.** Currently unlit, raw palette color. The Bullseye
  per-cell light-propagation bake is documented
  ([`ZONE_FORMAT.md`](ZONE_FORMAT.md#the-bullseye-subsystem-a-load-time-light-propagation-bake-not-ai-pathfinding))
  but not yet consumed by the renderer -- `.zmp`'s per-cell light level
  and `.zcp`'s `lightDelta` are already loaded and sitting unused.
- **M10 -- combat/inventory HUD.** Needs its own native-binding pass
  (Weapon/Armor/Item/Character-stats classes, all named in
  [`SIMKIN_NATIVE_API.md`](SIMKIN_NATIVE_API.md)) plus real HUD layout,
  not just menus.
- **M11 -- `.zsk`-baked room meshes and the second wall band.** The
  renderer only draws the tile-grid pipeline's lower wall band; the
  stepped-height upper band and the separate `.zsk` room-mesh path
  (used for non-tile-grid geometry) are both still unimplemented.
- **Real save file format** -- M5's save system is simulated in-memory
  only; no on-disk save format has been RE'd yet.
- **Real menu background images** -- still flat colors (`MenuBackground`
  stub); the 384-slot image cache's source format was never RE'd
  (flagged open in [`GRAPHICS_FORMAT.md`](GRAPHICS_FORMAT.md)).
- **Real font/glyph rendering** -- still a stand-in bitmap font; the
  original's glyph format was never RE'd.
- Audio: entirely unaddressed so far, format not RE'd.

## Verification approach

Every milestone gets a standalone smoke-test executable
(`port/src/tests/m<N>_*_smoke.cpp`) that exercises real game data without
depending on the windowed app or (flaky) screenshot tooling, plus an
interactive pass through the actual `shadowkey_port.exe` for anything
that's meaningfully different in a live loop (input timing, rendering).
