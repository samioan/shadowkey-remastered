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

- [x] **M9 -- lighting** (this session). `world/zone.cpp`'s
      `Zone::BakeLighting()` reproduces `Bullseye_BakeLighting`/
      `Bullseye_PropagateLight`
      ([`ZONE_FORMAT.md`](ZONE_FORMAT.md#the-bullseye-subsystem-a-load-time-light-propagation-bake-not-ai-pathfinding)):
      a 2D ray-cast (256 directions per light-source cell) that
      propagates light outward, bouncing off the first wall it hits per
      axis, plus each cell's `.zcp` `lightDelta` -- baked once at zone
      load, replacing the on-disk `.zmp` `lightLevel` field (confirmed a
      leftover editor baseline the real engine also discards/rebuilds,
      per the doc). `ZoneRenderer` applies the result as a flat
      per-face brightness multiplier on tile geometry (floor/ceiling/
      walls); entity models (M8) stay unconditionally full-bright.
      Verified against real `azra` data: 65 real light sources, 97% of
      open cells end up with nonzero light, and a light-source's own
      tile renders visibly brighter/more detailed than an ambient-only
      spot (`port/src/tests/m9_lighting_smoke.cpp`,
      `m9_render_at_smoke.cpp`). Known simplifications: the ray-cast is
      an approximation of the original's exact integer stepping (small
      fixed-step march instead), lighting is a flat brightness scale
      rather than the real per-vertex light value feeding a `.zlu`
      palette-chunk blend, and a small non-zero ambient floor (0.12) is
      a deliberate port-only tweak so unlit geometry doesn't render as
      literal pure black.

- [x] **M11 -- second wall band + `.zsk` room mesh** (this session). Two
      pieces:
    - **The wall "upper band."** `docs/ZONE_FORMAT.md`'s `ZcpEntry`
      documents each wall direction firing up to two stacked draws -- a
      floor-anchored "lower band" (already implemented since M6) and a
      ceiling-anchored "upper band" (`surIndexE_hi`/`W_hi`/`S_hi`/`N_hi`,
      unused until now) for a stepped floor/ceiling height difference
      with a neighboring tile. `zone_renderer.cpp`'s `CollectFaces()` now
      compares each open tile's edge-corner heights against its open
      neighbors' mirrored edge and draws a kick-wall segment up to the
      neighbor's floor and/or a lintel segment down from the ceiling to
      the neighbor's ceiling, only when that neighbor's edge height
      actually differs -- an ordinary flat corridor still draws nothing
      extra, and neighbors that are genuine walls/out-of-bounds are
      unchanged (single full-span wall, as before M11).
    - **`.zsk`-baked room mesh.** `Zone::RoomMesh()` (`world/zone.h`/
      `.cpp`) decompresses `<zone>.zsk` and parses it as an ordinary
      [`MODEL_FORMAT.md`](MODEL_FORMAT.md) resource -- confirmed against
      real `azra.zsk` (its header decodes exactly per that spec,
      including the `H5==H2*3` invariant). The per-resource parser was
      factored out of `world/model_archive.cpp` into a free
      `ParseModelResource()` so both M8's entity archive and this new
      loader share it. `ZoneRenderer::Render()` draws it via a new shared
      `SubmitModel()` helper (M8's per-entity transform/rasterize loop
      refactored into this, used by both call sites) with no per-instance
      offset -- a real, separate render step in the original engine
      (`RoomGeometry_TransformAndSort`) alongside the tile-grid pipeline,
      not a replacement for it.
    - **World-position open item -- resolved in a later pass** (see the
      "Post-M11 fix -- `.zsk` room-mesh world position, decompiled" entry
      below): decompiling `RoomGeometry_TransformAndSort` confirmed the
      raw-local-origin placement is correct -- there's no per-room
      world-position field in the real function at all.
      Along the way, fixed a real bug in the M9 debug tool
      (`tests/m9_render_at_smoke.cpp`): it was placing the camera at
      `zone.playerStartZ` regardless of the tile under test, which is
      wrong for any tile far from the player start's own vertical level
      (different parts of a zone can sit at very different floor
      heights) -- now uses the tested tile's own floor height instead.

- [x] **M10 -- inventory/character-manager screens + a live HUD** (this
      session). Scoped down from the original "combat/inventory HUD"
      framing in this doc's own "Next milestones" list below (kept here
      for context) to what a HUD milestone actually needs: real item/
      player data and real navigable screens, not full combat
      resolution -- see "explicitly out of scope" at the end.
    - **`ItemExecutable`** (`simkin_bindings/item_executable.h`/`.cpp`) --
      a real native binding for armor/weapon/item/consumable `.s` scripts
      (the M2 milestone's `TestArmorExecutable` made real), backing the
      setters those scripts actually call (`SetName`/`SetArmorValue`/
      `SetDamageMin`/`SetUsable`/...) and the getters
      `inventory.s`/`charactermanager.s` read back
      (`GetItemType`/`CanDrop`/`GetArmorText`/...). `GetItemType()` is
      *inferred* from which category setter fired, not stored by the
      loader -- confirmed against real corpus data, not guessed:
      `buysell.s`'s own `AddProduct(..., IPT_Weapon)`/`IPT_Spell`/
      `IPT_Armor`/`IPT_Consumable` calls use literal `1`/`2`/`3`/`4`
      with matching comments (`game_constants.h`).
    - **`game_constants.h`/`.cpp`** -- registers every bare-identifier
      enum constant the real armor/weapon corpus references
      (`AR_Medium`, `WR_Blunt`, ...) as Simkin global variables via
      `skInterpreter::addGlobalVariable`, without which those scripts
      throw on load. The `IPT_*` item-type values are real/confirmed (see
      above); the `AR_*`/`WR_*` sub-category values are a documented,
      arbitrary-but-consistent placeholder (nothing in this port reads
      them back).
    - **`PlayerExecutable`** extended with real vitals (health/magicka/
      fatigue + max), a stat block matching `statsscreen.s`'s
      `ShowStats()`/`ShowSkills()` exactly (fixed baseline defaults --
      no character-creation stat-rolling system exists to derive them
      from), and a real inventory of `ItemExecutable` objects.
      `LoadStartingInventory()` proves this against real data the same
      way M8 proved `model_archive.h` against real `models.idx`/`.huge`
      entries: it actually runs a curated set of 3 real scripts (one
      weapon, one armor piece, one consumable) through the interpreter
      at New Game start. `GetArmorRating()`/`GetAttack()` are *real*
      derived state (sum of equipped armor's `SetArmorValue()`/the
      equipped weapon's average damage), not static numbers -- equipping
      something through the real inventory screen visibly changes them.
    - **Deferred-removal item lifecycle.** Dropping/consuming an item can
      happen mid-script-call with a live script-side reference still in
      scope (`inventory.s`'s `UseItem()` calls `RemoveRow()` then
      immediately `OnUsedBy()` on the same `inv` variable) -- erasing the
      owning `unique_ptr` right then would be a use-after-free.
      `ItemExecutable::markedForRemoval()` + `PlayerExecutable::
      PurgeRemovedItems()` (called once per tick, after any in-flight
      script call chain has fully returned -- `main.cpp`) defers the
      real erase to a safe point instead.
    - **New `MenuExecutable` widgets/native calls** for the real
      screens: `AddButton`/`AddQuitButton` (cosmetic setters like
      `ShowBorder`/`SetHAdjust` stored but not rendered -- same
      simplification spirit as the stand-in bitmap font),
      `AddFloatingText`, `AddItemButton` (`item_button_executable.h`,
      the left/right hand equip-slot display), `AddTable`
      (`table_executable.h`, the inventory/stats/quest-log grid widget --
      supports both `statsscreen.s`'s direct `SetText(row,col,text)`
      grid-write mode and `inventory.s`'s host-populated,
      `ItemExecutable`-backed row mode via one shared `Row` model),
      `SetInventoryList`/`DisplayWeaponsPage`/`DisplayArmorMenu`/
      `DisplayConsumablesMenu`/`DisplayMiscItemsMenu`/`DisplaySpellsPage`
      (populate the remembered table from the player's real inventory,
      filtered by category), `DisplayCharacterManager`/`DisplayInventory`/
      `DisplayStatsScreen`/`DisplayQuestLog`/`OpenMainMenu` (fixed-target
      navigation to the real `.s` files), `UpdateEquipStatus`,
      `GetLocalizedString`, `TrimText`, `GetSelectedItem`. A row's
      display text can now come from either a stringtable id or an
      already-resolved literal string (`AddButton("Cymric",...)` vs.
      `AddButton(3043,...)`), decided per call via the argument's own
      `skRValue::type()`.
    - **`PopupMenuExecutable::UpdatePopupItem`** implemented for real
      (previously soft-failed) -- `inventory.s`'s action popup
      dynamically shows/hides Use/Equip/Drop/View based on the selected
      item's real state (`CanDrop()`, `IsInventoryEquipped()`, ...), and
      needed this to actually reflect on screen.
    - **A live in-game hook**: pressing the real default control scheme's
      own `CharacterManager` action (`docs/INPUT_HANDLING.md`, bound to
      `KeyHash`/`=` on PC) during the 3D view opens the real
      `charactermanager.s` chain, pausing gameplay; a minimal always-on
      HUD (three vitals bars, `main.cpp`'s `RenderHud`) draws over the 3D
      view itself. Deliberate simplification: `RightSelectionKey` while
      paused for the menu chain *always* returns straight to gameplay,
      even from a nested Inventory/Stats/QuestLog screen, rather than
      backing out one level at a time -- `charactermanager.s`'s own
      `OnRightSoftKey` handler calls `Quit()`+`OpenMainMenu()`, correct
      when reached from a menu but wrong reached mid-game, and no real
      in-game pause-menu entry point was ever found to disambiguate the
      two contexts.
    - Verified end to end against real data, not just "doesn't throw":
      `port/src/tests/m10_inventory_smoke.cpp` loads the real curated
      starting kit, opens the real `charactermanager.s` ->`inventory.s`
      chain, switches categories via the real `ArmorMenu()`/
      `WeaponsMenu()` script callbacks, equips the real armor item and
      confirms `GetArmorRating()` changes by exactly its real
      `SetArmorValue()`, and uses the real consumable (confirms fatigue
      actually rises and the item leaves the inventory). All 6
      pre-existing smoke tests (M2/M3/M6-M9) still pass unchanged.
    - **Explicitly out of scope** (a HUD/inventory-display milestone, not
      a combat-simulation one): actual combat resolution (attack rolls,
      damage, Monster/Actor AI -- those native classes are untouched);
      the buy/sell shop screens
      (`buysell.s`/`blk_market.s`, a separate, large "Store/shop menu"
      class surface); real quest content (`DisplayObjectives` shows one
      fixed placeholder row); a real orientation/world-drop for a
      discarded item.

- [x] **Post-M11 fix -- `.zlu` palette-selection bug (near-black/banded
      walls) + HUD bar repositioning** (this session, prompted by a
      player-submitted side-by-side: a port screenshot vs. the real
      game). Two issues, one root-caused, one deferred:
    - **The real bug**: wall/floor/ceiling faces sometimes rendered
      near-black with visible per-tile banding. Root cause: M9's
      `PaletteColor()` picked which 2048-byte "set" of a real `.zlu` file
      to read via `surfaceTextureIndex % 64`, always its first 512-byte
      chunk -- a documented, explicitly-unverified guess at the time.
      Dumping a real `azra.zlu` (131072 bytes) at every 512-byte boundary
      this session showed it isn't organized per-texture at all: it's
      **4 hue-family palettes of 64 brightness rungs each** (rung 0 of
      every family is pure black, rising smoothly to a clipped-white top
      by roughly rung 8-10, each family its own hue -- brown/tan,
      orange/red, teal/green, blue/purple). This lines up exactly with
      `kMaxLightLevel` (0x3f00 = 63<<8) and RENDERER_3D.md's separately-
      documented per-vertex light/fog scalar clamp `[0x400, 0x3f00]`
      (4-63) -- i.e. `ZmpCell::lightLevel` (already baked per-tile by
      `BakeLighting()`) directly selects the rung (a `>>8`, no rescale),
      and `.sur`'s flags byte (bits 4-5, a best-effort byte-offset guess,
      not independently confirmed by decompilation) selects the hue
      family. Fixed by rewriting `Zone::PaletteColor()` to take
      `(surIndex, lightLevel, texel)` and index `.zlu` directly by
      `(hueGroup*64 + rung)*512`, and by removing the now-redundant
      post-hoc RGB brightness multiply in `zone_renderer.cpp` (brightness
      is baked into the rung selection now, not applied twice). Verified
      three ways: `tests/m6_zone_load_smoke.cpp` now asserts a real wall
      texture's fully-lit brightness sum isn't near-zero (the exact
      regression this session hit), a direct before/after render of the
      same real camera position/yaw went from solid near-black to real
      visible wood-panel texture detail, and all other smoke tests still
      pass unchanged.
    - **Remaining known gap**: per-tile-flat lighting (not per-vertex
      blended -- see M9's own writeup above) still produces visible
      brightness seams between adjacent wall tiles; softening that would
      need real per-vertex light interpolation, not attempted this pass.
    - **The HUD issue**: `main.cpp`'s M10 vitals HUD (three flat bars) was
      anchored top-left; the real HUD anchors its equivalent bars
      bottom-left, inside an ornate gold dragon-head/wing border, plus a
      separate compass banner (heading readout flanked by two dragon
      heads) across the top -- neither art piece exists as a decoded
      on-disk asset. Fixed the position (now bottom-left); the ornate art
      itself was blocked on the 384-slot sprite/icon cache's *source*
      format, since decoded (M13 below) -- the HUD border/compass art
      specifically wasn't tracked down yet though, see "Next milestones".

- [x] **Targeted decompilation pass -- `.zlu` hue-family selector's real
      source, and `.sur`'s flags-byte layout** (this session, immediate
      follow-up to the fix above once the "still doesn't look right"
      report showed the `.sur`-flags-byte guess was wrong). Fully
      decompiled `SurfaceFace_BuildAndProject` (0x1005d784) and its only
      caller, `Render3DScene` (0x100166c8), via pyghidra
      (`shadowkey/extracted/decomp_1005d784.c`, `decomp_100166c8.c`) --
      not a general re-decompilation, just these two functions, enough to
      settle the two concrete unknowns blocking a real fix:
    - **`.sur`'s 8-byte record, byte-for-byte** (previously only byte[7]
        was used, the rest approximated by elimination): byte[0]/[1] = U/V
        bit-shift, byte[2..3]/[4..5] = two signed-16-bit UV offsets,
        byte[6] = flags, byte[7] = texture index. Confirms the earlier
        elimination-based guess was exactly right on layout -- but byte[6]
        itself turned out to be something this session's earlier fix had
        wrong: **bit0 = flip V, bit1 = flip U, bit5 = disable this face
        entirely** (`SurfaceFace_BuildAndProject`'s very first check,
        `(flags & 0x20) == 0`, gates the whole function body) -- not a
        `.zlu` hue-family selector at all. A real `azra.sur` record has
        bit5 set; the previous fix was mistakenly rendering that face
        (green-tinted, from a wrong hue-group guess) when the real engine
        never draws it. Wired up as `Zone::surfaceDisabled()`, checked in
        `zone_renderer.cpp`'s `AddWall`/`AddFloorCeiling` alongside the
        existing `surIndex == 0xff` check; flip-U/flip-V decoded but not
        yet wired into the port's (still-flat-0..1) UV computation.
    - **The real `.zlu` hue-family selector's source**: tracing
        `SurfaceFace_ClipAndDispatch`'s `*param_2` through
        `Render3DScene`'s 4 wall-direction blocks (`param_2` = `pbVar36 +-
        8`/a row-stride offset, i.e. simple pointer arithmetic on a
        `ZmpCell*`) shows it's **not** part of `.sur` at all -- it's
        `ZmpCell::flags` bits 4-5 (previously-undocumented bits in an
        already-mostly-decoded byte, ZONE_FORMAT.md's bit0/1/3/6), read
        from the *blocking neighbor's* cell for a wall face and the
        *current* tile's own cell for floor/ceiling. `Zone::PaletteColor()`
        now takes `hueGroup` directly from the caller instead of deriving
        it from `surIndex`; `zone_renderer.cpp`'s `CollectFaces()` resolves
        the right cell per direction (`hueGroupOf()` for walls, the
        current tile for floor/ceiling) and threads it through the new
        `Face::hueGroup` field.
    - **Re-tested against the same real camera position** used to verify
        the previous fix: no longer near-black (confirmed fixed), and the
        specific wall this pass flagged as green is now understood as
        *plausibly correct* rather than definitely-a-bug -- dumping the
        full 256-entry palette (not just the first ~10 indices sampled
        earlier) shows real material-to-material color variation *within*
        one hue family's mid/high index range, so a texture using those
        indices can legitimately look teal/green even in "family 0."
        Confirming an exact match to a specific reference screenshot would
        need identifying which real room the screenshot was taken in
        (not yet done) -- the *mechanism* (which byte selects what) is now
        decompiled ground truth either way, replacing every guess the
        previous fix pass made. All 8 smoke tests still pass; live app
        boot still responsive.

- [x] **Post-M11 fix -- `actionqueue.s`'s real hand-assignment flow,
      decompiled** (this session). Decompiled all three native class
      dispatchers behind `charactermanager.s`'s left/right-hand action-
      queue UI (`FUN_10034d8c` -- `SetLeftActionQueue`/
      `SetRightActionQueue`/`IsLeftQueue`/`ShowActionQueue`/
      `ShowRemovedQueue`; `FUN_10033660` case 1 -- `UpdateEquipStatus`;
      `FUN_1002ed74` -- an item's equip-category field read).
    - **`ShowActionQueue()` genuinely opens a screen in the real game** --
      it resolves the `actionqueue.s` menu slot, copies the caller's
      hand-selection flag onto it, and actually pushes it (confirmed via
      `FUN_10034c40`). The port's `ShowActionQueue()` was a complete
      no-op before this pass; now it really does `MenuStack::OpenMenu
      ("actionqueue")` and copies the flag, matching real behavior.
    - **But `actionqueue.s`'s row population is confirmed non-functional
      in the real shipped game.** `UpdateTextItems()` and `GetLastItem()`
      -- the two natives that script calls to fill its 5 floating-text
      rows with real item names and wire up `GetAssociatedObject()` --
      are **absent from the fully-enumerated real 702-entry native
      table** (cross-checked against
      `shadowkey/simkin_native_bindings.json`). They were never
      registered, so those calls always miss and soft-fail in the real
      binary too: the rows permanently show the literal placeholder text
      `"item"`, and `DropItem`/`ItemUp`/`ItemDown` always operate on a
      null associated object. Likewise `actionqueue.s` itself calls
      `IsRightQueue()`, which also isn't a registered name (only
      `IsLeftQueue` is) -- so its title always falls back to the
      left-hand string regardless of which hand was actually selected.
      `removequeue.s` (`ShowRemovedQueue`) is even more clearly dead:
      `FUN_10034d8c` resolves its menu slot but never calls the
      copy-flag-and-push helper `ShowActionQueue()` does, and
      `charactermanager.s`'s only call site for it is commented out. This
      port now reproduces that same (non-functional) screen faithfully
      rather than inventing a working one -- see `MenuExecutable`'s
      `ShowActionQueue()`/`IsLeftQueue()` handlers for the full writeup.
    - **`UpdateEquipStatus`'s real hand-choice logic, decompiled**: not
      "always right hand" (the previous stub) -- unequip-toggle if the
      item is already worn in either hand, else auto-fill whichever hand
      is currently *empty* (the real function only assigns a hand's
      active slot when that slot was previously empty). The real function
      also backs this with a genuine multi-item queue per hand (so a
      third weapon equipped while both hands are full joins an invisible
      waitlist, via roughly a dozen further list/UI helper functions not
      traced here) and an item-category field (`+0x1c0`, read by
      `FUN_1002ed74`) that can restrict some items to one hand only,
      neither of which this port models (disproportionate to the earlier,
      confirmed-broken screen that would let a player observe either
      one) -- `PlayerExecutable::UpdateEquipStatus` now does the
      empty-hand-first fill/unequip-toggle for real, and silently no-ops
      (still returns success) if both hands are already occupied.
      `GetAttack()` updated to sum both hands' weapon damage, since a
      solo weapon can now land in the left hand.
    - Verified: `port/src/tests/m10_inventory_smoke.cpp` extended with a
      real `charactermanager.s` -> `RightQueueSelected()` ->
      `actionqueue.s` round trip, asserting the target menu actually
      switches and carries the right-hand flag -- guards against
      `ShowActionQueue()` regressing to its old no-op. All 8 smoke tests
      still pass.

- [x] **Post-M11 fix -- `.zsk` room-mesh world position, decompiled** (this
      session). Decompiled `RoomGeometry_TransformAndSort` (0x10057890) in
      full to settle M11's open item above.
    - No per-room world-position field exists in the real function --
      unlike the actor pipeline's `actor+0x94/+0x9c` world position, the
      only room-specific placement inputs are an optional scale byte pair
      (`room+0x5e/0x5f`) and optional rotation angles
      (`room+0xa8/0xb2/0xb6`, each zeroed when a flag bit at `room+0x86`
      is set). The actual per-vertex translation applied comes from a
      single **fixed** `(0, 270, 0)` constant, identical for every room
      (not read from any room field), composed with the camera's own
      per-frame rotation matrix via `ComposeTransform3x4`.
    - **Confirms the port's existing no-offset placement is correct** --
      there was no missing per-zone translation to find. No port code
      changed as a result (nothing was wrong); this converts an
      "unconfirmed, plausible" open item into decompiled fact.
    - Two smaller loose ends explicitly left open (not chased further,
      low payoff for a small ~2x2-tile decorative set-piece): whether
      `room+0x5e/0x5f`'s scale is ever non-identity for a real zone (not
      traced -- the room object's own construction site wasn't found),
      and how camera *position* (not just rotation) factors into this
      path at all -- `engine+0x5d8`'s build only consumes camera angles,
      so it's plausible this mesh is a camera-orientation-relative
      decorative piece rather than a walk-around-able room. Full writeup:
      `docs/RENDERER_3D.md`'s "`.zsk` room-mesh world position --
      decompiled" section and `render3d/zone_renderer.h`'s updated M11
      comment.

- [x] **Post-M11 fix -- player ground/gravity physics** (this session).
      Requested directly (before scoping combat resolution): the player
      was "just a floating camera clipping through geometry, not
      respecting where the ground is" -- `gameCamera.z` was set once at
      zone load (`playerStartZ + kEyeHeightOffset`) and never touched
      again; only X/Y had wall collision (M7's `CircleHitsWall`).
    - **No RE ground truth exists for this** -- `docs/WORLD_MODEL.md`
      notes the real engine has fixed-point actor positions/collision
      but was never traced to the byte level, and no gravity/jump/step
      constant has ever been recovered. This is a from-scratch gameplay-
      feel design, same spirit as `render3d/camera.h`'s
      `kEyeHeightOffset`, not a decompiled behavior.
    - Added `Zone::FloorHeightAt`/`CeilingHeightAt` (`world/zone.h`/
      `.cpp`): bilinear interpolation across a tile's own stored
      `ZcpEntry::floorHeight`/`ceilingHeight[4]` corners (the same 4
      values `render3d/zone_renderer.cpp`'s `AddFloorCeiling` already
      draws the floor/ceiling quad from -- no new data, just sampled
      instead of rendered). Verified against real `azra.zcp` data in
      `m7_collision_smoke`: exact-corner sampling matches the stored
      value directly, and a real open tile's floor/ceiling gap is
      sane (thousands of raw units, consistent with M9's room-height
      finding).
    - `main.cpp`'s tick loop now does simple per-tick Euler gravity
      (`kGravity`/`kMaxFallSpeed`), a jump impulse on `Action::Jump`
      (`kJumpSpeed`, bound to Key1 per the real default scheme -- was
      decoded but never wired to anything), a floor clamp that snaps
      the player to `FloorHeightAt(x,y) + kEyeHeightOffset` whenever
      falling and at/below it (so slopes/steps of any height are
      auto-followed while grounded -- no max step-height wall-block,
      since there's no data on what the original's real ledge/stair
      behavior was and `CircleHitsWall`'s wall flag already blocks
      genuinely impassable tiles), and a ceiling clamp
      (`kHeadroom`) against `CeilingHeightAt`.
    - Also wired `Action::SideStepLeft`/`SideStepRight` (Key4/Key6) to
      real strafing -- like Jump, decoded in `docs/INPUT_HANDLING.md`
      but previously completely unused; movement forward/back/turn
      still reads the raw `ButtonSlot` layer unchanged.
    - Not attempted this pass: swimming/water, crouching (`SideStep`
      wiring above only covers horizontal strafe), and any of the
      real `PlayerExecutable`/menu-side physics hooks (if any exist --
      not searched for).

- [x] **M12 -- combat vertical slice: one weapon vs. the azra rat** (this
      session). Narrow first cut at "Combat resolution" below, scoped
      deliberately small (one monster type, one damage formula) instead
      of attempting the full ~250-method native surface at once.
    - **Real data, not a synthetic fixture**: `monsters/Azra_Rat.s`'s
      real `Init()` actually runs through the vendored interpreter
      (`MonsterExecutable`, `port/src/simkin_bindings/
      monster_executable.h/.cpp` -- same `skScriptedExecutable`-backed
      pattern `ItemExecutable` established for M10's real armor/weapon
      scripts), loaded via a chain confirmed this session:
      `entities.txt`'s 4th column (`EntityTypeDescriptor::name`, already
      decoded in `docs/ZONE_FORMAT.md` but never actually used by the
      port before now) is a real script path, and the currently-tested
      `azra` zone places 35 real typeId-202 instances. Verified against
      the real script's literal values (attack=3, defense=4,
      maxHealth=12, damageMin=3/damageMax=6, armorValue=2,
      chaseRadius=18000) in the new `combat_smoke` test
      (`src/tests/m12_combat_smoke.cpp`).
    - `OnKilled()`'s real quest-kill-count branch
      (`GetPlayer().QuestSolved`/`AddMonsterKilled`/`MonstersKilled`/
      `SetQuestSolved`) is deliberately left unimplemented on
      `PlayerExecutable` -- confirmed this session that the established
      soft-fail convention already makes it behave correctly with no
      kill counter modeled (walks the `else` branch, no-ops, the `>= 8`
      check never trips), not a gap.
    - **From-scratch, no RE ground truth**: only `AiDetect()`/
      `AiSleep()` are ever called from any real monster script in the
      whole corpus (`AiAttack`/`AiPursue`/`AiFlee`/`AiWounded`/
      `AiActivate` never are) -- confirming the real AI state machine is
      opaque/native, not scripted, so there's no real state machine to
      match. `main.cpp`'s own Idle/Chasing/Attacking loop and
      `simkin_bindings/combat.h`'s `RollDamage()` formula (hit-chance
      from attack-vs-defense, damage mitigated by armor) are a deliberate
      port-only design, same footing as the physics constants above --
      easy to retune, not a recovered constant.
    - Wires the real decoded `UseLeftAction`/`UseRightAction` bindings
      (Key7/Key5, previously unused) to melee attacks with whichever
      hand's weapon is equipped (bare fists otherwise).
    - Not attempted: any monster type other than `typeId 202`/azra_rat,
      spellcasting, loot spawning on death (`SetLoot`'s params are
      stored, unused), animations/sounds (this port has none at all
      yet), monster-vs-player physical collision, de-aggro/flee/return-
      to-post behavior (`SetWimpy` stored, unused), and the generic
      `Action::Use` interact binding (stays unbound -- this slice only
      covers the two hand-attack actions).
    - Not independently playtested for feel -- I can't drive the actual
      windowed game loop headlessly; `combat_smoke` verifies the real
      data and the host-side API mechanically (damage/death/soft-fail
      sequencing), not whether the fight feels right in the running
      game.

- [x] **M13 -- real menu backgrounds + item/HUD icons, plus the real-font
      question closed** (this session). The other two items the user
      asked to close out before returning to combat resolution.
    - **Font: closed with no code change** -- traced the real UI text-
      draw path (`DrawListItemIconAndLabel` -> `FUN_1007f49c` ->
      `FUN_1008f8a4` -> `FUN_10022b20`/`d48`/`f88`) and confirmed it
      calls genuine Symbian EIKON/GDI APIs (`CEikonEnv::LegendFont()`,
      or a `TFontSpec` for the stock `"Swiss"` family). The real glyphs
      are the Nokia N-Gage's own system font, living in the device ROM
      -- not a file this game ships, so there's nothing left to extract
      from this project's assets. Full trace: `GRAPHICS_FORMAT.md`'s
      new font section. The stand-in bitmap font
      (`port/src/graphics/bitmap_font.h`) is now a confirmed permanent
      substitute, not an open gap.
    - **Menu backgrounds/icons: real format decoded and wired up.**
      Traced `engine+0x4460`'s actual writer (not just its readers) by
      decompiling the whole ~2000-function binary and grepping for the
      offset (new `pyghidra_grep_decompiled.py` -- needed since the
      offset is built via ARM arithmetic, not a loadable literal).
      Found the lazy-load-and-cache function and its two format-string
      arguments: `"%s\global.spr"` (the sprite data, one global file)
      and `"%s\%s_sprites.txt"` (a per-category manifest -- newline-
      separated slot indices, same convention as the already-known
      `<zone>_models.txt`/`_sounds.txt`). `global.spr` is a 384-entry
      size table followed by the sprite blobs concatenated back-to-back
      -- each blob already the exact in-memory RLE layout
      `GRAPHICS_FORMAT.md` had already decoded from `Blit_RLESprite`,
      no separate decode step. Verified both structurally (the real
      file's byte-accounting matches exactly) and visually (rendered
      real slots to PNG -- a parchment-and-vine-border 176x208 menu
      background, a dagger icon, a character portrait, a UI gradient
      strip).
    - Grepped every real script's `MenuBackground(id)` call: only 3
      distinct ids ever used (20, 69, 174), all 3 real full-screen
      slots already in `menu_sprites.txt` -- `id` *is* the `global.spr`
      slot index directly, zero translation needed.
      `MenuExecutable::backgroundId()` already stored this; new
      `port/src/assets/sprite_archive.h/.cpp` (mirrors `world/
      model_archive.h`'s lazy-load-and-cache shape) plus
      `Backbuffer::Blit()` (straight opaque copy only -- the real
      engine's 50%-blend `blendMode==1` has no current UI use) draw it
      for real now, replacing the flat-color fallback whenever a slot
      decodes. **Confirmed live** -- launched the built port and
      screenshotted the real main menu: the parchment background and
      corner art render correctly.
    - Item icons (`ItemExecutable::SetIcon()`, ids 209-218 in the real
      corpus) load from the *active zone's* `<zone>_sprites.txt` (same
      per-zone manifest item scripts already need for every zone, not
      `menu_sprites.txt`) and draw next to `ItemButtonExecutable` rows
      (equip-slot buttons) when the resolved slot is row-sized (<=40px
      -- some of the same ids item scripts use resolve to full-screen
      176x208 panels, presumably a detail/examine view this port
      doesn't have; those fall back to label-only, same as an
      undecoded slot).
    - **Not attempted**: inventory *table* row icons (`TableExecutable`
      rows don't carry a per-row icon id in this port's model -- would
      need a new field threaded from `ItemExecutable`, a larger change
      than this pass), the `blendMode==1` translucency path, and
      identifying exactly what the larger (150x100-ish, and a couple of
      176x208) slots some item icon ids resolve to.
    - New `sprite_smoke` test verifies `global.spr`'s TOC against the
      real file's exact byte size, all 3 real `MenuBackground` ids
      decode to full-screen non-degenerate sprites, `menu_sprites.txt`
      decodes end to end, and out-of-range/unused-slot lookups fail
      safely.

- [x] **M14 -- real gameplay HUD: compass banner + vitals bar** (this
      session). Continuation of M13 -- traces the always-on in-game HUD
      draw path specifically (M13 only covered menu/list-item icons).
    - The obvious per-tick render path (`GameTick_UpdateAndPresent` ->
      `FUN_10068e0c`'s gameplay case -> `Render3DScene`) turned out to
      be a dead end -- `Render3DScene`'s full decompile has zero
      sprite-cache references. The real HUD functions are reached
      through **indirect vtable dispatch** on `ScreenModeController`'s
      secondary vtable (a static array at `0x100fb908`), invisible to a
      normal "find callers" search -- found instead by searching for
      each candidate function's own address as a raw value elsewhere in
      the binary (its vtable slot).
    - **Compass banner** (`FUN_1002ba64`): `global.spr` slot 0 (a
      322x13 "...N...E...S...W..." strip, wider than the screen on
      purpose) scrolled through a 68px window at a heading-derived
      source offset, with slot 1 (176x31, the dragon-head-flanked
      frame) drawn on top -- its transparent center window is exactly
      where the scrolled tape shows through.
    - **Vitals bar** (`FUN_1002c010`): slot 205 (79x9 red gradient)
      width-clipped to a percentage fill, with slot 206 (94x42
      dragon-wing frame) drawn on top, same fill-then-mask technique.
      No static caller found (same indirect-dispatch issue) so the
      exact stat isn't 100% certain -- red color/prominent position
      point to health.
    - **Equipped-item icons** (`FUN_1002bb54`): draws the real
      left/right-hand equipped item icon at (5,5)/(139,5), flanking the
      compass -- ties directly into the hand-equip system a previous
      session already decompiled (`UpdateEquipStatus`).
    - Implemented in `main.cpp`'s `RenderHud`: real compass + health
      bar at the real positions (`Backbuffer::BlitRegion`, new,
      supports both the compass's source-scroll and the bar's
      destination-width clip); magicka/fatigue keep the M10 flat-bar
      stand-in (no real asset/position found for them -- possibly this
      cut-down HUD only gives the ornate treatment to health, not
      confirmed either way), relocated clear of the new health bar.
      This port has no 16-bit fixed-point heading field to replicate
      the real byte-extraction formula exactly, so the yaw-to-scroll
      mapping is a documented, unverified-direction best effort, not a
      decompiled formula.
    - **Confirmed live**: created a character and screenshotted actual
      gameplay -- the gold dragon-head compass frame (with the `N`
      glyph visible in its window) and the dragon-wing health bar both
      render correctly.
    - Full writeup: `docs/GRAPHICS_FORMAT.md`'s "The real gameplay HUD"
      section.

- [x] **Post-M14 fix -- vitals bar was the wrong widget; real health/
      magicka/fatigue cluster found and implemented** (this session).
      Prompted by two user-provided screenshots of real gameplay: one
      showed a small 3-bar cluster (health/magicka/fatigue stacked in
      one 57x46 dragon frame) the port didn't render at all, the other
      showed the M14 big single bar actually does appear in real play
      too, just not as the default HUD.
    - Traced `FUN_1002ae88`'s (previously misidentified as a "weapon
      condition" indicator) real direct caller: `FUN_10029cb0`, itself
      vtable slot `+0x1c` on the same secondary vtable, confirmed called
      *every tick unconditionally*. Its gameplay-state branch calls
      `FUN_1002ae88()` + `FUN_1002ba64()` (compass) + `FUN_1002bb54()`
      (hand icons) together, every frame -- settling that `FUN_1002ae88`
      is the real vitals HUD, not `FUN_1002c010`.
    - `FUN_1002ae88` draws three 39x5 bars (`global.spr` slots
      162=red/160=blue/161=green) at `(10,182)`/`(10,190)`/`(10,196)`,
      then one shared frame (slot 180, 57x46) on top at `(0,162)`. Field
      order in the backing stat struct (red/green/blue, not red/blue/
      green) plus standard color convention fixes red=health (top),
      blue=magicka (middle), green=fatigue (bottom).
    - `FUN_1002c010`'s big single bar is real and does appear in actual
      play (per the second screenshot) but its trigger is still
      unresolved -- a whole-memory raw scan (not just literal pools) for
      its address and its vtable's base address both come back with
      exactly one static reference each (the one vtable slot itself),
      so the real call site is a fully dynamic dispatch this pass
      couldn't pin down. Left unimplemented; documented as an open item
      rather than guessed at -- see `docs/GRAPHICS_FORMAT.md`.
    - `main.cpp`'s `RenderHud` now draws the real 3-bar cluster (with a
      flat-bar fallback if the assets fail to load) in place of the
      single health bar + relocated flat magicka/fatigue bars M14 had.
    - **Confirmed live**: created a character and screenshotted actual
      gameplay -- the small dragon-head frame renders with all three
      real bars (red/pale-blue/green), matching the user's reference
      screenshot.

- [x] **Post-M14 fix -- the real N-Gage system font, decoded and wired
      in** (this session). M13's "real font" closure only established
      *where* the glyphs live (the N-Gage's Symbian ROM, not this
      game's files) -- a real ROM dump turned out to be available (an
      EKA2L1 emulator profile), reopening the question of whether that
      ROM's font could actually be extracted and used.
    - `Z:\System\Fonts\Ceurope.gdr` on a real N-Gage QD ROM dump is a
      genuine Symbian OS `.gdr` bitmap-font file -- not a shadowkey
      format, so no Ghidra angle; ported the byte layout from the
      open-source EKA2L1 emulator's own GPLv3 parser, then verified
      independently: the port's reimplementation consumes the *entire*
      31389-byte file with zero leftover bytes and recovers 8 real
      typefaces with correctly shaped glyphs.
    - The one real trap: embedded strings (typeface names, copyright
      text) are SCSU-compressed, and the on-disk length prefix is only
      an upper-bound *buffer* size to decompress into -- the real
      per-string byte length only comes out of actually running the
      decompressor. A naive "skip N bytes" first attempt desynced the
      whole rest of the file after the first string; had to port the
      real SCSU state machine to fix it. Full writeup: `docs/
      GRAPHICS_FORMAT.md`'s "The real Symbian .gdr font format" section.
    - New `port/src/assets/gdr_font.h/.cpp` (`GdrFont`, parses a `.gdr`
      and exposes one typeface's glyph bitmaps by codepoint) and `font_
      smoke` test (parses the real file, checks byte-exact full-file
      consumption, all 94 printable-ASCII codepoints decode to sane
      glyphs, missing-file handling stays non-fatal).
    - `bitmap_font.h`'s `BitmapFont::DrawString` now draws real glyph
      bitmaps (real per-glyph advance/left-bearing/baseline placement)
      wherever the loaded font covers a codepoint, falling back to the
      old blocky 5x7 placeholder otherwise. `main.cpp` loads
      `"LatinPlain12"` from a configurable path (default `port/assets/
      fonts/Ceurope.gdr`) -- like the retail game install, this file is
      Nokia device firmware, never committed to the repo (see
      `.gitignore`); each dev copies their own extraction in.
    - **Confirmed live**: the main menu now renders real proportional
      mixed-case Symbian UI text ("New Game", "Load Game", ...) in
      place of the old blocky upper-case-only placeholder.

- [x] **Post-M14 fix #2 -- the .gdr font above was wrong; real menu
      font, size, position, and alignment all corrected** (same
      session, immediate follow-up). A user-provided real screenshot
      compared against every glyph in *both* real ROM font files
      (`Ceurope.gdr`'s 8 typefaces, `Browsereur.gdr`'s 9 more) showed
      none of them match -- the real menu font is a distinctly rounded
      face, every real device font is a plain blocky sans.
    - Before concluding it must be a non-ROM font, checked whether it's
      instead a hand-drawn glyph-sheet sprite (plausible for 2004
      handset UI, and the letterforms do look hand-pixeled) -- scanned
      all 384 `global.spr` slots (already fully decoded) for a
      glyph-sheet shape; found none (the one dense run of same-sized
      slots is a torch-flicker animation, not letters). Checked
      `6r51.mbm` too (a real separate Symbian bitmap resource this game
      ships) -- 1770 bytes, just the launcher icon. Rules out the
      likely loose-asset locations, doesn't rule out glyphs compiled
      directly into `6r51.app`'s own resources (not checked).
    - The user identified the actual visual match by comparing against
      what EKA2L1 itself renders with: **"Nokia Cellphone FC"**, a
      freeware TrueType lookalike of classic Nokia phone displays.
      *How* EKA2L1 arrives at this font wasn't pinned down (checked its
      font-matching source, user-font-import mechanism, and config
      override -- none explain it on this machine) -- a visual-match
      finding, not a traced code path.
    - `bitmap_font.h`/`.cpp`'s `LoadRealFont` now dispatches by file
      extension: `.ttf` renders through a new `TtfFont` class using
      **Win32 GDI** (`AddFontResourceExA`/`CreateFontIndirectW`/
      `ExtTextOutW` into an off-screen DIB, composited by luminance
      blend) -- this port already links GDI for presentation, no reason
      to hand-parse TrueType outlines. `.gdr` still works via `GdrFont`,
      just isn't the default. `main.cpp` now loads `port/assets/fonts/
      nokiafc22.ttf` as `"Nokia Cellphone FC"`, sized at 8px (12px and
      10px both visibly too large against the real screenshot).
    - Position was also wrong: the main menu's background
      (`MenuBackground(69)`) has logo art baked into its top ~50px,
      unlike every other menu background -- the port's shared `y = 8`
      item-list start collided with it. Fixed to `y = 50` specifically
      for `backgroundId() == 69` (a measured constant, nothing in
      `mainmenu.s` sets this).
    - Alignment was wrong too: real menu items are centered with
      color-only selection (no arrow glyph); the port had left-aligned
      text plus an invented `>` selection arrow. `BitmapFont::
      TextWidth(text)` (changed from an unused fixed-pitch overload to
      a real GDI-measured width) now centers `MenuItem`/`StaticItem`
      rows in `RenderMenu`; the arrow draw was removed.
    - **Confirmed live**: font shape, size, position, and centered
      alignment all now match the real screenshot.
    - Like `Ceurope.gdr`, `nokiafc22.ttf` is third-party and never
      committed to this repo (see `.gitignore`) -- each dev supplies
      their own copy. Full writeup: `docs/GRAPHICS_FORMAT.md`'s
      "Correction" section right after the `.gdr` writeup.

- [x] **Post-M14 fix #3 -- fix #2 above was itself wrong; `Ceurope.gdr`
      is the real menu font after all, plus real text colors** (same
      session). Two things prompted a second look: the TTF still didn't
      look right, and decompiling shadowkey's own text-draw chain
      (`DrawUIText` -> `FUN_1008f8a4` -> `FUN_10022b20`) showed ordinary
      `AddMenuItem` rows hit the branch that calls genuine `EIKCORE::
      LegendFont()` -- the real ROM font -- confirming fix #2's TTF
      substitution was never actually justified by the game's own code.
    - Re-compared properly this time: downscaled the real screenshot
      back to native 176x208 (undoing a video capture's ~4.4x upscale)
      instead of comparing against the upscaled image, then checked
      "New Game" letter-by-letter against every `Ceurope.gdr` glyph --
      **exact bit-for-bit match against `LatinBold12`** (not
      `LatinPlain12`, fix #1's original guess). The "rounded" look that
      drove fix #2 was video-compression blur on a blocky ~11px bitmap
      font in the upscaled image, not a real font difference.
    - `main.cpp` loads `Ceurope.gdr`/`"LatinBold12"` again.
      `BitmapFont::TextWidth` (added in fix #2 for centering) now also
      sums real `.gdr` per-glyph advances, not just the TTF/placeholder
      cases -- needed for centering to measure the font actually in use.
      The TTF path stays in the code, working, just not the default.
    - Also fixed (separate user feedback, same real screenshot): menu
      text colors, sampled directly off it -- unselected items are dark
      red/maroon (`~112,48,48`), the selected item is near-white
      (`~216,216,216`), replacing the port's prior gold/light-gray
      scheme. Gave the disabled/static color its own distinct muted
      tone too (previously too close to the unselected color to read
      as a real third state).
    - Full writeup: `docs/GRAPHICS_FORMAT.md`'s "Correction #2" section.

- [x] **M15 -- Action::Use interact binding: doors** (this session). First
      (narrow) slice of "Combat resolution (beyond the M12 slice)"'s
      generic interact binding, same "one real category, not the whole
      57-method native surface" precedent M12 set for combat -- pickups
      and NPC talk stay unbound (see "Not attempted" below).
    - **Real script, not a synthetic fixture**: `door.s`/`door02.s`
      (read directly off the install image) are ordinary placed-entity
      scripts on the real `Object/Entity (world base)` native class
      (`shadowkey/simkin_native_bindings.json` trie `0x14d08`, docs/
      SIMKIN_NATIVE_API.md) -- `Init()` calls `SetUseText(941)`/
      `SetMPUsable(true)`; `OnUse()` toggles `saved_Open`, calling
      `SetPassable`/`AddRotationTurn`/`DoorOpened`/`SetUseText` each
      time. New `sk_bindings::DoorExecutable`
      (`simkin_bindings/door_executable.h/.cpp`) actually runs these
      through the vendored interpreter, same `skScriptedExecutable`-
      backed pattern `MonsterExecutable`/`ItemExecutable` already
      established -- confirmed against real `azra.ent` data (7 real
      typeId-54 placements, all resolving to `door.s`) in the new
      `interact_smoke` test (`src/tests/m15_interact_smoke.cpp`):
      `useTextId`/rotation/`passable` all match the script's own literal
      values across a full open-then-close cycle.
    - **Which entities get a live script**: generalized past M12's
      single-hardcoded-typeId check -- any placement whose
      `entities.txt` category is 11 (door, docs/ZONE_FORMAT.md's
      category table) *and* whose `name` column is an actual `.s`
      script (not the `"!label"`-only convention most category-11
      entries use for reused-category dungeon set-pieces with no unique
      behavior of their own -- switches, urns, mushrooms, ... only 2 of
      60 real category-11 typeIds are real scripts). Everything else
      still falls through to the unchanged static-prop path.
    - `AddRotationTurn`'s raw units share the same 65536-per-turn
      fixed-point convention already confirmed for `.ent`'s
      `rotOrScale` fields (docs/ZONE_FORMAT.md) -- `door.s`'s own
      `-64*256` is exactly a quarter turn, a physically sensible 90-
      degree swing. `render3d/zone_renderer.cpp`'s `SubmitModel` gained
      a new `entityYaw` parameter (rotating each vertex's local X/Z
      about the model's own origin before the existing camera
      transform) to actually show it -- `PlacedEntity` gained a `yaw`
      field, zero for every entity but a live door, so this is purely
      additive to M8's existing "no orientation" placed-entity render
      path.
    - `SetPassable`/`SetMPUsable`/`DoorOpened` are stored/no-op only --
      this port has no per-entity collision at all yet (only
      `Zone::CircleHitsWall`'s tile-grid test), so a closed door was
      never actually blocking movement to begin with (not a regression
      this pass introduces), and no multiplayer/replication exists to
      receive `DoorOpened`.
    - `main.cpp`: Key3 (`Action::Use`, the real default binding, docs/
      INPUT_HANDLING.md -- previously decoded but unwired) finds the
      nearest live door within 140 units in a forward-facing cone (same
      targeting shape M12's melee `tryAttack` already established) and
      calls its real `OnUse()`. The door's real `SetUseText()` string
      (`"Open door"`/`"Close door"`, resolved through the string table)
      now shows as an on-screen prompt when facing a door in range, at
      the same HUD line M12's monster-name/HP label uses (monster combat
      takes priority when both are in range at once).
    - **Visual confirmation**: extended `interact_smoke` to also render
      one real door (headless, `ZoneRenderer`, same PPM-dump technique
      `m8_render_entities_smoke.cpp` already established) before and
      after a real `OnUse()` call -- the door's projected footprint
      visibly changes from its closed orientation to a wider face-on
      profile after opening, consistent with a door swinging into view.
      Not independently confirmed in an actual windowed play session
      (same caveat M12's writeup already carries -- can't drive the
      real windowed game loop headlessly).
    - **Not attempted**: pickups (`entities.txt` categories 3/8/9,
      misc-loot/containers/consumables -- would need a world-to-
      inventory transfer this port's `ItemExecutable`/`PlayerExecutable`
      don't have yet) and NPC talk (guessed here as category 7/merchants
      -- **corrected by M16 below**, real NPC talk is actually category
      2/monster, the same category as ordinary hostile monsters,
      distinguished only by script content). Both still needed a
      materially different scope than a stateless door toggle -- NPC
      talk is M16's own slice below; pickups remain unbound.

- [x] **M16 -- generalized monster/NPC loading + `Action::Use` dialogue**
      (this session). Second slice of "Combat resolution"'s still-open
      items -- both "other monster types" and (per a real finding this
      pass) "NPC talk" turned out to be the *same* underlying gap: M12's
      loader only ever constructed a `MonsterExecutable` for the one
      hardcoded `typeId 202`.
    - **Real finding that reframed NPC talk's scope**: `entities.txt`'s
      category 2 ("monster") covers named quest NPCs alongside ordinary
      hostile monsters -- e.g. real `monsters/Tanyin_Aldwyr.s` (typeId
      166, 1 real placement in azra) is category 2, not the guessed
      category 7 (merchant) M15's "Not attempted" note above assumed.
      NPCs are distinguished purely by their own script content:
      `SetAggressive(false)` + `SetUsable(true)`/`SetUseText(...)` +
      (for essential named characters) `SetInvulnerable(true)`, with an
      `OnUse()` that calls `OpenMenu(...)` to start a real dialogue
      instead of participating in combat at all. So generalizing the
      loader past `typeId 202` and wiring `Action::Use` into it delivers
      real NPC dialogue "for free," using the exact same
      `MonsterExecutable`/`OnUse()` mechanism M15 already built for
      doors -- no new native class needed.
    - **Generalized past M12's hardcoded typeId**, same `HasRealScript()`
      (".s"-suffix) filter M15 already established for doors, now shared
      by both branches of the zone-load loop: any category-2 placement
      with a real script loads a live `MonsterExecutable`. Confirmed
      against real azra data: 11 distinct category-2 typeIds place real
      scripts in this one zone alone (65 placements beyond the 35
      azra_rats M12 already covered), including Tanyin Aldwyr.
    - `MonsterExecutable` gained `SetUsable`/`SetUseText`/
      `SetInvulnerable`/`OpenMenu` (the last routes through a new
      `MenuStack&` constructor parameter, same reference
      `MenuExecutable`'s own `OpenMenu` handler already goes through) and
      `InvokeOnUse()` (mirrors `InvokeOnKilled()`). `ApplyDamage()` now
      no-ops when `invulnerable()` -- ArithmeticException-free, an
      essential NPC genuinely can't be killed. Verified against real
      `Tanyin_Aldwyr.s`/`snowline/tanyinconvo.s` data in the new
      `npc_smoke` test (`src/tests/m16_npc_smoke.cpp`): real
      aggressive/usable/useTextId/invulnerable values, `ApplyDamage`
      surviving a lethal hit, and `OnUse()` actually opening the real
      conversation menu (confirmed non-null, changed, and carrying real
      rows -- the quest-state-gated `if` chain in `tanyinconvo.s`'s
      `Init()` soft-fails through unmodeled `QuestSolved`/
      `QuestAssigned` calls into a real, deterministic opening line
      rather than erroring, the same soft-fail philosophy already
      established everywhere else in this port).
    - `main.cpp`'s AI/melee loop gated on the real `aggressive()`
      flag (previously implicit/coincidental via a never-set
      `chaseRadius()` defaulting to 0) and melee targeting now skips
      `invulnerable()` entities outright, so an NPC never shows up as an
      attackable target. The y=34 HUD prompt line now shows an in-range
      usable NPC's real `SetUseText()` (same door-style prompt M15
      established) when no hostile monster is in melee range.
    - `Action::Use` (Key3) now checks both `gameDoors` and usable
      `gameMonsters`, firing whichever real placement is nearer. When an
      NPC's `OnUse()` opens a menu (detected by comparing
      `MenuStack::currentMenu()` before/after -- robust to whatever
      menu was already stale-cached from before `NewGame()`), the tick
      loop switches out of the 3D view the same way the
      `CharacterManager` action already does (`gamePausedForMenu`), so
      `Esc` returns straight to gameplay afterward -- `tanyinconvo.s`'s
      own `Quit()` call is a plain (unimplemented) `Quit`, not
      `QuitGame`, so it soft-fails as a harmless no-op rather than
      routing anywhere; only `Esc` actually leaves the conversation.
    - **Not attempted**: modeling the quest state (`QuestSolved`/
      `QuestAssigned`/`QuestCompleted`/`AddExperience`/`AddMonsterKilled`)
      dialogue trees like `tanyinconvo.s` branch on -- every such call
      soft-fails to a benign default, so conversations always render
      their first-visit branch rather than tracking real progress. A
      real, substantial feature on its own, out of scope for this pass
      (same "soft-fail lets scripts run, imperfectly, rather than not at
      all" precedent every prior milestone already relies on). Pickups
      (world-to-inventory transfer) remain the one genuinely unbound
      `Action::Use` category.
    - Not independently confirmed in an actual windowed play session
      (same caveat M12/M15's writeups already carry).

- [x] **M17 -- real quest-state tracking** (this session). Direct
      follow-up to M16: closes its own "Not attempted" note above --
      `tanyinconvo.s`-style dialogue trees can now actually progress
      across repeated visits instead of always soft-failing into their
      first-visit branch.
    - **Real semantics from the corpus, not a guess**: a research pass
      grepped every real `QuestAssigned`/`QuestSolved`/`QuestCompleted`
      call site across the whole `.s` corpus (94/68/70 getters, 57/75/70
      setters) and confirmed a simple monotonic per-quest-id 3-flag model
      (assigned -> solved -> completed, each independently settable):
      setters are overwhelmingly bare `SetQuestX(id)` (== `SetQuestX(id,
      true)`); an explicit `false` (retraction) appears only on
      `SetQuestAssigned`, e.g. `tanyinconvo.s`'s own
      `SetQuestAssigned(26, false)` when declining one dialogue branch to
      take another. `AddMonsterKilled`/`MonstersKilled(id)` confirmed as
      a separate shared per-quest kill-tally bucket, its own id
      independent of both the killed entity's `typeId` and any quest id
      (4 different monster scripts across `drgnfld/` share one counter,
      `id=101`). No counter-example to any of this found anywhere in the
      corpus. `PlayerExecutable` gained 3 `std::set<int>` + a
      `std::map<int,int>` kill-tally, real `SetQuestAssigned`/
      `SetQuestSolved`/`SetQuestCompleted`/`QuestAssigned`/`QuestSolved`/
      `QuestCompleted`/`AddMonsterKilled`/`MonstersKilled`/
      `AddExperience` handlers (`GetGold`/`SetGold`/`StatModGold` were
      already real from earlier work).
    - **A real, necessary fix alongside it**: `MenuStack::OpenMenu()`
      only ever reruns a menu's `Init()` once per path (cached
      thereafter, correct for genuinely stateful screens like inventory/
      stats/options); but a dialogue tree's quest-state branching *lives
      in* `Init()`, so a cached conversation would freeze at whatever
      line it first showed forever, quest state or not. New
      `MenuStack::ReopenMenu()` discards the cached instance first so
      `Init()` genuinely reruns; `MonsterExecutable`'s `OpenMenu` handler
      (M16) now calls this instead of plain `OpenMenu()` -- the narrower
      fix, not a change to `OpenMenu()`'s own general caching semantics
      (`MenuExecutable`'s own internal `OpenMenu()` calls, e.g. inventory
      <-> stats navigation, are unaffected).
    - **A real, separate latent bug found and fixed along the way**:
      `MonsterExecutable::InvokeOnUse()`/`InvokeOnKilled()` and
      `DoorExecutable::InvokeOnUse()` were calling their target script
      method with an empty argument list, not the placeholder-for-`(s)`
      argument every other real `Init(s)`/`OnUse(s)`/`OnKilled(s)` call
      site in this codebase already passes (main.cpp's zone-load block,
      etc.) -- latent since no reachable script body had ever actually
      read its own `s` parameter before. `azra_rat.s`'s real
      `OnKilled(s)` does, on its 8-kills branch (`if(s=false){Delay(2,
      0);}`), which the new quest-driven test below is the first thing
      in this project to actually reach -- would have thrown "Field s
      not found" without this fix. All 3 call sites now pass the same
      placeholder argument.
    - **Verified against real script data, `quest_smoke`
      (`src/tests/m17_quest_smoke.cpp`)**: walks `tanyinconvo.s`'s real
      handler chain (`Speech1`->`Speech2`->`Speech2a`->`Speech3`->
      `Accept`, real bodies) end to end and confirms a SECOND, later
      `OnUse()` conversation genuinely opens on a different real line
      (textId 2370 -> 2368) reflecting the quest state the first
      conversation wrote -- proof `ReopenMenu()` and the quest flags
      both work together, not just in isolation. Separately runs real
      `Azra_Rat.s`'s `OnKilled()` 8 times (8 real instances, one shared
      `PlayerExecutable`) and confirms the kill counter reaches 8 -- but
      explicitly asserts quest id 0 stays *unsolved*, because the real
      script's own `SetQuestSolved(0,true)` sits 3 statements after a
      `Level.GetEntity("trthgar")` call that still throws (see below);
      asserting this honestly, rather than silently assuming the whole
      branch works, matches this project's verification standard.
    - **Not attempted / found but not chased**: a `Level`/`GameEngine`-
      root global object -- confirmed (this pass) that no such thing is
      registered in the interpreter at all yet, a separate, much larger,
      pre-existing gap (`docs/SIMKIN_NATIVE_API.md`'s "Zone/Level" class;
      ~567 real scripts reference `Level.` in some form). This is why
      `azra_rat.s`'s 8-kill quest never actually completes yet even
      though its kill-count gate now works correctly -- worth a future
      milestone on its own, comparable in shape to M15/M16's `Object/
      Entity (world base)` work but for whatever `Zone/Level` itself
      exposes (`AddTrigger`, `GetEntity`, and whatever else real scripts
      call on it). `StatModGold`-adjacent reward calls, dialogue-tree
      quest ids beyond `tanyinconvo.s`/`menlinconvo.s` (not individually
      wired to anything else yet, though the underlying flag store is
      generic and already covers them), and a real experience/leveling
      curve (`AddExperience` accumulates but nothing consumes
      `GetExpToNextLevel()` to level up yet) are all separate, smaller
      loose ends.

- [x] **M18 -- the `Level`/`Zone` global object: real `GetEntity()` resolution,
      closing M17's own documented gap** (this session). Direct follow-up to
      M17's "Not attempted" note -- the `Level`/`GameEngine`-root global it
      flagged as missing turned out to be narrowly closeable, not the
      whole ~49-method surface at once.
    - **Real per-instance name field, never parsed before**: `docs/
      ZONE_FORMAT.md`'s `EntPlacement::name` (offset 0x20, 40 bytes) is the
      `.ent` record's own instance name -- distinct from entities.txt's
      per-typeId descriptor name (a script path). Confirmed against real
      `azra.ent` data: record 20 (typeId 141) decodes to name `"trthgar"`,
      exactly the identifier `monsters/azra_rat.s`'s `OnKilled()` looks up
      via `Level.GetEntity("trthgar")` on its 8th-kill branch. `Zone::
      EntPlacement` (`world/zone.h`/`.cpp`) now parses and stores it
      (empty for the overwhelming majority of placements that never set
      one).
    - **`Level` is a bare global, not a factory-returned object**:
      confirmed by `docs/SIMKIN_NATIVE_API.md`'s corpus research (Zone/
      Level `0x14d38` + Zone effects `0x14df8`, two chained facets of one
      object spliced into every script's default reachable set). New
      `sk_bindings::LevelExecutable` (`simkin_bindings/
      level_executable.h/.cpp`, `NativeStubExecutable`-derived, same
      native-only shape `PlayerExecutable` already established) is owned
      by `MenuStack` (same "shared global state" bucket as its `Player`)
      and registered as the literal global `Level` in the constructor.
    - **Deliberately narrow real-method slice** (18/41 of Zone/Level's own
      names are attested anywhere in the corpus; a full pass is
      comparable in scope to a whole further M15/M16-sized milestone, not
      attempted here): only `GetEntity(name)`, `GetPlayer()`, and
      `PlayAmbient(id,volume)` (no-op, no audio system, same precedent as
      every prior milestone) get real handlers -- picked because they're
      what this port's own already-loaded real scripts (`azra_rat.s`,
      `azra.s`) actually call. Everything else (`AddTrigger`,
      `CreateEntity`, `LoadLevel`, ...) still soft-fails.
      `RegisterEntity()`/`ClearEntities()` let the host populate/reset
      `GetEntity`'s registry -- `main.cpp`'s zone-load block registers
      every live door/monster with a non-empty real `.ent` name, and
      clears the registry before each zone (re)load so a stale pointer
      into a destroyed zone's objects can never leak out.
    - **A real, necessary companion fix**: SimKin's grammar has no `null`
      literal (checked the vendored parser directly) -- real scripts
      (`azra.s`'s `if (M1 != null)` after `M1 = Level.GetEntity("m1")`)
      reference it as an ordinary bare global that has to already exist.
      `game_constants.cpp` now registers `null` as a blank default
      `skRValue()` -- exactly what `GetEntity()` returns on a miss, so the
      comparison behaves correctly against the vendored interpreter's real
      `skRValue::operator==` (a T_Object receiver's cross-type branch
      against a T_String never matches; a T_String "not found" result
      against a T_String "null" does) -- read directly off
      `third_party/simkin/src/skRValue.cpp`, not guessed.
    - **A real generalization this unblocked**: `monsters/
      Gravel_Trothgar.s` (the entity "trthgar" actually resolves to, a
      blacksmith NPC/merchant -- `SetInvulnerable(true)`,
      `SetAggressive(false)`, `ClearProducts()`/`AddProduct(...)`,
      `OnUse()` opens `trothgarconvo`) is `entities.txt` category **7**
      (merchant), not category 2 -- so it was never loaded as a live
      object at all before this pass. `main.cpp`'s zone-load loop's
      category filter is now `category == 2 || category == 7`; the same
      `MonsterExecutable`/`HasRealScript()` machinery M16 already built
      covers it with zero new code (every one of Gravel_Trothgar.s's Init/
      OnUse calls either already has a real handler or safely soft-fails).
    - **Verified end to end against real data, `level_smoke`
      (`src/tests/m18_level_smoke.cpp`)**: confirms the real `.ent` name
      field decodes to `"trthgar"` at azra's one real typeId-141 placement
      and that its entities.txt category is genuinely 7 (structural,
      explains why the category-7 generalization was needed at all); runs
      `Gravel_Trothgar.s`'s real `Init()`, registers it, and confirms
      `Level.GetEntity("trthgar")` resolves to that exact object while
      `Level.GetEntity("m1")` (never registered) resolves to the same
      blank default as `null`; then re-runs M17's real 8-rat-kill sequence
      and confirms `SetQuestSolved(0,true)` now genuinely executes --
      `player.questSolved(0)` flips to **true** for the first time (M17's
      own `quest_smoke` test, deliberately left unmodified, still and
      correctly asserts `false` for its own isolated setup that never
      registers `"trthgar"` -- its header comment now explains why the
      *reason* changed even though the *result* didn't).
    - **Not attempted**: the rest of Zone/Level's 41 members (`AddTrigger`
      and its own further "Door/trap trigger" return-type class,
      `CreateEntity`/`CreateEntityScript`, save/load-level state, the
      `SetVis_*` visibility-raycast tuning knobs, ...) and Zone effects'
      8 members (`Vignette`, `SpawnWithinRadius`, `AddEncounters`, ...) --
      a substantial further milestone on its own, comparable in shape to
      M15/M16's "Object/Entity (world base)" work. `azra.s` itself (the
      zone-root script, as opposed to per-entity scripts) is still never
      loaded/run by this port at all -- a separate, larger gap noticed
      along the way, not attempted (its own `Init()` is where most of the
      `Level.GetEntity("m1".."m7")`/`PlayAmbient`/`SetZone` calls actually
      live).
    - Not independently confirmed in an actual windowed play session
      (same caveat every prior milestone's writeup already carries).

- [x] **M19 -- Action::Use interact binding: pickups, the last unbound
      category** (this session). Closes the "Not attempted: pickups"
      note M15/M16/M18 all repeated -- doors, NPC talk, and now world
      pickups are all real.
    - **Real placement, not reachable in `azra`**: a scoping pass found
      the currently-tested `azra` zone genuinely has zero real category-3/
      8/9 (misc loot/container/consumable) `.s`-scripted placements at
      all (only two "!label"-only category-8 stubs) -- confirming the
      earlier "chest scripts appear orphaned" finding wasn't azra-
      specific bad luck. Widened the corpus scan to every zone's `.ent`
      and found `snowline` has 20 real category-3 placements across 5
      distinct scripts (`foxglove.s`, `mountaintail.s`, `snowblossom.s`,
      `trefoilflower.s`, `yuinroot.s` -- an herb-gathering set, all
      structurally identical); `level_smoke`'s M18 work already
      established loading a non-`azra` zone by name is a supported test
      shape, reused here.
    - **Real script, `snowline/foxglove.s`** (and its 4 siblings, byte-
      for-byte identical shape): `Init(s) { SetID("herb5");
      SetName(2287); SetIcon(211); SetUseText(2288);
      SetItemDescription(2287); SetCanDrop(false); SetMPUsable(true); }
      OnUse(s) { GetPlayer().PickupItem(self); MirrorDestroyObject(self);
      }` -- every `Init()` setter already had a real `ItemExecutable`
      handler except `SetCanDrop`/`SetMPUsable` (soft-fail, same "nothing
      reads this back yet" status `CanDrop()`'s existing hardcoded-true
      comment already documents). `OnUse()` needed two new ones:
      `GetPlayer()` (bare self-receiver, same shape Door/Monster/Menu
      already have) and `MirrorDestroyObject(self)` (network/replication
      bookkeeping in the original, same `DoorOpened()`-style no-op
      precedent -- but reuses the real, already-existing
      `markedForRemoval` flag `OnUsedBy()` sets for a consumed inventory
      item, so the *removal* intent is real, not faked).
    - **`ItemExecutable` gained a `PlayerExecutable&` constructor
      parameter** (its one existing call site,
      `PlayerExecutable::LoadStartingInventory`, updated to pass `*this`)
      -- needed to resolve `GetPlayer()`, same reason `MonsterExecutable`
      gained a `MenuStack&` parameter back in M16.
    - **The real ownership-transfer problem, and how it's solved**: unlike
      a door/monster/merchant, a picked-up item's `ItemExecutable` has to
      actually move from the world (`main.cpp`'s new `gamePickups`,
      mirroring `gameDoors`/`gameMonsters`) into
      `PlayerExecutable::m_Inventory` -- but `PickupItem()`'s handler
      only runs mid-script-call, while `main.cpp` still holds the real
      `std::unique_ptr<ItemExecutable>`. Same "defer the tricky move to a
      safe point outside the live call frame" shape M10's
      `markedForRemoval()`/`PurgeRemovedItems()` already established for
      a structurally identical reason: `PlayerExecutable::PickupItem()`'s
      handler only records *which* object asked (a raw, non-owning
      pointer, `TakePendingPickupItem()`); `main.cpp`'s `Action::Use`
      handling, after `InvokeOnUse()` returns, checks
      `markedForRemoval()` and, if the recorded pointer matches this
      exact instance, moves ownership via the new
      `PlayerExecutable::AddItem()` before erasing the world entry.
    - **`Action::Use` (Key3) now picks the nearest of three lists**
      (doors, usable NPCs, pickups) instead of two -- same "nearest wins"
      rule each list already used internally, now applied across all
      three. The on-screen use-text prompt (y=34) gained the same
      three-way check. Picked-up pickups also naturally stop rendering
      the instant `gamePickups` shrinks (no separate "hide it" step
      needed -- the per-frame render list is rebuilt from `gamePickups`
      every tick, same as every other live-entity list).
    - **Verified end to end against real data, `pickup_smoke`
      (`src/tests/m19_pickup_smoke.cpp`)**: confirms the real 3 typeId-
      1008 placements and category-3 classification (structural), runs
      `foxglove.s`'s real `Init()` and checks its literal `icon`/
      `useTextId` values, then runs the real `OnUse()` and confirms
      `markedForRemoval()` is true, `TakePendingPickupItem()` returns
      exactly this item (and clears on read), and -- mirroring
      `main.cpp`'s own flow -- that `AddItem()` genuinely lands the same
      real object in `player.inventory()`.
    - **Not attempted**: the broader loot-menu/container pattern
      (category 8, `PickupItem(Item)` called from a loot-selection menu
      rather than directly from a world object's own `OnUse()` -- 126
      corpus call sites vs. this pass's 18, a materially different UI
      flow); monster-death loot-bag spawning (`SetLoot`'s params still
      just stored, unused -- needs the native RE M12 already flagged as
      not done, `FUN_1002c3a8`/`FUN_10084438`); dropping a picked-up item
      back into the world with a real position (inventory.s's `DropRow`
      already marks an item for removal on drop, but nothing spawns a new
      world placement for it).
    - Not independently confirmed in an actual windowed play session
      (same caveat every prior milestone's writeup already carries).

- [x] **M20 -- ranged weapons** (this session). Next slice of "Combat
      resolution" after M19 closed out `Action::Use` -- a real, previously-
      stored-but-unused weapon field turned out to be exactly what this
      needed.
    - **`SetRange()` was already being stored, never read back**: every
      real weapon script's `Init()` calls `SetRange(n)` (item_
      executable.cpp has handled it since M10, just to flag
      `kItemTypeWeapon`), but nothing in this port ever used the actual
      *value* for anything. A corpus-wide grep this session found exactly
      **two** distinct values across the whole real weapon corpus: **384**
      (65 melee weapons -- `weapons/club.s` etc.) and **16384** (16 real
      bow/crossbow/thrown weapons -- `weapons/bandit_longbow.s` etc.,
      flagged by `SetBow`/`SetCrossbow`/`SetThrowingWeapon(true)`) -- a
      clean, unambiguous, corpus-verified bimodal split, not a guess.
      `ItemExecutable` gained a `range()` accessor (already-stored value)
      and a `ranged()` accessor (new `m_Ranged` flag, informational --
      the actual range decision uses `range()` directly, which already
      captures the same split on its own) plus `SetBow`/`SetCrossbow`/
      `SetThrowingWeapon` handlers.
    - **No separate "fire" input exists to bind**: `docs/
      INPUT_HANDLING.md` had already found 8 unbound logical actions in
      the same resource-ID range as the real default control scheme,
      including `0xcf7` Shoot and `0xcf9` Reload -- confirmed real but
      genuinely never wired into Shadowkey's own default bindings.
      Cross-checked against script data this session: `SetClipSize`/
      `SetFireRate`/`SetReloadFrames` (ammo/rate-of-fire fields the real
      Weapon class exposes) have **zero** real call sites anywhere in the
      whole corpus -- confirming ammo/reload really is dead weight in the
      shipped game, not a gap. So the evidenced design is: a bow/crossbow
      just reuses the same `UseLeftAction`/`UseRightAction` (Key7/Key5)
      attack keys M12 already wired up, with its own real (longer) range
      -- not a new input action.
    - **New `sk_bindings::InAttackRange()`** (`simkin_bindings/combat.h`/
      `.cpp`) factors `tryAttack`'s inline nearest-in-range-and-facing-
      cone math (same shape `findNearbyDoor`/`findNearbyUsableMonster`/
      `findNearbyPickup` already used, M15/M16/M19) into a small, pure,
      testable function -- same "host-side pure helper, verifiable
      without the windowed game loop" precedent `RollDamage()` already
      set. `main.cpp`'s `tryAttack` now computes its range from the
      equipped hand's real `range()` (falling back to the existing
      `kMeleeRange` constant only for bare fists, which have no real
      script data) instead of always using the fixed melee constant --
      the real, evidenced behavior change this milestone delivers: a
      melee weapon's own real 384 now also governs its reach (previously
      every weapon, melee or not, used the port's own invented
      `kMeleeRange = 110`, which the real corpus never actually matched).
      The `facingMonster` HUD name/HP label search (main.cpp) was updated
      the same way, using whichever equipped hand reaches farthest, so
      the label appears from bow range too, not just the hit itself.
    - **Deliberately no line-of-sight/wall check** -- same "from-scratch,
      no RE ground truth, keep it simple" footing every other combat
      constant in this port already stands on (`combat.h`'s own class
      comment); a ranged shot can in principle clip a thin wall corner at
      extreme range, not attempted here.
    - **Verified against real data, `ranged_smoke`
      (`src/tests/m20_ranged_smoke.cpp`)**: loads real `club.s`/
      `bandit_longbow.s`, checks their literal `range()`/`ranged()`/
      damage values, then proves the actual payoff -- the *same* target
      geometry (500 world units directly ahead) is out of range for
      `club.s`'s real 384 but in range for `bandit_longbow.s`'s real
      16384, and the facing cone still rejects a target directly behind
      regardless of range.
    - **Not attempted**: spellcasting (a separate native class), the
      unused `SetClipSize`/`SetFireRate`/`SetReloadFrames` fields
      (confirmed genuinely dead, see above -- not a gap), and any visual/
      projectile representation of a ranged attack (this port's combat
      was already hit-scan/instant for melee, M12; a bow attack is the
      same instant-resolve, just with a longer real range, not a fired
      projectile sprite).
    - Not independently confirmed in an actual windowed play session
      (same caveat every prior milestone's writeup already carries).

- [x] **M21 -- monster-death loot-bag spawning + the loot-menu/container
      pattern** (this session). Closes two items flagged as separate,
      larger, not-yet-attempted work since M12/M19 -- turned out to be
      fully real-data-decodable with zero native decompilation needed.
    - **`SetLoot()`'s tag string is a real script path**: a real monster's
      `SetLoot(300, "Loot_ratseye", 1, 8)` (id is always literally 300,
      entities.txt's generic "!bag_loot" container typeId; the trailing
      min/max, plausibly a drop-chance roll, aren't stored -- nothing
      reads them back) -- the *tag*, lowercased, is exactly a real
      loadable script's filename (`loot_ratseye.s`, found sitting at
      `scriptRoot`'s top level; this port's filesystem is case-
      insensitive, same convention every other path lookup here already
      relies on). `MonsterExecutable` gained a real `SetLoot` handler
      (`lootTag()`) instead of soft-failing it.
    - **The real loot-bag script itself, fully decoded**:
      `loot_ratseye.s`'s `Init()` is `SetName(456); SetUseText(457);
      SetUsable(true); Item = Level.CreateEntity(704); AddObject(Item);`
      -- `704` is exactly `items/ratseye.s`'s real entities.txt typeId.
      `Level.CreateEntity(typeId)` (`LevelExecutable`, new handler)
      resolves a typeId through the same `EntityTypeTable` chain placed
      entities already use, loads a real `ItemExecutable`, runs its real
      `Init()`, and hands it back -- only item-shaped categories (3/4/5/
      6/9) resolve, matching every real loot-bag script's own usage.
      `loot_gold6-10.s` (`Item = Level.CreateEntity(52); Item.SetQuantity(
      Random(6,10)); AddObject(Item);`, typeId 52 = `gold.s`) additionally
      confirmed a real, separate, pre-existing gap: `Random(min,max)` had
      no handler anywhere in this port (every prior `Random(...)` call,
      e.g. `azra_rat.s`'s own `SetScale(Random(206,306))`, silently soft-
      failed to 0) -- new shared `TryHandleRandom()`
      (`native_binding_common.h`/`.cpp`) fixes it for both `ItemExecutable`
      and `MonsterExecutable`.
    - **`ItemExecutable` gained a real Collection** (`AddObject`/
      `GetFirst`/`GetNext`/`RemoveObject`, `m_Contents`) -- a bag's own
      `AddObject(Item)` takes real ownership of `CreateEntity`'s result
      out of `LevelExecutable`'s one-slot pending holder
      (`TakePendingCreatedEntity()`, same "defer the tricky move" shape
      `PlayerExecutable::TakePendingPickupItem()` already established,
      M19); `RemoveObject()` (called from `lootmenu.s`'s real
      `SelectItem()`, right after `PickupItem()`) completes the transfer
      into the player's real inventory synchronously -- unlike M19's
      world-pickup flow, a loot-menu selection's whole round trip happens
      within one ordinary script-to-script call chain, so there's no
      live-call-frame ownership problem to defer past.
    - **The real `lootmenu.s`, fully wired**: `GetOpener()` (new
      `MenuExecutable`/`MenuStack` concept -- `OpenMenu()`/`ReopenMenu()`
      gained an `opener` parameter, set *before* `Init()` runs so
      `UpdateMenu()`'s own `GetOpener().GetFirst()` call, made from
      inside `Init()` itself, resolves correctly -- an ordering bug this
      session's own test caught and fixed) and `AddMenuItem`'s real 3-arg
      form (`AddMenuItem(text, callback, associatedObject)`, `MenuRow`
      gained `associatedObject`, `MenuItemHandle::GetAssociatedObject()`
      reads it back) are both new, real, general-purpose `MenuExecutable`
      capabilities, not loot-bag-specific hacks.
    - **A real, separate latent bug found and fixed**: `ItemExecutable`
      (like `DoorExecutable`/`MonsterExecutable`) inherits
      `skTreeNodeObject::strValue()`, which defaults to the TreeNode's own
      *empty* root data for an ordinary loaded script -- meaning a real,
      live object's `strValue()` was often `""`, exactly as "equal" to
      the blank `null` global (M18) as a genuine miss, via `skRValue::
      operator==`'s T_Object-vs-T_String cross-type branch. Caught for
      real by this milestone's own test: `lootmenu.s`'s real `if
      (Opener.GetFirst() = null)` wrongly took its "empty bag" branch for
      a bag that actually held one real item. Considered (and rejected)
      making `null` itself a real singleton object instead -- that would
      have also made *calling a method on* a genuinely-missing result
      silently soft-fail instead of throwing (the vendored interpreter's
      `makeMethodCall` only proceeds for `T_Object`), which would have
      let `azra_rat.s`'s `Trthgar.SetPositionMirror(...)` silently
      succeed even with `"trthgar"` never registered, wrongly completing
      that quest and regressing `quest_smoke`'s own correct assertion.
      Fixed at the actual source instead: `ItemExecutable`/
      `DoorExecutable`/`MonsterExecutable` all now override `strValue()`
      to a guaranteed-non-blank constant, leaving `null`'s own blank-
      string design (and its real "comparison-safe, method-call-still-
      throws" property) untouched. `quest_smoke` re-verified unchanged
      after the fix (still correctly `false`).
    - **Verified end to end against real data, `loot_smoke`
      (`src/tests/m21_loot_smoke.cpp`)**: real `Azra_Rat.s` lootTag,
      real `loot_ratseye.s`/`loot_gold6-10.s` `Init()` (`Level.
      CreateEntity`+`AddObject`+real `Random()`-driven `SetQuantity`),
      the real `OnUse()` -> real `lootmenu.s` chain (`GetOpener`/
      `GetFirst`/`GetNext`/`AddMenuItem`'s associated-object form all
      exercised for real), and selecting that real row via `menu->
      method("SetSelectedItem",...)` + `ActivateSelected()` (the same
      dispatch `main.cpp`'s own input handling uses) genuinely landing
      the real `ItemExecutable` in the player's inventory and emptying
      the bag.
    - **`main.cpp` wiring**: a monster's death now calls a new
      `spawnLoot()` (resolves `lootTag()`, loads the bag the same way,
      drops it into `gamePickups` at the death position -- reusing M19's
      mechanism completely). `Action::Use`'s pickup branch now tells
      apart two real `OnUse()` shapes sharing one list: a direct item
      (`markedForRemoval()` true, moves into inventory, same as M19) vs.
      a loot bag (opens a real menu instead, detected the same before/
      after `currentMenu()` way the NPC branch already does, and stays in
      the world). `ItemExecutable`'s constructor was refactored from
      separate `(strings, PlayerExecutable&)` parameters to one
      `MenuStack&` (mirroring `MonsterExecutable`'s own precedent) --
      needed access to both `player()` and the new `level()`.
      `PlayerExecutable::LoadStartingInventory()` and every
      `ItemExecutable` construction site updated to match.
    - **Not attempted**: the loot bag has no real model
      (`entities.txt`'s typeId 300 is itself the "!bag_loot" label-only
      convention) -- renders as nothing (`ZoneRenderer` already skips an
      unresolved model index, M8), findable only via the real use-text
      prompt/`Action::Use`, not a visible 3D object; the empty-bag
      cleanup path (`QuitAndDestroyOpener()`/`QueryDestroy()`, real but
      soft-failed) -- a fully-looted bag stays in the world rather than
      despawning; `SetLoot`'s own trailing min/max args (plausibly a
      drop-chance roll, no further evidence chased).
    - Not independently confirmed in an actual windowed play session
      (same caveat every prior milestone's writeup already carries).

- [x] **M22 -- spellcasting** (this session). The last item in "Combat
      resolution"'s original scoping list.
    - **Real spell scripts are Item-shaped**, same `ItemExecutable`
      class every weapon/armor/consumable already uses -- `spells/
      blind.s`'s `Init()` is `SetUseText(2401); SetName(2401);
      SetRating(3); SetItemDescription(2402); RestrictUse(2,4,6,7);
      SetIcon(209); SetCost(1600); SetMarketValue(560); if (GetPlayer().
      IsItemEnabledFor(self)=true) SetUseText(2403); else SetUseText(
      2404);`, `OnUse(s) { GetPlayer().EquipItem(0,self); }`, `HitTarget(
      target) { DoAttackRoll(target,3); }` -- no new native class needed,
      just new handlers on the existing one.
    - **`SetRating()` looked like an obvious spell-category signal but
      isn't one**: a corpus check found `misc/ring_of_fangs.s` (a real
      *armor*-category ring) also calls it, *after* its own
      `SetArmorValue`/`SetArmorType` -- would have silently
      misclassified it back to "spell" if `SetRating` set `itemType()`
      the way every other category setter does. Left `itemType()`
      alone; `rating()` is a plain host-side-only accessor instead
      (confirmed never read back by any real script either -- no
      `GetRating()` call anywhere in the corpus).
    - **So "is this a spell" isn't classified at load time at all** --
      `main.cpp`'s `tryAttack` just treats *any* equipped item that
      isn't `kItemTypeWeapon` as a cast attempt (`ItemExecutable::
      InvokeHitTarget()`, mirroring `InvokeOnUse()`/`InvokeOnKilled()`'s
      existing host-triggered-call shape). A real spell's own
      `HitTarget` handler fires for real; anything else (a misc item, or
      nothing equipped in the traditional sense) has no `HitTarget`
      defined, so it harmlessly no-ops through `skScriptedExecutable::
      method()`'s own "no such handler" fallback -- same tolerance
      `MenuExecutable::TryInvoke()` already relies on. Verified directly
      (`spell_smoke`'s Part 5): calling `InvokeHitTarget()` on real
      `weapons/club.s` does nothing.
    - **`PlayerExecutable::EquipItem(hand, item)`** -- a real, direct
      hand assignment distinct from `UpdateEquipStatus()`'s empty-hand-
      first auto-fill (`inventory.s`'s own equip-toggle flow); every
      real spell's `OnUse()` calls it with hand `0`. Hand 0 -> left hand,
      else -> right -- which physical hand `0` really is isn't confirmed
      by any script reading it back, so this is a documented, arbitrary-
      but-consistent choice (same footing as `game_constants.h`'s AR_*/
      WR_* placeholders), picked so casting maps to `UseLeftAction`.
      Also closes a separate real gap: 35 corpus call sites (quest key
      items, `dhstartupmenu.s`'s debug menu, ...) call `EquipItem`, not
      just spells -- all previously soft-failed.
    - **`PlayerExecutable::IsItemEnabledFor(item)`** -- gates a class/
      race restriction (`RestrictUse()`'s own argument list) this port
      never modeled (M5's character creation only covers race/portrait/
      name, no class system at all) -- always returns `true`, the more
      permissive default, same spirit as `CanDrop()`'s own "every real
      item can be dropped" simplification. `RestrictUse()` itself is
      accepted as a no-op (100+ real call sites, never read back by any
      script) rather than soft-fail-logged.
    - **`ItemExecutable::DoAttackRoll(target, effectId)`** -- called bare
      from within a real spell's own `HitTarget()` (self-receiver, same
      convention every other native call in this codebase already
      follows). Only applies damage, via new `sk_bindings::
      RollSpellDamage(rating, targetMagicResistance)` (`combat.h`/`.cpp`,
      `max(1, rating*3 - magicResistance)`) -- deliberately from-scratch,
      same "no RE ground truth for the real formula" footing as
      `RollDamage()`. The real `effectId` argument (`blind.s`'s own `3`,
      distinct per spell across the corpus -- poison/paralyze/blind/
      fear/drain/...) selects a real status-effect system this port
      doesn't model at all -- a separate, much larger, mostly-native
      subsystem, explicitly not attempted. Closes another real "stored,
      never read back" gap along the way: `MonsterExecutable::
      SetMagicResistance()` (stored since M12) finally has a real
      accessor and consumer.
    - **No dedicated "cast" input** -- casting reuses `UseLeftAction`/
      `UseRightAction` (Key7/Key5), same reasoning M20 already
      established for ranged weapons: the real default control scheme
      has no separate cast/fire action either. `kSpellRange` (300 world
      units) is an invented, documented constant -- no real spell script
      ever calls `SetRange()` (only weapon scripts do, M20's own
      corpus-verified 384/16384 split), so there's no real value to
      read; same footing as `render3d/camera.h`'s `kEyeHeightOffset`.
      The `facingMonster` HUD name/HP label search was updated the same
      way (a factored-out `handRange()` lambda) so a cast-ready target
      shows its label from spell range too, not just the hit itself.
    - **Verified end to end against real data, `spell_smoke`
      (`src/tests/m22_spell_smoke.cpp`)**: real `spells/blind.s`'s
      literal `rating()`; its real `OnUse()` genuinely equipping into
      `player.leftItem()`; real `Azra_Rat.s`'s `magicResistance()`; the
      real payoff -- `HitTarget()` -> `DoAttackRoll()` applying exactly
      `RollSpellDamage(3,3)` damage to a real monster; and the
      robustness check above. (`blaze.s`, a sibling spell with real
      `Init()`/`HitTarget()` but genuinely no `OnUse()` at all -- it's
      equipped directly by a debug menu script instead -- was found and
      ruled out for this test along the way, not a bug.)
    - **Not attempted**: the real status-effect system (`DoAttackRoll`'s
      `effectId` argument, poison/paralyze/blind/fear/drain/... --
      native, no scripted table found anywhere to decode it from,
      unlike M21's loot tags); spell-specific UI (a spell "quick-cast"
      list, `actionqueue.s`'s own already-confirmed-non-functional
      screen); `PlayerExecutable`'s `spellToHit()`/`spellResistance()`
      stats (M10, still unread by anything, including this milestone's
      own `RollSpellDamage()` -- deliberately kept simple, same "no RE
      ground truth" reasoning as `RollDamage()`'s own hit-chance-only
      use of `attack`/`defense`); and casting at a target beyond melee
      range through a wall (no line-of-sight check, same `InAttackRange()`
      simplification M20 already documented).
    - Not independently confirmed in an actual windowed play session
      (same caveat every prior milestone's writeup already carries).

- [x] **M23 -- zone-root `<zone>.s` script loading** (this session). Closes
      M18's own "Not attempted" note -- `azra.s` itself (as opposed to
      every per-entity script this port already runs) had never been
      loaded at all.
    - **Confirmed real, not dead code**: `azra.s`'s `Init()` references
      `Level.GetEntity("m1")`..`("m20")`, `("trinket")`, `("birg")`,
      `("skelos")`, `("azra")`, `("vil1")`..`("vil4")`, `("heather")`,
      `("tanyin")` -- dumping every one of azra.ent's 282 real placements'
      own 40-byte name field (same field M18 first decoded) found every
      single one of these is a real, named placement, not a stale/
      leftover reference. `"m1"` resolves to typeId 106
      (`monsters/Bandit_Brawler.s`), `"m2"` to typeId 202 (`Azra_Rat.s`,
      already loaded throughout this session) -- both ordinary category-2
      monster scripts.
    - **New `sk_bindings::ZoneScriptExecutable`** (`simkin_bindings/
      zone_script_executable.h`/`.cpp`) -- same `skScriptedExecutable`-
      backed shape every other class in this port uses, holding a
      `MenuStack&`. Real handlers: `GetPlayer()`, `GetEntity(name)`
      (`azra.s` calls this *both* bare and `Level.`-qualified in the same
      script -- both delegate to the same `LevelExecutable` registry),
      `isObject(x)` (a plain "is this a real found object, not `null`"
      check -- `x.type() == T_Object`), and `SetZone(a,b)` (a real bare-
      reachable "Zone effects" member per `docs/SIMKIN_NATIVE_API.md`'s
      corpus research -- stored only, real meaning/consumer unconfirmed,
      no getter anywhere reads it back). `main.cpp` loads and runs
      `<zoneName>.s`'s real `Init()` once, right after that zone's doors/
      monsters/pickups are loaded and registered into `Level` -- so its
      many `GetEntity(...)` calls can actually resolve to the real
      objects they reference.
    - **A real, necessary, broadly-applicable fix along the way**:
      `azra.s`'s `Init()` reads/writes dozens of arbitrary `GetPlayer().
      saved_X` flags (`saved_EndGame`, `saved_Birgidda`, `saved_Skelos`,
      `saved_Rescue1`-`4`, `saved_Heather`, `saved_SkelosDead`, ...) as
      ad-hoc storage, never through a declared native method. Every other
      `skScriptedExecutable`-backed class (Item/Door/Monster/Menu) gets
      this for free (real TreeNode-backed field storage); `Player
      Executable` doesn't (a native-only singleton, `NativeStubExecutable`
      -derived), so every one of these would have thrown "Field ... not
      found" without a fix. New `PlayerExecutable::setValue()`/
      `getValue()` overrides add a generic scalar-field fallback
      (`m_Fields`), with a never-written field reading back as a benign
      `skRValue(0)` rather than throwing -- matching every real
      `if (GetPlayer().saved_X = 1)` check's implied assumption that an
      unset save flag is simply falsy. Almost certainly needed by other
      not-yet-loaded scripts too, not just `azra.s`.
    - **`MonsterExecutable` gained `DestroyObjectMirror(self)`** (real,
      called as `M1.DestroyObjectMirror(M1)`) -- a silent world removal
      distinct from combat death (no `OnKilled()`/loot spawn), new
      `destroyed()` flag checked everywhere `alive()` already gates AI/
      targeting/rendering (`main.cpp`, 5 call sites updated).
    - **Verified end to end against real data, `zonescript_smoke`
      (`src/tests/m23_zonescript_smoke.cpp`)**: confirms `"m1"`/`"m2"`'s
      real typeIds structurally, then runs `azra.s`'s real `Init()` on a
      completely fresh player -- every one of its dozens of real
      `saved_X`/`QuestX` checks correctly false-by-default, so the whole
      thing runs to completion without throwing despite the sheer number
      of real native calls, only its own top-level `saved_SetZone[0]`
      guard actually firing (`SetZone(1,2000)`, its own real literal
      arguments) -- then sets `saved_EndGame=1` (via the new field
      fallback, the same real mechanism an actual script assignment would
      use) and reruns `Init()`, confirming the real `DestroyObjectMirror`
      calls this time genuinely reach and mark the real `"m1"`/`"m2"`
      objects `destroyed()`.
    - **Not attempted**: `EnterZone(s)` (trigger-volume-driven -- `"Skelos
      _Dead"`/`"YouSure"`/zone-transition triggers like `"ghasts"` ->
      `Level.LoadLevel(...)` -- no real trigger-volume data source traced
      yet: is it `.ent`-based, `.zcp`-cell-based, or something else
      entirely, a separate investigation); `SummonMe()`/`SummonMe2()`
      (called on entities in branches that never fire on a fresh game --
      `saved_Birgidda`/`saved_Skelos`/`saved_RescueN`/`saved_Heather`/
      `QuestCompleted(24)` all default false/unset -- plausibly the
      inverse of `DestroyObjectMirror`, real semantics not chased);
      `CountInventory(tag)` (soft-fails to 0, harmless for `azra.s`'s own
      `= 7` check, but not a real implementation); `SetZone`'s own real
      meaning/consumer.
    - Not independently confirmed in an actual windowed play session
      (same caveat every prior milestone's writeup already carries).

- [x] **M24 -- `AddTrigger` kill-count callbacks** (this session). A first,
      narrow slice of `Level`'s "Door/trap trigger" class (0x14ccc, docs/
      SIMKIN_NATIVE_API.md) -- the return value of `AddTrigger(name)`,
      called from a real zone-root script (M23).
    - **Confirmed real: two genuinely distinct usage shapes share one
      factory.** A corpus-wide read of every real `AddTrigger` call site
      (`broken1.s`, `crypt1.s`, `dstar_e.s`, `erthcave.s`, `ghstpass.s`,
      `lothcav.s`) found a physical, position-based trap (`spikeTrap.
      AddEntity(1013); spikeTrap.SetTrap(8,16,10); spikeTrap.
      RemainActive();`) and a zone-scoped *kill-count* trigger
      (`zombieTrigger.SetEntityID(104); zombieTrigger.SetLimit(2);
      zombieTrigger.SetCallback("GPZombiesKilled");`, `ghstpass.s`/
      `lothcav.s`) -- syntactically non-overlapping method sets on the
      same returned object, easy to tell apart and implement separately.
      Only the kill-count variant is implemented here -- the physical
      variant needs real trigger-volume/position data this port doesn't
      have (the same gap blocking `EnterZone`'s own trigger tags, M23's
      "Not attempted" note) -- while the kill-count variant is purely
      data-driven and composes directly on already-real infrastructure
      (M12's combat loop, M17's quest state, M23's zone-root scripts).
    - **New `sk_bindings::TriggerExecutable`** (`zone_script_executable.h`
      /`.cpp`) -- a real, owned-by-`ZoneScriptExecutable` native handle
      (`AddTrigger`'s real return value), answering `SetEntityID`/
      `SetLimit`/`SetCallback` for real; `RemainActive()` is real but
      inert (this port doesn't reproduce the fire-once-vs-stays-armed
      distinction, only relevant to the unimplemented physical-trap
      variant). `ZoneScriptExecutable::NotifyKilled(typeId)` -- called by
      `main.cpp` on every real monster death (`MonsterInstance` gained
      its own `typeId` field, the real entities.txt typeId that
      placement resolved from) -- matches against every registered
      trigger's real `entityId()` and, the one time a match's running
      count first reaches its real `limit()`, invokes the real script
      callback (same host-triggered-call convention -- placeholder
      `(s)` arg, catch+log exceptions -- every other `Invoke*()` in this
      codebase already establishes).
    - **A real, separate problem found and fixed along the way**:
      `ghstpass.s`'s own `Init()` calls `AddEncounters(...)`
      *unconditionally* (an "Encounter spawner" class, not attempted
      here) and immediately chains 5 real `.AddRandomSets(...)` calls on
      the result, no `if`-guard. Soft-failing `AddEncounters` to a plain
      int (this port's usual "not implemented" default) would have made
      every one of those chained calls throw "Cannot call Method ... on
      a non object" (the vendored interpreter's own `makeMethodCall()`
      only proceeds for a real `T_Object`) -- aborting the *rest of
      `Init()`*, not just the one unimplemented call, which would have
      silently broken `zombieTrigger`'s own setup too if it came later in
      script order. New `sk_bindings::InertHandleExecutable`
      (`native_binding_common.h`, general-purpose, reusable) returns a
      real object whose every method individually soft-fails instead,
      letting the rest of a real script's `Init()` run to completion --
      the general fix for "a soft-failed factory call's result gets
      chained further," not just this one call site.
    - **Verified end to end against real data, `trigger_smoke`
      (`src/tests/m24_trigger_smoke.cpp`)**: confirms typeId 104's real
      entities.txt entry, runs `ghstpass.s`'s real `Init()` (confirming
      the `AddEncounters` fix -- all 10 real chained `AddRandomSets()`
      calls soft-fail cleanly instead of crashing), then calls
      `NotifyKilled(104)` twice (matching the real `SetLimit(2)`) and
      confirms `player.questSolved(14)` flips from `false` to `true`
      only on the second call -- the real `GPZombiesKilled()` callback
      genuinely ran and called the real `GetPlayer().SetQuestSolved(14)`
      -- plus a robustness check that a third/unrelated-typeId kill
      neither double-fires nor crashes.
    - **Not attempted**: the physical-trap variant (`AddEntity`/
      `SetTrap`/`SetDoor`/`OpenDoor`/`ShowDamageMessage`/`IsActive`, all
      soft-failed) -- needs real trigger-volume/position data; the
      "Encounter spawner" class itself (`AddEncounters`/`AddRandomSets`,
      random monster-group spawning) -- a separate, real feature, not
      just an inert stand-in; and `Level.Log(...)` (a plain debug-log
      call, soft-fails harmlessly, no gameplay effect either way).
    - Not independently confirmed in an actual windowed play session
      (same caveat every prior milestone's writeup already carries).

- [x] **M25 -- menu equip flow, real `charactermanager.s` layout, and the
      first-person weapon viewmodel** (this session). User-directed: "the
      menu equipping flow complete... show the actual sprite complete with
      all its animations," plus a reference screenshot for the in-game
      character-manager screen's look.
    - **Equip flow: already correct, just untested.** Added weapon-equip
      coverage to `inventory_smoke` (`weapons/club.s`, mirroring the
      existing armor-equip check) -- confirms `UpdateEquipStatus()` fills
      an empty hand, `GetAttack()` picks up the real weapon's damage
      (10 -> 13), and `equipped()` flips true. No code bug found; this
      exact path is what `main.cpp`'s live melee combat already depends on
      every tick, just never independently exercised before.
    - **Real `charactermanager.s` absolute layout.** Read the real script
      in full and extracted its exact coordinates (portrait at (109,0);
      health/magicka/fatigue/class/level/gold text at x=8, y=10..70;
      name button (10,70..); Stats/Equip buttons at (10,100)/(90,100),
      80x23, real `ShowBorder(true)`; left/right hand `AddItemButton`s at
      (10,123)/(90,123), 80x60; Quest button at (68,178), 54x13,
      `ShowBorder(true)`). `MenuRow` (`menu_executable.h`) gained real
      `x/y/w/h/showBorder` fields, populated by `AddFloatingSprite`/
      `AddButton`/`AddFloatingText`/`AddItemButton`'s real position/size
      arguments (previously stored nowhere or hardcoded). `x < 0` means
      "no real position was ever given" -- every `AddMenuItem`-based
      screen (mainmenu.s etc.) never sets it, so `main.cpp`'s `RenderMenu`
      falls back to its old centered-vertical-list rendering completely
      unchanged for those; a screen that *does* set real coordinates
      (charactermanager.s) now draws every row at its own real absolute
      position instead, with `DrawRectOutline` for a real `ShowBorder(true)`
      button. Verified structurally against every one of the coordinates
      above via a new `inventory_smoke` block that opens `charactermanager`
      and asserts each row landed at its real position -- confirmed by a
      real build+run, not just compiled.
    - **Portrait: a reference screenshot's art was ruled inauthentic, by
      measurement, not by eye.** The user's own reference screenshot's
      portrait was downscaled to the real N-Gage's exact 176x208 resolution
      (`ffmpeg`, nearest-neighbor for viewing) and still reads as smooth-
      shaded/photographic even at native resolution -- not 2004 N-Gage
      pixel art, unlike every other confirmed real asset in this port. User
      decision: keep the screenshot's real *layout*, but render this port's
      own already-RE'd real portrait sprite (`global.spr` slot 45, 64x64,
      a real elf face) instead of the screenshot's own image.
      `chooseportraitmenu.s`'s real `GetMalePortrait(race)`/
      `GetFemalePortrait(race)` (previously unimplemented natives) both
      return slot 45 for now -- real per-race/sex art almost certainly
      exists in `global.spr`'s unidentified range, but only this one slot
      is actually pinned down, a documented simplification rather than an
      invented mapping. **Corrected below, see "Post-M25 fix" -- this
      single-shared-slot design turned out to be wrong.**
      `charactermanager.s`'s own `portrait.SetSprite(
      GetPlayer().GetPortraitID())` now draws for real via
      `FloatingSpriteExecutable::spriteId()`.
    - **First-person weapon viewmodel: fresh RE work, per explicit user
      choice, done first.** Decompiled the real per-tick draw function
      (`FUN_1002b1b0`, full decompile) -- sole callee of `FUN_10029cb0`
      (the same `ScreenModeController` tick hook `RenderHud`'s own comment
      already documents), gated on a real "active weapon" `Item*` at
      `engine+0x618+0x204`. Recovered the real `WeaponViewState` struct
      shape at that offset (`+0x230` alternate/"swing" sprite slot,
      `+0x234` interpolation accumulator, `+0x238` post-swing hold
      countdown, `+0x244`/`+0x248` idle-sway phase+gate) and all three real
      branches (idle sway via a fixed-point trig table, post-swing hold,
      active swing interpolating the equipped `Item`'s own `+0x180` frame
      count / `+0x184` 8.8-fixed scale) -- all converge on the same
      `Blit_RLESprite` primitive the compass/vitals HUD already use.
      **One real gap, left honestly unresolved**: the exact native call
      that *starts* a swing (writes `+0x230`/`+0x234`/`+0x238`) wasn't
      found despite tracing every write site reachable from the Weapon
      class dispatcher -- same footing as `RenderHud`'s own still-open
      `FUN_1002c010` mystery. Implemented as a port-only recreation of the
      real state machine's *shape* (`sk_bindings::WeaponViewmodel`,
      `simkin_bindings/weapon_viewmodel.h`/`.cpp`, factored out of
      `main.cpp` the same way `combat.h` was, so it's unit-testable without
      the windowed loop): `StartWeaponSwing()` triggers on the same
      `UseLeftAction`/`UseRightAction` keypress that already drives
      melee/ranged/spell resolution (the best-evidenced substitute for the
      real trigger, not a decompiled fact) and is a no-op for an item with
      no real `weaponSprite()` (bare fists, a spell -- `SetWeaponSprite` is
      only ever called by a real weapon script). `ItemExecutable` gained
      `weaponSprite()`/`animationFrames()` accessors reading back a real
      weapon script's own `SetWeaponSprite()`/`SetAnimationFrames()`
      (stored since M10, never read back until now). `main.cpp`'s
      `RenderWeaponViewmodel` draws the resulting sprite/frame bottom-right
      of the 3D view (a documented, non-decompiled screen position); frame/
      hold tick counts and the idle-sway curve are likewise this port's own
      choices, not recovered constants. A dropped/consumed weapon mid-swing
      is guarded the same way `PlayerExecutable::PurgeRemovedItems()`
      already guards `m_LeftItem`/`m_RightItem` against the same
      use-after-free shape.
    - **Verified end to end against real data, new `weapon_viewmodel_smoke`
      (`src/tests/m25_weapon_viewmodel_smoke.cpp`)**: confirms `club.s`'s
      real `weaponSprite()`/`animationFrames()` (88/5) and
      `bandit_longbow.s`'s (175/4), confirms a real spell
      (`spells/blind.s`) never sets a `weaponSprite()` and that
      `StartWeaponSwing()` correctly no-ops for it, then runs a real
      `club.s` swing through every phase of the state machine (Swinging ->
      Hold -> Idle) and checks it lands back exactly where expected.
    - **Not independently confirmed in an actual windowed play session**
      (same caveat every prior milestone's writeup already carries) -- the
      character-manager layout and weapon viewmodel should be visually
      checked by running `shadowkey_port.exe`.

- [x] **Post-M25 fix -- one shared portrait slot was wrong; the game has
      a real, distinct 64x64 portrait per race and sex** (this session).
      User correction, with a real reference (en.uesp.net/wiki/
      Shadowkey:Races, listing all 8 playable races/both sexes): M25's
      `GetMalePortrait`/`GetFemalePortrait` stand-in returning `global.spr`
      slot 45 for every race/sex was flatly wrong -- the real game has a
      distinct face per combination, not one shared one.
    - **Real race indices, confirmed from script data**:
      `menus/chooseracemenu.s`'s combo box (`AddOption`) fixes the order
      `GetPlayer().ChooseRace(raceId)` stores and `GetRace()` returns:
      0=Argonian, 1=Breton, 2=Dark Elf, 3=High Elf, 4=Khajiit, 5=Nord,
      6=Redguard, 7=Wood Elf -- exactly UESP's own 8-race list.
    - **Found the real portrait table by rescanning the whole 384-slot
      `global.spr`, not guessing.** Every 64x64 slot (the size slot 45 was
      already confirmed at) across the full archive: exactly 16 in one
      contiguous run, 31-46, plus one outlier (28, also 64x64 but visually
      a stray reptilian face that doesn't fit the pattern below --
      unidentified, unused). Rendered all 17 to real images and visually
      matched every one of the 16 against UESP's own race gallery, in
      exact `chooseracemenu.s` race order, alternating male then female:
      `31/32`=Argonian M/F, `33/34`=Breton M/F, `35/36`=Dark Elf M/F,
      `37/38`=High Elf M/F, `39/40`=Khajiit M/F, `41/42`=Nord M/F,
      `43/44`=Redguard M/F, `45/46`=Wood Elf M/F -- a clean, unambiguous
      one-for-one visual match on every race's real distinguishing
      features (Dark Elf's grey skin/red eyes/fangs, Khajiit's feline
      face, Redguard's dark skin, Nord's pale/blonde look, etc.). Slot 45
      (M25's original hardcoded guess) turns out to be *specifically*
      Wood Elf Male, not a generic elf.
    - **Fix**: `MenuExecutable::method()`'s `GetMalePortrait(race)`/
      `GetFemalePortrait(race)` handler now computes
      `31 + race*2 + (female ? 1 : 0)` instead of returning a constant
      (`menu_executable.cpp`), clamping an out-of-table `race` to 0
      (`GetRace()`'s own real default). No other code needed to change --
      `chooseportraitmenu.s`'s real `male.SetSprite(maleId)`/
      `female.SetSprite(femaleId)` and `charactermanager.s`'s real
      `portrait.SetSprite(GetPlayer().GetPortraitID())` already read
      whatever id these two natives return.
    - Not independently confirmed in an actual windowed play session --
      the portrait picker and in-game character manager should be checked
      for all 8 races/both sexes.

- [x] **M26 -- the real zone-transition loading screen, and `Level.
      LoadLevel(...)`** (this session). User correction, with a real
      screenshot ("Travel to: Azra's Crossing" on the Shadowkey key-logo
      splash): the open "`FUN_1002c010`'s big single vitals bar" mystery
      (docs/PORT_ROADMAP.md's "Next milestones", previously guessed as an
      enemy lock-on/target health bar) is actually a real loading-progress
      bar shown during zone/level travel, not a combat HUD element.
    - **Fresh RE, dispatched to a research fork**: `FUN_1002c010`
      (`ScreenModeController`'s own secondary-vtable slot `+0x44`) is
      called from *inside* `FUN_10029cb0` itself (the same per-tick
      dispatcher `RenderHud`'s own comment already documents) via a
      self-vtable dispatch -- not a separate function, which is why the
      exhaustive literal-address caller search that found only the static
      vtable-table reference (Post-M14 fix entry above) never found a
      caller. Gated on `ScreenModeController`'s own mode field
      (`this+0x78`) equal to 3, 4, 10, or 0x1f (31); states 3 and 10
      additionally draw the "Travel to: `<zone>`" banner. **The bar's fill
      is a real, live progress readout, not decorative**: traced to
      `GameEngine_InitLevel`'s own progress counter, reset to 0 right as
      it spawns the real zone load on a background `RThread` and written
      through a real percentage sequence (0, 3, 5, 10, 12, 14, 22, 30, 40,
      45, 50, 55, 58, 60, 65, 70, 75, 85, 87, 90, 95, 98, 100) as loading
      genuinely proceeds through the real documented stages (docs/
      ZONE_FORMAT.md's debug-marker names). The "Travel to: `<zone>`" text
      itself: a real localized prefix string concatenated with a
      per-zone display name from a formatting function not fully traced.
    - **Found the splash art and the exact banner text myself, closing the
      fork's two open gaps**: rescanned every full-screen (176x208)
      `global.spr` slot in `menu_sprites.txt` and rendered the candidates
      -- slot 174 is visually an exact match for the user's screenshot
      (the glowing key / "The Elder Scrolls Travels SHADOWKEY" logo art;
      slots 245-247 turned out to be the publisher boot splashes --
      Bethesda Softworks/Vir2L Studios/TKO Software -- not this screen).
      Grepped `stringtable.eng` for the real `"Travel to: "` prefix
      (id 3950, confirmed verbatim) and found it sits right before a real
      contiguous run at 3821-3840: exactly one display name per real zone
      (`azra.zmp` etc., 21 real zones total), in a fixed order (id 3820
      itself is `"Loading..."`, used here for the very first zone entered
      a session before any previous zone exists to "travel from").
      `ffarena` is the one real zone with no entry in this run -- left
      unmapped, falls back to its own raw internal name rather than a
      guessed slot.
    - **New `assets/zone_display_names.h`/`.cpp`**: the real 20-zone
      lookup table above, factored out of `main.cpp` (which turns an id
      into real text via `sk::StringTable::Get()`) so the table itself is
      unit-testable without the windowed game loop -- same precedent
      `simkin_bindings/combat.h`/`weapon_viewmodel.h` already established.
    - **New real `Level.LoadLevel(name)` handler** (`level_executable.cpp`,
      previously soft-failed) -- a real script's own mid-game zone-
      transition request (e.g. `cheatmenu.s`'s real
      `Level.LoadLevel("azra")`). Routes to new `MenuStack::
      RequestZoneChange(name)`, which sets the exact same two fields
      `RequestGameStart()` already uses for the very first zone (so
      `main.cpp`'s existing zone-load block needs no duplicate logic) but
      deliberately skips `RequestGameStart()`'s one-time starting-
      inventory grant -- a mid-game transition must not touch the
      player's already-in-progress inventory.
    - **`main.cpp`'s new loading-screen state machine**: latches the
      instant `stack.gameStartRequested()` fires (initial start or a real
      `LoadLevel`), then renders a fixed run of frames -- the real key-
      logo splash (slot 174), the real dragon-wing bar frame (slot 206,
      94x42, at (40,166)) and red-gradient fill (slot 205, 79x9, at
      (44,182), width-clipped through the real percentage sequence
      above), and the real banner text (`"Loading..."` for the very first
      zone this session, `"Travel to: <zone>"` via `ZoneDisplayName()`
      for every later transition) -- before the actual (synchronous,
      already-existing) zone-load work runs. **Port-only, not decompiled**:
      since this port's zone loading is synchronous (no real background
      thread), there's no live progress to sample -- the percentage steps
      through the real stage list on a fixed per-tick schedule purely for
      visual continuity with the real screen, not a measurement of actual
      work done; which of the 4 real trigger states maps to which
      scenario, and the exact "Travel to" text's screen position, weren't
      determined -- see "Next milestones" below.
    - **Verified end to end against real data, new `loading_screen_smoke`
      (`src/tests/m26_loading_screen_smoke.cpp`)**: confirms the real
      zone-name table (including two mixed-case real internal names,
      `cheatmenu.s`'s own `"GhstPass"`/`"LothCav"`) against the real
      localized text, confirms `ffarena` correctly falls outside the real
      table, confirms the real `"Travel to: "` prefix string, and confirms
      a real `Level.LoadLevel(...)` call genuinely sets
      `gameStartRequested()`/`requestedZone()` without re-granting
      starting inventory.
    - Not independently confirmed in an actual windowed play session --
      the loading screen's look/timing and a real cheat-menu-triggered
      zone transition should be checked live.

- [x] **M27 -- real audio** (this session). User-directed: "let's continue
      with the audio, which hasn't been addressed or RE'd at all." Full
      writeup: `docs/AUDIO_FORMAT.md`.
    - **The data itself needed no RE at all** -- every real sound file
      under `system/apps/6r51/` turned out to be a standard, off-the-shelf
      format: 38 ordinary uncompressed-PCM `.wav` files (mono, 8000Hz,
      16-bit) and 6 genuine Ogg Vorbis `.ogg` files (confirmed by real
      file headers, not guessed). The real RE-worthy question -- what does
      a script's `PlaySound(id)`/`Level.PlayAmbient(id,volume)` `id`
      argument actually mean? -- was answered entirely by real corpus
      cross-reference, no Ghidra needed: `id` is the currently-loaded
      zone's own real `<zone>_sounds.txt` manifest slot index (same
      numbered-slot-plus-`NULL.<ext>`-sentinel convention already known
      from `<zone>_models.txt`/`<category>_sprites.txt`) -- proven by
      matching `door.s`'s real `PlaySound(63)`/`PlaySound(62)` (open/
      close) against `azra_sounds.txt`'s own `63 door_open.wav`/`62
      door_close.wav`, `azra.s`'s real `PlayAmbient(73,100)` against
      `73 explore3.ogg`, and (bonus corroboration) the already-documented
      hidden Konami-code Easter egg's own native sound-effect call
      (`docs/INPUT_HANDLING.md`'s `SecretSequence_OnComplete`, id `0x57`
      = 87 = `pl_cast_powerup.wav`) using the exact same numbering.
    - **New `port/src/audio/`**: `wav_file.h`/`.cpp` (a plain RIFF chunk
      walker, no library needed), `vorbis_decoder.h`/`.cpp` (wraps
      vendored `third_party/stb_vorbis`, single-file public-domain/MIT,
      `PROVENANCE.md` -- same "vendor a real, focused implementation"
      precedent `third_party/puff` already established), `audio_engine.h`
      /`.cpp` (XAudio2 playback, bundled with Windows -- one transient
      voice per one-shot SFX, auto-reaped once finished; one persistent
      looping voice for ambient/battle music, always replacing whatever
      was playing).
    - **New `assets/sound_archive.h`/`.cpp`**: the real per-zone manifest,
      reloaded on every zone change (`main.cpp`'s zone-load block,
      alongside the existing icon-set load) -- decoded PCM cached by
      resolved file path (not by slot, since the same slot number means a
      different real file in different zones), so a sound shared across
      many zones' manifests (`door_open.wav` etc.) is only ever decoded
      once.
    - **New real handlers**: `PlayerExecutable::PlaySound(id)` (the large
      majority of real corpus calls, via `GetPlayer()`/`GetOwner()` -- an
      item's real owner once picked up), `MonsterExecutable::PlaySound
      (id)` (the real bare/self calls that aren't the player, e.g.
      `monsters/umbra_keth.s`), `LevelExecutable::PlayAmbient(id,volume)`
      (real looping music, `volume` linearly mapped to XAudio2 gain -- no
      real mixing formula was RE'd, a documented simple default).
      `PlayerExecutable`'s constructor gained optional `SoundArchive*`/
      `AudioEngine*` params (it predates and holds no reference to the
      `MenuStack` that owns it, unlike Door/Monster/Item/LevelExecutable,
      which already reach both through their own `MenuStack&` via new
      `sounds()`/`audio()` accessors there).
    - **Verified end to end against real data, new `audio_smoke`
      (`src/tests/m27_audio_smoke.cpp`)**: real `.wav`/`.ogg` decode
      against real files, the real `azra_sounds.txt` manifest (including
      its real `NULL.wav` sentinel resolving to nothing), and a real
      `door.s`/`azra.s` script call genuinely reaching a real, live
      `AudioEngine` (confirmed `AudioEngine::Init()` actually succeeds in
      this environment -- a real device, not just a decode-only headless
      check) without throwing.
    - **Not attempted** (`docs/AUDIO_FORMAT.md`'s own section has the full
      list): native-only player-action sounds (attack/jump/death/
      footsteps -- real assets exist, but no script ever calls them, so
      there's no real trigger *point* to verify against, only a trigger
      *asset*); main-menu background music (no real script trigger
      found for `menu_sounds.txt`'s own likely track); `crypt2/
      controller.s`/`twilite/steamsound.s`'s own unresolved object-loading
      mechanisms; positional/3D audio; the exact volume/pan formula.
    - Not independently confirmed by ear in an actual windowed play
      session (this agent has no way to listen) -- door/chest/lock sounds
      and each zone's ambient music should be checked live.

- [x] **M28 -- full audit pass: the tile-grid renderer rebuilt against a
      complete decompile, plus real menu-navigation and monster-AI fixes**
      (this session). Prompted by three player-reported glitches --
      "textures and lighting are not correct, some walls have the wrong
      textures or they can be misaligned", "the menus don't work properly,
      especially moving between the options and selecting them", and "the
      enemies have gigantic aggro ranges and ignore all pathfinding and
      collision". All three traced to real defects; most turned out to be
      port-side mistranslations of things the decompile already had, or
      could settle.
    - **Textures: the real `.sur` UV pipeline, implemented.** The port had
      been mapping a flat 0..1 UV across every quad since M6, so a single
      128x128 texture was stretched over a whole wall face regardless of
      its real world size. The real formula (decompiled from
      `SurfaceFace_BuildAndProject`'s tail, with the fixed-point scale
      pinned down by its consumer `SurfaceFace_RasterizeTextured_v3`) is a
      function of the vertex's **world position**: `u = (worldAxis +
      uOffset) << uShift`, `v = (vAxis + vOffset) << vShift`, negated per
      the flip bits, with the texel fetch being `mask & (uv >> 8)` -- so
      one texel is `2^(8-shift)` raw world units and textures genuinely
      **tile** (a wrap, not the clamp the port used). Real `azra.sur`
      shifts are 4-7; the dominant 6 puts 64 texels across a 256-unit
      tile. `.sur` bytes 0-5 were parsed for the first time
      (`SurfaceRecord`, `world/zone.h`).
    - **Textures: a wall's material comes from the *neighbouring* tile.**
      The single largest visual defect. `Render3DScene`'s four wall blocks
      rebuild the type-table pointer from the neighbour cell's own
      `zcpIndex` (`pbVar36 + 0xc` / `- 4` / row strides) -- the port read
      the *current* tile's index. Measured across shipped data the two
      disagree for **100%** of azra's wall boundaries, 98% of snowline's,
      92% of ghstpass's, 12% of crypt1's. Concretely: azra's wall tiles
      hand back a surface whose flags bit5 marks it *disabled* (the real
      game draws nothing and shows the canyon beyond), while the port was
      filling those faces in with whatever texture the tile underfoot
      happened to use -- which is why the starting area rendered as
      floor-to-ceiling grass instead of the stone-and-timber interior it
      actually is.
    - **Textures: the two wall bands' roles were swapped**, and the whole
      wall-face gate was wrong. `0x16`-`0x19` (named `_lo`) is the
      **main** band, spanning `max(currentFloor, neighbourCeiling)` to the
      current ceiling; `0x1a`-`0x1d` (`_hi`) is the **lower floor-step**
      band. Faces are not gated on "is the neighbour a wall" at all -- any
      boundary can carry one, `0xff` means none, and a flat corridor
      simply produces zero-height bands. Wall tiles are iterated too (only
      their floor/ceiling is suppressed); the height comparisons keep that
      from double-drawing.
    - **The corner-index convention was reversed.** `floorHeight[4]`/
      `ceilingHeight[4]` index 0 is `(x0,y1)`, 1 `(x1,y1)`, 2 `(x1,y0)`,
      3 `(x0,y0)` -- read off the floor block's vertex assignments and
      confirmed by all four wall blocks. The port assumed
      `0=NW,1=NE,2=SE,3=SW`, the exact reverse, which mirrors every sloped
      tile about its diagonal -- in the renderer *and* in
      `Zone::FloorHeightAt()`, i.e. in the ground the player physically
      stands on.
    - **A whole `ZcpEntry` field was missing.** The floor-draw gate reads
      `entry+2`, not the ceiling threshold at `+4` (an earlier version of
      `ZONE_FORMAT.md` quoted it wrongly, and the port had never parsed
      `+2` at all). Now `floorBandThreshold`; the ceiling A/B selection
      (previously always band A) uses `+4` as documented.
    - **`ZcpEntry+0x23`'s corner nudge, implemented.** Already documented
      but never ported: the real engine shifts each face vertex by half a
      tile per an 8-case code on the cell that vertex falls in, which is
      where the game's diagonal/bevelled corners come from. ~7% of real
      `.zcp` entries across the 21 shipped zones use it.
    - **Lighting is now per-vertex, with the real distance fog.**
      `vertex.light = engine+0x484 + cellLight - (viewDepth >> 1)`,
      clamped `[0x400, 0x3f00]`, interpolated across the triangle -- the
      port applied one flat value per face and had no distance falloff at
      all. `engine+0x484` is referenced exactly once in the whole binary
      (this read) and written nowhere, so the real ambient base is
      effectively 0 -- the same "never explicitly initialised" finding
      already recorded for the eye-height field. The distance-driven
      texture detail mask (`0x7f`/`0x7e`/`0x7c`/`0x78` by average
      triangle depth) is implemented too.
    - **Real near-plane clipping**, replacing M6's whole-quad cull. A
      polygon with any vertex behind the eye used to be dropped entirely,
      which punched a hole in the floor and ceiling of the tile the player
      was standing in every frame, and made entity models vanish in chunks
      up close. Now clipped Sutherland-Hodgman against the near plane
      (what `Poly3D_ClipAgainstPlane` does in the original), for both the
      tile pipeline and models.
    - **Menus: the selection index was compared against the wrong
      counter.** `selectedItem()` is a 1-based index into *all* rows (the
      number `AddMenuItem`/`AddStaticItem` hand back to scripts, and what
      `SetSelectedItem` takes -- confirmed against real scripts like
      `configkeys.s`), but `main.cpp`'s menu and popup renderers counted
      only *selectable* rows. On any screen mixing static and selectable
      rows the highlight landed on a different row than the one Enter would
      activate -- and on the main menu, where Load/Delete/Multiplayer start
      disabled, moving the selection made the highlight vanish entirely.
    - **Menus: `SetPrevMenu` was soft-failing, stranding the player.** 15
      real screens call it in `Init()` and **no script in the corpus ever
      reads it back** -- there is no `GetPrevMenu` anywhere -- so it can
      only have been consumed natively by the back key. 11 of those screens
      (Options, ConfigKeys, Load/Save/Delete Game, ChooseCharacterMenu,
      MultiPlayerMenu, ...) define no `OnRightSoftkey` handler at all, so
      pressing back on them did nothing whatsoever. Now
      `MenuExecutable::GoBack()`: the screen's own handler if it has one,
      else its real `SetPrevMenu()` target.
    - **Menus: held direction keys did nothing after the first press.**
      `SetButton()` deliberately latches only the 0->1 edge, so menus were
      tap-only. Added a menu-only auto-repeat (`TickRepeats()` /
      `ConsumeJustPressedOrRepeat()`, ~320ms then ~120ms); gameplay actions
      keep the exact old edge-only behaviour so holding an attack key still
      doesn't machine-gun. Also fixed a stale-selection case
      (`EnsureValidSelection()`): a screen whose `OnDisplay()` rebuilds its
      rows came back with nothing highlighted and a dead confirm key.
    - **Enemies: aggro is now sight-gated.** 222 of the corpus's 270
      `SetChaseRadius` calls pass **18000** raw world units -- 70 tiles on
      a 128x128 grid, over half the map. Taken as a bare radius (which is
      what the port did) that is "the whole level notices you"; it only
      makes sense as "anywhere I can actually see". Added
      `Zone::HasLineOfSight()` (an Amanatides-Woo DDA over the same wall
      flag the renderer and `Bullseye` light rays use -- the real engine
      has exactly this facility as `TileGrid_RaycastVisibility`, though how
      its AI consumes it was never traced, so the specific use is this
      port's design). Aggro now also requires vertical proximity, so a
      creature two storeys down no longer wakes up, and a chaser gives up
      ~2s after losing sight instead of never de-aggroing.
    - **Enemies: collision and steering.** Live monsters are solid to the
      player (they were walked straight through). Chasers steer around
      obstacles -- straight line first, then progressively wider turns --
      instead of grinding into the wall between them and the player, and
      push apart from each other so a pack doesn't collapse onto one point.
      Still not a real pathfinder: the original's own `.pth` spawn/patrol
      data is undecoded past its header.
    - **Real per-instance `SetSkin`/`SetScale`**, both previously
      soft-failed (and both visible in the user's own play-session log):
      creatures rendered as skin 0 at 1:1 regardless of what their script
      asked for. `SetScale` is 8.8 fixed point (256 == 1:1), consistent
      with every real call site including `azra_rat.s`'s
      `SetScale(Random(206,306))`.
    - **Ranged attacks no longer shoot through walls** -- closes M20's own
      documented gap now that a sightline test exists.
    - Two host-side defects fixed while in here: the Win32 message loop
      busy-spun a full CPU core between the 25Hz ticks (now waits on the
      message queue), and `WM_SYSKEYDOWN`/`WM_SYSKEYUP` were dropped, which
      could leave a key latched down forever if its release arrived as a
      SYS message.
    - New `port/src/tests/m28_surface_uv_smoke.cpp` asserts the real
      `.sur` field values from `azra.sur` byte-for-byte, the UV scale
      (shift 6 = 64 texels/tile, 7 = 128, 4 = 16), the wall-vs-floor V
      axis, the flip bits, the corner-nudge half-tile, the corner-index
      convention (via `FloorHeightAt` at each corner of a real sloped
      tile), the two distinct threshold fields, and line-of-sight
      behaviour. All 27 smoke tests pass, and the result was confirmed
      live: the starting area now renders as a real stone interior with
      timber ceiling beams, arched openings, a rug and props, in place of
      the uniform green it showed before.
    - **The Options screen was missing half its rows.** `AddMenuSlider`,
      `GetLanguageStr` and `MuteOnCall` all soft-failed, so options.s's two
      volume sliders didn't exist as rows at all and its God-Mode/language
      branches took the wrong path. Implemented for real
      (`simkin_bindings/slider_executable.h`, plus master SFX/music gains
      on `audio/audio_engine.h` that the sliders actually drive). The
      screen now shows Sound Volume / Music Volume / Select Language /
      Customize Controls / Mute-when-in-call, and Left/Right moves a
      slider by its real step.
    - **`GoBack()` falls through to `SetPrevMenu` even when the screen
      *has* a handler** -- because the handler is often dead in the real
      game too. options.s's `OnRightSoftkey` body is a single call to
      `OptionsMenuBack()`, and `OptionsMenuBack` is **absent from the
      fully-enumerated real 702-entry native table**; so are `MenuBack()`
      (called by inventory.s / questlog.s / buysell.s / actionqueue.s) and
      `HostGameMenuBack()`. Same already-documented "registered nowhere,
      so it misses in the real binary too" case as
      `UpdateTextItems`/`GetLastItem`/`IsRightQueue`. Confirmed live: Esc
      on Options now returns to the main menu.
    - **Not attempted**: `SetAttachedWeapon` (a held-weapon model on a
      creature), and replacing the fixed-radius tile scan with the real
      `TileGrid_RaycastVisibility` fan.

- [x] **M29 -- creature animation, and the model trailer decoded** (this
      session, direct follow-on from M28's audit). Every monster script in
      the corpus sets four animation numbers and the port soft-failed all
      of them, so creatures were static resting-pose statues sliding
      around the level -- one of the largest remaining visual gaps.
    - **`docs/MODEL_FORMAT.md`'s open "what is the resource trailer?"
      follow-up is resolved**: it's the **animation clip table**, one
      6-byte `(startFrame, endFrame, rate)` record per clip. What settles
      it is that the records **exactly partition** `[0, frameCount)` --
      first starts at 0, each start is the previous end, last end equals
      the header's own frame count -- for **every** animated entry in the
      real archive (33 of 226, up to 200 frames and 11 clips), asserted by
      the new `m29_animation_smoke` test. The clip *count* also matches
      script usage: `arat.s` names clips 0-3 and its model carries 4; the
      creatures naming clip 8 resolve to models carrying 9 or 11. That is
      exactly what `SetIdleAnimation`/`SetWalkAnimation`/
      `SetSwingAnimation`/`SetDeathAnimation`/`PlayAnimation` index.
    - `world/model_archive.h` now keeps **all** frames (M8 kept only frame
      0), at the real per-frame offset `(frameIndex * H5 + H0) * 2`, plus
      the parsed clip table; `Model::VertexAt(frame, vertex)` clamps
      out-of-range frames. `PlacedEntity::frameIndex` threads the chosen
      frame to the rasterizer.
    - `MonsterExecutable` stores the four real clip numbers and
      `PlayAnimation`/`PlayAnimationOffset`'s starting pose;
      `main.cpp`'s AI loop selects idle / walk / swing / death from the
      creature's own state and advances playback per tick.
    - **A dead monster no longer blinks out of existence** -- it plays its
      real `SetDeathAnimation()` clip and holds the final pose as a body.
      (It stops blocking movement and stops being targetable the moment it
      dies, as before; only the visual changed.) Without this the death
      clip every real script sets had nothing to play it.
    - **This port's own choices, not recovered behaviour**: reading the
      clip record's third field as frames-per-second (its units were never
      traced to a consumer -- observed values are 1/3/5/6/10/11/15/17/29),
      and looping every clip except death.

- [x] **M30 -- interaction, doors, weapon viewmodel, camera pitch, and AI
      behaviour** (this session). A second round of player-reported bugs.
      As in M28, most were port-side gaps in front of data that was already
      decoded.
    - **Interact prompts almost never appeared.** Two causes. (a) The Use
      reach was **140 raw world units** -- barely half a 256-unit tile --
      so you had to stand nearly on an entity's placement point. Raised to
      **384**, which is the game's own melee reach: every real melee weapon
      in the corpus calls `SetRange(384)` (64 of them; the other 16 are
      bows/thrown at 16384), so it's a corpus-verified "arm's length"
      rather than an invented number. The forward cone widened from ~60 to
      ~72 degrees. (b) Only `entities.txt` **category 3** was loaded as a
      world pickup, so weapons (4), spells (5), armor (6), **containers
      (8)**, consumables (9), trapped containers (12), scrolls (14),
      shields (15) and the unique weapon (16) placed in a level were inert
      scenery with no script and no prompt -- which is exactly "the prompt
      doesn't appear for a lootable object". All ten item-shaped categories
      now load (`IsPickupCategory`), with containers routed to the existing
      M21 loot-menu path. azra goes from 0 live pickups to 1 (its real
      `blaze.s` placement); zones like `broken1` have many more.
      The targeting rule moved into `sk_bindings::InInteractRange()` so it
      is testable: `m15_interact_smoke` now asserts all **10/10** real azra
      door placements are reachable from one tile away, and that the same
      approach failed under the old 140-unit reach.
    - **Every door looked permanently open, and none of them blocked.**
      `.ent`'s per-placement heading was decoded long ago
      (`docs/ZONE_FORMAT.md`: record offset `0x14`'s low u16 feeds the
      object's `+0xb6` orientation channel, the same field and 65536-per-turn
      format the player's compass heading uses) but **never used** -- every
      entity rendered at yaw 0, so a door authored into an east-west wall was
      drawn face-on and read as a gap. Real azra doors carry 0 / 16640 /
      16768 / 33152 / 49408 / 49472 / 49536, i.e. the four axis directions.
      Now applied to doors (composed with the script's own
      `AddRotationTurn` swing, same units), monsters, pickups **and** static
      scenery. Separately, `SetPassable` was stored but inert: `door.s`
      starts every door closed (`saved_Open [0]`) and only calls
      `SetPassable(true)` from `OnUse()`, so closed doors are now solid.
    - **An equipped weapon showed no viewmodel.** `WeaponViewmodel::item`
      was only ever assigned inside `StartWeaponSwing()`, i.e. on an attack
      keypress -- and only for the hand you pressed, while
      `UpdateEquipStatus` fills whichever hand is *empty*. So equipping
      showed nothing, and attacking with the empty hand showed nothing
      either. The viewmodel now tracks the equipped weapon every idle tick,
      and a swing falls back to whichever hand actually holds one. The art
      was always there: `weapons/club.s` sets `SetWeaponSprite(88)` +
      `SetAnimationFrames(5)`, and `m25_weapon_viewmodel_smoke` now asserts
      slot 88 and all five swing frames decode from the real archive.
    - **Camera pitch, which the renderer never had.** The original supports
      it -- `SurfaceFace_BuildAndProject`'s vertex transform has a
      full-3x3-matrix path and a yaw-only fast path, gated on
      `engine+0x5d4` -- and the real default control scheme's own
      `LookUp`/`LookDown` (Key2/Key8) were decoded but bound to nothing
      because there was no pitch to drive. Added `Camera::pitch` through
      both projection paths, wired those two keys, and added the requested
      automatic aim-assist: the camera eases down onto a nearby hostile and
      levels off again when it leaves range. "Small" is *measured* --
      `Model::minLocalY/maxLocalY` (new) gives the creature's real height,
      scaled by its own `SetScale`, so a rat produces a real downward tilt
      and a humanoid almost none, with no per-creature special-casing.
      Manual look overrides it while held. The auto-aim itself is a
      port-side design; nothing has been traced that aims the original's
      camera automatically.
    - **Enemy behaviour.** Aggro was driven directly by `SetChaseRadius`,
      whose value is **18000 raw units** in 222 of the corpus's 270 calls --
      70 tiles on a 128x128 grid, more than half the map and larger than any
      zone's playable extent (`SetAttackRange`'s 12000 is ~47 tiles, equally
      implausible). Whatever those units are, they are not a euclidean
      radius, so the script value is now honoured only as an upper bound,
      capped at 8 tiles, with line of sight doing the real gating. Movement
      speed dropped 22 -> 14 world units/tick (the player walks 40); a
      chaser now clamps its step so it stops exactly at bodily contact
      instead of walking into the player; and melee no longer requires a
      clean centre-to-centre sightline, which is what made a creature in a
      doorway keep advancing instead of stopping to swing. Creatures also
      turn to face where they are going.
    - **Soft-fails cleared** (from the play-session log): `Menu::Quit()` --
      30+ real scripts call it and every NPC conversation's "Goodbye" row is
      just `Quit()`, so choosing it did nothing; it now closes the screen,
      resuming gameplay if it was opened from the 3D view, else falling back
      to `SetPrevMenu`. Also `Monster::SetLevel`/`GetLevel`, which real
      spell damage formulas read (`Random(3, (GetOwner().GetLevel()+1)*2)`).
    - **Still soft-failing, deliberately**: `AiDetect` (the real AI state
      machine is native and opaque -- it's the only Ai\* call any script
      makes), `AddProduct`/`ClearProducts` (the buy/sell shop screens, out
      of scope since M10), `SetAttachedWeapon` (a second model in a
      creature's hand), `DetectOnKilled`, and a few
      character-creation/quest odds and ends.

- [x] **M31 -- the monster AI cluster decompiled; the real distance unit
      found** (this session). Follow-up to M30, which had capped
      `SetChaseRadius` at a guessed 8 tiles because its shipped value
      (18000) was implausible as a linear radius. Decompiling the AI
      settles it: the value was never a linear radius at all.
    - **`FUN_100683d4`** (actor vtable `+0x5c`) is the distance every AI
      threshold is compared against, and it returns
      `(dx*dx >> 8) + (dy*dy >> 8)` -- a **scaled squared** distance. So a
      script value stands for `16 * sqrt(value)` raw world units. The
      corpus's dominant `SetChaseRadius(18000)` is therefore **8.4 tiles**,
      not 70; `SetAttackRange(12000)` is 6.9 tiles, not 47; and the
      `SetChaseRadius(1)`/`(15)` outliers are "never chase" sentinels.
      Every shipped value becomes sensible at once, which is the check that
      makes this conclusive.
    - **The AI packages** (`monster+0x2a8`): -1 asleep, 2 idle/detect
      (the constructor default, and what `AiDetect` sets), 3 pursue/attack,
      4 flee, 6 spell-assist.
    - **Which call writes what**: `SetChaseRadius` -> `+0x2b8`, the
      give-up distance; `SetAttackRange` -> `+0x2dc`, the stand-off at
      which the tick zeroes the creature's velocity and swings.
      `SetMeleeAttackRange` is a **genuine no-op** in the shipped game (its
      dispatcher case stores nothing), as are `AiActivate` and `AiWounded`.
      This also pinned the Monster dispatcher's case numbering as running
      **3 below** the enumerated binding-table indices -- proven by a
      Set/Get pair landing on one field (`SetChaseRadius`/`GetChaseRadius`
      both on `+0x2b8`), which M30 had flagged as unresolved and a reason
      not to trust conclusions drawn from that table.
    - **Aggro really is line-of-sight gated**: acquisition runs a 3D
      raycast from the attacker's eye (`FUN_10082004`, budget
      `attackRange >> 8` = the same threshold in tiles^2) rather than
      resting on distance alone -- confirming M30's sight-gating, which had
      been a from-scratch design, as the right shape.
    - Port changes: `MonsterExecutable::chaseRadius()`/`attackRange()` now
      convert to real world units, M30's invented 8-tile cap is gone, and
      creatures stop at their own real `attackRange` (660 units by default
      -- roughly arm's length, since a person model is ~800 units tall)
      instead of the port's invented 110. Full writeup:
      `docs/WORLD_MODEL.md`'s new "The monster AI" section.
    - **Not attempted**: the flee (4) and spell-assist (6) packages, the
      `.pth` patrol-path system, and the attack cadence timer
      (`monster+0x2c4`, accumulated per tick and reset to `rand & 0x1f`).

- [x] **M32 -- the flee/spell-assist packages, the attack cadence, and the
      status-effect system** (this session). Requested directly as a
      follow-on to M31. Two of the three turned out to be barely
      implemented in the original, which is itself the finding.
    - **The attack cadence is real and is now the port's.**
      `monster+0x2c4` accumulates the engine's per-frame delta, the whole
      attack block is gated on `0x100 < accumulator`, and afterwards it
      resets to `rand & 0x1f` -- a jitter so a pack doesn't swing in
      lockstep. That works out to roughly one attempt every 256ms,
      replacing this port's invented fixed ~1s cooldown. Nothing else
      throttles attacking.
    - **`monster+0x294` is a paralysis lockout, not an attack cooldown** --
      the attack function's first test is `+0x294 < 1`, and the tick only
      steers toward a target while it is exactly 0. Written from exactly
      one place, the `SetParalyzed` dispatcher case. Implemented, and it
      now blocks both swinging and turning.
    - **Flee (package 4) is the Fear spell, not creature morale.** Its
      whole implementation is `FUN_10086b98(actor, 4, duration)`: set the
      package, arm a `duration << 8` countdown, drop the current target and
      record "come back as idle". **The AI tick has no `package == 4`
      branch at all** -- there is no per-tick flee steering in the
      original, only that one-shot move goal. `SetWimpy` does *not* trigger
      it (the AI never reads `monster+0x2ae`). This port runs the creature
      directly away for the duration, which is its reading of that one-shot
      goal -- noting the original's own goal expression,
      `(target - self) * 0x14`, yields an absolute position that isn't
      generally away from the threat, and may be a bug in the original.
    - **Spell-assist (package 6) is a no-op in the shipped game.** A
      whole-program decompiled grep for `0x2a8) == 6` returns nothing, and
      a creature left in package 6 matches neither branch of the tick's
      package dispatch -- it simply stops acting. Reproduced faithfully
      rather than invented. (No script in the corpus calls it, or
      `AiFlee`, either -- only `AiDetect` and `AiSleep`.)
    - **The status-effect system, resolved** -- closing a long-standing
      "Next milestones" item. `FUN_100458e4` selects the effect from the
      **spell entity's own `entities.txt` typeId** (`actor+0xc8`), not from
      `DoAttackRoll`'s second argument, which is a magnitude: 4020 =
      `spells\Fear.s` -> the flee package for `magnitude * 5`, 4025 =
      `spells\Paralyze.s` -> the action lockout, plus identified branches
      for Absorb/Blind/Drain/HarmArmor/IgniteFoe/Disease/Poison. Real
      `Fear.s` passes `DoAttackRoll(target, 10)`; real `blaze.s` passes 1.
      Fear and Paralyze are implemented; the rest have no system in this
      port to attach to yet.
    - `AiDetect`/`AiSleep`/`AiAttack`/`AiFlee`/`AiSpellAssistTarget`/
      `SetParalyzed` are all real handlers now (`AiDetect` alone was 35 of
      the play-session log's soft-fails), and `AiActivate`/`AiWounded`/
      `AiPursue` are accepted as the genuine no-ops they are in the
      original. New `m32_ai_package_smoke` asserts all of it against real
      `arat.s`/`Fear.s`/`blaze.s` data -- 29/29 smoke tests pass.
    - **Frame-delta caveat**: every AI timer counts in the engine's own
      per-frame delta, a field the engine writes each frame whose magnitude
      was not recovered. The port uses its fixed 40ms tick, which makes the
      decompiled `0x100` threshold land at ~256ms and the `rand & 0x1f`
      reset a sub-frame jitter -- the proportions the real constants imply,
      but not a measured value.

- [x] **M33 -- the remaining status effects: poison, disease, drain,
      blind** (this session). Requested directly. M32 had identified all
      nine branches of the real dispatcher but implemented only two; this
      decodes the shared machinery underneath and implements five more.
    - **Two primitives do all the work**, both decompiled:
      `FUN_1004aa28` applies a **timed stat modifier** (a record carrying
      `{stat, delta, expiry = now + duration * 0x100}` on a per-actor list,
      with a **strongest-wins** rule -- a new modifier on a stat that
      already has one is rejected unless its magnitude is strictly greater,
      in which case it replaces it), and `FUN_1004bae8` sets a **timed
      effect flag** plus the damage-over-time kind.
    - **The damage-over-time tick is `FUN_10049780`**, and it settles what
      poison actually does: `+0x76` is **both the effect kind and the
      damage dealt** -- it is passed straight to `DoDamage` -- so poison's
      `3` means three points every 256 delta units, the same "one second"
      every other engine timer counts in.
    - **Stat indices resolved** from `FUN_1004ad40`'s switch and
      cross-checked against the character-stats dispatcher (0x10048244),
      where `SetAttack` writes the stat block's first halfword,
      `SetDefense` the second and `SetArmorValue` the seventh: **1 =
      attack, 2 = defense, 7 = armor** -- exactly the stats Drain, Blind
      and HarmArmor target, which is the corroboration that the mapping is
      right.
    - **Implemented, in the engine's own parameters**: Poison (flag 8, 3
      damage per second for `magnitude`s), Disease (attack -2 when
      `magnitude < 7` else -3, **and** defense -3, both for a fixed 30s --
      the only effect whose duration ignores magnitude), Drain (attack -10
      for `magnitude + 8`s), Blind (attack -10 **and** defense -10 for
      `magnitude + 5`s, plus the blind flag). HarmArmor (armor
      -`magnitude`) came free from the same machinery.
    - **Blindness has teeth**: a blinded creature fails the perception
      check, so it stops acquiring the player and loses track once they
      move -- which is what its stat penalties imply.
    - **`magnitude` is not `DoAttackRoll`'s argument.** The dispatcher
      reads it from the *caster's* spell-power stat (`stats+0x34`, index
      0x18) and clamps it to 25, never looking at what the script passed.
      The proof is in the scripts: real `Poison.s` and `Disease.s` call
      `DoAttackRoll(target)` with **no second argument at all** yet apply
      fully-parameterised effects. This port has no spell-power stat, so
      the spell's own `SetRating()` stands in (poison 8, blind 3, drain 12,
      fear 14, harmarmor 17, paralyze 20) -- a documented substitution,
      with the real clamp reproduced.
    - **Corrected an older test**: `m22_spell_smoke` asserted that
      `blind.s` deals `RollSpellDamage()` damage, which was only ever true
      because the port treated every spell as a damage spell. Blind is not
      a damage spell; it now asserts no damage plus the real -10/-10
      modifiers.
    - ~~**Still unmodelled**: Absorb (4009) and IgniteFoe (4024)~~ --
      **both implemented in M34, below.**
    - `m32_ai_package_smoke` grew to 24 assertions against real
      `Drain.s`/`Blind.s`/`Disease.s`/`Poison.s`/`arat.s` data -- 29/29
      smoke tests pass. Full writeup: `docs/WORLD_MODEL.md`'s "The
      status-effect primitives, decoded".

- [x] **M34 -- Absorb and IgniteFoe: the last two status effects, and the
  stats block's second periodic channel.** The two branches of
  `FUN_100458e4` that carry their own damage rather than being pure
  status. Full writeup: `docs/WORLD_MODEL.md`'s "The two damage-carrying
  branches".
    - **The damage roll.** The dispatcher picks a `min`/`max` pair and
      rolls between them **only when they differ**. Absorb sets them equal
      (`magnitude + 12` both), making it the one spell in the table with no
      variance; IgniteFoe sets `(magnitude+1)*2 .. (magnitude+1)*5`.
    - **Absorb's heal, and the magic constant.** After the damage, the
      caster's health is set to
      `health + ((damage * 0x100 * ((magnitude << 16) / 6400)) >> 16) + 6`.
      The 6400 is not written as a divisor -- the binary multiplies by
      `DAT_10045e54` and shifts right 43, and that constant reads
      `0x51EB851F`, the standard magic for signed division by 25 (shift 3)
      plus 8 more for the 256. Since `magnitude` is itself capped at 25 at
      the top of the function, the whole thing collapses to
      **`heal = damage * magnitude / 25 + 6`**. The setter is
      `FUN_1004bb88`, a three-instruction `SetHealth` that clamps into
      `[0, maxHealth]` -- so absorbing at full health does nothing.
      `PlayerExecutable::SetHealth()` now implements it, and the
      script-facing `SetHealth` handler routes through it too (it used to
      assign straight through, letting a script exceed the maximum).
    - **The second periodic channel.** `FUN_10049780` runs two independent
      channels, not one. The second (`+0x78`/`+0x7a`/`+0x7c`) has its own
      `kind`: 6 = magicka regen, 7 = health regen, 8 = burn. IgniteFoe is
      the only site in the whole dispatcher that arms it, and it arms
      kind 8 for `magnitude * 2` seconds at 1 damage a second. Kind 8
      writes health *directly* instead of going through `DoDamage`, so a
      burn is reduced by neither armour nor resistance, and it kills
      through the actor's kill vtable slot.
    - **`+0x76` is shared between the channels**, and this is reproduced:
      poison writes 3 there as its kind-and-damage, IgniteFoe writes 1
      there as its per-tick damage, so poisoning a burning creature really
      does triple its burn. Channel 1's expiry is also not a clear-to-zero
      -- it assigns `flags = 2` and `+0x76 = 3` -- which corrects M33's
      implementation and, for the same reason, is now observable.
    - **Independent confirmation of the duration**: the branch also builds
      a particle emitter (`FUN_10067f84`) at the target's own x/y/z with
      sprite range 62..68 and a lifetime of `magnitude * 2 << 8`, matching
      the burn timer exactly. The visual itself has no emitter system here
      to attach to and is not reproduced.
    - **Two real aliases wired up**: `spells\IgniteScroll.s` and
      `spells\U_Ignite_Foe_8_lvl8.s` are three-line scripts whose whole
      `Init()` is `SetSpellType(4024); ... RunScript("spells\\IgniteFoe")`.
      A corpus sweep found exactly 7 such `RunScript` spell aliases, and
      these are the only two pointing at a modelled effect.
    - **Fixed alongside**: a creature killed by a damage-over-time in
      `TickAi()` never ran `OnKilled()`, dropped loot, or fired the zone
      script's kill-count trigger -- that handling only existed on the
      player-swing path. Latent for poison since M33; IgniteFoe, which
      exists to kill over time, would have hit it constantly.
      `SetMPUsable` (49 real call sites, `absorb.s` among them) is now
      accepted as a no-op instead of logging soft-fail noise.
    - **Not adopted, and recorded in the docs**: the dispatcher gates the
      *entire* effect -- status included -- behind a hit roll of
      `casterPower * 0x100 / (casterPower + resistance)` against
      `rand(0, 0x100)`. Adopting it would change every existing spell's
      behaviour and needs the same caster spell-power stat this port
      doesn't have.
    - `m32_ai_package_smoke` grew to 31 assertions, adding real
      `Absorb.s`/`IgniteFoe.s`/`lakvan.s` coverage including the shared
      `+0x76` interaction -- 29/29 smoke tests pass.

- [x] **M35 -- eight reported gameplay/UI bugs, and the four root causes
  under them.** A user bug list that turned out to be four shared causes
  plus four local ones. Every fix is against decompiled or shipped-data
  evidence, not tuning.
    - **The engine's time unit was wrong by 3.9x** (attack noise firing
      continuously). `kAiFrameDeltaUnits` was 40, on the reasoning that the
      per-frame delta was never recovered so the port's own 40ms tick would
      do -- which conflated milliseconds with the engine's unit. The unit
      is not a free choice: every duration is stored as `seconds * 0x100`,
      and `FUN_1002f694` advances the clock those expiries are compared
      against (`+0x460`, zeroed in `FUN_1002fca4`) by exactly this delta
      each frame. So the clock counts 1/256ths of a second and the delta is
      `0x100 * frameSeconds` = `0x100/25` = 10.24, not 40. Every AI timer
      in the game -- attack cadence, poison ticks, fear, paralysis, burn --
      was running nearly four times too fast. The 0x100 attack cadence is
      now ~26 ticks, i.e. **one swing a second**, which is what that
      constant and its `rand & 0x1f` jitter are proportioned for.
    - **Models were drawn a quarter turn off** (creatures not facing the
      player). A model's forward axis is its local **+Z**, decompiled from
      `BuildRotationMatrix3x4` + the actor vertex loop and corroborated by
      the shipped geometry (quadrupeds longest along Z, humanoids
      *narrowest* along Z, door.s a slab whose thin axis is Z). See
      `docs/MODEL_FORMAT.md`. The port rotated local +X to the heading, so
      creatures tracked the player but always presented their flank.
      Placement yaws are compensated so doors/pickups render unchanged --
      their raw `.ent` zero-reference was fitted by eye, and that fit had
      already absorbed the same quarter turn.
    - **The `.ent` record tail is two strings, not one 40-byte name**
      (couldn't loot chests). `char name[8]` at 0x20 and `char script[32]`
      at 0x28 -- run together, container placements read as
      `"ContaineRaiders\RT_A.s"`. That second field is the real
      per-placement script path and overrides `entities.txt` (1530/1532
      monster placements, 393/570 containers, 442/559 doors resolve). It is
      how a chest gets its contents: every container in the game is an
      `entities.txt` "!label" with no script, and the randomised loot
      script is named per placement. Resolving it also picks up
      zone-specific NPC variants (azra's "tanyin" runs
      `Tanyin_Aldwyr_Azra.s`, not `Tanyin_Aldwyr.s`). Full writeup in
      `docs/ZONE_FORMAT.md`.
    - **Most world items define no `OnUse` at all** ("Learn Blaze" doing
      nothing). Every weapon (83 scripts), every armour piece (89), every
      shield (10), every scroll (7) and half the spells (14) have only
      Init/HitTarget -- the engine's native default takes the item, and the
      script's own Init already picks the prompt that says so (blaze.s sets
      404 "Learn Blaze" or 405 "Pickup Blaze Scroll" depending on
      `IsItemEnabledFor`). The port drove pickups purely off the script's
      OnUse, so the prompt appeared with nothing behind it.
      `InvokeOnUse()` now reports whether a handler existed and main.cpp
      runs the native default when it did not.
    - **Equip screen navigation** (two of the reported bugs). Selection was
      one flat vertical list, but `inventory.s` lays five category buttons
      side by side at y=25 and the item table at y=60. Flattened, Up/Down
      walked buttons and table as if stacked, Left/Right went to
      `CycleSelectedCombo` and did nothing (there is no combo on that
      screen), and the table swallowed Up/Down forever once reached.
      `NavigateDirectional()` now reads the layout the scripts already
      provide -- rows in a y band navigate with Left/Right, Up/Down steps
      between bands, and a table releases focus at its own first/last row.
      Also fixed `AddTable` discarding its real x/y/w/h, which left the
      table as the one unpositioned thing on a hand-laid screen.
    - **Repeated attacks restarted the swing.** `StartWeaponSwing` reset to
      frame 0 on every press; the real draw function's swing branch only
      ever advances its progress accumulator. The post-swing hold stays
      interruptible, which is what chains one swing into the next.
    - **Models ignored fog** (visible through it). `RasterizeModelTriangle`
      passed a hardcoded 1.0 brightness, so creatures/props/trees stayed
      fully lit at any distance while the walls around them faded. The real
      actor pipeline does fade them -- five of the ten
      `Poly3D_RasterizeTextured` variants interpolate a depth-scaled
      intensity into the output colour -- but neither its global factor nor
      its lookup table was extracted, so models now take the same
      `cellLight - depth/2` term the surface pipeline already uses, applied
      as an RGB scale since a model carries its own RGB444 skin instead of
      indexing a `.zlu` rung. Approximate in the mapping, correct in the
      behaviour: the two pipelines agree about distance now.
    - New `m35_placement_menu_smoke` (13 assertions against real
      `raiders.ent`/`Raiders\RT_A.s`/`inventory.s` data) plus the corrected
      cadence assertions in `m32_ai_package_smoke`; 30/30 smoke tests pass.
    - **Not verified live**: synthetic input could not drive the running
      game past character creation into a zone, so the in-world half of
      these (facing, fog, chests, prompts) is verified by tests and offline
      renders rather than by play.

- [x] **M36 -- follow-up on four reported regressions/misses from M35.**
    - **Creature facing: sign corrected.** M35 established the axis (a
      model's forward is its local Z, decompiled from the real actor
      transform) but guessed the direction along it, turning a reported
      flank into a reported back -- half a turn, the signature of exactly
      that. Forward is local **-Z**: models are authored facing the viewer.
      Verified live this time, with a creature spawned in front of the
      player (see the debug aid below): it looks straight back.
    - **Emptying a container left a blank menu.** lootmenu.s's UpdateMenu()
      calls LootExit() and returns *before adding any rows* once GetFirst()
      is null, so the screen is meant to be closed by LootExit's own two
      natives -- and the port implemented neither, leaving the player on an
      empty page with the "Okay" row (string 528) gone. Now implemented:
      `QueryDestroy()` closes it (the container stays, already made
      unusable), and `QuitAndDestroyOpener()` closes it and condemns the
      container, which is the branch a *dropped loot bag* takes because
      real spawn scripts (monsters/arat.s's own `Loot.SetDestroy(true)`,
      fearfrst/goblin_hero.s, crypt1/shadowkeygate.s) set that flag while a
      placed chest never does. `SetDestroy`/`GetDestroy` and `GetTemplate`
      are implemented alongside; the world instance is erased on the next
      world tick, not inside the handler, for the usual deferred-removal
      reason. Closes M21's long-standing "empty-bag despawn" open item.
    - **The equip screen's category tabs were invisible.** `AddButton`'s
      real 5th/6th arguments are global.spr slot ids for the button's
      normal and highlighted art, and both were being discarded -- and
      inventory.s builds its five tabs with an *empty* label
      (`AddButton("", "WeaponsMenu", x, y, 47, 48)`), so there was nothing
      on screen at all and M35's Left/Right navigation was moving a
      selection the player could not see. The art is drawn now, with the
      selection outline the real screen shows. Two placement misses fixed
      with it: `AddTable` had its real x/y recorded but unused (the item
      list drew over the icons instead of below them), and AddQuitButton's
      "Back" label drew inline in the vertical flow instead of at the
      bottom of the page. Checked against the shipped screen.
    - **The reported crash was not reproduced** and is not fixed. A
      creature spawned in melee range, attacking a player with a weapon
      equipped, ran clean; reading every path M35 touched found nothing.
      What was added instead is a crash reporter
      (`platform/win32/crash_report.h`): an unhandled-exception filter that
      writes the fault, the faulting address and a symbolized stack into
      shadowkey_port.log, so the next occurrence names its own location.
    - **Debug aids** (`SK_DEBUG_SPAWN`, `SK_DEBUG_EQUIP`, env-gated and
      inert unless set): spawn a named monster script in front of the
      player start, and equip the first weapon in the starting inventory.
      Added because verifying an in-world fix otherwise means playing to
      wherever a creature happens to be -- which is what made M35's facing
      sign a guess in the first place.

- [x] **M37 -- the caster stat and the real magic to-hit gate.** Closes the
  status-effect system: every branch of `FUN_100458e4` is now implemented,
  with its real input rather than a substitute.
    - **The stats block's layout, recovered in full** -- the finding
      everything else here rests on. `FUN_1004ad40`'s stat-index switch and
      `FUN_10048244`'s 85 named getters/setters read and write the same
      halfwords, so cross-referencing them names each field from the
      shipped engine's own vocabulary rather than by inference:
      `+0x04` spellcast, `+0x06` magic resistance, `+0x14`-`+0x22` the
      attribute block (strength ... luck), `+0x30` experience,
      **`+0x34` character level**, `+0x38` gold. Table in
      `docs/WORLD_MODEL.md`.
    - **Correction: `+0x34` is the character level, not "spell power."**
      M33 recorded it as a spell-power stat because the dispatcher was the
      only thing that had been read; `GetLevel`/`SetLevel` (binding index
      0x37/0x38) read and write exactly that halfword. So the real
      `magnitude` behind every effect is **the caster's level, clamped to
      25** -- and the 25 that divides Absorb's heal is the level cap, which
      is why "absorb a share proportional to how far you are toward the
      cap" is the actual design.
    - **The scroll fallback, and what killed the old substitution.** The
      full rule is `if (!scroll && (caster == null || casterIsCharacter))
      magnitude = casterLevel; else magnitude = spell->level`, where
      `spell->level` is the Spell class's own `SetLevel` (`+0x1d0`) and
      `scroll` its `SetScroll` (`+0x1d4`, set by the loot generators, e.g.
      `broken1/loot_random_a.s`'s `Scroll.SetScroll(true)`). Reading the
      whole `spells/` directory corroborates it from the data side: the
      *only* spell scripts that call `SetLevel` are the scroll and unique
      wrappers (`IgniteScroll.s` `SetLevel(8)`, `U_Blaze_lvl5.s`, ...),
      each matching its own filename -- and the same read is what retired
      the port's `SetRating()` substitution, whose values turn out to be a
      dense near-alphabetical ordinal run with duplicates (absorb 1,
      blind 3, bodytomind 5, disease 5, poison 8, daedricweapon 8, ...
      azrasustenance 28), absent from those wrappers entirely. The
      practical effect: IgniteFoe used to hit for `(19+1)*5 = 100` on a
      level-1 character.
    - **The resistance gate replaces the flat subtraction.** The real
      engine never subtracts resistance from spell damage; it resolves
      resistance once as a hit chance in front of the *whole* effect
      (`power * 256 / (power + resistance)` against `rand(0, 0x100)`, with
      an `== 0x100` certain-hit short-circuit), so a resisted Poison now
      applies no poison at all rather than a weakened one. Both terms come
      from formulas that are each confirmed twice -- a standalone helper
      (`FUN_1004bc60`/`FUN_1004bbd0`) and a script-callable binding
      (`GetSpellToHit`/`GetSpellResistance`) that computes the same thing
      inline: `spellcast + 2*willpower` and
      `magicResistance + willpower/5`.
    - **The dispatcher's four remaining branches**, all damage-only and
      all now nameable because `entities.txt` maps their typeIds to
      shipped scripts: Blaze (50, `3 .. level*3+3`), Blaze-greater (4006, a
      flat 45..50 with no magnitude term at all), DeadToDust (4002),
      DoomHammer (4012/4017) and DeathHowl (4035). Blaze matters most --
      it is the spell every new character is given, and it was the one
      spell still falling through to this port's from-scratch damage path.
    - **DeadToDust's target test**, and two more real natives with it:
      `FUN_10086f88` is `monster+0x2d0 == 2`, and that field is a single
      creature-kind selector -- `SetSpider` writes 1, `SetUndead` writes 2,
      `IsUndead` is `== 2`. All three were soft-failing (15 real
      `SetUndead(true)` call sites).
    - Also implemented: `SetSpellType` (the typeId itself, which is the
      only way to separate the two branch pairs that share a script file),
      and the player-side `SetLevel`/`SetSpellcast`/`SetWillpower` plus the
      `GetWil`/`GetWill`/`GetWillpower` alias trio the real class carries.
    - New `m37_spellpower_smoke` (24 assertions, all against real shipped
      scripts -- `Absorb.s`, `IgniteScroll.s`, `blaze.s`, `DeathHowl.s`,
      `DoomHammer.s`, `DeadToDust.s`, `Azra_Rat.s`, `lakvan.s`); the M32
      and M22 spell assertions rewritten around the caster level, which
      makes them stronger than before (Disease's `< 7` branch can now be
      pinned on *both* arms by casting at level 5 and level 8, impossible
      while the magnitude was a per-spell constant). 31/31 smoke tests
      pass.
    - **Not attempted**: the enchantment terms both to-hit helpers add for
      an equipped item of enchantment type 4 or 7, and the damage
      reduction the dispatcher applies when the target's owner is in state
      `+0xf3c == 6` -- all three read object graphs this port has no
      equivalent of. Each is a missing *bonus*, not a wrong formula.

- [x] **M38 -- zone triggers: the physical trap/door half, and the
  "trigger volume" question answered.** The blocker recorded since M23/M24
  ("no real trigger-volume data source traced yet: is it `.ent`-based,
  `.zcp`-cell-based, or something else entirely") turns out to have a
  third answer: **there is no volume**.
    - **How a trigger identifies what it guards.** `FUN_10090818` (fire)
      and `FUN_1007307c` (the walk over the zone's trigger list at
      `level+0x420`) show a trigger is notified about an *entity*, with a
      mode saying what happened, and matches it either against its own
      `AddEntity(...)` list -- which holds integer `entities.txt` typeIds
      *or* entity names, `FUN_1009138c` comparing `entity+0xc8` for one and
      strcmp'ing `entity+0xcb` for the other -- or against the trigger's
      **own name**, compared to the entity's.
    - **Corroborated twice from shipped data.** `crypt1.s` builds five
      triggers named `door1`..`door5` with no `AddEntity` and no `SetDoor`
      at all; `crypt1.ent` contains exactly five placements with those
      names (all typeId 27, a door), and `crypt1.stn` independently binds
      those same five names to a shared `resistDisarm[28]` slot. Three
      unrelated files agreeing is what makes this a reading rather than a
      guess. Meanwhile `lothcav.s`'s three traps match by typeId (1013,
      1011, 1012) -- all three real category-10 (trap) entities.
    - **The three notification modes**, from `FUN_1007307c`'s three call
      sites: **0** entity killed (M24's kill-count variant), **1** door
      opened (`FUN_1002e6cc`, which notifies *before* running the door's
      own open -- which is what makes a trapped door bite as you open it),
      **2** a trap entity's own periodic proximity check (`FUN_1008ff14`,
      which builds a +/-0x100 box around the trap and the player's box from
      the player's `SetRadius`/`SetRadius2` half-extents). All three are
      wired: mode 1 on the door Use path, mode 2 as an edge-triggered
      per-tick box test over the zone's real trap placements.
    - **`SetTrap`/`RemainActive`/`ShowDamageMessage`/`IsActive`/`SetDoor`/
      `OpenDoor`/`AddEntity`** implemented, with the real flag bits
      (`trigger+0x24`, whose constructor value is the one-shot bit -- which
      is why `RemainActive()` *clears* a bit rather than setting one).
    - **A real vendored-Simkin incompatibility, found by this work.**
      `skTreeNodeObject::setValue()` stores a native object assigned to a
      *declared* field as `child->data(v.str())` -- it keeps the string and
      drops the object. `lothcav.s` declares `fight41` and `doorTrigger` at
      the top of its class, so `fight41.AddRandomSets(272, 1)` threw and
      **aborted that whole zone's `Init()` before any of its three trap
      triggers were registered**; `ghstpass.s` does the same thing without
      declaring the fields (undeclared identifiers become stack locals,
      which hold real objects), which is why it never showed up. Fixed with
      an object-field side table on all four `skScriptedExecutable`
      subclasses; plain values still take the normal TreeNode path, so
      `saved_X` flags are untouched.
    - **Trigger callbacks take two parameters, not one.** `crypt1.s`
      declares `OnOpenDoor[ (trigger, who) ]` and immediately calls
      `trigger.IsActive()` and `who.OpenDoor("Open Door")`; the real
      argument array (built at `LAB_10090d70`) is exactly the trigger
      object and the notifying entity. Every other host-triggered call in
      this port passes a single placeholder, so this one needed its own
      shape.
    - **The Encounter spawner is a real object now** (`AddEncounters` used
      to return an inert handle). `AddRandomSets` reads its arguments in
      (typeId, count) **pairs** and appends the whole run as one set --
      five calls give five alternative sets, which is what makes them
      random; `SetLimit(index, count)` indexes the *region* list, not the
      set list.
    - New `m38_trap_trigger_smoke` (18 assertions against real `lothcav.s`,
      `crypt1.s`, `ghstpass.s`, `entities.txt` and `crypt1.ent` data);
      33/33 smoke tests pass.
    - **Not attempted**: the pair of saving throws in front of trap damage
      (`FUN_1003e438` reads a player stance model and an 8-slot equipped-
      effect array this port has no equivalent of; `FUN_10044e58` is a
      second, cheaper roll) -- so a trap here always connects, a missing
      mitigation rather than a wrong formula. And where an *encounter
      region* is positioned: dumping every named placement in
      `lothcav.ent`/`ghstpass.ent` found none of `battle41`/`fight12`/
      `fight13C`/... so regions live in a per-zone source not yet
      identified (the real lookup goes through `FUN_100731b4` against a
      list at `level+0x44c`, which is very likely also what `EnterZone`'s
      named triggers walk).

- [x] **M39 -- the small documented loose ends.** Five open items, each
  one call or argument, each left unguessed in its own milestone's "Not
  attempted" note. All five resolved from the decompile and checked
  against real shipped scripts.
    - **`SetLoot`'s trailing pair is a drop *chance*, not a quantity.**
      The monster dispatcher's case 0xd rolls `rand(min, max)` and keeps
      the loot name **only when the roll comes up equal to `min`**,
      discarding it otherwise -- a 1-in-`(max - min + 1)` chance, decided
      once at Init rather than at death. So
      `SetLoot(300, "Loot_ratseye", 1, 8)` is a one-in-eight rat eye and
      `SetLoot(300, "loot_gold25-35")` (no trailing pair) always drops.
      This port ignored the pair, which made every creature in the game a
      guaranteed drop.
    - **`SetZone(zoneId, total)` is the zone's experience budget.**
      Zone-effects case 0 counts every creature in the zone with
      `monster+0x2ef` set, divides `total` by that count, and writes the
      share into each one's `expWorth` (`stats+0x0e`) -- overwriting
      whatever the creature's own script set. And `+0x2ef` is written by
      exactly one thing: the monster class's own `SetMob()`. So a zone
      author writes one number (azra.s 2000, lothcav.s 21500, crypt1.s
      25000) and the engine spreads it. Corroborated by the corpus:
      `monsters/Azra_Rat.s` calls `SetMob` and counts;
      `monsters/Tanyin_Aldwyr.s`, a scripted NPC, does not and is skipped.
    - **`SummonMe()`/`SummonMe2()` are not natives at all.** Every target
      of azra.s's `Birg.SummonMe()` / `Skelos.SummonMe()` /
      `Vil1..4.SummonMe()` / `Heather.SummonMe()` declares the handler in
      its own `.s` file, and the whole body is a single `SetPosition`
      (`monsters/birgiddaazra.s`: `SummonMe[() { SetPosition(31083, 3483,
      -2816); }]`). The missing piece was the setter, which soft-failed --
      so azra's entire summoned cast stood wherever `azra.ent` put them.
      `SetPosition`/`SetPositionMirror` are implemented and mirrored onto
      the live instance.
    - **`CountInventory(id)`** -- Player case 0x40, a count of matching
      entries in the player's own inventory collection keyed on the item
      script's `SetID()`, not its display name. Real callers gate quest
      progress on it (azra.s and glcrcrwl/nym_convo.s both branch on
      `CountInventory("stooth") = 7`, and `items/startooth.s` is what sets
      that id).
    - **`Level.Log(s)`** -- a debug trace with ~100 real call sites; it
      now lands in `shadowkey_port.log` with everything else, which is
      genuinely useful reading now that whole zone scripts run.
    - New `m39_loose_ends_smoke` (12 assertions); the M21 loot assertion
      updated for the drop chance it was silently relying on being 100%.
      34/34 smoke tests pass.

- [x] **M40 -- the save container, decoded and implemented.** The
  roadmap item read "M5's save system is simulated in-memory only; no
  on-disk save format has been RE'd yet". The container half is done --
  see the new [`SAVE_FORMAT.md`](SAVE_FORMAT.md) for the full writeup.
    - **The file set**, from the binary's own format strings:
      `game00.sav`..`game03.sav` (the four slots), `current.sav` (the
      in-progress game), `character.dat`, a per-zone `<level>.dat`
      ("Using %s to save our previous level"), `levelinfo.txt` (a
      two-line `"%d
%d
"`), and `dragonstar.cfg`/`.set`. A UTF-16
      `v1023_1.0` version tag and the `SaveCorrupted` message sit in the
      same code.
    - **The format is a named-blob archive**, not a struct dump: a
      256-entry table of contents of `(name, offset, length)` triples
      followed by the blobs, handled for every file above by one class
      (constructor `0x10009b2c`, instance size `0x1924`).
    - **The leading `u32` is an integrity check, not a header field.** The
      create path writes a placeholder, the close path seeks back to 0 and
      patches in the real size, and the open path rejects the file when
      that disagrees with the size on disk. A save interrupted mid-write
      is caught by construction, with no checksum -- which is what
      `SaveCorrupted` is for.
    - Implemented as `assets/save_archive.h`/`.cpp` with
      `m40_save_archive_smoke` (14 assertions). No real save file exists
      to check against -- saves are created at runtime on the device and
      none ships on the install image -- so the test asserts the encoding
      **field by field against the decompiled writer** (the NUL-inclusive
      name length, the `+4` offset bias the reader applies, the size
      patch) plus the two corruption cases the real open path checks,
      rather than only round-tripping with itself. 35/35 smoke tests pass.
    - The whole script-facing API is mapped too (`SaveGame`/
      `ActuallySaveGame`/`LoadGame`/`GetSaveSlot`/`GameAvailableForLoad`/
      `DeleteGame`/... on the GameEngine root, plus Level's `AutoSave`/
      `SaveLevelState`/`RestoreSaveLevel`), including where the current
      slot lives (`engine+0x14a71`) and the in-memory slot-name table
      (`engine+0xdf74`, 8 bytes per slot).
    - **Not attempted**: what is *inside* each member. This is the
      envelope, not the letter -- the serialization of player stats,
      inventory, quest flags and per-level entity state is a separate and
      larger job, and M5's in-memory slots are untouched by this
      milestone.

- [x] **M41 -- the real per-frame visible-tile set.** M6's fixed-radius
  square tile scan was the last placeholder left in the wall/surface
  pipeline; `TileGrid_RaycastVisibility` (`FUN_1000f694`) replaces it.
    - **A fan of rays, marching one tile per step**, from the camera, each
      appending every tile it crosses to a set de-duplicated by a per-cell
      frame stamp -- and stopping at walls, which is the whole point:
      standing inside azra's own starting building the set is **62 tiles
      instead of the old scan's 41x41 = 1681**, and none of them are on
      the far side of a wall.
    - **Correction: the three tiers are a zoom setting, not a quality
      knob.** `engine+0x608` selects (150 rays, 25 steps, angle step 170) /
      (177, 93, 96) / (178, 172, 52). Multiplying rays x angleStep in the
      engine's own 65536-per-turn angle unit gives each fan's total arc:
      **140 / 93 / 51 degrees**. So the tiers narrow the field of view
      while pushing the range out -- which is what a zoom factor does and
      not what a detail level would, and `engine+0x608` is the same field
      used elsewhere as a `0x100`-is-identity render scale. The boundary is
      `< 0x101`, so identity itself takes the *widest* tier.
    - Two details a from-scratch version would not have had: **every ray
      starts four steps behind the camera** (which is the only reason the
      tile the player is standing on, and the ones just behind, are in the
      set at all -- the fan never points backwards), and **walls are
      ignored for the first five steps**, with the stop test being
      `(flags & 0b1010) == 0b0010` -- so a cell with bit3 (force-draw) set
      does not block even though it is a wall, the same bit the face
      pipeline already honours.
    - New `m41_visibility_smoke` (16 assertions against real azra data,
      including all three tiers' constants and arcs); a rendered frame
      checked by eye as well. 36/36 smoke tests pass.
    - `docs/WORLD_MODEL.md`'s raycaster section updated with the tier
      table, the zoom correction, and both details above.

- [x] **M42 -- the screen-mode question, and a correction it turned up.**
  An RE-only milestone: no port behaviour changes, but three roadmap
  items move.
    - **Three of the four `FUN_1002c010` gating modes are identified.**
      `FUN_1006a344` is `SetScreenMode(controller, mode)`, writing
      `this+0x78`; walking its 13 call sites gives:
      **1** = normal gameplay (set by `GameEngine_InitLevel` when a level
      finishes loading, and by the menu stack on Quit), **3** = a zone /
      level change (`FUN_1006c31c`, the level-change entry point),
      **4** = **saving** (see below), **5** = a menu/UI mode, and
      **10** = loading a saved game (`LoadGame`'s own path). That matches
      M26's note that modes 3 and 10 additionally draw the "Travel to:
      `<zone>`" banner: those are exactly the two that travel.
      **0x1f is still unidentified** -- but the reason it resisted a
      literal-argument search is now known: the screen fade
      (`FUN_1004fc50`) calls `SetScreenMode` with a *deferred* target
      stored at `engine+0x5ac`, so that mode need never appear as a
      literal at any call site.
      > **Closed by M54** (below): `0x1f` is "quit to the main menu", and
      > the deferred-fade theory was the wrong lead. `FUN_10026f40` writes
      > the value directly into `controller+0x78` as it spawns the
      > `nGEN_Quitting` thread, so it is not an argument to anything.
    - **Correction: `FUN_10018e70` is the save-game writer, not a
      crash/error display.** `docs/WORLD_MODEL.md` had it as the latter
      since the first pass over the `0x1006Bxxx` cluster, and that made
      `FUN_1006c31c` read as "load level or show an error dialog". It is
      really "**autosave `current.sav`, and abort the level change if that
      fails**" -- which is why per-level `<level>.dat` members exist at
      all. The confusion is understandable: the writer does return a
      non-zero code on failure, but the code is `3`, "not enough free disk
      space". Full writeup in `SAVE_FORMAT.md`, including the two members
      it writes (`character.dat` and `<level>.dat`), the add-vs-replace
      pair (`FUN_1000a21c`/`FUN_1000a9f4`), the disk-space check, the
      literal 3/10/30/50/70 progress percentages it feeds mode 4's bar --
      and one nice behavioural detail: it swaps the player's live position
      for a stored entry point before serializing and back afterwards, so
      a save records where you came in, not where you are standing.
    - **`SetAttachedWeapon` is stored and unused in the shipped engine
      too.** It writes `monster+0x304`; an exhaustive scan for `LDR`s of
      that offset across the whole image finds exactly one function, and
      that one is mesh/skeleton code operating on an unrelated struct. So
      the port storing it and never drawing a second model is *faithful*,
      not a gap -- recorded as an exhaustive-search result the same way
      M25's swing trigger was.
      **WITHDRAWN by M43**: `monster+0x304` is `SetMeleeRoll`'s field, not
      this one, and it does have a consumer -- the scan missed it because
      the read is an `LDRSH`. See M43's correction; `SetAttachedWeapon` is
      back on the open list.
    - **Still open**: `AnimationClip::rate`'s units. Frames per second
      remains the working reading and still has no traced consumer.

- **M43 -- monsters as spell casters, and the four things that turned out
  to be behind that.** The last implementable item on the "Next milestones"
  list. M37 had recovered the real caster stat and the magic hit gate but
  read both off the *player*, and Absorb's heal always went to the player
  too -- correct only for as long as nothing else could cast. 32 shipped
  creature scripts call `AddSpell`. Full RE writeup in
  `docs/WORLD_MODEL.md`'s "Creature casting, `SetMob`, and the melee
  model"; smoke test `src/tests/m43_monster_caster_smoke.cpp` (33 checks
  against real shipped scripts).

  It did not stay a one-function milestone. Each of the five steps was
  forced by the one before it.

    - **`FUN_1002fd30`, and the two vtable predicates it pins.** Three
      lines: `vtable+0xe4` true means "a monster", with its stats block at
      `actor+0x224`; `vtable+0xcc` true means "the player", block at
      `actor+0x3ac`. **A monster and the player own the same stats-block
      layout**, and every status primitive in the engine (`FUN_1004aa28`,
      `FUN_1004bae8`, `FUN_1004bb88`, `FUN_10049780`, `FUN_1004bc60`,
      `FUN_1004bbd0`) takes *that block*, never the actor. The status
      dispatcher's first two lines resolve caster and target through it
      alike, which is what makes the whole system symmetric.

      Reflected in the port by `simkin_bindings/actor_stats.h` (the block,
      with the timed modifiers, effect flags, both periodic channels and
      the paralysis lockout that had lived on `MonsterExecutable` since
      M33) and `spell_actor.h` (the two predicates, plus
      `ResolveSpellActor()` = `FUN_1002fd30`). `MonsterExecutable` and
      `PlayerExecutable` implement it; the old accessors stayed as
      forwarders, so nothing that already used them changed.

    - **Three branch guards were being read backwards.** With the
      predicates pinned: **Blind**'s flag lands on the *player only* (a
      blinded creature takes the two -10s and nothing more -- this port had
      it exactly inverted, with a note reasoning that it did not matter
      because only creatures could be targets; both halves were wrong).
      **Fear** works on a *creature only*. **DeadToDust** spares a living
      *creature* but not the player.

    - **The caster is `spell+0x170`, the spell's own owner.** The magnitude
      rule's first arm is guarded by `owner->vtable[0xcc]()`, i.e. it only
      ever means *the player's* level -- a creature-cast spell always
      scales with the spell's own `SetLevel`. That is why every shipped
      caster script pairs each `AddSpell` with an explicit
      `Item.SetLevel(n)` that differs from the creature's own level.

    - **`AddSpell` / `SetMeleeRoll` / `FUN_1008457c` / `FUN_100835b8` --
      the creature's actual loadout and attack choice.** A fixed four-slot
      `{u8 chance, Spell*}` table at `monster+0x30c`, cumulative
      `rand(0,100)` thresholds (which the scripts' own comments confirm:
      `tunnel_wight.s` adds three at 30/70/100 and annotates them "30%",
      "40% of the time", "30%"), a cached Blind slot at `+0x32c` so a
      caster never re-blinds an already-blind target, and a melee-vs-cast
      branch whose direction is the reverse of its name -- a **higher**
      `SetMeleeRoll` means **less** melee, and the default 0 means never
      melee at all, which is exactly what the eight mage scripts that add
      spells and never call it rely on.

    - **`SetMob` is a stat template, not a flag -- and it is a
      prerequisite for creature casting.** M39 read the handler's first
      line (`monster+0x2ef = 1`, the zone-XP opt-in) and stopped. The rest
      is a nested four-tier `switch` that **overwrites most of what the
      script just set by hand**, scaled by `FUN_1008467c`, a 21-entry
      zone-name-to-difficulty table (in the game's own progression order,
      azra 1 ... crypt3 21). Full table of the four templates in
      `WORLD_MODEL.md`.

      The tell is exact: the magic gate is `spellcast + 2 * willpower` and
      returns 0 when that is `<= 0`, so a `SetSpellcast(0)` creature with
      no willpower could never land a spell. **21 of the 32 caster scripts
      call `SetSpellcast(0)`. All 21 call `SetMob`. All 11 that do not call
      `SetMob` set a real spellcast instead.** No exceptions in either
      direction across the whole corpus. What `SetMob` gives them is a
      willpower.

      The consequence is broad: a creature's script literals are mostly
      dead weight. `monsters/Azra_Rat.s` writes `SetAttack(3)`,
      `SetDefense(4)`, `SetMaxHealth(12)` and then `SetMob(4)` -- in azra
      it actually fights with attack 8, defense 62 and 23 hit points.
      `m12_combat_smoke` used to assert those literals and now asserts what
      overrides them.

    - **...which forced the melee model.** `combat.h` had carried a
      from-scratch `clamp(50 + (attack - defense) * 5, 10, 95)` since the
      combat slice, on the explicit grounds that no ground truth existed.
      A real defense of 62 makes that formula nonsense, so the real one had
      to be found, and it is two functions. `FUN_1004b620`:
      `chance = attack * 256 / (attack + defense)`, rolled as
      `rand % 0x100 <= chance` -- the same ratio model as the magic gate,
      with a zero-defense case that is its exact opposite (capped at half,
      not made certain). Plus the damage tail of `FUN_100835b8`:
      `damageMin + rand % max(1, damageMax - damageMin) - fullArmourRating`,
      exclusive at the top, and a fully-absorbed hit deals literally
      nothing. Both `RollDamage`'s "deliberately from-scratch" note and
      `WORLD_MODEL.md`'s "the actor combat system was never traced" are
      retired for melee.

      One knock-on: the player's `baseAttack`/`baseDefense` placeholders
      went from 10 to 50, to sit on the same scale as the rest of that
      placeholder block (every attribute there is 50) and as the creature
      side, which stopped being a placeholder this milestone. Still a
      documented placeholder, not a recovered value.

    - **Two more status branches, found by asking what creatures cast.**
      4008 `spells\Weakness.s` (-10 attack for `magnitude + 8`, byte-for-byte
      identical to Drain) and 4021 `spells\FeebleBlade.s` (`magnitude + 5`).
      Neither is in the player's spell list, which is why nine passes over
      this dispatcher went by without them.

    - **Correction: Paralyze's duration is `(magnitude + 4)` seconds**, read
      straight off the branch. The port had `magnitude * 5`, which its own
      comment admitted was a shape rather than a recovered value -- the
      difference between a raider's `SetLevel(5)` Paralyze holding the
      player for 9 seconds and for 25.

    - **Correction: `monster+0x304` is `SetMeleeRoll`, not
      `SetAttachedWeapon`.** M39 recorded "`SetAttachedWeapon` writes
      `monster+0x304`, and an exhaustive scan for that offset finds no real
      consumer, so storing and ignoring it is faithful". The offset belongs
      to dispatcher case 9 = `SetMeleeRoll` (binding index 9; cases 0, 2, 6,
      8, 9 and 10 all map 1:1 to their binding names, each confirmed by what
      they write), and it has a very real consumer in `FUN_100835b8`'s
      melee-vs-spell branch. The scan missed it because
      `pyghidra_find_reads.py` matches `LDR` with an immediate offset and
      the read is an `LDRSH`. `SetAttachedWeapon`'s own field is back to
      unknown -- see "Next milestones".

    - **The player can now be a spell target at all**, which is the half
      that had no representation before: `PlayerExecutable` gained the
      shared stats block, a per-tick `TickStatusEffects()` driven from
      main.cpp's game tick alongside every creature's `TickAi()`, and
      `attack()`/`defense()`/`armorRating()` that fold in live modifiers.
      A creature's Poison, Disease, Drain, Weakness, FeebleBlade, Blind,
      HarmArmor, Paralyze and IgniteFoe all do something to the player now
      instead of landing and vanishing.

    - **Also implemented:** `SetPlaySpellCasting` (`monster+0x2bd`, the
      cast sound), and `statusEffect()` now resolves a spell by its real
      entity typeId (`templateId()`) before falling back to the script
      path -- creature spells all arrive through `Level.CreateEntity`, and
      resolving them by filename silently mis-read a creature's blaze
      (typeId 50) and DoomHammer (4017) as their alternate variants.

- **M44 -- the `Zone/Level` global object: named regions, and what M38
  left.** Closes the whole three-part bullet. RE writeups in
  `docs/ZONE_FORMAT.md` ("`.zon` is the named-region list") and
  `docs/WORLD_MODEL.md` ("Entering a named region, and the zone effects");
  smoke test `src/tests/m44_zone_region_smoke.cpp` (29 checks).

    - **Where a named zone region lives: `<zone>.zon`.** The file this
      project decoded byte-for-byte three milestones ago without
      recognising what it was *for*. `ZONE_FORMAT.md` had the record --
      `u16 count` then 72-byte `{u16 a,b,c,d; char name[64]}` -- and
      guessed that two of the four fields "index into some zone-wide array
      sized by zmpTotal (portal/neighbor list? texture-atlas range? light
      index?)". They are **coordinates in a flipped axis**: the record is
      an axis-aligned tile rectangle, and `b`/`d` are stored as
      `gridHeight - y`.

      The find itself was embarrassingly direct once the right question
      was asked -- `grep -rl battle41` over the install tree returns
      `lothcav.s` and `lothcav.zon`. What took the work was proving the
      rectangle reading, and three independent things do:
      azra's region named `start` decodes to x 118..121, y 42..46 and
      azra's `.ent` player-start tile is (118, 46); every record in all 21
      zones has `x0 < x1` and `y0 < y1` after the flip; and
      `LockZone`/`UnlockZone` iterate the pair of ranges straight over the
      cell grid, which only type-checks as a rectangle.

      With that, `EnterZone(name)` works: main.cpp diffs the set of
      regions containing the player once per tick and fires the zone-root
      script's own handler on entry. The end-to-end test is crypt1 --
      whose spawn tile sits inside one of its two regions named `vig`, and
      whose script says `if (zone = "vig") Level.Vignette(1)`.

    - **Inclusive containment, half-open iteration -- and the data that
      settles it.** `LockZone`/`UnlockZone` run `for (x = x0; x < x1)`
      loops, so locking a region misses its last row and column.
      Containment is inclusive, though: across all 21 zones, 12 spawn
      tiles land inside a region, every one of those regions is named like
      an arrival point (`start`, `entry`, `Entrance`, `Enter`, `Exit`,
      `Zvoldoor`, `vig`), and four of the twelve hold *only* under
      inclusive bounds because the spawn sits exactly on the far edge.
      Both conventions reproduced as found rather than reconciled.

    - **`LockZone`/`UnlockZone` are asymmetric, and it is observable.**
      Lock goes through the name-to-room map (one rectangle) and
      **assigns** 4 to each cell's second byte; Unlock walks all 40 room
      slots with `strcmp` and clears bit 2 on **every** rectangle sharing
      the name. Names are not unique -- azra has four called `YouSure` --
      so locking blocks one doorway and unlocking opens all four. The
      second byte of a `.zmp` cell had no known meaning and was skipped by
      this port's loader entirely; it is the block flag, and a locked tile
      now stops movement (30-odd real `UnlockZone("swdoor")`-shaped call
      sites, all gating a barrier behind a key or a lever).

    - **Correction: `level+0x44c` is the encounter list, not the region
      list.** The bullet this milestone closes said both `EnterZone`'s
      tags and the spawner's regions "are looked up the same way --
      `FUN_100731b4` against a list at `level+0x44c`". They are looked up
      the same way, but that is not the list: `FUN_100731b4` searches a
      wide-string binary tree at `levelObj+0x84` built from `.zon` at load
      time, while `level+0x44c` holds registered `Encounter` objects.
      `FUN_1002ef44` walks the encounter list *first*, and a region an
      encounter claims never reaches the script at all.

    - **The per-level tile-change journal.** Every Lock/Unlock routes
      through `FUN_1006d7bc`, a fixed 100-entry array at `level+0x100` of
      `{i16 x, i16 y, u16 savedByte}` that overwrites an existing entry
      for the same tile rather than appending. That is exactly "the cells
      this level has diverged from its `.zmp` in". Implemented and
      asserted.

      > **Correction (M50).** This bullet used to call the journal "save
      > state" and "a concrete piece of what is inside a `<level>.dat`".
      > It is neither: `<level>.dat` contains no tile data at all. The
      > divergence is restored by re-running each saved entity's `Init`
      > with its script variables already loaded, which re-applies the
      > `UnlockZone` that made the change. The array is live state, not
      > save state.

    - **`CreateEntity`'s non-item category.** The four-argument form,
      `Level.CreateEntity(274, x, y, z)` in crypt1.s's own
      `EnterZone("UmbraHere")` branch, creates a **creature** -- the
      category the one-argument loot-bag form refuses. Implemented: the
      script object is built and returned synchronously (crypt1.s
      null-checks it and immediately calls `SetCanTeleport(true)`), and
      the world instance is created by the host on its next tick.

    - **Zone effects: two of the six matter.** `LightRect(x0,y0,x1,y1,n)`
      overwrites the baked light of a half-open tile rectangle with
      `n << 8` -- 8 call sites, all passing 64, lighting one alcove per
      puzzle step in crypt2 and the arena umbra_keth appears in.
      `Vignette(n)` turns out to be a **screen mode**, not an effect: a
      full-screen slideshow that stops the player, advances one slide per
      7 seconds and skips on any key. The table of all six (sprite runs
      and caption ids) is recovered from `FUN_1002bc6c`'s switch and
      implemented as a real screen.

      The other four -- `SpawnWithinRadius`, `TickZones`,
      `AddInterestPoint`, `ClearInterestPoints` -- have **zero call sites
      anywhere in the shipped corpus**, so they keep soft-failing. Same
      treatment `SetClipSize`/`SetFireRate`/`SetReloadFrames` already get.

    - **Two engine-hardcoded region names, both dead.** `FUN_10070534`
      strcmp's the entered region against `"Minefield"` -- which calls a
      function that is **empty** in the shipped build -- and strcasecmp's
      it against `"FinalEscape"`, discarding the result. Neither name
      appears in any `.zon`.

    - **Faithfully not implemented: the region-entry trigger walk.**
      `FUN_1002ef44`'s third step selects triggers with flag 8 whose name
      matches the region, then calls the fire routine as
      `FUN_10090818(trigger, 1, *(engine+0x618))` -- mode 1 with the
      **world object** as the notifying entity. Mode 1 requires
      `entity == trigger->doorObject` and the trap branch requires an
      AddEntity match; the world object is neither, for any trigger any
      shipped script builds. The walk selects triggers it then cannot
      fire. Recorded rather than reproduced.

    - **Narrows the `0x1f` bullet.** `FUN_1001b788` is the fade-arming
      function M42 was hunting -- it writes the deferred `SetScreenMode`
      target at `engine+0x5ac`. Its complete call-site set is `1`, `5`,
      `0xc`, `0x20`, plus the attract slideshow's `1..0x10` run and the
      vignette's `0x20..0x24` run. **0x1f is in none of them**, so that
      mode is not any fade target; the vignette run starting one above it
      is the nearest thing to a lead.
      > **Closed by M54** (below). The negative result held -- `0x1f` is
      > not a fade target -- but the vignette lead was a false one: the
      > run starting one above it is `0x1f`'s *neighbour*, not its
      > relative, and the real dispatch (`mode < 0x25 && 0x1f < mode`)
      > excludes `0x1f` by exactly one.

    - **Still not implemented: encounter spawning.** The Encounter object
      model (sets, per-region limits, respawn) has been in place since
      M38, and regions now resolve to real rectangles -- but placing
      creatures inside one needs a free-tile search this port does not
      have. `EnterRegion()` returns the matching encounter and main.cpp
      logs it rather than silently dropping it, so the gap is visible in
      the log. See "Next milestones".

- **M45 -- encounter spawning.** The last piece of M38's `Zone/Level`
  bullet. RE writeup in `docs/WORLD_MODEL.md` ("The encounter spawner");
  smoke test `src/tests/m45_encounter_spawn_smoke.cpp` (32 checks).

    - **`FUN_1008a76c`, transcribed.** The gate
      (`(live < 1 || respawnSeconds != 0) && active && live < limit`), the
      set draw (`setCount == 1 ? 0 : rand() % setCount` -- the real code
      skips the RNG entirely for one set), the per-entry `count` loop, and
      the early-out when a region reaches its limit. Two details preserved
      because they are load-bearing rather than tidy: the live count is
      incremented **before** the placement search, so a creature that
      cannot be placed still consumes a slot; and with `respawnSeconds` at
      its default 0 -- every encounter in the game -- a region will not top
      itself up while anything it spawned is alive.

    - **`FUN_1008ae94`, the placement search**, now
      `Zone::FindFreeTileInRegion()`. Draws a random tile from the
      region's rectangle, inclusive at both ends, and retries at most
      `x1 - x0` times -- the region's *width*, which has nothing to do with
      its area, so a long thin region gets very few tries. The inclusive
      draw is independent corroboration of M44's inclusive containment,
      arrived at from a different function.

      The occupancy test reads `engine+0x6904`, a **per-tile array of
      4-byte entity pointers** distinct from the 8-byte cell array at
      `+0x6908` -- the first use found for that array. This port has no
      equivalent and scans its live creatures instead; wall and locked
      tiles are additionally rejected, which is a port safety property and
      is marked as one (the real grid holds no entity on a wall tile, so
      the engine would happily spawn inside one).

    - **The live-count backlink.** A spawned creature carries its region
      at `actor+0x2e4`, and the death routine (`FUN_10083c04`) calls
      `FUN_1008b118(actor->region)`, a one-line `live--`. That is what
      lets a cleared region spawn again, and it is wired through
      `MonsterInstance::encounter` on both of main.cpp's death paths.

    - **Defaults recovered from the constructors.** An encounter is
      **active** by default (`FUN_1008b074`) and a region starts live 0,
      limit **99** (`FUN_1008b000`).

    - **What the shipped game does with all of it is almost nothing**, and
      establishing that was most of the value. Three `AddEncounters` call
      sites exist. **Both of ghstpass's name six regions that do not exist
      in `ghstpass.zon`** -- so those two encounters and the ten
      `AddRandomSets` calls behind them are dead. The third, lothcav's, is
      a quest reward rather than an ambush: built dormant
      (`SetActive(false)`), capped at one creature (`SetLimit(0,1)` -- the
      corpus's only call), and switched on only by the pilgrim's-remains
      menu, behind a `saved_madewight` flag. **One Tunnel Wight, one time,
      in the whole game.** Asserted directly, since getting it wrong would
      mean inventing behaviour for six regions that cannot fire.

    - **Faithfully not implemented: the respawn tick.** `FUN_1008ae00`
      exists and is decompiled, but the level tick only calls it when
      `level+0x459` is set, and that flag is written by exactly one thing
      -- the `TickZones(bool)` binding, which no shipped script calls.
      Recorded rather than reproduced, along with a units mismatch nobody
      can observe: the timer accumulates the engine's per-frame delta (256
      units to the second) and is compared against `SetRespawnSeconds`'s
      argument **raw**, so `SetRespawnSeconds(60)` would fire after 60/256
      of a second.

    - **Correction to M44's own entry**: it said `SetLimit` is never
      called. `lothcav.s` calls it once. `SetRespawnSeconds` and
      `TickZones` really do have zero call sites.

- **M46 -- what `SetAttachedWeapon` actually writes.** The roadmap's last
  "curiosity" item, which turned out to be neither unknown nor unused. RE
  writeup in `docs/WORLD_MODEL.md` ("The attached weapon"); smoke test
  `src/tests/m46_attached_weapon_smoke.cpp` (45 checks).

    - **The field is `monster+0x2c2`**, a signed 16-bit `models.idx`
      archive index, initialised to `-1` by the constructor
      (`FUN_100815e0`). Dispatcher case `0xc`, one line. It stayed unknown
      because every consumer loads it with `LDRSH` and
      `pyghidra_find_reads.py` only matches `LDR` with an immediate offset
      -- the exact blind spot that produced M39's wrong answer. Grepping
      the decompiled corpus instead finds all five sites in one pass.

    - **Both of the roadmap entry's claims were wrong.** It said the field
      was unknown (it is one grep away) and that the corpus had **zero
      call sites**. The corpus has **92, across 86 creature scripts** --
      making this one of the most widely used monster bindings in the
      game, not a curiosity. The bullet was written from a grep that
      missed them.

    - **It is drawn as a second whole model welded to the body.**
      `FUN_10083490` is a monster-class render override: if the field is
      not `-1` it looks the model up in the `engine+0x6b38` cache, copies
      the actor's position, orientation, scale (`+0x5e`) and **entire
      animation block including the absolute frame index (`+0x70`)**, submits
      it, and only then falls through to the ordinary actor draw for the
      body.

      That works because the weapon models are **frame-aligned exports of
      the humanoid rig**: all five carry 144 frames and a byte-identical
      11-clip table to `male_long_tunic` / `male_short_tunic` /
      `female_long_tunic` / `female_short_tunic` / `delfran`, and those
      eleven (plus a duplicate dagger at slot 25) are the *only* 144-frame
      entries in the 226-entry archive. Asserted directly against
      `models.huge`, with `rat.bin` (57 frames, 4 clips) as the negative
      control. Implemented in `main.cpp` by pushing a second
      `PlacedEntity` that reuses the body's frame index rather than
      re-running the clip lookup, which is what the engine does.

    - **The five values are the five weapon models**: 222 `sword.bin`,
      223 `mace.bin`, 224 `dagger.bin`, 225 `bow.bin`, 226 `ax.bin` --
      at the same archive indices in **all 21 real zones'**
      `<zone>_models.txt` (only `menu_models.txt` leaves them NULL). 86
      creature scripts were previously drawn empty-handed.

    - **225 is hardcoded in the AI.** `FUN_10082224`'s ranged test is
      `equippedIsRanged || spellSlot0 != 0 || attachedWeapon == 225`, and
      a ranged creature skips both the facing-cone test and the melee
      reach/LOS raycast. The corpus corroborates the clause exactly: all
      24 scripts passing 225 are archers and no script passing any other
      value is. Exposed as `MonsterExecutable::ranged()`.

      **Deliberately changes no behaviour in this port's tick.** The port's
      attack gate already has neither a facing cone (it turns to face the
      player immediately before swinging) nor an attack-time raycast --
      that second omission is a stated, reasoned choice recorded at the
      gate itself, because reinstating it makes creatures in doorways walk
      into the player instead of stopping to swing. So every creature here
      already behaves like a ranged one, and wiring `ranged()` in would
      mean *adding* the raycast for melee creatures, i.e. reversing that
      decision. Recorded rather than faked.

      The equipped-weapon clause is also not reproduced: it reads an
      inventory item from the stats block's `+0x48`, which this port's
      monsters never carry.

    - **The field is replicated over the wire.** `FUN_1003c1c0` has no
      direct callers; its address appears once in the binary, as one slot
      of a 48-entry handler table at `0x100fdf34` whose every entry lies
      in the `0x10038000`-`0x1003c000` networking block. It sets another
      entity's attached weapon by entity id, gated by the same
      `engine+0x5c0` / `engine+0x5cc` / `engine+0x5d0` trio the attack
      path and `ReplicateTeleport` use. Nothing about multiplayer is
      implemented here; this is recorded because it is the third consumer
      and it identifies that table.

    - **Corrections to two older docs.** `RENDERER_3D.md` and
      `ZONE_FORMAT.md` both said `actor+0x2c2` was the entity's *own*
      model archive index, set at `<zone>.ent` placement time from the
      type descriptor. It is the attached weapon's index, set only by this
      binding. The descriptor chain those docs trace (descriptor `+0xc` ->
      `engine+0x6b38[index]` -> the object's model pointer at
      `actor+0x54`) is unaffected -- only the field it was attributed to
      was wrong. Both now carry the correction inline.

    - **Left open:** `actor+0x2d4`, the submit call's third argument,
      turns out not to be a skin index but the actor's slot in a registry
      at `engine+0x14620`. What `Poly3D_ClipAndDispatch` does with it (and
      with the mode byte that differs between the weapon's draw and the
      body's, `0` vs `2`) is not chased here.

- **M47 -- the real weapon-swing trigger.** M25's own open item, recorded
  there as an exhaustive search that came up empty. RE writeup in
  `docs/WORLD_MODEL.md` ("The weapon swing"); smoke test
  `src/tests/m47_weapon_swing_smoke.cpp` (36 checks).

    - **The search was exhaustive in the wrong direction.** M25 traced
      outward from the Weapon class dispatcher. The state is on the
      **player object** (`engine+0x618`), so nothing reachable that way
      touches it. Grepping the decompiled corpus for the *write text*
      (`pyghidra_grep_decompiled.py "0x234) ="`) -- M46's technique --
      finds every writer of all five fields in one pass over 2006
      functions. Both new gap-closing passes this session came from the
      same lesson: search for the write, not for a path to it.

    - **`FUN_100425bc(player)` is the player's attack function**, and the
      primary trigger. It is dispatched virtually, so a callers-of search
      finds nothing either. Besides starting the swing it carries the
      player's own **attack cadence** (`+0xf48`, reloaded from the
      character's attack-speed stat and multiplied by **6** for a ranged
      weapon), the swing's **4-point fatigue cost**, and the gate: no
      swing while a weapon swap or another swing is running. Two more
      entries share the swing half -- `FUN_10042394` (use item / cast) and
      player vtable **+0x190** (`FUN_1001d880`).

    - **Two of M25's field readings were wrong**, and are corrected here.
      `+0x230` is not an "alternate/swing sprite" and `+0x238` is not a
      "post-swing hold": they are the **previous weapon's sprite** and a
      **weapon-swap transition timer**, written by `FUN_1001d778` (player
      vtable **+0x194**). A swing has no hold phase at all -- it ends when
      its accumulator empties. This port's deliberately-interruptible Hold
      had no counterpart in the real machine and is gone.

    - **The payoff: a weapon owns 16 sprite slots, and melee weapons pick
      one of three swings at random.** The accumulator starts at
      `(SetAnimationFrames + 2) << 8` and the drawn slot is
      `base + ((frames + variant + 1)*0x100 - (acc - 0x200)) >> 8`, where
      `item+0x17f` is a per-swing random **variant** of 0 (half the time),
      10 (a quarter) or 5 (a quarter). For `weapons/club.s` that is exactly
      89-93 / 94-98 / 99-103 after the idle pose at 88 -- which is what the
      16-slot spacing between weapon bases (72/88/104/120/136) is for.
      Asserted frame by frame, and confirmed by decoding the sixteen real
      sprites: three visibly different swings (overhead, low sweep,
      backhand). A swing runs 32 ticks, about 1.3 seconds.

    - **The viewmodel is a full-screen overlay, and this port was drawing
      it wrong.** Every viewmodel slot in `global.spr` is a **176x208**
      frame and the real blit puts a swing frame at **(0,0)**. This port
      drew a small sprite in the bottom-right corner at a position its own
      comment flagged as "a documented, non-decompiled screen position".
      It now draws where the engine does.

    - **Reproduced rather than tidied.** The accumulator lands on exactly
      `0x200` on its final tick, which the `< 0x200` test lets through, so
      one extra frame one slot past the animation is drawn -- real, and one
      frame long. And a **ranged weapon starts two frames in**, because it
      skips the `+2`: `bandit_longbow.s` shows only 178 and 179, the last
      two of its five-slot strip. Whether that is a bug or a deliberate
      "no windup, straight to the release" is not something the code says,
      so it is left as found.

    - **The walk bob is now real**, including the engine's own sine table
      at `0x100f4954` (2048 int32 entries at stride 4, 8.8 fixed point,
      verified at five points against the real bytes and regenerated rather
      than embedded). x is a triangle wave 0..10px, y is `|sin| * 5 >> 7`,
      also 0..10px, and a swing or a swap resets the phase. The previous
      sway was an invented `sin(tick * 0.05) * 2`.

    - **Three things honestly not resolved**, each recorded at the code:
      the swap timer's decrement is 1 per call against seeds of
      0x800/0x400, which at 25Hz is 82 seconds and cannot be the intended
      duration (the port keeps the real seeds and picks its own rate); the
      `speed` argument's units in the bob (its call site was not
      identified, so the port's rate is its own constant); and the bob's
      y-index arithmetic, which evaluated literally gives a bob of
      essentially zero, so the port uses the straightforward reading of it.

    - **`SetReloadSpeed` (`item+0x184`) has zero call sites**, so it always
      holds its constructed value, which has to be 0x100 -- any other
      default would rescale or freeze every swing in the game. Inference
      from the game working, not a read of the constructor; the port takes
      the unscaled path. The wider Item class it belongs to
      (`SetNumClips`, `SetClipSize`, `SetIsAutomatic`, `SetHasZoom`,
      `SetScoped`) is a **leftover FPS weapon class** the engine was reused
      with.

    - **Pointer for the projectile bullet:** `FUN_100425bc`'s ranged
      branch spawns one via `FUN_10005730` with entity type id 598
      (thrown) or 599 (bow), after a five-pass target search and a
      `rand() % weapon->+0x1ce` roll. Recorded here, not implemented --
      and note M48, which closed the spell side, found the **arrow is a
      different class** (0x16c bytes, constructed by `FUN_10007da4`) from
      the spell projectile (0x198 bytes, `FUN_1005f0b4`). The two share
      the actor base's position/velocity layout and nothing else.
      **Closed by M49**, which also corrects two readings above: the
      `+0x1ce` roll is the arrow's *damage* (`SetDamageMax`), not a
      spread, and `+0x1a7` is set by `SetRange` exceeding `0x400`, not by
      `SetBow`.

- **M48 -- the rest of a real cast, and the projectile it launches.** The
  roadmap's own "rest of a real cast (`FUN_10046764`)" bullet, closed. RE
  writeup in `docs/WORLD_MODEL.md` ("The rest of a real cast, and its
  projectile"); smoke test `src/tests/m48_spell_projectile_smoke.cpp` (65
  checks).

    - **What was actually wrong.** This port called `HitTarget()` straight
      at a target on both the player's cast and a creature's, which meant
      two things: every spell was **hitscan**, and the fourteen shipped
      spells with no `HitTarget` handler at all -- Energize, Sanctuary,
      HealWound, BodyToMind, both cures, Shield, Frenzy, Righteousness,
      RaiseStrength, RemoveEnchantment, DaedricWeapon, AzraWrath,
      AzraSustenance -- **did nothing whatsoever**. Half the spell list was
      inert.

    - **`FUN_10046764` is a 200-line branch tree over the spell's
      entities.txt typeId** producing four things: a magicka cost (almost
      always `casterLevel + <per-spell bonus>`), a sound slot, whether to
      spawn a projectile, and -- for the spells that spawn none -- a
      self-targeted effect applied to the caster inline. The whole table is
      in `WORLD_MODEL.md` and in `simkin_bindings/spell_cast.cpp`.

    - **The table is verified three independent ways, and all three
      agree.** Reading a Ghidra branch tree that dense is exactly the kind
      of thing that goes wrong silently, so: (1) **the `HitTarget` split is
      exact** -- fifteen shipped spell scripts define a `HitTarget[...]`
      handler and fourteen do not, and the fifteen are precisely the fifteen
      this table gives a projectile, with one explained exception
      (`AzraWrath`, whose damage is a native area effect); (2) **the sound
      slots name themselves** -- `0x54` is `pl_cast_fire.wav` and `0x57` is
      `pl_cast_powerup.wav` in all 21 shipped `*_sounds.txt`, and the table
      hands 0x54 to the offensive spells and 0x57 to the buffs and cures;
      (3) **three rows are stated outright by scripts** that call
      `SetSpellType(51)`, `SetSpellType(4022)` and `SetSpellType(50)`. The
      smoke test's part 1 re-derives all three from the shipped files
      rather than restating the port's own numbers.

    - **The projectile is the mechanism, and `blaze.s` proves it.**
      `FUN_1005f0b4` spawns a 0x198-byte entity at the caster aimed along
      their facing; `FUN_1005f928` flies it at **0x100 units a tick --
      exactly one tile -- for twelve ticks**, which is the only range a
      spell has (there is no `SetRange` on the spell side at all); and
      `FUN_1005f3c8`, the impact, **calls the spell script's own
      `HitTarget(target)`** through the UTF-16 literal at `0x100b105c`.
      That is why `blaze.s` has a `HitTarget[ (target) { DoAttackRoll(
      target, 1); } ]` in the first place. The clincher is the line
      immediately below it, commented out:
      `//Level.CreateEffect( 12, 19, 1, target.GetPositionX(), ... )` --
      against `FUN_1005f928`'s own tail, which for typeId 50 or 4006 calls
      `FUN_10073528(level, 0xc, 0x13, 1, x, y, z, 0x80, 0x10, 0x80, 0x80,
      0)`. The same call with the same twelve arguments, moved into the
      engine and hardcoded to blaze. The author commented theirs out
      because the native projectile had taken it over.

    - **The two periodic-channel arms `WORLD_MODEL.md` recorded as "set
      somewhere else, not yet found" are here.** `spells\Energize.s` arms
      kind 6 and `spells\AzraSustenance.s` arms kind 7, and both add the
      caster's own level once a second. **Correction while passing: kind 6
      is fatigue, not magicka** -- `+0x2c` is clamped by `FUN_1004bb54`
      against `+0x26` while magicka is `+0x2e` clamped by `FUN_1004bb20`
      against `+0x28`, and M47 landed on the same field independently when
      it found a swing costing 4 points of `player+0x3d8`. A fourth kind,
      4, turns out to be Sanctuary's and to have **no tick behaviour at
      all**; it is a bare duration that gates the weapon-swap branch of
      `FUN_10042394` and stamps its own re-cast cooldown when it expires.

    - **`SetRefireRate` (`+0x1d2`) is the cast cooldown, and it is a
      Sanctuary-only rule.** `FUN_10046680` gates only the player, against
      a timestamp on the player object plus the spell's own refire rate --
      and **exactly one shipped script sets one**: `spells\Sanctuary.s`'s
      `SetRefireRate(768); //3 secs`, whose comment is an independent
      confirmation of the `seconds * 0x100` unit this port already uses
      everywhere. Every other spell leaves it 0, which makes the gate
      inert. Its two clauses are asymmetric in a way that is real: only the
      general one is guarded on "something has actually been cast", so for
      the first three seconds of a level Sanctuary alone is refused.

    - **Three quirks reproduced rather than tidied.** The periodic
      duration store is a **16-bit** one, so Energize's `magnitude * 0xa00`
      wraps negative at magnitude 13 and simply never runs for a caster
      that high. Every periodic arm writes `+0x76 = 1`, and that is the
      field poison and the burn *share* -- so casting Energize while
      poisoned really does drop the poison from 3 points a second to 1, the
      same shared-field quirk M34 found from the other direction. And kind
      6 has **no clamp**, so fatigue really can run past its own maximum.

    - **What changes in the port.** Casting no longer needs a target, and
      the invented `kSpellRange` targeting cone is gone -- the cast fires
      down the caster's facing and the flight finds something, or does not.
      A creature's spell can now miss by the player stepping aside. Fifteen
      self-targeted spells do their real thing. A scroll is consumed by
      being read. `MonsterExecutable::CastSpellAt()` no longer touches its
      target at all.

    - **Not reproduced, and recorded at the code.** The projectile's art:
      `+0x134` is 2 for blaze/Blind/DoomHammer and 5 for the other twelve,
      and it is *not* a `models.txt` index (2 is `lantern.bin`, 5 is
      `sbarrel.bin`), so it selects from something unidentified -- the port
      simulates the projectile without drawing it. **M63 identified it: it
      is a `global.spr` slot** (both are real 32x32 sprites), though the
      port still does not draw it, for want of a billboard pass. Also
      left: the blaze impact effect (M63 implemented `Level.CreateEffect`,
      but nothing raises it from the cast path yet), the multiplayer mirror,
      HealWound's extra term for a player of class 7 (an undecompiled
      stats-block vtable slot), and the monster-only predicate at target
      vtable `+0x170` the impact sweep rejects on (the port uses "still
      alive", which is what the surrounding code means by it).

- **M49 -- the arrow.** M47's own "pointer for the projectile bullet",
  closed, and with it the last piece of ranged combat. RE writeup in
  `docs/WORLD_MODEL.md` ("The arrow"); smoke test
  `src/tests/m49_arrow_projectile_smoke.cpp` (58 checks).

    - **A different class from M48's, and a different flight model.** The
      arrow is 0x16c bytes (`FUN_10007da4` / `FUN_10005730` /
      `FUN_10007214`) against the spell projectile's 0x198
      (`FUN_1005f0b4` / `FUN_1005f928`). They share the actor base's
      position and velocity fields and nothing above them. Where a
      fireball flies one tile a tick for exactly twelve ticks and stops on
      a wall flag, an arrow flies **1.25 tiles a tick with no lifetime at
      all** -- there is no age counter in the class -- and is stopped by
      **floor height**.

    - **`SetBow` is not what decides a ranged attack; `SetRange` is.** The
      player's attack routine branches on `weapon+0x1a7`, and the Weapon
      dispatcher writes that field in exactly one place: `SetRange(v)`
      stores `v` and then sets `+0x1a7` **only when `v > 0x400`**. None of
      the three marker setters touches it. That is safe only because the
      corpus is bimodal, which this port already knew from the other
      direction (M20) -- and re-checked against the threshold, **all 16
      weapons with `SetRange(16384)` are precisely the 16 that call a
      marker, and all 65 with `SetRange(384)` call none**. M20's
      convenience proxy turns out to be literally the engine's rule.

    - **Which projectile is a second, separate byte, and the shipped data
      names both.** `+0x17a` (`SetBow`/`SetCrossbow`/`SetIsLaunched`)
      picks entities.txt **599**, otherwise **598** -- and
      `entities.txt` maps those to `models.idx` 175 and 176, which
      `models.txt` names **`arrow.bin`** and **`throw_dagger.bin`**. The 7
      bows and crossbows fall on one side and the 9 darts and throwing
      knives on the other. (The full 24-case Weapon dispatcher is mapped
      to its field offsets in `WORLD_MODEL.md`; three names share two
      bytes, and `SetAnimationFrames` and `SetReloadFrames` really do
      share `+0x180`.)

    - **An archer creature never melees.** `FUN_100835b8` puts its entire
      melee resolution inside `if (monster+0x2d8 == -1)`, so a creature
      that calls `SetProjectile` shoots *instead of* swinging. Twelve
      shipped scripts do, and they agree on everything: all twelve
      `SetAttackRange(12000)` (except `lakvan/deadeye.s`, 50000), all
      twelve `SetAttachedWeapon(225)` (`models.txt` 225 = `bow.bin`, the
      visible bow M46 draws), all twelve `SetAttackNoise(1)` -- which is
      `barch_firebow.wav` in 21 of 22 shipped sound tables and is the same
      slot the *player's* ranged branch plays as a bare literal -- and not
      one of them calls `GiveWeapon`, which is why the branch's weapon
      gate passes for them.

    - **`SetProjectile`'s argument is dead.** All twelve pass `175`,
      `models.txt`'s own index for `arrow.bin` -- but the spawn hardcodes
      the entity type to 599 and `FUN_10089820` resolves the *model* from
      that type through `entities.txt`. The script's number lands in
      `+0xc6`, a per-entity draw parameter the render override forwards
      into the rasterizer dispatch. So the call is load-bearing purely as
      "this creature shoots", and changing its value would not change the
      art.

    - **What stops an arrow is geometry, not a wall flag.** The four
      +-0x20 probes call `FUN_1008977c`, which is
      `z < FUN_1001beac(sector, tile, x, y, engine, z)` -- a **collision
      height**: the tile's floor, or, for a two-storey tile (the `.zmp`
      cell's *second* byte, bit 1) whose ceiling surface is solid and
      which the actor is already above, that ceiling. Both height lookups
      interpolate their four stored corners only when the cell's flags say
      so (bit 2 for floors, bit 6 for ceilings -- the same two bits the
      renderer already reads for the same purpose) and otherwise return
      the flat authored band value at `ZcpEntry+2`/`+4`.
      **Checked against real azra data**: all 725 wall-flagged tiles stop
      an eye-height shot -- and so do a further **1858 tiles that are not
      flagged as walls at all**, every one of which a wall-flag test (what
      the spell projectile uses) would fire straight through. What is left
      is a single connected space 13695 tiles across. Implemented as
      `Zone::CollisionFloorHeightAt()`.

    - **Two combat quirks that make a bow genuinely different from a
      sword.** Ranged damage is `rand() % damageMax` on *both* sides -- so
      the weapon's `SetDamageMin` is simply never consulted, and a 3..9 bow
      rolls 0..8, where melee goes through the real
      `RandomRange(min, max)`. And a ranged hit **does not subtract
      armour**: the melee path fetches the target's armour rating and
      subtracts it before calling DoDamage, while the arrow's two impact
      paths pass the rolled damage straight through.

    - **Reproduced rather than tidied.** A blocked probe reflects the
      arrow's velocity, pushes it out of the cell *and* despawns it, then
      carries on into the next probe -- so a shot can be dead and still
      hit something in the same tick. A missed roll in the general sweep
      also despawns the arrow and then keeps walking the same entity list,
      rolling again. The `-y` probe's "one frame back" tile lookup has its
      **x and y arguments transposed** (verified in the disassembly; the
      other three are correct), which only ever waives that probe near the
      grid edge. And the pitch term indexes the sine table at `>> 6` where
      every other consumer in the engine -- the spell projectile's
      structurally identical pitch term included -- uses `>> 5`, so this
      class reads elevation at half scale.

    - **The first projectile in this port that is actually visible.**
      M48's fireball is simulated without art, because its `+0x134`
      selector is still unidentified. An arrow's model is completely
      pinned down, so it is drawn -- a real `arrow.bin` or
      `throw_dagger.bin` in flight, pointed along its heading (which
      needed the inverse of M48's engine-to-port yaw conversion,
      `PortYawFromEngineYaw`).

    - **Also found, not applicable here.** The player's ranged branch sets
      its refire cooldown to `attackSpeed * 6` against the melee path's
      bare `attackSpeed` -- a bow is six times slower to ready. This port
      has no player attack-speed timer at all (one swing per keypress), so
      there is nothing to hang it on. And `FUN_1003c45c` is the
      multiplayer mirror, replaying a remote player's shot with zero
      damage and zero skill as pure art; not implemented, same as M48's.

    - **Two simplifications, recorded at the code.** The port's candidate
      list for both sweeps is actors only, so an ordinary world object
      cannot shadow a creature in the three-column sweep the way it can in
      the real engine. And the monster-side predicate at target vtable
      `+0x170` that the player's first-look pass rejects on is still
      unidentified -- "still alive" stands in for it, as everywhere else
      in this port.

- **M50 -- what is inside a save file's members.** M40's own open item,
  closed, and with it the whole save format. RE writeup in
  [`docs/SAVE_FORMAT.md`](SAVE_FORMAT.md) ("The members"); smoke test
  `src/tests/m50_save_records_smoke.cpp` (59 checks).

    - **One stream, one chain, thirteen functions.** Every member of a
      save file is written through a single 0x14-byte byte-stream class
      (`FUN_1008c06c`: a `new[] 0x40000` buffer, a write cursor at
      `+0xc`, a read cursor at `+0x10`, and no bounds check anywhere),
      by a `Save`/`Load` pair at **vtable slots `+0x130` and `+0x134`**.
      Thirty-two vtables carry that pair; between them they name thirteen
      distinct save functions, and those thirteen turn out to be a single
      inheritance chain --
      `Entity -> Drawable -> {Item -> {Stackable -> Wearable, Weapon},
      Spellbook -> InventoryHolder -> {Actor, LinkedActor, Character ->
      Player}}` -- plus one non-virtual leaf, the stats block, that Actor
      and Player each call on their own. `character.dat` is one Player
      record; `<level>.dat` is a tagged list of everything else.

    - **The stream's overload set collapses to seven primitives.**
      Several vtable slots are byte-for-byte identical code at different
      indices (four separate "write one byte" thunks, four "read one
      byte") -- distinct C++ overloads that compiled to the same body. Two
      details survive that: an i32 is written as **two i16 halves, low
      first**, which on a little-endian target is just a 32-bit store;
      and a **string's i16 length prefix does not count the NUL**, which
      is the exact opposite of the container's own TOC (M40). The two
      layers of the same file disagree, and both are reproduced.

    - **Nothing is version-tagged, and the typeId is load-bearing.**
      There is no magic, no field count and no per-record length: a
      record is exactly as long as the class that wrote it, so a reader
      that picks the wrong class desynchronises everything after it,
      silently. Which class to use comes from the **typeId written
      immediately before each record** by whoever owns it -- an i16 in the
      level file, an i32 inside an inventory holder -- resolved through
      `entities.txt`.

    - **The quest state names itself.** `character.dat` opens with the
      character's name and then **three interleaved 256-byte arrays**,
      which the Player/GameState dispatcher's own bindings identify:
      `+0x430` `SetQuestAssigned`/`QuestAssigned`, `+0x530`
      `SetQuestCompleted`, `+0x630` `SetQuestSolved`, followed by a
      fourth, `+0xa30`, `AddMonsterKilled`/`MonstersKilled` -- a bare
      `+= 1` with no clamp, so the kill counter wraps at 256. Each
      accessor is two instructions and gives its array away in the `ADD`
      immediate.

    - **The eighteen global story flags, named and verified.**
      `+0xfd8`..`+0x101c` is not an anonymous int block: `FUN_1003e7a4`
      and `FUN_1003ebd8` are the player object's own `getValue`/
      `setValue`, matching a field name against eighteen hardcoded wide
      strings, one per int. **Seventeen appear in the shipped scripts as
      `GetPlayer().saved_X`** (`GetPlayer().saved_Guild = 3`,
      `if(GetPlayer().saved_EndGame = 1)`); the eighteenth,
      `saved_NA_Crystal`, appears nowhere -- a cut flag the format still
      carries. This is the architectural point the format turns on: the
      corpus has 309 files using some `saved_*` name and hundreds of
      distinct names (`Level.saved_Open` alone 134 times), but those are
      ordinary script variables that ride in *their own entity's* record
      and die with the level. Only these eighteen live on the player, and
      so survive a zone change. `saved_Birgidda` here versus the separate
      `Level.saved_Birgitta` the scripts set beside it is the clearest
      case: two stores, two spellings, both live.

    - **The tile journal is not in the save at all -- `Init` is.** M44
      found the per-level tile-change array at `level+0xfc`/`+0x100` and
      recorded it as "the level-state half of a save file". It is not
      serialized anywhere: `<level>.dat` contains no tile data. What
      happens instead is that the entity root's last act is to dump its
      script object's SimKin instance variables **as name/value text**
      (skipping any value containing `{`, i.e. anything that reads like a
      method body), and on load `FUN_100188dc` recreates each entity with
      those variables already restored and then calls it by name:
      `Init`. A door whose `saved_Open` came back as 1 re-applies its own
      `UnlockZone` from inside `Init`, and the tile bytes follow. The
      journal is live state, not save state.

    - **`character.dat` starts with the *level* name.** `FUN_1001e754`'s
      first field is an 8-bit string taken from `engine[0x28]+0x28`, next
      to the debug string *"Saving %s as our current level"* -- and
      `FUN_1001ea54` `strcpy`s it straight back into the engine. That is
      how a load knows what to load. (A multiplayer session writes the
      player's *original* level instead, `player+0x1078`, next to
      *"Saving %s as original level"*.)

    - **The rest of the character-creation block, all named.** `+0xf35`
      `HasCreatedCharacter`, `+0xf38` `ChooseCharacter`, `+0xf3c`
      `ChooseRace`, `+0xf40` `GetPortraitID`, `+0xfac` `SetSex`,
      `+0xfb0`/`+0xfb4` the special- and race-ability ids, `+0xfc2`
      `GetManaReduction`, `+0xf44` `GetLevelUpPoints`, `+0xf8c[8]` the
      equipment slots by typeId, and `+0xf4c`/`+0xf68` the two five-deep
      hand queues (with the stats block's `+0x48`/`+0x4c` saved as an
      *index into* them, not an id). Note that **class and race are
      32-bit in memory and one byte each on disk** -- with eight classes
      it never matters, but it is what the format can hold.

    - **Four more stats-block fields named**, from the same two-switch
      cross-reference M43 used: `+0x10` **strength bonus**
      (`GetStrengthBonus`), `+0x12` **health bonus** (`SetHealthBonus`/
      `ModHealthBonus`, the one field with no stat index at all), `+0x20`
      **personality** and `+0x22` **luck**. The block is 70 bytes and is
      written *out of field order* -- `+0x00`..`+0x0e`, then
      `+0x14`..`+0x2e`, then experience/level/gold, and only then
      doubling back for `+0x10`/`+0x12`.

    - **A save drops one live effect field.** The stats loader zeroes
      `+0x70`..`+0x7a` and `+0x7c` and then fills only four halfwords, so
      the two damage-over-time accumulators (harmless) and **`+0x7c`, the
      second periodic channel's *kind***, are not restored. The third is
      not harmless: a regeneration or drain effect keeps its timer
      (`+0x78`) across a save but loses the kind that made it do
      anything, and comes back ticking down inert. There is nowhere on
      the wire to put it.

    - **`<level>.dat`'s trailing byte identified.** `engine+0xbe0e`, the
      one non-entity field in the file, is the "this level is ready to
      render" gate: `GameEngine`'s constructor sets it,
      `GameEngine_InitLevel` clears and re-sets it around a level load,
      and `Render3DScene` refuses to draw the 3D view while it is 0.

    - **M40's disk-space estimate is now legible.** `FUN_10018e70`
      serializes the player once to measure it, resets the stream,
      serializes the level state to measure that, and requires
      `free + existingFileSize - 512000 >= 3 * playerBytes +
      2 * levelBytes`.

    - **The port's save slots are real files.** M5's four in-memory slots
      are now real `game0<N>.sav` archives holding a real `character.dat`
      (`PlayerExecutable::BuildSaveRecord` / `ApplySaveRecord`),
      `GameAvailableForLoad` probes the files, `DeleteGame` unlinks them,
      and `LoadGame` returns to the level the record names instead of
      always re-entering the tutorial zone.

    - **Two port-side gaps, stated rather than papered over.** An
      `ItemExecutable` loaded straight from a `.s` file has no
      `entities.txt` typeId (`templateId()` is -1 unless it came through
      `Level.CreateEntity`, an M36 finding) -- and the equipment slots
      and hand queues name items *by* typeId, so for a save this port
      writes they come back empty. For the same reason this port writes
      every inventory child under one record class rather than resolving
      one per typeId: it re-runs each item's own script on load, so
      nothing is lost, but it is not what the engine does. Both are
      asserted in the smoke test, not glossed.

    - **Still no real save file to check against**, same as M40: saves are
      created at runtime on the device and none ships on the install
      image. The verification is that every field is asserted against the
      decompiled *writer*, and the writer cross-checked against its own
      *loader* -- a genuinely independent second source, since the two are
      separate functions that must agree byte for byte -- plus the
      real-data checks above wherever the format names something the
      shipped scripts also name.

- **M51 -- audio's remaining gaps.** M27's own five open items, closed
  together, because they were one system. RE writeup in
  [`docs/AUDIO_FORMAT.md`](AUDIO_FORMAT.md) ("The mixer"); smoke test
  `src/tests/m51_audio_mixing_smoke.cpp` (42 checks).

    - **The mixer, and what it is not.** The sound manager is
      `app->+0x3a8`: 256 sound objects, an 8-entry command queue, two
      master volumes, and a per-buffer tick (`FUN_10008b34`) that zeroes a
      32-bit accumulator, mixes every playing slot and clamps to 16 bits.
      **There is no panning anywhere** -- one accumulator, one channel --
      so "positional audio" turns out to be a volume model only. Two
      structural facts fall out: **at most eight voices sound at once**
      (the loop `break`s at eight, walking in *slot order*, so the cap is
      "lowest manifest slot number wins" rather than oldest or loudest),
      and **every voice is halved before summing**, which is the headroom
      that makes eight fit in 16 bits.

    - **The volume/pan formula, in full.** Per voice
      (`FUN_100080a0`): `accum += ((curve[master] * curve[voice] >> 8) *
      (sample >> 1)) >> 8`, where `curve` is a **101-entry i16 table at
      0x100a5e9e** indexed by a 0..100 volume. It is a 2.56-per-unit ramp
      -- 98 of its 100 steps are 2 or 3 -- with exactly one **5-unit jump
      at index 13** (30 -> 35 where a ramp gives 33) and a flat step at
      the top. That single 5 is why the table is transcribed verbatim
      rather than recomputed. And the master volume is picked **per
      voice**, not per bus: a slot gets the music volume if it is the one
      slot `+0xac0` names and the SFX volume otherwise.

    - **Distance is linear in *squared* distance.** `FUN_10027980`:
      `d2 = (dx*dx >> 8) + (dy*dy >> 8)`; full volume when `d2 >> 8` is
      0 (inside one tile), silent at 512, and
      `(volume << 16)/0x20000 * (0x20000 - d2) >> 16` in between. The 512
      bounds the *square*, so the audible radius is sqrt(512) ~ **22.6
      tiles**, not 512 -- 96 at four tiles, 87 at eight, 50 at sixteen, 5
      at twenty-two. `FUN_10064e00` is the same formula with a
      per-entity range, for a looping ambient attached to an object.

    - **The direction term is arithmetically dead.** The third
      `PlaySound` argument asks for an extra cut based on the angle
      between the listener's bearing and the **emitter's own heading**
      (a directional source, not a listener with ears -- consistent with
      there being no pan). But the step is `(volume >> 3) / 32`, an
      integer divide, which is **zero for every volume below 256**, and a
      volume is 0..100. So the whole branch subtracts exactly zero at
      every angle. Reproduced exactly, because the arithmetic is the
      finding.

    - **`PlaySound`'s real signature**, from the dispatcher's own
      skRValue defaults: `PlaySound(id, volume = 100, directional =
      false, repeats = 1)`. **`repeats` is a count, not a loop flag** --
      the queue writes it into the sound object's own repeat byte, and
      255 is simply the largest a byte holds, which is what the music
      path and an entity ambient arm themselves with. The corpus uses one
      argument 120 times and all four **exactly once**
      (`twilite/steamsound.s`'s `PlaySound(65, 75, 1, 255)`: a quieter,
      directional, endlessly repeating steam hiss), and that one call is
      what pinned each argument.

    - **The native trigger table, and a correction to M27.** There are
      exactly two ways into the mixer -- `FUN_1001b198` (a world
      position) and `FUN_1001b204` (the listener's own position, so
      always full volume) -- and between their 27 call sites the engine
      plays: **1** on a ranged shot, **59** on a chest, **63** on the
      native door path, **80** on a melee swing that finds a target *and*
      on the player taking damage *and* on the player dying, **81** on a
      swing that finds nothing, **87** on level-up and on the cheat
      sequence, **91/92** on a jump, and **99/100** in the UI. So 80 and
      81 are **hit and miss**, not two weapon classes; and a jump is not
      free -- it is refused below 5 fatigue and spends 5. 91 vs 92 is
      chosen on `player+0xfac`, the field `SetSex` writes (M50), which
      makes **sex 0 female**.

      M27 recorded ids 79-82 and 88-96 as "native-only, triggered by the
      engine's own movement/combat code". Half of that was right. The
      other half -- **79, 90, 93, 94, 95, 96** -- is triggered by nothing
      at all, native or scripted. **The shipped game has no footstep
      sounds and no death voice lines.** The samples are real and mapped
      in 14 of the 22 manifests; nothing plays them. Corrected in
      `AUDIO_FORMAT.md`.

    - **The main-menu music.** `FUN_1002707c`, the native "return to the
      front end" path, loads the sound bank named `"menu"` and calls
      `FUN_1001b180(engine, 0x46, 100, 0xff)` -- `menu_sounds.txt` slot
      70, `battle3.ogg`, volume 100, repeating 255 times. No script
      triggers it because returning to the menu is not a scripted event,
      which is why M27 could not find one.

    - **The fade, and the two sliders.** `FadeMusic`/`UnFadeMusic`
      (GameEngine 11 and 12, four real call sites around the multiplayer
      and bluetooth menus) move the music volume **5 per 256-sample
      buffer** -- 32 ms a step at 8 kHz, so a full fade is about
      **0.64 s**. `FadeMusic` only records a restore target when the
      music is currently audible, so fading from silence leaves
      `UnFadeMusic` with nothing to do; the asymmetry is reproduced. And
      the native slider handler (`FUN_1007f8f8`) matches a slider's own
      name against two wide strings and writes `+0xabc` for
      **`SoundFXSlider`** and `+0xab8` for **`MusicSlider`** -- exactly
      the two `AddMenuSlider` rows `options.s` declares.

    - **Three more dead bindings.** `SetAmbientSound` (GameEngine 103)
      reads its argument through `AtomToInt` and then **does nothing with
      it**; `SetAmbient`, `StopSound` and `CreateSound` are implemented
      but have **zero call sites** in the corpus. The entity-attached
      looping ambient behind `SetAmbient` (`FUN_100681f0` /
      `FUN_10064e00`) is fully decoded and reachable by nothing.

    - **Wired up in the port**: the curve, both attenuation terms, the
      eight-voice cap, the fade, the four-argument `PlaySound` on the
      Player/Monster/Level bindings, `FadeMusic`/`UnFadeMusic` on the menu
      binding, the front-end music, and the five real native triggers
      (melee hit, melee miss, taking damage, jump with its fatigue cost,
      and the ranged shot). A creature's attack noise now attenuates by
      distance; the player's own sounds go through the listener-position
      path, which is what the engine does.

    - **Left recorded, not implemented.** The bearing function
      (`FUN_1001c20c`) is a normalised-vector lookup quantised to a 32x32
      grid; this port uses a real `atan2`, which is unobservable given
      the term it feeds is zero. And the sound object's own start/decode
      path (its `vtable+0x10`, and the Symbian media server behind it)
      was not decompiled -- the port applies the recovered *gain* through
      XAudio2 rather than reproducing the saturating integer accumulator
      sample by sample.

- **M52 -- what the save record's scalars are, and the settings file.**
  Two of M40/M50's leftovers, closed together because the same
  whole-program pass answers both. RE writeup in
  [`docs/SAVE_FORMAT.md`](SAVE_FORMAT.md) ("What the scalars are" and "The
  configuration files"); smoke test `src/tests/m52_save_fields_smoke.cpp`
  (33 checks).

    - **The method: the binding dispatchers are the same tree as the save
      functions.** Every native binding lives in a class dispatcher's
      `switch`, and a dispatcher tail-calls its base's on a name it does
      not recognise. Following those tail calls recovers the class
      hierarchy independently -- and it comes out **identical to the save
      chain, layer for layer** (`10061a60` Entity -> `10065a70` Drawable
      -> `1006ca90` Item / `10028594` Spellbook -> ... -> `1003f130`
      Player). So an offset appearing inside exactly one binding's `case`
      block is that binding's field. Two corrections had to be built into
      the join before it was trustworthy: a `(**(code **)(vt + 0x8c))(...)`
      is a **vtable slot, not a field** (unfiltered, `Entity+0x8c` "means"
      `SetUseText` purely because `SetUseText` calls through slot `+0x8c`),
      and **the chain is a tree, not a line**, so sibling branches reuse
      offsets -- `Stackable+0x1bc` is an item's weight while
      `InventoryHolder+0x1bc` is a step toward a move target. Its own
      sanity check is that it re-derives what M50 already knew from
      elsewhere: `+0x94`/`+0x9c`/`+0xa4` come back as
      `GetPositionX`/`Y`/`Z`, `+0x1cc`/`+0x1ce` as `SetDamageMin`/`Max`,
      `+0x2a8` as the AI package. New tools:
      `shadowkey/ghidra/scripts/pyghidra_dump_program.py` (one whole-program
      decompile, 2006 functions, 0 failures, so later questions are a local
      grep) and `name_save_fields.py` (the join itself).

    - **Named: all but two.** The Entity layer alone gives up the
      **collision cylinder** (`+0x8e` radius, `+0x90` height -- the wall
      resolver walks `x±r`/`y±r` against `Map_GetTileAt`), **roll**
      (`+0xb2`, the third orientation channel beside pitch and yaw),
      **skin** (`+0xca`), **passable**/**usable**, the **object id string**
      `GetID` returns (`+0xcb`), a **name flag and two string-table ids**
      (`+0xd9`/`+0xdc`/`+0xe0`), and **two separate wall-clock timers** --
      the script `Delay(n, k)` at `+0x10a`/`+0x10c`/`+0x110` and a native
      one at `+0x114`/`+0x115`/`+0x118`, each with its own armed flag.
      Elsewhere: `+0x1e4` is **`SetActive`**, `+0x160` is the **`Init`
      guard** that makes re-running a restored entity's `Init` idempotent,
      `+0x1d4` is the **armour weight class** (`AR_Light`/`Medium`/`Heavy`,
      58 uses in the corpus), and `+0x1b9`/`+0x1bc`/`+0x1c0` are the
      **move order** `SetState` issues as `(target − self) × 20` per axis.
      Only `+0x8c` and `+0x92` are left: zeroed by the constructor, read by
      nothing. Every name was then checked against the shipped scripts'
      real argument values.

    - **The animation player, and `AnimationClip::rate` (a second
      milestone closed).** The seven fields Drawable re-writes are one
      mechanism: mode, loops-remaining (`0x7f` = forever), and four 8.8
      fixed-point **frame** fields plus a rate, set as a group by the clip
      starter at vtable `+0x13c` and advanced by `FUN_10065438`. That
      pins the model file's own `rate` field, open since M29:
      `FUN_100655a8` converts it as `internalRate = clipRate * 384`, and
      the tick advances the cursor by `(dt * rate) >> 8` with `dt` in 8.8
      **seconds**, so `fps = rate / 256 = clipRate * 1.5`. The field is
      **not frames per second** -- it is fps in units of 1.5, and the
      archive's common value of 10 means **15 fps**. The port read it as
      fps and was a third too slow on every animation; fixed.

    - **A correction to `ZONE_FORMAT.md`.** The `.ent` record's `unkB` low
      halfword lands in object `+0x5e`, which that doc called
      `modelFlags`. It is the model's **scale**, 8.8 fixed: its binding is
      `SetScale`, and the scripts call it with 192, 256, 352 and 512,
      where 256 is 1.0.

    - **Three mechanisms that are saved and cannot happen.** The same
      shape of finding M51 turned up in the mixer. `Character`'s "using an
      object" is complete -- two link ids saved indirectly, a post-load
      fixup that resolves both, and a stored position to return to -- and
      **nothing in the image ever sets the flag that arms it**, nor its
      two neighbouring busy flags, which are only ever cleared.
      `Character+0x37c`/`+0x380`/`+0x384` are touched by the save
      function, its loader, and *nothing else in the image* -- not even
      the constructor: three words of pure round-trip ballast. And eight
      bindings that write or read saved fields (`MountGun`,
      `MountFlak88`, `IsInUse`, `SetLifespan`, `SetFrozen`,
      `SetParalyzed`, `PutInReverse`, `GetWalkingState`) have **no caller
      in any shipped script**.

    - **The three config files: one exists.** `levelinfo.txt`'s full path
      and the `"%d\n%d\n"` format beside it are referenced by **no word
      anywhere in the image**, so nothing can open the file;
      `dragonstar.cfg` is worse -- only a bare filename exists for it, with
      no full path at all. Both survive only inside `DeleteAllGames`'
      seven-name cleanup list (`current.sav`, `dragonstar.cfg`,
      `dragonstar.set`, `ngen.log`, `levelinfo.txt`, `tmp.big`,
      `6r51.cfg`), written against a longer file set than this build
      produces. **`dragonstar.set` is the whole of it**: plain text, five
      keys (`ACTIONMAP` + count + 16 button slots, `LANGUAGE`,
      `SOUNDVOLUME`, `MUSICVOLUME`, `MUTEONCALL`), writer `FUN_10019c04`
      and reader `FUN_100199e0`. Three asymmetries reproduced: the writer
      hardcodes 16 while the table has 17 slots (so the 17th never
      persists), the writer refuses below 5000 bytes free rather than
      truncating (that is what `SaveConfigFailed` is for), and the volumes
      are read from and written to the **mixer** rather than a settings
      struct -- `soundMgr+0xabc` and `+0xab8`, which is M51's
      `SoundFXSlider`/`MusicSlider` pair arrived at from a completely
      different direction. Implemented as `assets/game_config.h`, loaded
      at startup and written on exit.

- **M53 -- the script timer, and the two "unplaceable" scripts.** The
  bullet paired `crypt2/controller.s` and `twilite/steamsound.s` on the
  theory that neither one's placement mechanism was a category the
  zone-load block resolves. **That was wrong for both, in opposite
  directions**, and the thing actually missing was a mechanism nothing in
  this port had. RE writeup in
  [`docs/WORLD_MODEL.md`](WORLD_MODEL.md) ("The script timer"); smoke test
  `src/tests/m53_script_delay_smoke.cpp` (34 checks).

    - **`Delay(seconds, tag)` -> `DelayReached(tag)`.** Every entity
      carries a one-shot timer. M52 named its three fields (`+0x10a`
      armed, `+0x10c` deadline, `+0x110` tag) from the binding side
      without knowing what fired them; the other half is `FUN_1006410c`,
      run once per frame per entity: `if (armed && deadline <= clock) {
      armed = 0; script->method("DelayReached", tag); }`, with the method
      name coming from the wide literal at `0x100b1634`. Arming is
      `deadline = clock + seconds * 0x100`. **The clock is not a wall
      clock** -- it is `engine+0x470 -> +0x460`, accumulated 8.8
      fixed-point *seconds* (the same per-frame delta the animation player
      uses, M52) and zeroed on a level load. Clearing the armed flag
      *before* dispatch is load-bearing: every chained sequence in the
      corpus ends each case by arming the next one.

    - **A real bug in the save format**, where M52 and this meet.
      `Entity`'s save writes `+0x10c` as `value - time(0)` and its loader
      adds `time(0)` back -- the same treatment it correctly gives
      `+0x118`, which really is a Unix time. Applied to a *game* clock it
      leaves the deadline shifted by however many real-world seconds
      passed between the save and the load, divided by 256; a
      `Delay(1, ...)` reloaded a day later comes back about five and a
      half minutes of game time away.

    - **Sixteen shipped scripts define `DelayReached`, across 59
      placements** -- and every one of them is placed, in categories 2, 3
      and 8, all of which this port already loads. The scripts were being
      read and `Init`-ed and then simply never ticked. What that cost:
      `monsters/azra_rat.s` (**35 placements in azra**, the zone this port
      starts in) arms `Delay(2, 0)` on the eighth rat kill and its
      `DelayReached(0)` opens the `rathurrah` menu -- the completion
      message for the game's first quest, which has never appeared;
      `monsters/umbra_keth.s` uses the timer as the **final boss's phase
      cycle**, vanishing and arming `Delay(Random(8, 12), 1)` to return,
      so without it the fight cannot progress; and crypt1's nine
      sarcophagi plus `crypt2/sarc_entity.s`'s eleven stagger the creature
      climbing out of each one.

    - **`crypt2/controller.s` was always placed.** `crypt2.ent` record
      #166: typeId **6023**, which `entities.txt` maps to category 3 and
      the label `!final_tp`, placement name `"star"`, at `(4480, 7808,
      70)`. `crypt2.s`'s `AddCrystal()` starts it on the seventh crystal
      with `Star = GetEntity("star"); Star.Delay(1, 0);`, and from there
      the script is a pure sequencer: seven steps that each light one
      crystal alcove (`Level.LightRect(..., 64)`, which M44 already
      implemented) and arm the next, then an eighth that spawns the Umbra
      with `Level.CreateEntity(274, 4480, 7808, 400)` -- **the same spot
      the sequencer itself stands on**. Eight seconds of cutscene. Two
      things were missing rather than one: the timer, and the registry --
      pickups (categories 3 and 8) were the one loaded category never
      entered into `Level.GetEntity`'s table, so the trigger died on its
      first line.

    - **`twilite/steamsound.s` is cut content.** Its whole body is
      `SetPassable(true); ShowEntity(false); PlaySound(65, 75, 1, 255)` --
      an invisible walk-through ambient emitter using the four-argument
      `PlaySound` M51 decoded. Nothing loads it: no `.ent` placement in
      any of the 21 zones, no `entities.txt` row, no other script. The
      steam *regions* do exist (`twilite.zon` carries `Steam01..Steam05`
      and `twilite.s`'s `EnterZone` opens `MistMenu` for each) but that is
      a different mechanism with a different effect. And it is not
      special: **145 of the 1535 shipped scripts** are named by nothing at
      all. So the right reading is not "this port cannot place it" but
      "the shipped game cannot either".

    - **Wired up in the port**: `ScriptDelay`/`GameClock`
      (`simkin_bindings/script_delay.h`) with the engine's exact
      arithmetic, `Delay`/`StopDelay` on the two hosts every shipped
      `DelayReached` script lands in (`ItemExecutable` for categories 3
      and 8, `MonsterExecutable` for category 2), the per-frame dispatch
      in the tick loop, the clock reset on zone load, and pickups added to
      the `GetEntity` registry.

- [x] **M54 -- screen mode `0x1f` is "quit to the main menu".** The last
  of the four states that reach `FUN_1002c010`, and the last open item on
  the screen-mode table M26 opened, M42 narrowed and M44 dead-ended. Full
  RE writeup in [`docs/RENDER_LOOP.md`](RENDER_LOOP.md) ("Screen mode
  0x1f"); smoke test `src/tests/m54_quit_to_menu_smoke.cpp` (31 checks).

    - **Why three passes missed it: nothing passes the value to
      anything.** `FUN_10026f40` writes `0x1f` *straight into*
      `controller+0x78` on the line where it spawns a background
      `RThread`, so it is neither a `SetScreenMode` argument (M42's
      search) nor a deferred fade target (M44's). And the caller search
      failed for a second, independent reason: the function that calls it
      is a **three-instruction thunk at `0x1006c268` Ghidra never marked
      as a function**, so it appears in no call graph. It is
      `ScreenModeController` vtable slot **`+0x3c`**, in both the concrete
      vtable (`0x100fb908`) and its base (`0x100fe574`) -- one slot below
      the level changer (`+0x2c`, mode 3) and two below the bar draw
      (`+0x44`).

    - **The two background threads are a symmetric pair.** There are
      exactly two named threads in the image: `nGEN_Loading`
      (`FUN_10027d44` -> `GameEngine_InitLevel`, mode 3 or 10) and
      **`nGEN_Quitting`** (`FUN_10026f40` -> `FUN_1002707c`, mode
      `0x1f`), each with its own closing log line. Both write the *same*
      progress counter (`appview+0x408`), which is exactly the value
      `FUN_10029cb0` hands the bar -- so a quit fills the same bar a zone
      load does, on its own seven-stage list (4, 15, 22, 30, 40, 80, 100)
      rather than loading's twenty-three.

    - **The banner is not part of the gate.** `FUN_10029cb0` draws the bar
      for `mode == 3 || 4 || 10 || 0x1f`, but nests the "Travel to:
      `<zone>`" text one level deeper under `mode == 10 || mode == 3`
      only -- the two that travel to a named zone. A save and a quit show
      the bar over the bare splash. (M26's own note in `main.cpp` that
      this was "undetermined" is now corrected.) The vignette's per-frame
      sub-dispatch immediately above is `mode < 0x25 && 0x1f < mode`, so
      `0x1f` is deliberately *excluded* from the run beginning one above
      it -- which is why M44's last lead went nowhere.

    - **`FUN_10023910` is `UnloadLevelAssets`, and it proves the mode.**
      It frees all 384 `global.spr` slots at `engine+0x4460` and all 256
      model slots at `engine+0x6b38` -- **except slots `0xcd` and
      `0xce`**, skipped by index. `0x4460 + 205*4 = 0x4794` and
      `+ 206*4 = 0x4798` are precisely the two pointers `FUN_1002c010`
      reads. The progress bar's own art is the one thing a level unload
      may not throw away, which is what lets the bar keep drawing on a
      screen where nothing else is loaded any more.

    - **`"menu"` is a real pseudo-level.** The quit thread calls
      `FUN_10024c8c(this, "menu")` -- now identified as the
      `<level>_sprites.txt` manifest loader -- and `menu` ships
      `menu_sprites.txt`/`_models.txt`/`_sounds.txt` but no `.zon`,
      `.ent` or `.zmp`. Slot 70 in `menu_sounds.txt` is `battle3.ogg`,
      its only non-`NULL.wav` music entry, so the progress-80
      `FUN_1001b180(engine, 0x46, 100, 0xff)` is the main menu theme
      starting. `FUN_10024c8c` also carries a dead `strcmp(level,
      "menu")` whose `CMP` at `0x10024cf0` nothing consumes.

    - **The script-visible name is `QuitToMenu()`**, GameEngine root
      binding index `0x33` -- and the three consecutive cases in
      `FUN_10078de4` confirm each other outright: `0x32` `Quit` sets mode
      5, `0x33` `QuitToMenu` calls slot `+0x3c`, `0x34` `QuitGame` ends
      the process. **Seven shipped scripts call it** (`deathmenu.s`,
      `mpdeathmenu.s`, `gameended.s`, `mainmenu.s`'s End Game
      confirmation, `saveconfirm.s`'s save-then-quit chain, and
      `savegamecorrupted.s`/`savegamenospace.s`, which name it as a menu
      row's handler outright). This port implemented `QuitGame()` and
      never `QuitToMenu()`, so **every one of those rows did nothing** --
      dying and choosing "back" included.

    - **The 0x1f screen has one decoration a zone load does not**: a
      wrapped message at `engine+0x14a82`, drawn only when
      `engine+0x5c0` (a live Bluetooth session) is set. Every writer of it
      is a multiplayer path, and its two constant messages are string
      **3811** "Connection lost" and **4081** "Game terminated by the
      host ". The quit screen doubles as the "why your session ended"
      screen -- and only in multiplayer. (Incidentally this pins the
      string-table addressing for good: the offsets are plain `index * 4`,
      and M26's known id 3950 "Travel to: " sits at `+0x3db8`.)

    - **Wired up in the port**: `engine/screen_mode.h` holds the recovered
      table (bar gate, banner gate, both progress lists, the two reserved
      sprite slots, the front-end level name and music slot) so it is
      testable outside the windowed loop; `main.cpp` renders the mode-0x1f
      screen on the real quit stage list and only then tears the session
      down -- scripts, entities, zone, region occupancy, timers, viewmodel
      -- reloads the `menu` manifests and reopens MainMenu.
      `MenuStack::RequestQuitToMenu()` keeps it a *request*, which two
      shipped scripts depend on (`QuitToMenu(); OnDisplay();` only works
      because the real native hands off to a thread and returns).

    - **Also implemented, found by the test**: `QuitAfterSave()` /
      `SetQuitAfterSave(b)` (root bindings `0x37`/`0x38`) were
      soft-failing. They are the flag that carries the quit intent across
      the save screen: `mainmenu.s`/`gameended.s` arm it before sending
      the player to SaveGameMenu, and `saveconfirm.s`'s `MenuDoneSave`
      reads it back and calls `QuitToMenu()` instead of `Quit()`. Without
      it, "End Game -> save first" left the player on the save screen with
      the session still running.

- [x] **M55 -- the two unnamed entity scalars are the collision system.**
  The last open item from M52, and much bigger than the bullet implied:
  `Entity+0x8c` and `+0x92` are not leftovers, they are the front end of
  the engine's entity collision, which this port did not have. RE writeup
  in [`docs/WORLD_MODEL.md`](WORLD_MODEL.md) ("Entity collision"); smoke
  test `src/tests/m55_model_collision_smoke.cpp` (31 checks).

    - **Why M52 saw nothing, and the method lesson.** M52's join
      deliberately discards offsets that appear inside a
      `(**(code **)(vt + 0xNN))` group, because those are slot indices,
      not fields. That filter is right and it has a blind spot: a field
      can be reachable *only* through a vtable, when the compiler emits
      an inline accessor as a three-instruction thunk in a slot. Both
      fields are exactly that -- `LDRB r0,[r0,#0x8c]; BX lr` at
      `0x100a151c` and five siblings, none of which Ghidra marked as a
      function, so they appear in no call graph and no offset search.
      **All 31 Entity-derived vtables carry the identical six accessors**,
      no subclass overriding any, so a call through one of those slots is
      unambiguous. Two of the six are already-known bindings
      (`SetRadius`/`SetRadius2`, slots `+0x50`/`+0x54`), which is what
      made the neighbouring pair worth reading together.

    - **`Entity+0x8c` is "this entity is solid", and it comes from the
      model.** `Entity::Init` (`FUN_100610e4`) looks up the placement's
      `entities.txt` descriptor and copies three fields off a per-model
      table at `engine+0x6f38` -- which `ZoneModelList_Load` fills from
      columns 2, 3 and 4 of `<zone>_models.txt`. **Those are not the "3
      flags" `ZONE_FORMAT.md` had**: they are a solid flag and two box
      half-extents in 8.8 world units. The shipped data says so plainly:
      `bottle 2 64 64`, `door 2 64 256` (thin one way, wide the other),
      `rail 2 64 512` (a fence run), `table 2 256 256`,
      `roof 0 1500 1500` (huge, and deliberately not solid),
      `dagger 0 0 0`. Column 2 is **only ever 0 or 2 across all 21
      zones** -- never 1 -- which is also why this one-byte field is
      written through the save stream's **i32** overload, the only place
      in the whole image that happens: the member is an enum, not a
      `TBool`.

    - **Five readers, and they are the whole of non-wall collision**:
      `FUN_100017c8` (movement -- walks the tile's own entity list at
      `engine+0x6904`, requires `solid && extentX && extentY &&
      !passable`, and resolves an overlap by backing the move out and
      re-applying one axis at a time), `FUN_10001fd0` (does a stance
      change still fit -- and it sets the actor's own half-extent to
      `0x80`, so **the player's box is 128, exactly half a tile**),
      `FUN_10000ab8` (is this tile occupied), and the tile-stamp pair
      `FUN_10066204`/`FUN_1006640c`. The overlap predicate is
      `FUN_1001c48c`, `<=` on all four edges.

    - **`Entity+0x92` is "wider than the tile it stands on".**
      `GameEngine_InitLevel` sets it on exactly
      `halfExtentX > 128 || halfExtentY > 128`, and the engine then walks
      the box *rotated by the entity's heading* and ORs bit 2 into the
      flags byte of every tile cell it covers. So entity collision is
      split two ways: small things are tested per-entity from the tile
      list, big things are **baked into the tile grid** and block like
      walls. The save loader recomputes the flag from the extents, which
      is why it round-trips; and `FUN_10005d60` refuses to clear a
      stamped entity's box on deactivation, because that grid bit would
      be left behind.

    - **Two smaller things fell out**: `+0x98`, `+0xa0` and `+0xa6` are
      the per-tick movement delta for x, y and z -- the partners of
      `+0x94`/`+0x9c`/`+0xa4` the constructor zeroes right beside them,
      closing the three gaps in the position block. And `FUN_1001c0f4`,
      the function called alongside stamping an entity into the grid, is
      **empty** in this build.

    - **Wired up in the port**: `world/model_collision.h` holds the
      table, the two predicates and the decompiled box overlap;
      `main.cpp` loads it per zone beside the other manifests and tests
      the player's move against every solid door, creature and prop. That
      replaces two invented body radii (a door was 90, a monster 40) and
      adds the case that was simply missing -- **static props, which had
      no collision at all**: every barrel, table, rock, tree and fence in
      the game was walk-through. In azra, **266 of 280 placements are
      solid** (104 pine trees, 36 rats, 25 barrels, 16 chairs, 8 crates,
      8 rocks, 7 tables, 7 doors, 5 fence rails), and 141 of those are
      large enough that the original bakes them into the grid.

    - **One knock-on correction.** `kMeleeRange`, this port's invented
      reach for bare fists, was 110 -- shorter than the 256 units the
      recovered collision now holds an actor off a creature, so
      bare-handed combat would have become impossible. It is now
      `2 * kActorHalfExtent`. Still invented (no shipped script sets a
      range for bare hands) but no longer free: a real weapon's own
      `range()` is 384 and a monster's default `attackRange()` is 660, so
      only the fists needed it.

    - **Two documented departures**: this port walks the live instance
      lists rather than a per-tile list plus a stamped grid (the same set
      of blockers, reached differently), and it uses the axis-aligned box
      everywhere, where the original's stamp uses the box rotated by the
      entity's heading -- so a rotated fence blocks a slightly different
      footprint here.

- [x] **M56 -- the Player object's script API, and the audit that found
  it.** Asked "is anything still soft-failing", built a tool to answer it
  properly, and closed the biggest thing it found. Coverage writeup in
  [`docs/SIMKIN_NATIVE_API.md`](SIMKIN_NATIVE_API.md) ("Port coverage, by
  receiver"); smoke test `src/tests/m56_player_api_smoke.cpp` (36 checks).

    - **The audit, and why it had to be per-receiver.** New tool
      `shadowkey/ghidra/scripts/analyze_port_native_coverage.py` (plain
      Python, no Ghidra) reads the port's implemented set out of each
      binding class's own `skString("Name")` literals and counts real
      `Receiver.Method(` call sites across all 1,535 shipped `.s` files.
      SimKin dispatches on the object -- 28 independent tries, 43 names
      reused across two or more -- so a flat "is this name implemented
      anywhere in `port/src`" check is not merely imprecise but
      *optimistically wrong*, and that is exactly what had hidden the
      largest gap in the port. `OpenMenu` existed on Item, Menu and
      Monster, so any name-level check called it done;
      `GetPlayer().OpenMenu(...)`, **332 real call sites**, was
      soft-failing.

    - **`GetPlayer()` was both the busiest receiver and the worst
      covered**: 2207 call sites, more than every other named receiver
      combined, and only 55% of them landing on something. (`Level` was
      already 97%, `GetOwner()` 64%, `GetOpener()` 79% -- and most of
      *that* residual is not native at all, it is SimKin-to-SimKin
      dispatch into the opened object's own script, as the API doc's
      factory-call section already established.) This milestone closes
      the inventory-and-menu half: **715 call sites that previously
      reached nothing**, taking `GetPlayer()` to 87%.

    - **`OpenMenu(name)` -- 332 sites.** The Player class registers it at
      trie index 3 and its dispatcher's recovered switch has no `case 3`,
      so the shape came from the Object/Entity class's own `OpenMenu`
      (`FUN_10061a60` case `0x22`): `FUN_100779b8(menuManager, name, 1,
      self)`, i.e. open by name with the caller as the new menu's opener.
      Routed through `ReopenMenu()` for the same M17 reason every other
      class's `OpenMenu` already is -- the target menu's `Init()` is where
      the quest-state branching lives and has to re-run per visit.

    - **`FindInventory(id)` -- 198 sites -- has a special case in front of
      the search.** Before looking at anything, case `0x3f` compares the
      argument against the literal `"frozen_key"` and, if that key's
      global flag bit is set, returns the bare *integer* `0x325` instead
      of an item object. Which means that from the moment the key is
      acquired the function never returns the object at all -- and that is
      why `RemoveItem` has an integer arm.

    - **`HasAmulet(name)` -- 10 sites -- never looks at the inventory.**
      It, the special case above, and `RemoveItem`'s integer arm are all
      built on one 16-bit word at `registry+0x468`
      (`FUN_1002f914`/`FUN_1002f8fc`/`FUN_1002f8e0`), fed from the other
      end by `FUN_1003d8e0` -- the add-to-inventory path the player
      vtable's `+0x164` names, which tests the incoming item's template id
      and sets the matching bit. Four ids, verified against the shipped
      `entities.txt`, each of whose scripts sets the *same* string as its
      own `SetID()`: 717 `RedAmulet` -> `redam` -> bit 2, 719
      `GoldAmulet` -> `goldam` -> bit 4, 718 `BlueAmulet` -> `blueam` ->
      bit 8, 805 `frozen_key` -> bit `0x10`. The bit order deliberately
      does not follow the template order. Full table in the API doc.

    - **`RemoveItem`'s off-by-one is reproduced, not fixed.** The
      frozen-key arm reads `if (count == 0) clear_bit(0x10); count--;` --
      the test precedes the decrement, so one acquired key survives its
      first removal with the flag still set and only the second clears it
      (the counter going negative). That is observable behaviour, not an
      internal detail: it is what decides how many of glcrcrwl's five
      gates a single key opens. Left exactly as the original has it, with
      the reasoning in `simkin_bindings/amulet_flags.h`.

    - **`GetCharacter()` -- 57 sites -- is clamped.** Case `0x2e`
      (`ChooseCharacter`) does not store its raw argument; it stores
      `FUN_100207ec(arg)`, a nine-arm switch mapping 0..8 to themselves
      and everything else to 0. It writes a *different* field
      (`player+0xf38`) from the one `ChooseRace` uses (`+0xf3c`), so this
      port's shared `m_Temp` scratch field would have been the wrong
      backing store. Every real call site is
      `if (GetPlayer().GetCharacter() = N)`, so all 57 branches were dead.

    - **`GiveItem(typeId)` -- 29 sites** -- builds the entity through
      `LevelExecutable`'s existing `entities.txt` item factory and pushes
      it through `AddItem()`, which is also where the key-item bits get
      set, exactly as the original funnels every acquisition through one
      vtable slot. **`HasItem(id)` -- 18 sites** -- is an Actor binding
      (`FUN_10003810` case `0x1b`) reached through the player's composite,
      a walk of the inventory chain comparing `item+0xcb`.

    - **A structural change this needed**: `PlayerExecutable` held no
      reference back to the `MenuStack` that owns it (its own header said
      so). `OpenMenu` and `GiveItem` cannot be answered from player state
      alone, so `MenuStack`'s constructor now calls `AttachStack(*this)`.
      A `PlayerExecutable` built standalone leaves it null and those two
      handlers soft-fail as before rather than pretending.

    - **A crash the milestone created and then fixed.** Making 332 script
      bodies reachable immediately found one that aborts the process:
      `glcrcrwl/gate1.s` opens with `Gate = Level.GetEntity("box1"); if
      (Gate.saved_Gate = 0)`, and with that entity absent the interpreter
      raises "Cannot get field saved_Gate from a non-object" out of
      `extractValue()` -- a *runtime* exception, where `MenuStack` only
      ever contained *parse* ones. Both `Init()` and `OnDisplay()` are now
      contained where the "file not found" path already decides what a
      failed open does: log, discard the half-built screen, stay on the
      current menu. Covered by a check that runs that exact real script.

    - **Suite soft-fail lines: 162 -> 97.** Two of those came from
      elsewhere: `Menu: OnDisplay()` was going through the logging path
      although it is an optional hook most menu scripts don't define (it
      now uses the same base-class call `TryInvoke` already did, for the
      reason written there), which was 70 of the remaining lines.

    - **Deliberately not attempted**, and what is left on `GetPlayer()`:
      the effects system (`AddEffect`, 83 sites counting `GetOwner()`'s
      63 -- its own milestone), the merchant cluster
      (`SetMerchant`/`BuyFromMerchant`/`SellToMerchant`/`VisitStore`/
      `BuyItem`/`SellItem`, ~40), `SetCameraStart` (49, a six-argument
      spawn-position-and-heading override needing host integration),
      `DoDamage` (19), the attribute setters `levelup.s` uses (~25), the
      `TestX` skill rolls (~17), and `DropGold` (9, which spawns a real
      world pickup). Each is listed with its call count in the tool's
      output.

- [x] **M57 -- the map.** `FUN_1002b430`, carried as "probably the
  automap, left un-renamed pending more evidence" since the
  `Map_GetTileAt` pass, is the in-game map, and the whole feature is now
  recovered and implemented. Full RE writeup in
  [`docs/WORLD_MODEL.md`](WORLD_MODEL.md) ("`FUN_1002b430` is the map");
  smoke test `src/tests/m57_automap_smoke.cpp` (34 checks).

    - **It is not a screen.** No screen mode, no `.s` script, no menu
      entry -- which is why nothing turned it up from the script side. It
      is a HUD overlay hanging off one boolean, `player+0x3a4`, flipped
      by a three-line function (`FUN_1001ee50`) with exactly one caller:
      the player's per-frame input poll, edge-triggered on logical action
      **9**. Nothing pauses; the game keeps running underneath it.

    - **A correction to `INPUT_HANDLING.md` that this needed first.**
      That doc's default-scheme table lists the 16 actions in the order
      `FUN_1001a220` binds them, and this port's `Action` enum had copied
      that row order as the action *indices*. The indices are the second
      argument of each `FUN_1001a784(input, action, key, nameResId)` call
      and are a different permutation. The (action, key) pairs the doc
      recorded were all right; the numbers were not, and it matters the
      moment something is read out of the binary by index -- action 9 is
      `"Map Toggle"` (Key 9), not "Side Step Right". Cross-checked
      against every branch of the same input poll. The enum is now the
      engine's own numbering.

    - **The picture**: optional backdrop from sprite slot 20, the zone
      display name centred at y=10, then a 64x64-tile window centred on
      the player drawn 2x2 pixels per tile at (24, 40) -- 128x128 on a
      176x208 screen. Larger world Y is *up*: the tile loop counts world
      Y down while the screen row counts up. Four tile colours, with the
      `+ 5` every in-bounds arm adds folded in: `0x0ca5` unknown,
      `0x0868` blocked, `0x0db5` flat, `0x0a85` stepped. Out-of-bounds
      and unexplored are deliberately the same colour, so the edge of the
      world and the edge of your knowledge look identical.

    - **This settles `GRAPHICS_FORMAT.md`'s open pixel-format question.**
      Those colour literals go **straight into the framebuffer**, and
      they only read as map tans and greys in `0x0RGB` 4-bit-per-channel
      (Symbian `EColor4K`); as RGB565, `0x0ca5` is a dark green.
      `FUN_1008f97c` confirms it independently by unpacking its own
      colour argument nibble by nibble. So: 16bpp carrying 12 bits of
      colour, top nibble unused.

    - **The explored bitmap is what makes it a map rather than a
      minimap.** `registry+0x45c`, one bit per tile, and
      `TileGrid_RaycastVisibility` -- the same per-frame ray fan that
      picks which faces to draw -- ORs a bit in for every tile a ray
      passes through, every frame, open or not. So the map shows exactly
      the set of tiles that have ever been rendered. This port feeds it
      from `Zone::RaycastVisibleTiles`, its own reimplementation of that
      very function.

    - **A confirmation of M56 fell out of it.** The bitmap is saved and
      restored (`FUN_1002fa5c`/`FUN_1002f934`) as width, height, `w*h/8`
      bytes -- and then, in the same record, the u16 at `registry+0x468`
      and the u16 at `registry+0x478`. Those are M56's key-item flag word
      and frozen-key counter, independently confirming that reading
      including the counter's width.

    - **The blocking mask turned up an undocumented cell bit.** The "draw
      solid" test is `cellU16 & 0x2402`, the literal's only occurrence in
      the image, spanning the cell's first *two* bytes: `flags` bit 1
      (wall) plus `blockFlags` bits 2 and 5. A first draft assumed both
      extra bits were runtime-only; the smoke test disproved it. Bit 2 is
      authored on **40,517 cells across all 21 zones**, 36,332 of them
      not walls -- so authored blocking, M44's `LockZone` and M55's
      entity tile-stamp all share one flag. Bit 5 is authored on
      **13,064 cells across exactly six zones** and **never once on a
      wall**; its only other reader (`FUN_100001ac`) tests it during
      movement and fires a virtual call when the actor's Z is at or below
      that cell's floor height -- a surface you sink into. Water or a
      hazard pool fits, but it is recorded rather than named. In azra
      this is the difference between 725 wall cells and 4,715 tiles the
      map draws solid.

    - **The player arrow** is three white lines from the map centre,
      angled off a 2048-entry sine table at `0x100f4954` whose every
      entry is exactly `round(256 * sin(2*pi*i/2048))` -- checked against
      all 2048, zero deviation. One leg at the facing (radius 8px), two
      at +/-45 degrees (4px). The two short legs are not exact mirrors:
      an arithmetic `>> 6` rounds a negative away from zero and a
      positive toward it, so at heading 0 they land at (91, 107) and
      (86, 107) either side of a long leg ending at (88, 112). Pinned in
      the test rather than tidied.

    - **One deliberate departure**: the original allocates the explored
      bitmap once and never resizes it, so a later, larger zone would
      index past a buffer sized for the first. This port sizes it per
      zone.

    - Not carried over: the multiplayer block that draws up to two other
      players as 2x2 markers, on the same footing as every other
      Bluetooth path in this port.

- [x] **M58 -- the effects system.** `AddEffect` was the largest
  unhandled native in the shipped corpus: 83 real call sites, 63 on
  `GetOwner()` and 20 on `GetPlayer()`. The machinery it lands in
  (`ActorStats`) was already there from M43/M48; what was missing was the
  script-facing entry point and the effect table behind it. Both are now
  recovered. New `src/simkin_bindings/effects.{h,cpp}`; smoke test
  `src/tests/m58_effects_smoke.cpp` (55 checks). **`GetOwner()` coverage
  goes 64% -> 100%**, `GetPlayer()` 87% -> 88%.

    - **The three enumerations are engine constants, and no `.s` file
      defines them.** A shipped line reads
      `AddEffect(Timed, ArmorValue, Increment, 5, 120)`, and `Timed`,
      `ArmorValue` and `Increment` appear in no script anywhere. They are
      named integer constants the engine pushes into the interpreter at
      startup, through the same `SIMKIN_MakeIntAtom` +
      `SIMKIN_MakeStringAtom` + `SIMKIN_RegisterConstant` shape
      `SIMKIN_BRIDGE.md` first found for `IPT_*`. Recovering them is
      mechanical: pair each int atom with the string atom that follows
      and resolve the literal. **59 constants**, from two registration
      runs -- `GameEngine_FirstTickBootstrap` into the one global
      interpreter, and `FUN_10073d3c` (the menu base class, which gives
      every menu object its own interpreter) into that one.

    - **The stat enumeration is the stats block's own field map**, and it
      confirms M43 from a completely independent direction:
      `Attack` = 1, `Defense` = 2, `ArmorValue` = 7 are exactly
      `ActorStats::StatIndex`'s three, which M43 had inferred from three
      unrelated dispatcher cases.

    - **`Strength` is not the strength field.** `FUN_1004ad40` case 9
      writes stats+0x10, which `GetStrengthBonus` reads, while
      `GetStrength` reads stats+0x14. Every other attribute in the table
      writes the attribute proper. So the six attribute shrines and every
      "gain 5 Strength" herb move a *bonus* the strength readout never
      shows.

    - **Three durations, three different mechanisms.** `Permanent` (3)
      applies the change and returns -- no node, nothing to expire, which
      is what the shrines use. `Timed` (1) is strongest-wins per stat: a
      weaker new effect is dropped outright, a stronger one *undoes* the
      old before applying itself. `Equipped` (2) stacks in a second list.
      A node is 0x18 bytes and stores the magnitude for an `Increment`
      but the field's *previous value* for a `Set` or `Decrement`, which
      is what lets one expiry routine undo either.

    - **This port now writes stats through, as the engine does.** M43
      modelled a timed effect as a modifier a reader added to a base
      value; the engine writes the field and remembers how to put it
      back. Switching to the real model is what makes `Permanent` (which
      has no node to consult) work at all, and it means a script's own
      `GetAttack()` and the port's `attack()` are the same number.
      `statModifier()` survives as bookkeeping.

    - **`FUN_10049698`, the derived-stat recompute**, is new here too:
      `maxHealth = healthBonus + (strength + endurance) / 2`,
      `maxFatigue = will + strength + endurance`, `maxMagicka` from
      intelligence. All three are assignments, so a script's
      `SetMaxHealth` survives only until the next attribute change -- and
      the `strength` it reads is the +0x14 one the effect table cannot
      reach. Six of the eight attributes tail into it; `Strength` and
      `Speed` do not.

    - **Two shipped potions are broken, and the corpus says so.**
      `items/shadowseed.s` reads as "+10 to all eight attributes"; its
      fifth line names `Perception`, which neither registration run
      defines. An unresolved bare identifier is not zero in this dialect
      -- it raises `Field Perception not found` and aborts the handler.
      So four attributes land, three never run, and neither does the
      `PlaySound` or the `DestroyObject` after them: the seed is not even
      consumed. `items/thunder_herb.s` has the same bug in its *first*
      line (`Damage`), so it does nothing whatsoever. Both are pinned in
      the test, running the real scripts.

    - **`RemoveEffect` is broken in the engine.** The dispatcher case
      finds the node and calls only the *free* -- neither of the two
      things the engine's own internal remover (`FUN_1004bd24`) does
      either side of it: it never unlinks the node from its list, and
      never undoes the stat change. No shipped script calls it, which is
      presumably why nobody noticed. Reproduced as "drop the node, leave
      the stat", with the correct remover kept alongside for the callers
      that use it.

    - **Three more `mvn`/`and`/`mvn` clears where `bic` was meant.**
      `AddEffect`'s `Blindness` arm, `SetPoisoned` and `SetDiseased` all
      write `flags = ~(~flags & BIT)` on their clear path, which turns on
      every flag *except* that bit -- and, from a state where the bit was
      already set, every flag including it. `SetBlindness` is the only
      one of the four written correctly, and it is the only one the
      corpus calls (`cure_blindness_balm.s`). Reproduced.

    - **`ItemUsed` is a cooldown marker, not a stat.** Two of the stat
      switch's cases (30 `ItemUsed`, 31 `MonsterAttackBonus`) exist with
      empty bodies: a node with a name and a timer and no stat behind it.
      `twi_crystals.s` is the whole idiom and the corpus's only
      `FindStringEffect` call site -- "if there is no effect named
      *crystals* on ItemUsed, do the thing and set one for 60 seconds."

    - **What Sanctuary's periodic channel was for.** M48 could only say
      kind 4 "has no tick behaviour at all -- it is purely a duration."
      `FUN_10049e78`, the stats block's own DoDamage, opens with
      `if (stats+0x7c == 4) return` -- total damage immunity. Kind 9
      (which `snowray_powder.s` arms) skips damage whose source is a
      creature's melee. Both are now wired; the port's two
      `ApplyDamage` entry points carry the kind-4 gate.

    - **The `Equipped` duration is a feature that did not ship.** Nothing
      pushes onto the second list, no shipped script passes `Equipped`,
      and the pair of functions that would remove such a node
      (`FUN_1004b3c0`/`FUN_1004b4f0`) have no callers in the image --
      and disagree with each other anyway, one searching the timed list
      and the other unlinking from the equipped one.

    - **A correction to three constant families.** `AR_*` and `WR_*` were
      carried in `game_constants.cpp` as "real identifiers, unconfirmed
      values" with sequential ordinals. They are **bit flags**:
      `AR_Light` 2, `AR_Medium` 4, `AR_Heavy` 8; the nine weapon ratings
      one bit each from `WR_Melee` 4 to `WR_EnchantedBlade` 1024. Two
      more, `SR_Small` 8 and `SR_Medium` 16, the port never had. And
      `IPT_Consumable` is registered as **4** globally but as **0** by
      the per-menu constructor -- where 0 is `IPT_Misc`, so the same
      comparison means different things in a menu script and an item
      script. This port takes the global 4.

- [x] **M59 -- merchants.** `buysell.s` and the `dstar_e`/`dstar_w` shop
  conversations were dead ends: ten merchant scripts between them made
  **363** `AddProduct` calls that reached nothing, and every buy/sell
  native on the player soft-failed. The whole system is now recovered --
  the data file it runs on, the store object, the three natives that fill
  it and the three verbs that spend it. New
  `src/assets/product_database.{h,cpp}` and
  `src/simkin_bindings/store.{h,cpp}`; smoke test
  `src/tests/m59_merchant_smoke.cpp` (37 checks). `GetPlayer()` coverage
  88% -> **90%**, and the suite's soft-fail count drops 97 -> 81 even
  though `buysell.s` now runs for the first time.

    - **`products.dat`, the last shipped data file this port had never
      opened.** 279 records, decoded from `FUN_10035634` and verified by
      exact byte consumption (7258 of 7258). Full format in
      [`ZONE_FORMAT.md`](ZONE_FORMAT.md). Two traps: the read order is
      **not** the offset order (the one-byte armour slot at `+0x12` is
      read before the name string id at `+0x10`, so an offset-ordered
      parse desynchronises on record one), and the "version" field is
      really the record stride -- the guard is `version < 0x24` and 0x24
      is 36, the size of a record.

    - **The shop scripts lie, and the file proves it.** A merchant stocks
      itself with `AddProduct(678, "Steel Pauldron", 111, 10, IPT_Armor)`
      and the handler reads **the first argument and the fourth**. Name,
      price and category come from `products.dat` -- where a Steel
      Pauldron is 1111, not 111. Across the corpus's 363 calls every
      template id resolves in the file and **65 of them quote a price the
      file contradicts**. The literals in the scripts are stale
      documentation.

    - **The trie is not the whole native surface** -- see
      [`SIMKIN_NATIVE_API.md`](SIMKIN_NATIVE_API.md)'s new section.
      `AddProduct`, `ClearProducts` and `ReducePrices` are dispatched by
      `FUN_1008607c`, a layer *above* the creature's trie dispatcher that
      tests three names with a plain `wcscmp` chain and tail-calls the
      trie for everything else. They appear nowhere in the 703-binding
      enumeration because nothing ever inserts them into a trie, and they
      are called 363 times. So "absent from the 703" no longer implies
      "script-level handler" -- the extra step is to search the image's
      string data for the literal, which finds all three.

    - **A store is 0x40 bytes inside a creature.** There is no shop
      object: `GetPlayer().SetMerchant(self)` is literally
      `player+0xf84 = monster + 0x330`, so "the shop I am standing in" is
      one pointer and every trading native is inert while it is null --
      and inert in a specific way, `return 1` with **no return value
      written**, so a script reading one outside a shop keeps whatever its
      variable already held rather than seeing a 0. Reproduced.

    - **`BuyItem`'s four return codes**, which `buysell.s` shows four
      different messages for, and the order of the tests that produce
      them: stock (2) before inventory room (3, a flat cap of 100 items)
      before gold (0). A bought item is created from the entity factory
      and given the **products.dat row's** rating rather than its own
      script's -- and a Consumable the player already carries stacks onto
      the existing one instead of making more objects.

    - **What a merchant will and will not buy.** `FUN_10035c28` is both
      the "will you take this" test and the code that puts it on the
      shelf. Misc is refused outright (the four-way type test names every
      category except it). A Spell is *accepted* -- the player is paid and
      loses the item -- but the guard that puts it on the shelf sits
      **after** the accept decision, so selling a spell to a merchant who
      does not already deal in spells makes it vanish. Reproduced, and
      pinned against the real `dstar_w/weapons_merchant.s` and
      `monsters/llewydr.s`.

    - **`ClearProducts` leaks the counters.** It frees the shelf list and
      zeroes the head, count and tail, and never touches the five
      per-category counters at `+0x24`. Those counters are exactly what
      `GetProductCount` and `GetDefaultStore` read, so a merchant is left
      claiming tabs it cannot fill. Every merchant script opens its
      `Init()` with `ClearProducts()`, so on a second visit the counters
      are double what the shelves hold. Reproduced.

    - **Selling more than one removes all of them.** `SellItem(item, n)`
      with `n > 1` does not remove `n`: the real loop removes by
      *template id* until nothing matches. `SellAllItems` counts a
      Consumable's whole stack and everything else one each, then goes
      through the same path.

    - Smaller findings: `wendek_freetalker.s` lists template 704 twice
      under two different names ("Rat I" and "Ratseye") and the handler
      appends unconditionally, so that really is two shelf lines sharing
      one product row; `ReducePrices(pct)` recomputes from the base price
      rather than compounding, so `ReducePrices(0)` puts every price back;
      and `GetProduct` and `GetItemDescription` both return the
      *description* string, with only the latter falling back to the
      player's own inventory when the name is not on the shelves -- which
      is what makes the same popup work on the sell page.

    - **Not carried over**: the store *screen* itself. `buysell.s` now
      loads and runs (`BuyFromMerchant`/`SellToMerchant` open it with the
      buy flag `IsBuyMode()` reads back), but the six store-menu bindings
      that populate its product table -- `DisplayWeaponsPage`,
      `DisplayArmorMenu`, `DisplayConsumablesMenu`, `DisplaySpellsPage`,
      `DisplayMiscItemsMenu`, `SetInventoryList` (trie `0x14de0`) -- are
      still soft-failing, along with the widget natives around them
      (`SetGoldText`, `SetLocalizedText`, `Button::SetX/SetVisible`,
      `PopupMenu::UpdatePopupItem`, `Table::SetInset`). The data model
      underneath them is complete; what is missing is the table
      population. That is the natural follow-up. **Done in M60 below.**

    - Also decompiled in passing, for whoever takes the next entry:
      **`SetCameraStart(a, b, c, d, e, f)`** is Player case `0x39`. It
      sets a flag at `engine+0x14a20` and writes the six arguments to
      `+0x14a24`, `+0x14a28`, `+0x14a2c`, **`+0x14a34`**, **`+0x14a38`**,
      **`+0x14a30`** -- the last three out of order, exactly as the
      roadmap's own entry suspected.

- [x] **M60 -- the store screen.** M59 left `buysell.s` loading, running,
  and drawing an empty table. The screen now works end to end: the
  merchant's shelves are listed, priced, compared against what the player
  is holding, bought from and sold to. Nine store-menu bindings, the one
  function behind all of them, the engine's shared widget class and the
  table's own cell class were decompiled for this. New smoke test
  `src/tests/m60_store_screen_smoke.cpp` (53 checks). The suite's
  soft-fail count drops **81 -> 62**, and the store screen contributes
  none of the remainder.

    - **`FUN_10032f78` is the store screen.** All five `Display*Page`
      natives, `RedrawPage`, and the tab buttons' focus wiring funnel into
      one function that takes a category and a clear flag and fills the
      table `SetInventoryList()` remembered. The five categories the
      `Display*` natives pass **are the `IPT_*` constants M58 recovered**
      -- Weapon 1, Spell 2, Armor 3, Misc 0, Consumable 4 -- which
      independently confirms that table from an unrelated function.

    - **The screen mode is not a bool.** `menu+0xd0` has three values, and
      `FUN_10032f78` branches three ways on it: **1** lists the merchant's
      shelves, **2** lists the player's items with prices, **0** lists them
      with their derived stat line instead. `IsBuyMode()` is exactly
      `mode == 1`, so the sell page and the plain inventory page both
      answer false and are still completely different pages. It is written
      from one place, `FUN_10034f38(menu, buying) { mode = buying ? 1 : 2;
      }`, on every visit rather than at construction -- one screen object
      serves both sides of the counter. This port had it as a bool on the
      menu stack; it is now the real field, stamped onto the screen as it
      opens.

    - **The two columns `buysell.s` calls "rating" are not a rating.**
      `productTable.AddColumn(10); //rating left` and `//rating right` are
      10px wide and hold a single space each. `FUN_100a0cec` draws into
      them: it compares the shelf line's rating against **whatever the
      player has in that hand** -- left for one column, right for the
      other, or, for armour, whatever is worn in that row's own
      `products.dat` armour slot -- and blits global.spr sprite **24** if
      the shelf is better or **25** if it is worse. Those are the two ids
      `buysell.s` assigns to `image_item_active`/`image_item_dormant` on
      the first two lines of its `OnDisplay()` and then never uses, because
      the native renderer is what picks between them. A Consumable is
      never compared (the renderer returns early on category 4), which is
      why the potions page shows two blank columns. Reproduced, including
      the early-outs for an empty hand and for a mismatched category.

    - **The cost column, and a shipped cosmetic artefact.** It is built
      from two *stringtable ids*, not literals: 3039 and 3040, formatted
      with `"%s: %d %s: %d"`. String 3039 ships as `"GP "` -- with a
      trailing space -- so the line the shipped game renders really does
      read `GP : 111 Qty: 3`, space before the colon and all. The sell
      page uses the same format for a Consumable and `"%s: %d"` for
      everything else, because only a Consumable has a stack to count.

    - **A fourth hand-added native, and a whole hand-added class.** M59
      found three natives dispatched by a `wcscmp` chain above the trie and
      left "are there others?" open. There are. `FUN_10034588` is the same
      layer over the store menu and carries **`SetGoldText`** (and a
      second, redundant copy of `IsBuyMode` that shadows the trie's own).
      And the table's **cell object is an entire class with no trie at
      all** -- eight methods in a flat `wcscmp` chain in `FUN_100a4ce4`
      (`GetItemType`, `IsInventoryEquipped`, `IsItemEnabledFor`,
      `GetItemDescription`, `DropItem`, `GetAssociatedObject`,
      `GetArmorText`, `GetItemText`). See
      [`SIMKIN_NATIVE_API.md`](SIMKIN_NATIVE_API.md)'s new subsection.
      `GetItemDescription` carries a real type confusion: an out-of-range
      description id makes it return a boolean `true` where the caller
      concatenates a string.

    - **`SetGoldText` throws the "gp" away.** Its whole body is
      `sprintf(buf, "%d", player->gold)` into the widget's item text -- a
      bare `%d`. `buysell.s` creates that widget as
      `AddFloatingText("0 gp", ...)`, so the placeholder's "gp" disappears
      the moment the screen finishes loading and the player sees a bare
      number. Reproduced.

    - **The widget class is one class.** This port had grown five widget
      binding classes, each re-declaring the setters it happened to need.
      The engine has exactly one (trie `0x14d20`, `FUN_1007e954`, fourteen
      methods) that a button, a floating text, a text area and a table all
      inherit -- which is why `buysell.s` mixes `SetVisible`/`SetX` on
      buttons with `SetLocalizedText`/`SetItemText` on floating text
      without distinguishing them. Consolidated into one shared handler
      (`row_owner_ref.h`), which is what made `Button::SetX`,
      `Button::SetVisible` and `MenuItem::SetLocalizedText` work at once.
      Two real details kept: `SetVisible` writes only the visible byte
      while `SetEnabled` writes visible *and* selectable, and
      `SetItemText`/`SetLocalizedText` write the same slot so the later
      call wins.

    - **`UpdatePopupItem` takes three arguments, not two.** The third is a
      **callback name**, applied only when the argument count exceeds two.
      That is how one popup serves both sides of the counter:
      `popup.UpdatePopupItem(0, 3787, "BuyInv")` on the buy page and
      `(0, 3789, "SellItem")` on the sell page. Without it the confirm row
      kept whatever `AddItem()` first gave it and pointed at the wrong
      handler on one of the two pages.

    - **The table callback takes the selected cell.** `SetCallback
      ("SelectedItem")`'s handler is declared `SelectedItem[ (cell)`, and
      this port was invoking it with no arguments -- so `buysell.s`'s
      `selectedItem=cell` assigned an unset variable and every later
      `BuyInv()`/`SellItem()` had nothing to name. The round trip that
      makes the screen work is small and complete: column 0's text is the
      `products.dat` name, `cell.GetItemText()` hands it back, and
      `BuyItem(itemText, n)` resolves it against the shelves.

    - **`RedrawPage(false)`'s argument is the *clear* flag**, and
      `buysell.s` passes false. The repopulation therefore overwrites the
      existing cells rather than destroying them first, and the row count
      (`table+0xe4`) is what hides the leftovers -- rows from a longer page
      stay allocated with their old contents. The table now models the
      allocated row count and the used row count separately, because that
      distinction is observable.

    - Smaller findings: `SetInset(15)` is `table+0xe2`, a scroll-arrow
      layout number; the buy branch grows the table to the merchant's
      **whole** stock count and then sets the used count to just this
      category's; and `FUN_10032f78` opens with a `sprintf` of `"Pre load
      merchandise create rows(%d)"` into a stack buffer that nothing ever
      reads -- a leftover debug line still shipping in the retail binary.

    - **Not carried over**: the comparison columns render as ASCII
      `+`/`-`/`=` rather than sprites 24/25, because the text-cell render
      path has no sprite reachable from it; and the armour-slot lookup
      resolves a template's slot through `products.dat` and finds the
      wearer by scanning equipped armour, since this port has no
      `player+0xf8c` per-slot array. Same answers, different bookkeeping.

- [x] **M61 -- the scripted spawn override, `SetCameraStart`.** 49 call
  sites and the largest unhandled native left. It is armed by the zone the
  player is *leaving*, consumed by the zone they arrive in, and disarmed if
  they change their mind -- all three ends decompiled this pass, along with
  the level-transition bindings that make the middle step a conversation.
  New smoke test `src/tests/m61_camera_start_smoke.cpp` (31 checks). Suite
  54/54.

    - **The six values are stored in `.ent` record order, not signature
      order.** `engine+0x14a24..+0x14a38` receive arguments 1, 2, 3, **6**,
      **4**, **5** -- which reads as a bug until you notice the struct is a
      verbatim copy of the first six 32-bit fields of a `.ent` **placement
      record** (`x, y, z, rotOrScale[0..1], rotOrScale[2..3], unkA`), whose
      three orientation channels land at `+0xb2` (roll), `+0xa8` (pitch)
      and `+0xb6` (yaw) in exactly that file order. The script signature is
      the human one, `(x, y, z, pitch, yaw, roll)`. The storage mirrors the
      file, the signature mirrors the reader, and `GameEngine_InitLevel` is
      where the two meet. **This independently re-derives
      `ZONE_FORMAT.md`'s destination table** -- which had itself been
      corrected once, from raw disassembly, after an earlier pass read it
      off the decompiler's variable naming -- from an unrelated function.

    - **The shipped data agrees on which slot is which.** Across all 49
      sites, pitch and roll are 0 *every time* and yaw is a signed
      65536-per-turn angle at 45 of them. A spawn point and a facing,
      nothing else. The smoke test parses all 49 out of the shipped
      scripts and asserts exactly that, because it is the check a wrong
      slot mapping could not survive.

    - **`Level.LoadLevel(name, x, y)` does not load anything.** It pushes
      the current level name to a "previous" slot, writes the destination
      and the two optional coordinates, and raises **`levelconfirm.s`** --
      the "Travel to: / Go / Don't Go" prompt. `Go` calls
      `ActuallyLoadLevel(GetNextLevel(), GetNextLevelX(), GetNextLevelY())`,
      reading back the three fields `LoadLevel` wrote. `Don't Go` calls
      `RestoreSaveLevel()`, whose entire body is `engine+0x14a20 = 0`
      followed by `strcpy(current, previous)`.

      So **disarming the spawn override is half of what "Don't Go" does**
      -- which is the evidence that every shipped script's arm-*after*-load
      ordering is deliberate rather than sloppy. The script arms an
      override for a level that has not loaded and might never load, and
      the decline path is responsible for taking it back. Implemented:
      `ForceLoadLevel`, `ActuallyLoadLevel`, `GetNextLevel`,
      `GetNextLevelX`, `GetNextLevelY`, `RestoreSaveLevel`, and the pending
      /previous level pair behind them.

    - **The roadmap bullet this milestone came from had the timing
      backwards.** It said main.cpp needed to consume the override *after*
      the zone script's `Init()`. `GameEngine_InitLevel` consumes it during
      the entity pass, which runs **before** the level's script is loaded
      at all -- so a zone script cannot arm a spawn for its own arrival,
      only for the next one. The corpus agrees: all 49 sites sit in
      `EnterZone` or menu handlers of the zone being left. Consumed at
      camera-placement time here, matching.

    - **Which way a heading points, finally derived instead of fitted.**
      `FUN_100063f0` ("walk forward") advances an entity by
      `dx += speed*sin(h + 0x4000)` (`== +cos h`) and
      `dy += speed*sin(h + 0x8000)` (`== -sin h`), against this port's own
      `dx = cos(yaw), dy = sin(yaw)` -- so `yaw = -heading`, exactly. M57's
      automap marker had assumed that relation and its own comment called
      the direction unverified; the compass tape had assumed the
      *opposite*, so the two disagreed. Both now go through one helper, and
      `RenderHud` runs the real decompiled formula (the high byte of
      `player+0xb6`, capped at 254) on a real heading.

    - **A correction to `RENDERER_3D.md`.** Its note on the camera rotation
      matrix pairs `camera+0xa8/0xb2/0xb6` with "pitch/yaw/roll", listing
      the call's *argument* order against the read's *field* order --
      and the call shuffles them. Working the matrix out from
      `FUN_10073760`'s body gives `+0xa8` = pitch, `+0xb2` = **roll**,
      `+0xb6` = **yaw**, which is what the compass, the automap marker and
      `SetCameraStart`'s shipped data all already said.

    - **Two asymmetries between the override branch and the record branch**
      of `GameEngine_InitLevel`, both real: the record branch recomputes
      the eye height (`player+0x224 = player+0xa4 + CMap+0x1a`) and the
      override branch does not, so a scripted arrival inherits the previous
      level's eye height; and the record branch is gated on InitLevel's
      `param_3` while the override branch is not, so a scripted spawn
      places the player even in the mode where an ordinary entry would
      leave them alone. Recorded in `ZONE_FORMAT.md`.

    - **The `.ent` player-start record's own heading was being dropped.**
      This port read the position out of the `typeId == 1` record and left
      the camera at yaw 0, so every zone entry faced due +x regardless of
      where the level was authored to start. Real azra starts at heading
      25600 (~140 degrees). Now read, from the same three offsets the
      generic-entity branch uses.

    - **Six of the 49 sites are in dead scripts**, which is why nobody ever
      noticed that they call `ForceLoadLevel` (immediate) *before*
      `SetCameraStart` and would therefore arm the override one transition
      late. `broken1/to_bw2.s`, `broken2/to_bw1.s` and the four
      `snowline/*menu.s` are unreachable -- the `OpenMenu` that would raise
      them is commented out in the shipped parent script, or absent. The
      smoke test checks the commented-out line rather than trusting the
      observation.

    - **String 3950 belongs to the confirm prompt.** `levelconfirm.s`'s
      `Init` is `AddStaticItem(3950,false); AddStaticItem(
      GetNextLevelName(),false)`, so `"Travel to: "` is that dialog's
      header. M26 used it as the loading-screen banner. Left as is -- the
      loading screen has to say something and no better candidate turned up
      -- but the shipped use site is now on record.

    - **The spawn override is part of the multiplayer protocol.**
      `FUN_1003cc5c` serialises all seven fields, flag included, into a
      0x20-byte packet tagged `0x2e`; `FUN_1003b038` applies an incoming
      one as an override alongside a level-name copy. Out of scope, noted.

    - **Not carried over**: the eye-height asymmetry above (this port's
      `kEyeHeightOffset` is a documented calibrated stand-in, not the real
      `CMap+0x1a`, so reproducing an omission built on it would only put
      scripted arrivals at floor level), and the confirm prompt itself --
      `LoadLevel` still requests the transition directly rather than
      raising `levelconfirm.s`, which needs a menu screen wired through
      main.cpp's mode machinery plus the menu-side `GetNextLevelName()`.
      Both are one-line departures with the real behaviour written down
      beside them.

- [x] **M62 -- `SetPosition` and the Object/Entity position bindings.**
  The sibling of M61's `SetCameraStart`: a teleport *within* a level. Five
  bindings on class `0x14d08`, the base a player, a monster, a door, an
  item and a prop all derive from -- `SetPosition`, `SetPositionMirror`,
  `GetPositionX/Y/Z` -- and 63 call sites on four receivers, of which only
  the monster had any implementation. New smoke test
  `src/tests/m62_set_position_smoke.cpp` (30 checks). Suite 55/55.

    - **The bullet this came from was stale in both halves.** It said
      `FUN_1001beac` was "not yet decompiled". It was decompiled in M49
      (for arrow collision, `WORLD_MODEL.md`) *and* already implemented in
      this port as `Zone::CollisionFloorHeightAt`. The only genuinely new
      pieces were the two constants around it and the predicate that
      decides whether it applies at all.

    - **The snap is `resolveSurface(x, y, z + 0x180) + 0x80`.**
      `FUN_100686e0` is a one-line wrapper on the collision-height
      function; its caller adds the lift. So the requested `z` is **raised
      by 384 and used as a probe**, then discarded, and the resolved
      surface is lifted by 128 so the actor stands on it rather than in it.
      On an ordinary tile the argument has no effect whatsoever; on a
      two-storey tile it is what chooses the floor. `Zone::
      SnapActorToGround()`.

    - **Only actors are snapped, and that is what makes `gate.s` work.**
      `vtable[0xc8]` is the predicate for membership of the engine's actor
      list at `engine+0x640` -- the list `SetZone`'s experience-budget loop
      walks, narrowing it further with the "is a monster" predicate at
      `vtable[0xe4]`. A player and a monster are in it; a door, an item and
      a prop are not.

      `gate.s` is a **portcullis**, and its entire open/close mechanism is
      `z = GetPositionZ() +/- 1200; SetPosition(x, y, z)`. A snapped door
      would drop straight back to the floor and never open. The smoke test
      drives the real script twice and checks the gate goes up 1200 and
      comes back to exactly where it started.

    - **What the two functions the player's physics could call are worth,
      measured.** The per-tick ground used `FloorHeightAt` (always bilinear
      over the four corners) rather than `CollisionFloorHeightAt`
      (`FUN_1001beac`), which has two extra branches. Across all 21 zones,
      331,776 tiles:

        - the **flat-vs-corners** branch changes nothing, anywhere: on all
          241,174 tiles with `flags & 0x04` clear, the four stored corners
          already equal `ZcpEntry+2`. It is an authoring distinction, not a
          behavioural one.
        - the **two-storey** branch is entirely real and entirely local:
          **1,921** tiles carry it and only two zones have any -- `dstar_w`
          (1,723, 1,644 with a standable ceiling) and `glaciercrawl`
          (198/118). On 1,532 of them an actor above the ceiling resolves
          up to **2,112 raw units** higher than the corner blend returns.

      That is the difference between standing on Dragonstar West's upper
      level and falling through it, and the player could not do it before.
      Now fixed -- passing `gameCamera.z` is what makes the branch
      reachable, and it is the argument `FloorHeightAt` had no way to take.
      Guessing would have got this backwards: the branch that *sounds*
      significant is free, and the one that sounds like an edge case is two
      zones' verticality.

    - **`z` is optional.** The real case branches on `argc == 3` and passes
      a literal 0 otherwise. `lakvan.s`'s `To_4A`/`To_4B` transitions are
      the two shipped two-argument calls, and the port's monster-only
      handler rejected them outright (`args.entries() >= 3`).

    - **One implementation, on the base class.** The same consolidation M60
      did for the widget class: `simkin_bindings/entity_position_ref.h`
      holds the storage, the setter, the three getters and the actor
      predicate, and Player/Monster/Door/Item mix it in. The three getters
      existed nowhere before, so `gate.s` would have computed `(0, 0,
      1200)` and teleported itself to the world origin even if the setter
      had worked. Placements are seeded from their `.ent` record **before**
      `Init()` runs, which is the order the engine uses.

    - **`Player` is not a global.** All 19 `Player.SetPosition(...)` sites
      are preceded by their own `Player = GetPlayer();` -- an undeclared
      identifier, so SimKin makes it a stack local holding a real object.
      Checked in the smoke test rather than assumed, since a *declared*
      field would have hit M38's object-field bug instead.

    - A shipped authoring slip, recorded not fixed: `glcrcrwl/icegate.s`
      subtracts 128 from x and y on **both** its raise and its lower, so an
      open/close cycle walks the gate 256 units diagonally instead of
      returning it.

    - The coverage tool did not move, for the third documented reason:
      `SetPosition` already counted as "implemented on another receiver"
      because the monster had it, and the bare and local-receiver calls --
      36 of the 63 -- are invisible to it entirely.

- [x] **M63 -- `Level.CreateEffect` and the animated-sprite entity.**
  Zone/Level dispatcher case `0x23`, an inlined copy of the engine's own
  `FUN_10073528`. Thirteen shipped call sites, all ten-argument, eight in
  `crypt1.s` and five in `twilite.s`, plus a fourteenth commented out in
  `blaze.s` -- and every one of the thirteen sits in its zone script's own
  `Init()`, so this is the game's **static scenery animation** primitive,
  not a combat effect. `simkin_bindings/effect_entity.h`/`.cpp`, a
  `CreateEffect` handler on `LevelExecutable` that owns the live list, and
  a per-tick drain in main.cpp beside the status effects. Smoke test
  `src/tests/m63_create_effect_smoke.cpp`, 49 checks. Suite 56/56,
  soft-fails **62 -> 39**, `Level` coverage 97% -> 98%.

    - **The art is `global.spr` -- M48's open question, answered.** M48
      recorded the spell projectile's `+0x134` as "not a `models.txt`
      index (2 is `lantern.bin` and 5 is `sbarrel.bin`), so it selects
      from something else that has not been identified". It selects from
      the **384-slot `global.spr` cache the menus and HUD already draw
      from**: the sprite entity's draw (`FUN_1008b25c`) does
      `sprite = *(u16 **)(engine + 0x4460 + entity->0x134 * 4)`, so
      `engine+0x4460` is the loaded-slot pointer table and `+0x134`
      indexes it directly. This world has two art sources, not one, and
      nothing had noticed the second.

      Confirmed past the pointer arithmetic by decoding the real archive:
      slots **181-192** are twelve consecutive 16x64 frames averaging RGB
      (204, 159, 102) -- a tall narrow flickering orange column, i.e.
      **fire**, which is what `crypt1.s` spawns; **193-204** are twelve
      more 16x64 frames averaging (120, 121, 117), the same shape in grey
      -- **steam**, in the one zone that also ships a `steamsound.s`;
      **12-19** are eight 32x32 orange frames whose opaque pixel count
      falls from 337 to 59, a **dissipating blast**, which is `blaze.s`'s
      impact; **71** is 32x32 averaging (133, 4, 7), the **blood** spurt
      `FUN_10081e5c` throws with random velocity under gravity; and
      M48's own **2** and **5** are real 32x32 sprites, 2 a gold-orange
      fireball. Both twelve-frame runs are bounded by slots of other
      sizes (180 is 57x46, 205 is 79x9), so each is a whole animation
      rather than a window into a longer sequence.

    - **Every shipped effect is permanent, and that is a real branch.**
      `lifetime * 15` seeds the countdown, but `lifetime == 0` sets the
      "no lifetime" flag at `+0x152` instead and skips it -- and all
      thirteen shipped calls pass 0. The same flag gates the scale ramp,
      so a scripted effect also draws at the constructor's flat 0x80,
      half scale, for its whole life. Only `blaze.s`'s commented-out line
      passes a real lifetime, and it is the one that shows the units:
      16 -> 240 units -> **24 ticks, 0.94 s**, because the multiplier is
      15 and not 16. Its eight frames need 28 ticks at the shipped rate,
      so that impact would have died on frame 18 of 19 -- it never showed
      its last frame.

    - **`if (argc < 10) return 1` is the first instruction of the case.**
      A short call is *accepted* and does nothing; the nine per-argument
      `Leave` guards that follow are dead code. So unlike M62's
      `SetPosition` there is no shorter form, and soft-failing a
      nine-argument call would report a missing native that is not
      missing. Reproduced, including the acceptance.

    - **The bullet's guess was wrong about two of the ten arguments.** It
      read them as "a position, a radius and a duration". The duration is
      argument 8; arguments 9 and 10 are two independent size scalars,
      and they are not world units -- the draw multiplies each by its
      axis of the sprite's *own pixel size*, twice:

      ```c
      entity->0x5a = (i16)(sizeX * spriteWidth);
      halfWidth    = ((i16)entity->0x5a * spriteWidth) >> 8;
      ```

      which is why the scalars run inversely to the sprite (512 against a
      16-pixel-wide flame, 40 against a 64-pixel-tall one). The quad is
      laid out in camera space (`+0xbc`/`+0xbe`/`+0xc0`, filled by
      `FUN_1001610c` from the camera matrix) and runs *upward* from the
      entity's own height, so a billboard is **bottom-anchored** at its z
      -- the same anchoring M62's floor snap assumes.

    - **Recorded as unverified, not asserted:** taken literally that
      formula makes crypt1's first flame 1024 x 1280 world units, four
      tiles by five. The doubled multiply is in the disassembly rather
      than a decompiler artefact and is reproduced as-is, but nothing in
      this port draws a billboard, so the magnitudes have not been seen.
      The mapping is exact; the scale is a claim nobody has checked.

    - **Simulated, not drawn** -- the same position M48's spell
      projectile is in and for the same reason: `render3d/zone_renderer.h`
      draws `models.idx` meshes and has no billboard pass, so adding one
      is its own milestone. Everything such a pass needs is here and is
      real: the current slot every tick, the world position, the
      camera-space half-extents, and the scale. main.cpp already loads the
      active zone's `<zone>_sprites.txt`, so the art is in memory whenever
      a zone's effects exist.

    - **A manifest shape that had no reason before.** `azra_sprites.txt`,
      `ghstpass_sprites.txt` and `snowline_sprites.txt` are 191 entries
      and every other zone's is 215; the extra 24 are exactly 181-204,
      the two effect runs. Weak evidence on its own -- the 22 shipped
      manifests are only three distinct files, so they are boilerplate
      rather than per-zone-tailored -- but it points the same way as the
      geometry.

    - **Not reproduced:** the multiplayer mirror (`FUN_1003c124` replays
      a remote `CreateEffect` from a packet with one field per argument,
      which is what pinned the argument *types*), and the two
      engine-internal spawners -- the blood spurt and `FUN_10067f84`'s
      attached effect -- which are separate call paths rather than
      script-visible bindings. The struct covers both; nothing raises
      them yet.


- [x] **M64 -- the `levelup.s` cluster, and the character system behind
  it.** The bullet asked for four natives and eight setters. Following
  `UpdateAttributes` one call deep turned up **the game's whole
  race/class/level system**, none of which had been looked at: the
  per-race starting attributes, the nine class rows, the experience
  curve, and the two ability ranks every level raises.
  `simkin_bindings/character_progression.h`/`.cpp` for the three tables,
  four new methods on `PlayerExecutable`, and M58's one deliberately
  unfinished branch closed. Smoke test
  `src/tests/m64_level_up_smoke.cpp`, **142 checks**. Suite **57/57**,
  soft-fails **39 -> 39**, `GetPlayer()` coverage **92% -> 94%** (42 more
  handled call sites, 13 fewer unhandled names).

    - **The character-creation stat system exists, and M10 said it did
      not.** `player_executable.h` has carried this since M10: "Stat block
      values are fixed, documented placeholders (strength=50 etc.) -- no
      character-creation stat-rolling system exists (M5's race/class
      picker doesn't feed into these), so there was nothing to derive them
      from." There is one. `chooseportraitmenu.s`'s two handlers each end
      `GetPlayer().UpdateAttributes(true)`, and that native
      (`FUN_1001fc24`) is an eight-arm switch on the race, each arm
      branching on the sex, writing all eight attributes from a table of
      30 / 40 / 50. The picker did feed into them; nothing had followed
      the call.

      The mapping is confirmed by the series' own lore, which is what
      rules out an off-by-one: Nord strength 50, Breton intelligence *and*
      willpower 50, Redguard endurance 50, Khajiit agility 50, Dark Elf
      speed 50, Wood Elf agility and speed 50, High Elf intelligence and
      personality 50 -- and High Elf is separately the one race that gains
      magicka per level.

    - **Two of the sixteen rows are unbalanced, and they are real.** Six
      races total exactly 310 points whichever sex is picked -- the sex
      branch is a pure swap. Two are not: an **Argonian male totals 330**
      against the female's 310, and a **High Elf totals 320 male / 330
      female**, both above everyone else's 310. Verified in the ARM
      disassembly, not just the decompiler's C, precisely because six
      clean rows make the other two look like a transcription error. They
      are not; the jump table's arms really do write those values, and
      nothing downstream rebalances them.

    - **`UpdateAttributes` is two functions sharing a name**, selected by
      the character's *current* level, which is why `ChooseCharacter`'s
      third line (`player+0x3e0 = 1`, the level) is load-bearing and why
      this port had to start reproducing it. At level 1 it seeds the
      attributes and refills all three pools; above level 1 it skips the
      table entirely and instead raises two ability ranks, rescales the
      magicka bonus, and refills only if asked. `levelup.s`'s back key
      passes false, `chooseportraitmenu.s` passes true, and that single
      boolean is the whole difference between the two callers.

    - **Every level-up counts twice.** The rank increments are
      unconditional in the above-level-1 path, so they count
      *UpdateAttributes calls*, not levels -- and a real level-up makes
      two of them: `FUN_10044618` (the award) calls it, then the player
      closes the screen and `LevelUpBack` calls it again. So a character
      who gains one level comes out two ranks higher, and a High Elf
      collects the racial +5 magicka twice. Nothing debounces it.
      Reproduced and asserted in the test rather than tidied up.

    - **M58's one open branch, closed.** `effects.cpp`'s
      `RecomputeDerivedStats` carried "the real player branch adds two
      fields at player+0xfb8/+0xfba and returns early -- without writing
      max magicka at all -- when the character's class row says the class
      has none. This port has neither the class table nor those two
      fields." This milestone supplies both, via two new `SpellActor`
      virtuals whose defaults keep the creature arm exactly as it was.
      The early return is the interesting half: max health and max fatigue
      are *already written* by the time it fires, so a non-caster's
      recompute is a partial one, and its max magicka keeps whatever was
      last put there rather than being zeroed. The test proves that by
      writing max magicka through the effect table and watching a
      `SetEndurance` move health while leaving magicka alone.

    - **`HasMagic` and the magicka growth are different sets of classes.**
      The class table (`FUN_1001f9ac`, nine 0x10-byte rows built field by
      field in code) has a `+6` byte the `HasMagic` native returns
      directly: Battlemage, Nightblade, Spellsword, Sorcerer. The growth
      switch inside `UpdateAttributes` has arms for Battlemage,
      Nightblade and Sorcerer only. **A Spellsword has magic and no
      growth arm**, so its pool is its intelligence at level 1 and its
      intelligence at level 40. Battlemage's and Sorcerer's arms are two
      separate code paths computing the identical `0x26` -- the compiler
      folded the constant differently in each -- so they are not merely
      similar, they are the same number, which makes the Spellsword's
      absence look like an omission rather than a tuning choice. Recorded,
      not corrected.

    - **The experience curve is the triangular numbers.** `AddExperience`
      (`FUN_1004a104`) is also the level-up trigger, and it is the only
      thing in the shipped game that awards a level-up point outside
      `cheatmenu.s`'s `LevelUp()` -- which is what makes the whole screen
      reachable. Its threshold is a per-class base (900 Thief/Barbarian,
      1000 Knight/Nightblade/Rogue, 1100 Assassin/Spellsword, 1200
      Battlemage/Sorcerer) times a 99-entry `u16` table, and reading that
      table out of the binary gives 1, 3, 6, 10, 15 ... 4950: `n(n+1)/2`
      for all 99 entries, so it is reproduced as the closed form rather
      than as 198 bytes of data. Three quirks came with it and are all
      reproduced: the test is strictly greater (exactly the threshold does
      not level), **only one level per call** however large the award, and
      the amount is converted through a 16-bit read on the way in, so
      `StatModXP(40000)` banks -25536.

    - **`SetSpeed` is the one setter that does not recompute.** Seven of
      the eight attribute setters end `b 0x10048d50`, a shared
      `mov r0, r6 / bl 0x10049698` tail; `SetSpeed` ends `b 0x10048428`,
      straight to the epilogue. Checked in the disassembly because it is
      exactly the kind of distinction a decompiler flattens. Speed feeds
      none of the three derived maxima, so on its own it changes nothing
      -- it is observable only as a *missed* recompute, which is how the
      test catches it: move strength behind the recompute's back, then
      watch `SetSpeed` leave the stale maximum and any other setter fix
      it.

    - **`GetSpecialAbility` and `GetRaceAbility` were returning the wrong
      type.** Both answered the string `"None"`. Both real cases return an
      **int** -- the class rank at `player+0xfb0` and the race rank at
      `+0xfb4`, the same two words `UpdateAttributes` raises --  and
      `statsscreen.s` writes `""#GetPlayer().GetSpecialAbility()`,
      string-concatenating a number. Their setters were missing entirely;
      three guild-training conversations and `crypt2/shadowgate.s` use
      them to sell a rank outright. Neither getter reads its argument
      count in the engine, which matters because
      `dstar_e/join_thief_guild.s` calls `GetSpecialAbility(val)` with a
      stray argument -- so neither checks it here.

    - **A menu-layout constant this port had guessed, now decompiled.**
      `levelup.s` line 20 is `SetStartCoord(10)`, its only unhandled
      native and the corpus's **only** call to it. It writes `menu+0x96`,
      which is the y the real menu draw (`FUN_10076b64`) starts its row
      cursor at -- and the menu constructor seeds that field with `0x32`.
      main.cpp had `menu.backgroundId() == 69 ? 50 : 8` with a comment
      saying the 50 was measured off a screenshot because "no native
      y-offset call exists". The 50 was right and the reasoning was not:
      it is every menu's default, not the main menu's logo art. Now
      `menu.startCoord()`. `AddTitle` appends to the same row list the
      draw walks, so the title sits at the start coordinate too, which is
      the shape this port already had. **Not screenshot-verified** -- it
      moves every non-main menu's list down 42 pixels, the largest visible
      change in this milestone.

    - **`DisplayLevelUp` is how the screen opens**, and no script calls
      it: the six `Display*` natives are the character-manager menu's, and
      five of them hand a screen-mode number to one shared setter while
      this one calls `FUN_1002c274(controller, 8)` directly. Implemented
      so `levelup.s` is reachable at all; the engine-side trigger that
      raises it (`FUN_1002f694` gates on `levelUpPoints < 1`) is a HUD
      prompt this port does not have.

    - **Five save words that had slots and nothing to put in them.** M50
      and M52 recovered `+0xf44`, `+0xfb0`, `+0xfb4`, `+0xfb8`, `+0xfba`
      and the class byte into `SavedPlayer`; every one of them was being
      written as a hard zero. All five round-trip now. The class matters
      most: it is what every derived number is computed from, so a save
      that dropped it came back as `FUN_1003d670`'s default Battlemage
      wearing someone else's numbers.

    - **A fourth way to be invisible to the coverage tool.** The eight
      setters were first written as one loop over a `{name, field,
      recomputes}` table -- tidier than eight near-identical blocks, and
      the tool scored them as still unimplemented. It collects
      `skString("Name")` *literals* out of the binding sources, so a name
      that reaches `skString` through a variable does not exist as far as
      it is concerned. Rewritten as a literal chain (one shared body, an
      `if`/`else if` picking the field) so the measurement keeps working.
      Worth knowing before refactoring any handler group.

    - **Not reproduced:** the two ability ranks' *effects* beyond magicka
      -- the Thief's trap-avoidance bonus (`CanAvoidTrap` adds `+0xfb0`
      for class 8) and the Argonian's haggling bonus (`FUN_10020894` adds
      `+0xfb4` for race 0) are separate systems that happen to read these
      fields; the fields are correct now, the two consumers are not
      implemented. Nor are the three unnamed `u16`s in each class row,
      which look like starting-equipment ids but are read by nothing this
      port has decompiled, so they are carried verbatim rather than
      claimed.

- [x] **M65 -- the `TestX` attribute rolls, and the `crypt1.s` gauntlet
  they gate.** Character-stats cases `0x0b..0x12`: eight bindings, one per
  attribute, 14 shipped call sites. The roll itself is four instructions,
  but they are not the four anyone would guess, and following the fail
  branch turned up two natives with no handler anywhere in the port.
  `AttributeCheck()` in `simkin_bindings/character_progression.h`/`.cpp`,
  the eight bindings plus `DoDamage` on `PlayerExecutable`, and `Random`
  on `MenuExecutable`. Smoke test
  `src/tests/m65_attribute_check_smoke.cpp`, **40 checks**. Suite
  **58/58**, soft-fails **39 -> 39**, `GetPlayer()` coverage
  **94% -> 96%** (33 more handled call sites, 5 fewer unhandled names).

    - **The difficulty is not a threshold -- it is extra sides on the
      die.** All eight arms of the dispatcher are the same shape:

      ```
      span = attribute + difficulty        // the script's argument
      roll = Math::Rand(engine->iSeed) % span
      return !(attribute < roll)
      ```

      The obvious readings are all wrong. It is not "roll d100 and beat the
      difficulty", and it is not "the attribute must exceed the
      difficulty": the attribute is *inside the die*, so a bigger attribute
      both widens the range and raises the passing band, and the pass
      chance is exactly

      ```
      P(pass) = (attribute + 1) / (attribute + difficulty)
      ```

      At strength 50 the easiest shipped check (`TestStrength(25)`) passes
      68% of the time and the hardest (`TestStrength(45)`) still passes
      54%. Measured against the closed form over 20 (attribute, difficulty)
      pairs at 200k trials each; worst error 0.0012.

    - **A check is never certain and never impossible.** Both follow from
      the `+1` and the `>=`, and both are asserted rather than assumed.
      `P(fail) = (difficulty - 1) / (attribute + difficulty)`, so a
      difficulty of 1 or 0 can never be failed *at any attribute* -- which
      the field-map test below then leans on to get a deterministic answer
      out of a random function. In the other direction, 24 of the 1045
      outcomes still fail at attribute 1000 against difficulty 45, and a
      character with the attribute at zero still passes roughly one attempt
      in 45. There is no critical success, no critical failure, and no luck
      term -- `TestLuck` exists as a binding and no script calls it.

    - **The binding-index order is not the setter order, and the fields
      still agree.** Reading the eight `Test` cases straight down gives
      Strength, Intelligence, **Agility**, **Will**, Speed, Endurance,
      Personality, Luck; reading the eight setters at `0x24..0x2b` gives
      Strength, Intelligence, **Will**, **Agility**, Speed, ... The two
      middle entries are transposed between the two tables. This is the one
      slip in this milestone that would have been invisible in play and
      wrong forever, so it was checked three ways: against the getters at
      `0x1b..0x23` (which read `+0x18` for `GetAgility` and `+0x1a` for
      `GetWill`, agreeing with the `Test` cases), against the setters'
      actual stores (`0x26 SetWillpower` writes `+0x1a`, `0x27 SetAgility`
      writes `+0x18` -- so the *fields* agree and only the indices differ),
      and
      against `effects.h`'s independently-derived stat map from M58, which
      lists the same two offsets. The smoke test's Part 4 then drives all
      eight bindings against all eight fields and requires an exact 8x8
      identity matrix, writing the fields through `EffectStatSlot()` so
      that a future divergence between this milestone's map and M58's fails
      a test rather than going unnoticed.

    - **The roll reads the live field.** It reads `stats+0x14..0x22`
      directly -- the same storage the effect applier writes -- so a
      Fortify Strength really does move the odds of a strength check, and a
      drain really does move them the other way. Checked end to end: 30
      strength against difficulty 45 passes 41%, and after a `+40` effect
      the same check passes 62%.

    - **A drained attribute always fails, whichever way the span lands.**
      Only reachable through the effects system, but it is reachable, and
      the arithmetic is not obviously safe: a large enough drain makes
      `attribute + difficulty` negative. Signed remainder takes the
      *dividend's* sign and `Math::Rand` is documented non-negative, so the
      roll stays `>= 0` either way and the comparison fails. Reproduced as
      found rather than clamped.

    - **What failing a check costs, which nothing implemented.** Every one
      of the 14 sites has the same shape -- a named trigger zone, a
      once-only `saved_` flag, and a pass/fail popup -- and the fail popups
      are where the milestone stopped being about one native:

      - `DoDamage` (Character-stats `0x16`) had **19 live call sites and no
        handler on any receiver**. All three fail menus open with
        `GetPlayer().DoDamage(Random(2,4))` (4-8 and 6-12 for the harder
        tiers), so without it the entire cost of failing a check was
        silently zero. It needed no new RE: the case reads its argument as
        a short and calls the stats block's own vtable slot at `+0x80`
        index 4, which is `FUN_10049e78` -- already recovered in M58 as
        `ApplyDamage()`, Sanctuary gate included. So this is a route, not
        behaviour. All 19 live sites are `GetPlayer()`; the only trace of
        anyone damaging a creature this way is a **commented-out**
        `target.DoDamage(...)` in `blaze.s`.

      - `Random(min, max)` is a Menu-class binding in its own right (index
        `0x6a` on the menu dispatcher `FUN_10078de4`), not only the
        root-class one M21 wired into items and creatures, and a menu
        script reaches it bare from inside `Init()`. It soft-failed to 0,
        which is what made `DoDamage(Random(2,4))` deal nothing even after
        `DoDamage` existed. One line, sharing M21's existing helper.

      This is M64's "a milestone that opens new ground can hold the
      soft-fail count steady only by finishing what it opened" happening
      again, and for the same reason: the new smoke test drives a dungeon
      the suite had never entered.

    - **Half the bindings have no caller.** `TestIntelligence`,
      `TestWill`, `TestPersonality` and `TestLuck` are registered and never
      used, which is asserted rather than assumed -- it is why those four
      have no shipped difficulty to check them against, and why Part 4's
      matrix is the only thing pinning their fields.

    - **The whole feature is one dungeon.** All 14 calls are in `crypt1.s`:
      four attributes (Strength, Agility, Endurance, Speed) at difficulties
      25, 30 and 35, plus two strength-only checks at 40 and 45. Passing
      opens `crypt1/checkpass.s`; failing opens `checkfail5`/`6`/`7to9.s`,
      which deal 2-4, 4-8 and 6-12. The same dungeon separately has four
      `brazier_*.s` objects that cost health and grant a permanent `+5` to
      the matching attribute -- the intended way to pass the later tiers.
      Part 6 of the smoke test drives the real script: a forced failure
      (strength -2, which cannot pass) opens the fail menu and costs
      health, the check stays live and hurts again on re-entry, a
      high-strength character passes and latches `saved_str5` so five more
      entries cost nothing, and `str6` is confirmed independent with its
      own flag.

    - **Not reproduced.** Symbian's `Math::Rand` LCG itself -- the engine
      seeds it from the clock and its exact stream is not observable, so
      this uses the same host `std::rand()` every other roll in the port
      already uses. The four `crypt1` brazier menus that grant the `+5`
      rewards work (they are `AddEffect`, which M58 implemented) but the
      trigger objects that open them are entity `OnUse` handlers, not part
      of this milestone.

- [x] **M66 -- the `ZoneRenderer::Render` crash, and a second one behind
  it.** The oldest open item in this file: an access violation recorded at
  `render3d/zone_renderer.cpp:731` since M56, never chased, and the stated
  reason M50-M56 were all verified against data and unit tests rather than
  by playing. It reproduces in about two hundred frames, and the cause is a
  clip buffer sized from a bound that does not hold for this geometry.
  Fixed in `ZoneRenderer::Render`/`ClipNear`; smoke test
  `src/tests/m66_render_clip_smoke.cpp`, **16 checks**. Suite **59/59**,
  soft-fails **39 -> 39** (this milestone adds no script coverage, so
  neither measure was expected to move).

    - **Reproduced first, in a harness, not by playing.** A sweep that
      stands the camera on sampled open tiles and renders every yaw at
      seven pitches faulted after 224 frames with
      `ACCESS_VIOLATION (0xc0000005)`, reading an address inside its own
      stack frame. The existing M36 crash reporter
      (`platform/win32/crash_report.h`) was linked into the harness to
      symbolize it, and named `ZoneRenderer::Render` -- the same projection
      loop the roadmap's line number points at. Adding *camera pitch* to
      the sweep is what made it reproduce at all; nothing else about the
      harness is clever.

    - **The bug: a textbook bound applied to non-textbook geometry.**
      `ClipNear` is a Sutherland-Hodgman clip against the near plane, and
      it was documented as needing room for `count + 1` outputs -- true,
      and only true, for a **planar convex** polygon. A plane meets one of
      those in a line, so exactly two of its edges cross, giving
      `kept + 2 <= count + 1`. The tile-grid pipeline hands it quads that
      are neither. It writes six vertices into a five-entry
      `ViewVertex clipped[5]`, and the projection loop then writes a sixth
      `ProjectedVertex pv[5]` -- both on the stack, in the frame that also
      holds the `faces` vector and the loop counter, which is why the fault
      surfaces as a wild *read* a line or two after the write that caused
      it.

    - **Two independent things in the shipped data break the
      precondition,** and the smoke test's Part 2 replicates the renderer's
      own view transform to demonstrate each from real `.zcp` records
      rather than from argument:

        1. **Four independent corner heights.** `AddFloorCeiling` builds
           every floor and ceiling from `ZcpEntry::floorHeight[4]`, i.e. a
           bilinear patch, not a plane. It is planar only when the two
           diagonals' height sums agree, and across the 21 zones
           **33,363 of 248,001 open tiles have a non-planar floor** and
           7,386 a non-planar ceiling -- 47.2% of azra, 48.8% of stouttp,
           45.2% of ghstpass. Once the camera pitches, a vertex's view
           `forward` picks up a height term, and such a tile can put its
           four corners in an in-out-in-out ring: `azra` tile (81,2),
           heights `[-1984 -608 -4608 -1664]`, gives `2 kept + 4 crossings`
           = six vertices.
        2. **M28's half-tile corner nudge**, with no pitch at all. The
           nudge moves each vertex by 128 raw units from the cell it falls
           in, so a tile's four XY positions stop being the corners of a
           square and the projected ring can lose convexity. `azra` tile
           (48,5) -- flat, all four heights within 320 units -- reaches six
           vertices at `pitch == 0`. 6.7% of azra's open tiles are nudged.

      The same sweep confirms the bound *does* hold where its precondition
      does: a planar, un-nudged tile never exceeded five in 5.4 million
      probes. So the old comment was not wrong so much as unqualified.

    - **The fix is to clip the two triangles the quad is already drawn
      as.** The rasterizer never draws a quad; the old code clipped a
      four-sided ring and then fanned the result into triangles. Splitting
      into `{0,1,2}` and `{0,2,3}` *before* clipping restores `count + 1`
      exactly (a triangle is planar by construction), and for an unclipped
      quad it produces the identical pair the fan did. `ClipNear` also
      takes an explicit `capacity` now -- the invariant the old code stated
      in prose and got wrong, moved somewhere the running program can see
      it.

    - **What that changes on screen, measured rather than asserted.**
      Twelve of the fourteen tracked `.ppm` render dumps are **byte-identical**
      before and after. The two that move -- the entity-render views at yaw
      0 and 270 -- move *entirely* because of `SurfaceDetailMask`, the real
      engine's depth-keyed texel-precision band, which is chosen per
      triangle from its three vertex depths: changing which triangles exist
      near the near plane can move a face into a different band. Pinning
      that mask to a constant makes even those two byte-identical, which is
      the experiment that says the difference is texel precision on
      near-camera surfaces and nothing else. It is also the better answer,
      since the band is now measured on the triangles actually being
      rasterized. Part 4 of the smoke test covers the other half of the
      argument directly: on a planar quad the two possible triangulations
      agree to 2e-6 at every interior sample, so where the split runs is
      not observable.

    - **A second crash, on the same function, found by the same sweep --
      and the more likely one in practice.** Once the clip overflow was
      fixed the sweep still died, on the *second* zone, with
      `Assertion failed: invalid bounds arguments passed to std::clamp`.
      `RasterizeModelTriangle` clamps texel coordinates to
      `[0, model.width - 1]`, and **nine of the twenty-one shipped zones
      load a `.zsk` room mesh whose texture header reads
      `skinCount=256, width=256, height=0`** -- broken1, broken2, crypt1,
      crypt2, crypt3, erthcave, ffarena, lothcav, twilite. `hi < lo` is
      undefined behaviour, and a Debug MSVC STL turns it into an outright
      `abort()`, so a Debug build died on the first frame of any of those
      nine zones -- including all three crypts and twilite, i.e. the whole
      dungeon chain M65 had just been working on. `port/build.bat` builds
      Debug, so that is the configuration anyone playing this port is
      running. Guarded: a model with no skin pixels draws nothing, which is
      exactly what already happened (every `TexelAt` sample was returning
      the chroma key), just without the abort.

    - **Open, and deliberately not chased here: what a `.zsk` actually
      is.** The nine zones above ship *byte-identical* `.zsk` files, and so
      do several of the others -- across all 21 zones there are only
      **three distinct meshes**: a 30-vertex/56-face one shared by eleven
      zones, a 98-vertex/192-face one shared by the nine above, and a
      variant of the first unique to raiders. Their 128 KB "texture" blocks
      are all zeros in the second group. Whatever these are, they are not
      the per-zone baked room mesh M11 took them for, and the port draws
      them at a zero offset in every zone. That is a data/loader question
      in `world/zone.cpp` + `ParseModelResource`, not a renderer one, and
      re-opening M11 is a milestone of its own. Recorded here because the
      renderer now survives them either way.

    - **Not reproduced.** The exact original crash report from M56 --
      whether the player who hit it hit the clip overflow, the `std::clamp`
      abort, or both -- cannot be recovered; the recorded line number
      matches the projection loop, so this entry attributes it to the clip
      overflow. `render_clip_smoke` takes ~75s at its default stride
      (Part 3's broad sweep is essentially all of it; Parts 1 and 2 cost
      under half a second between them), and takes a stride and a zone name
      to narrow or widen it.

- [x] **M67 -- "the door is a wall no matter if it's open or closed": the
  entity tile stamp.** Reported from actual play, at the first house in
  azra. Every door in the game was impassable, opened or shut, and the
  cause was a piece of the engine this port had read the *output* of since
  M44 without ever knowing it was output. Fixed in `Zone::StampEntityBox` +
  `DoorExecutable::SetPassable`; smoke test
  `src/tests/m67_door_stamp_smoke.cpp`, **22 checks**. Suite **60/60**,
  soft-fails **39 -> 39**, and all 14 tracked `.ppm` renders unchanged
  (this milestone does not touch the renderer).

    - **The bit was right; nothing ever cleared it.** `.zmp` cell byte 1
      bit 2 makes a tile block, and `Zone::CircleHitsWall` has read it
      since M44 under the name `IsLocked()`. M44 found it via
      `LockZone`/`UnlockZone` and reasonably assumed a *script* was the
      thing that set it. The shipped data says otherwise: **40,517 cells
      arrive with the bit already set, 36,332 of them open floor** -- 25%
      of azra's walkable map, 46% of glaciercrawl's. A bit that a quarter
      of a town's floor carries on disk is not "a barrier awaiting a
      key." (Both counts land exactly on M57's independent census in
      `ZONE_FORMAT.md`, which is a useful cross-check that this milestone
      and that one were measuring the same thing.)

    - **The corpus confirms it from the other direction.** Grepping all
      21 zone scripts finds **thirty `UnlockZone` call sites and not one
      `LockZone`**. Nothing ever locks at runtime, because whatever starts
      blocked already says so on disk. That asymmetry had been sitting in
      plain sight since M44 and is the tell.

    - **What actually sets it: `FUN_10066204`, the tile stamp.** The
      engine splits entity collision two ways (`world/model_collision.h`):
      up to half a tile it tests per-entity from the tile's own list, and
      **anything wider it bakes into the tile grid**, where it blocks like
      a wall. The bake walks the entity's box in 128-unit steps *rotated
      by its heading* and ORs bit 2 into each covered cell.
      `FUN_1006640c` is the same walk with `&= ~mask`. A door is
      `2 64 256` in `<zone>_models.txt` -- 256 is wider than half a tile,
      so **every door in the game is a tile-stamped entity**, and its
      closed footprint is part of the shipped `.zmp`.

    - **Verified against the data rather than argued.** Recomputing the
      footprint of every solid tile-stamped placement and re-stamping it
      onto the shipped grid: **2,069 placements across the 21 zones set
      2 previously-clear cells**, i.e. the walk, its half-tile step, its
      rotation and its edge clamp all reproduce what baked those bits,
      with 19 of 21 zones exactly clean (glaciercrawl and raiders differ
      by one cell each, at a grid edge where the real loop clamps). That
      is Part 2 of the smoke test, and it is what licenses calling this a
      transcription.

    - **The missing call site is `SetPassable`.** Object/Entity dispatch
      case 0x17 is
      `entity->passable = arg; if (isTileStamped()) arg ? unstamp(4,1) :
      stamp(4,1);` -- so a door's own `SetPassable` is what lifts its
      footprint out of the grid. This port did the assignment and none of
      the stamping. **That one missing line is the whole bug**: `door.s`
      opened the door, swung it, changed its use-text, and left its
      footprint sitting in the tile grid forever.

    - **Why it has to happen inside the call.** The walk uses the heading
      *at that moment*, and `door.s` changes passability on both sides of
      its turn -- `SetPassable(true); AddRotationTurn(-64*256)` to open,
      and `SetPassable(true); AddRotationTurn(64*256); SetPassable(false)`
      to close. That leading `SetPassable(true)` on the *closing* path
      has looked redundant since M15; it is not. It clears the old
      footprint before the door turns, so the re-stamp lands on the new
      heading. Reconciling passability after the script ran would clear
      the closed footprint at the open heading and vice versa. A native
      door path in the image spells the same three steps out literally.
      `DoorExecutable` therefore reaches the grid through a `TileStamp`
      interface, the same layering `LevelExecutable::ZoneRegions` uses
      (`sk_bindings` does not link `sk_world`).

    - **Renamed, because the old name is what hid this.** `IsLocked()` ->
      `IsBlocked()`, `kBlockLocked` -> `kBlockSolid`. The bit was never
      "locked"; it is "this square blocks", and it has two writers.

    - **Worth being honest about how long this sat there.** None of the
      RE was missing. `WORLD_MODEL.md` has had the stamp and its inverse
      written up since M55, and `ZONE_FORMAT.md`'s M57 census already said
      in as many words that bit 2 is "authored blocking, a locked gate and
      an oversized prop all land on one flag." Everything needed to see
      the bug was on the page. What nobody did was ask the next question
      -- *if something ORs this bit in per entity, what ANDs it out?* --
      and go looking for the call site. The lesson is not "decompile
      more": it is that a documented write with no documented erase is a
      loose end worth chasing, and that a **bug report from playing** got
      there in one afternoon when 23 milestones of data-driven
      verification had not, because every one of those milestones tested
      what the port computed rather than whether a door opened.

    - **Reproduced as-is, not fixed:** the bit is one bit and not a
      reference count, so two overlapping stamps do not survive one of
      them clearing. The engine has that flaw; the port now has it too,
      and the smoke test pins it so it stays deliberate.

    - **Left open.** Recall is the other half of Part 2 and is *low* --
      the stamps account for 12,403 of the 39,908 set cells, so roughly
      two thirds of the bit's on-disk population is authored level data
      (the barriers those 30 `UnlockZone` calls open, and whatever else).
      Naming the rest, and the byte's other populated values
      (0x02/0x20/0x40/0x60 -- 0x02 is already known to be the
      upper-storey selector `CollisionFloorHeightAt` reads), is a
      milestone of its own. Also unwired: an opened door's cleared
      footprint is journalled in `Zone::tileChanges()` exactly as the real
      `param_3 = 1` asks, but nothing writes that journal into a save yet,
      so a door reverts to shut across save/load.

### M68 -- the developer debug suite

Not a feature of the game: a tool for finding bugs in it. Requested
directly after M67, and for exactly the reason M67 gave.

M67 is the argument for this milestone. Every door in the game was
impassable, and that survived twenty-three milestones of data-driven
verification, a per-milestone smoke suite, two independent tile-flag
censuses and a documentation pass that had already written down the
mechanism. It was found in an afternoon by someone playing the game and
walking into a door. **Smoke tests verify what the port computes; they
cannot see a feature that was never wired at all.** The conclusion is not
"write better tests" -- it is that playing is the irreplaceable measure,
and playing is much faster with tools.

What it is: an in-game console (F1), a stat overlay (F2/F3), and an
instrumentation layer, built as one static library `sk_debug` behind a
CMake option that is checked to build clean both ways. Full reference in
**docs/DEBUG_SUITE.md**; what follows is only the reasoning worth keeping
in the roadmap.

- **It cannot affect the game's rendering, structurally rather than by
  promise.** The debug UI draws over the presented frame at the window's
  native resolution, through a GDI implementation of a new
  `sk::OverlaySurface` interface, *after* `StretchDIBits` has already put
  the game's 176x208 `Backbuffer` on screen. It never writes a pixel into
  the backbuffer. So it cannot perturb the software rasterizer and it
  cannot move the fourteen tracked `.ppm` dumps -- which is also the
  practical reason for it: at the game's own 6px bitmap font a 176px-wide
  console is 29 columns, and a command line needs more than that.

- **The native bridge is what keeps the command table small.** `call
  <receiver> <Method> [args]` and `sk <receiver> <code>` build a real
  `skRValueArray` and invoke the real binding object, or run a fragment
  through `skInterpreter::executeString`. Because the port already
  implements the real natives for stats, race, class, quests, gold,
  experience, equipment and entity creation, "change a stat" needed no
  interface method at all -- and, more importantly, it runs the *same*
  dispatch the shipped scripts run. In a behavioural port, a debug command
  that poked a C++ field directly would be exercising something the game
  never does, which makes it useless as evidence. The same reasoning made
  `spawn` route through `Level.CreateEntity` (`entities.txt`'s fourth
  column is the script path, so a script name resolves back to its type id
  and both forms take the engine's own factory) and `killall` route
  through `ApplyDamage` (so loot drops, kill counters and zone triggers all
  really run).

- **The one place a curated command beat the bridge**, and it is a real
  finding rather than a convenience: the engine recomputes derived stats in
  `UpdateAttributes`, not in the eight attribute setters. A bare
  `SetStrength(90)` leaves max health, magicka, attack and defense stale.
  `stat str 90` calls the setter *and* `UpdateAttributes`, which is what
  makes it mean what a level-up means. `race` and `class` do the same.

- **"What is running" came from two interpreter hooks this port had never
  touched.** The vendored Simkin carries `setTraceCallback` and
  `setStatementStepper`; with `Interpreter.tracing` on, the first logs
  every script method call with its source location and line, and the
  second gives a per-frame statement count and turns a script exception
  into a recorded event. Two landmines, both handled and both commented at
  the call site because getting either wrong would change how the game
  runs: `statementExecuted` returning false **halts the running method**,
  and `exceptionEncountered` returning false **swallows the exception**.
  This tracer always returns true. With tracing off both hooks are set back
  to null, so a normal session runs the identical path it ran before M68.

- **`mark` / `diff` is the actual bug-hunting loop.** Snapshot every
  counter, do the thing, and see only what moved. It is the direct answer
  to "detailed stats on what is running each time I do something", and it
  beats reading a log because it answers by subtraction rather than by
  search.

- **Commands are queued on Enter and drained from inside the game tick.**
  A console command can run real script code, and running script from a
  window procedure would execute it outside the tick, on a half-updated
  world, in a place no exception handler covers. A throwing command is
  caught and printed rather than taking the process down -- the one thing a
  debug tool must never do.

- **The layering is the existing one.** `sk_debug` links `simkin` and
  `sk_bindings` but deliberately not `sk_world`, and cannot see main.cpp's
  file-local `MonsterInstance`/`DoorInstance`/`PickupInstance` types.
  Everything arrives through an abstract `sk_debug::DebugHost` that
  main.cpp implements -- the same seam as `LevelExecutable::ZoneRegions`
  (M44) and `DoorExecutable::TileStamp` (M67). The interface stayed at
  sixteen virtuals because every read-only view is a generic `StatGroup`
  list keyed by a page name, so one virtual serves every panel and the
  overlay has no per-page code.

- **Removability was verified, not asserted.** `-DSK_DEBUG_SUITE=OFF`
  builds clean: the library is not built, the macro is undefined, and all
  twelve `#if SK_DEBUG_SUITE` blocks in main.cpp compile out. What is left
  behind is four inert additions to game files (a null function-pointer
  hook in `native_binding_common.h`, a null `std::function` in
  `Window::Present`, `EntityTypeTable::all()`, and seven
  `PlayerExecutable` attribute getters mirroring the `willpower()` already
  there).

- **It found two things on its first live run, which is the point.** Both
  are recorded here because both are engine behaviour, not tool bugs:

  1. **`UpdateAttributes` at level 1 undoes an attribute you just set.**
     `stat str 90` on a fresh character reported `str: 50 -> 40`. The
     header for `FUN_1001fc24` has said since M64 that the level-1 path
     *reseeds all eight attributes from race and sex* -- it was written
     down and still surprised the first command that used it. `stat` now
     calls the recompute only above level 1 and says plainly what it
     skipped and why; both branches are pinned in the smoke test.
  2. **`Level.CreateEntity` has a single pending slot, not a queue.**
     `spawn 202 3` created one creature. `LevelExecutable` parks the
     result in one slot the tick drains, on the documented grounds that
     nothing in the corpus calls `CreateEntity` twice without an
     `AddObject` between -- true of the shipped scripts, and not true of a
     console. That is a faithful reproduction, so the fix belongs on the
     tool's side: `spawn` now queues and issues one call per tick (which
     is what `DebugHost::HostTick` exists for) and says so in its reply.

  Neither would have been found by any test that did not actually run the
  command. That is M67's lesson repeating itself inside the tool built to
  answer it.

- **Measurement.** This milestone moves neither script-coverage measure and
  correctly so -- soft-fails **39 -> 39**, and no native was implemented.
  The evidence is instead: `debug_suite_smoke` (70 checks, and the suite is
  built against abstractions precisely so it can be driven with no window,
  no zone and no interpreter -- including a sweep that runs *every*
  registered command with no arguments, which is the failure a console is
  most likely to have); the full suite at **61/61**; all fourteen tracked
  `.ppm` renders byte-identical; the `SK_DEBUG_SUITE=OFF` configuration
  building clean; and the thing itself driven end to end on a live window
  by posting real key messages to it. That last run is worth recording:
  `zone azra` from the main menu skipped character creation entirely,
  `ents door` listed all seven of azra's doors including `homdoor` at
  (30130, 11272) -- M67's door -- as `solid`, `tpt 117 44` teleported onto
  its doorway tile and warned `[that tile is BLOCKED]`, and the `tilegrid`
  overlay drew the doorway in the block bits themselves:

  ```
  y42  W...W...#
  y43  W...W...#
  y44  W...@...#
  y45  W...#....
  ```

  M67 took a day of Python census work to reach that picture. It is now
  four keystrokes.

- **Left open.** The `freezeai` toggle stops the creature tick but not
  projectiles, script delays or the effect clock, so "freeze" is not yet a
  true single-step pause; a `step` command that advances exactly one tick
  is the natural follow-up. There is no way to *save* a console session's
  state, so `exec` scripts are written by hand rather than recorded.
  Nothing yet visualises geometry in the 3D view itself (collision boxes,
  the visibility raycast, entity origins) -- that would need drawing into
  the backbuffer, which this milestone deliberately does not do, so it
  needs a different mechanism than the overlay.

### M69 -- the real control scheme

The port's PC key layout came from the M0 scaffold and its own comment
called it a guess: arrow keys for the D-pad, top-row digits for the keypad,
`-`/`=` "arbitrary-but-documented" stand-ins for `*`/`#`. It was never
anyone's actual playing layout. Replaced with the user's own N-Gage
emulator binding sheet, verbatim, so muscle memory transfers between the
emulator and this port.

**Only the physical-key side changed.** `InputState::InitDefaultBindings`
-- the action -> slot indirection recovered from `FUN_1001a220`, which is
the game's own default control scheme -- is untouched. What a PC key *is*
changed; what that N-Gage button *does* did not. Nor is any of this
persisted: `dragonstar.set`'s ACTIONMAP block holds the logical table, so
an existing settings file has no effect on the new layout.

    W A S D        D-pad          move forward / turn left / back / turn right
    arrow keys     keypad 2/4/6/8 look up-down, sidestep left-right
    Space          keypad 1       jump
    E              keypad 3       Use -- doors, NPCs, pickups
    Q              keypad 7       swing the left-hand weapon
    left mouse     keypad 5       swing the right-hand weapon
    M / G / C      keypad 9/0/*   map, cycle right queue, cycle left queue
    Tab            keypad #       character manager
    Return         middle softkey confirm
    Esc            left/right     back / cancel

Three things the sheet forced, each a decision rather than a transcription:

- **The left mouse button** is the sheet's only non-keyboard binding, and
  the port had no mouse input at all. Delivered through the existing key
  callback as `VK_LBUTTON` -- a virtual-key code `WM_KEYDOWN` can never
  produce -- so the whole mapping stays one table and nothing downstream
  needs a second input path. `WM_LBUTTONDOWN` takes the mouse capture,
  because pressing inside the window and releasing outside otherwise never
  delivers `WM_LBUTTONUP` and leaves the slot latched down forever: the
  same hazard `window.cpp`'s `WM_SYSKEYUP` comment already documents for
  Alt. Adding capture made the neighbouring gap obvious, so `WM_KILLFOCUS`
  now clears held keys too (`InputState::ReleaseAll`) -- alt-tabbing away
  mid-stride used to leave the game walking forward.

- **The sheet gives Esc as both the left and the right softkey**, and
  Return as the middle one. This port has only two selection slots (the
  engine's own table is 21 slots; 18/19/20 are gaps). `RightSelectionKey`
  is the one confirmed to mean back/cancel -- `mainmenu.s`'s
  `OnRightSoftkey` backs out to the quit-confirm popup -- so Esc goes
  there, and Return, the D-pad centre a device actually confirms with,
  takes `LeftSelectionKey`. Which is what the previous scheme already did,
  for the same reason.

- **F3 and F4 (green/red softkey) map to nothing.** They are the N-Gage's
  call and end-call keys and the engine's input table has no slot for
  either. Left unmapped rather than invented -- but the **debug suite moved
  off them anyway**, because the user's sheet says they are game keys and
  an F3 that opens a debug panel would be surprising whether or not the
  game listens. M68's F1-F4 block became F1/F2 with Shift for the secondary
  actions (Shift+F1 mini bar, Shift+F2 previous page), which also keeps all
  eight of F5-F12 free for `bind`. `bind` now refuses F1-F4 outright:
  binding a key the game uses would silently swallow it, which is a worse
  failure than not being able to bind it.

The collision question the user actually asked is now a test rather than a
claim -- `debug_suite_smoke` walks every virtual key the sheet assigns to
the game (WASD, the arrows, Space, E, Q, M, C, G, Tab, Return, Esc,
VK_LBUTTON, F3, F4) through an attached `DebugSuite` and fails if any of
them is consumed, then checks the inverse: that an open console swallows
`E` so typing a command never opens a door.

Verified by playing: `M` opened the map, `Tab` the character manager, `W`
walked, and `E` on azra's `homdoor` flipped it from `solid` to `passable`
in `ents` (M67's fix, reached through the new Use key), while two mouse
clicks and one `Q` registered as `combat.swings +3` in `diff`. Suite 61/61,
soft-fails 39 -> 39, all 14 tracked `.ppm` renders byte-identical,
`debug_suite_smoke` 70 -> 87 checks.

- [x] **M70 -- what a `.zsk` actually is: the zone's skybox.** M66's last
  finding, left open deliberately: all 21 zones ship only three distinct
  `.zsk` meshes between them, nine of them byte-identical, and M11's
  reading of `.zsk` as *this zone's* baked room geometry -- verified
  against `azra` alone -- cannot survive nine zones sharing one file.
  It is the **skybox**, and nothing about it was a judgement call.

    - **The engine says the word.** Grepping `6r51.app`'s strings for sky
      terms lands on `"InitLevel Pre skybox load"` /
      `"InitLevel Post skybox load"` and
      `"Bullseye constructer Post newing Skybox"` (plus `"Skybox"` on its
      own in dozens of assert/debug sites). Chasing the first two through
      their literal-pool references (`0x10026618`/`0x10026620`) lands
      inside `GameEngine_InitLevel` on exactly the `WholeFile_Load` call
      whose result becomes `(*(engine+0x62c))+0x54` -- the `.zsk` load this
      project has been tracing since M11, now with the engine's own name
      attached to it. `.zsk` is "zone sky", the same naming as `.ztx`
      (texture), `.zlu` (LUT), `.zfg` (fog).

    - **The shipped data says it too, and answers the nine-zone puzzle.**
      Three meshes but **six distinct skins**: what varies per zone is the
      picture, not the geometry -- the shape of a shared sky asset, not of
      per-zone rooms. Dumped as images, eight of those skins are
      unmistakable: `azra` is a night sky with **Masser and Secunda**,
      `drgnfld`/`ghstpass`/`snowline`/`stouttp` share a blue day sky,
      `glaciercrawl` a grey blizzard, `dstar_e`/`dstar_w` a hazy overcast.
      The other thirteen are black, and the split against the shipped
      display names is exact: every open-air zone has a picture, every
      interior (the three Crypts of Hearts, Earthtear/Fearfrost/Loth'Na
      Caverns, both Broken Wings, Twilight Temple, Delfran's Hideout,
      Lakvan's Stronghold, Raider's Nest, the arena) has black. The
      30-vertex mesh is a closed dome -- four rings of shrinking radius
      (254 → 236 → 181 → 91) around a single apex -- and that apex maps to
      texel **(127,129)**, the centre of the 256x256 skin. The skin is a
      fisheye projection and the apex is the zenith.

    - **The draw path says it a third time.** In `Render3DScene` the mesh
      is not one more thing drawn into the scene: it is *the alternative to
      clearing the frame*. `if (!engine+0xbe0e || !skybox->model) { fill
      all 0x8f00 = 176*208 scene words with bgColour | 0x7fff0000 } else {
      RoomGeometry_TransformAndSort(); }` -- before any wall or actor. And
      `RoomFace_RasterizeTextured`, the single rasterizer that path uses,
      writes `texel | 0x7fff0000` per pixel: the same far-depth word the
      flat fill writes, with no depth test, no light term and no
      chroma-key cutout. It paints background.

    - **The port now draws it as one** (`render3d/zone_renderer.cpp`):
      first, anchored to the camera (turning with the view, never
      translating with it), unlit, and without touching the depth buffer,
      so every wall and actor drawn afterwards is in front of it. The two
      placement constants are transcribed, not tuned: the fixed
      `(0, 270, 0)` translation already decompiled in the post-M11 pass,
      and a **2x** scale -- `skybox+0x5e = 0x200`, which the zone loader
      writes on every single load. That last one closes
      `docs/RENDERER_3D.md`'s open question about whether that scale byte
      is ever non-identity: it is never anything *but* 2x. The same
      loader block zeroes the object's pitch, roll and yaw, leaving the
      transform's own fixed `-0x4000` quarter turn as the only rotation --
      the one constant here whose effect (which compass bearing azra's
      moons sit over) can't be checked against a device, so it is
      transcribed from the code and flagged as such in
      `zone_renderer.h`'s `kSkyboxYaw`.

    - **The nine impossible texture headers, explained rather than
      guarded.** They read `skinCount=256, width=256, height=0` -- zero
      pixels by their own arithmetic, which is what aborted a Debug build
      before M66. The engine never reads that header for a `.zsk`:
      `RoomFace_RasterizeTextured` addresses its texel with a hardcoded
      256-wide stride (`(v & 0xff00) + ((u >> 8) & 0xff)`) where the actor
      pipeline derives a shift from the header's width. Every one of the 21
      files physically carries the 131072-byte skin at the fixed offset,
      so on real hardware those nine draw a **black dome** -- an interior's
      blank sky -- and the port now does the same via a new
      `ParseSkyboxResource()` (`world/model_archive.h`) next to
      `ParseModelResource()`. M66's rasterizer guard stays; nothing reaches
      it by this route any more, which `render_clip_smoke` now checks
      explicitly alongside M66's original nine-zone census (re-taken from
      the raw file, so that evidence stays checkable).

    - **Also renamed for what it is**: `Zone::RoomMesh()` ->
      `Zone::SkyMesh()`, and `ZoneRenderer` gained `drawSkybox` -- which is
      the real `engine+0xbe0e`, the flag the loader sets on a successful
      `.zsk` load and clears on a failed one, and the flag
      `Render3DScene` picks its two arms on. The three `Room*` Ghidra
      labels are left alone for continuity with every earlier note, with a
      correction recorded next to them in `docs/RENDERER_3D.md`.

  **Verification.** New `m70_skybox_smoke` (31 checks, suite **61/61 ->
  62/62**) carries the census over all 21 real files, both readings of the
  texture header compared byte-for-byte, and three behavioural checks
  against the real `ZoneRenderer::Render`: turning the sky on changes
  **only** pixels that were bare background (so it occludes nothing);
  walking three tiles moves it by **zero** pixels out of 23,114 shared open
  sky; turning 34 degrees moves 21,064 of 22,834. Of the fourteen tracked
  `.ppm` dumps, seven changed and seven are byte-identical -- and **every
  one of the 32,736 changed pixels across all seven was previously the flat
  background fill**, which is the same claim as the first behavioural check
  made against dumps that existed before this milestone. Two new dumps
  (`skybox_azra.ppm`, `skybox_drgnfld.ppm`) are tracked from here on: the
  Dragonfields one is a blue sky with clouds over the terrain, and the
  azra one has both moons in it.

- [x] **M71 -- placed models sit and face where the level says, and the
  camera sees what the device saw.** Reported from play against a device
  screenshot of the same room: tables hovering a few inches off the floor,
  chairs facing the wrong way, and "the starting room in the original seems
  much larger than in the port." Four separate defects, all of them port-side
  mistranslations of things the decompile already had -- the recurring shape
  this roadmap has now seen enough times to expect.

    - **The field of view was a guess, and the real one is in the binary.**
      Both pipelines end in `screenX = 0x5800 + 0x5800*x/z`,
      `screenY = 0x6800 - 0x6800*y/z`, which `RENDERER_3D.md` has recorded
      since the first pass as "the screen center in 8.8" and never turned
      into a frustum. Read alone it looks anisotropic (focal 88 across, 104
      down); it isn't, because `Render3DScene` scales the camera matrix's
      **screen-right row only** by `(recip[176] >> 15) * 0xd0 / 0x10000`,
      where `recip` is the `2^31/n` table at 0x100b4954 -- i.e. by
      **208/176**, the aspect ratio. Effective focal length is therefore
      **104 px both ways**: `fovY` is exactly **pi/2**, `fovX` about 80.5
      degrees. The port had `fovY = 1.2` (focal 152) at every call site since
      M6, a **1.46x over-zoom** -- the whole of the "room looks smaller"
      report. Now `sk::kEngineFovY`, and the default for `Camera`.

    - **`yaw = pi/2 - heading`, not `-heading` -- M61 read the wrong
      function.** M61 derived the heading convention from `FUN_100063f0`,
      calling it "walk forward". It is **sidestep**. The forward one is
      `FUN_100065b4`, which advances by `(sin h, cos h)` -- a quarter turn
      away from what M61 concluded. The renderer agrees independently: the
      camera matrix `FUN_10073760(engine+0x5d8, -roll, -pitch, -heading)`'s
      depth row is `[sin h, 0, cos h]`. And so does this port's *own*
      `sk_bindings::PortYawFromEngineYaw`, which M48 derived from the spell
      spawn's `vx = sin(yaw); vy = cos(yaw)` and which has quietly disagreed
      with `CameraAngleRadians` ever since. Three derivations, one answer;
      `main.cpp` now has one conversion instead of two that contradict.

    - **Which is why chairs faced backwards.** `PlacementYawRadians`'s own
      comment admitted its zero reference "was never derived from the binary,
      it was fitted by eye until doors stopped reading as permanently open" --
      and a door is a flat slab, so that fit could not see a half turn.
      Against the real relation the old formula was off by `pi - 2*heading`:
      **exactly zero at 90 and 270 degrees, exactly 180 degrees off at 0 and
      180**. Shipped doors cluster at 90/270, so it looked right. Measured on
      the level data instead of by eye: of the 180 shipped door placements
      whose heading is near 0 or 180 -- the only ones where the two
      conventions differ -- the corrected one puts the door's panel in the
      doorway rather than inside the wall for **163**, against **3** for the
      old one (14 ties).

    - **The `-0x40` draw offset.** `Actor3D_TransformAndSubmitModel` hands
      its matrix the vertical translation `(actor.z - cam.eyeZ) + -0x40` for
      every non-player actor: models are drawn **64 raw units below** their
      stored `.ent` Z, about 13 cm at this game's scale. The level data is
      authored against that -- over 3,507 floor-standing scenery placements,
      74% have their lowest vertex above the tile's floor as stored, and the
      offset drops that to 35% (median +38 -> -26 units). That is the
      hovering, to the inch.

    - **Two orientation channels and a per-instance scale the port had never
      read.** `ZONE_FORMAT.md` decoded `.ent`'s `rotOrScale[2]`/`[0]` ->
      `+0xa8`/`+0xb2` in M35 and `unkB`'s low halfword -> `+0x5e` (the 8.8
      model scale) in M52; the renderer used none of them. `SubmitModel` now
      transcribes `BuildRotationMatrix3x4` (0x10073a70) literally instead of
      the yaw-only special case, and the loader reads all three fields. 203
      and 232 placements carry a real angle, and **958 of 8,216 are not 1:1**
      (0.125x to 29x, 764 of them ordinary scenery) -- so nearly a thousand
      props were drawn at the wrong size. The scale is applied in the
      engine's own order too: `GameEngine_InitLevel` writes `+0x5e` in step
      5, *after* the entity's `Init()`, so a placement's size overrides what
      its script asked for at load (`MonsterExecutable::SetPlacementScaleRaw`).

    - **Doors compose their swing in raw units now.** `d.placementYaw +
      script->yawRadians()` added a converted heading to a raw turn's
      radians, which runs the swing backwards under the corrected
      convention; it is `PortYawFromEngineYaw(script->headingRaw())` --
      the same composed heading the M67 tile stamp already walks.

  **Verification.** New `m71_placement_smoke` (21 checks, suite **62/62 ->
  63/63**): the focal-length arithmetic against `0x5800`/`0x6800`; the
  heading relation against the engine's forward vector and its own inverse
  over 1024 sampled headings; that the renderer's `C = -(yaw + pi/2)` is
  exactly the engine's `heading + 0x8000`; that `BuildRotationMatrix3x4`
  sends local -Z to `(sin h, cos h)`, independently re-confirming M36; the
  163-vs-3 door census; the 3,507-prop floor-gap census; and a real frame
  rendered from azra's actual player-start record. That frame is the check
  that matters -- side by side with the device screenshot of the same spot it
  now shows the same room at the same scale, with the table, chest, chair,
  candelabra and door all where the original puts them. All thirteen tracked
  `.ppm` dumps move, as they must when the projection changes; a fourteenth,
  `placement_azra_start.ppm`, is tracked from here on.

  **Still open, and visible in that comparison:** the roof model reads much
  darker than the original's. That is M35's model-lighting approximation (a
  flat RGB scale by `cellLight/kMaxLightLevel`) rather than the engine's
  fade-rasterizer + `engine+0x5c4` remap table, whose contents were never
  dumped -- a large model spanning several cells samples wall cells with
  almost no light and goes black. Also unexplained: a bright green patch on
  the wall behind azra's candelabra, present before this milestone and not in
  the device screenshot.

- [x] **M72 -- the camera only stoops for the small things again.** Reported
  from play: the original pans the camera down when the player closes on a
  rat or a spider, but the port did it for **every** enemy, bandits included.
  The port's version (M30) was written from the requested behaviour, and said
  so -- "No RE ground truth: nothing has been traced that aims it
  automatically, so this is a port-side design." It has now been traced, and
  the difference is not a tuning problem: the real feature is a **hard gate
  on a discrete per-creature height**, where M30 approximated "small" as a
  continuous function of the drawn model's extent. A continuous function has
  no way to say *no*, which is exactly the reported symptom.

    - **Where it lives.** One self-contained block at the tail of
      `Render3DScene`, **0x10017b7c..0x10017e20**, after the visible-entity
      pass. It only picks a target angle -- `player+0xac` -- and raises
      `player+0xc3`; two other paths move the camera. Fully written up in
      docs/RENDERER_3D.md's new "The automatic aim-assist pitch" section.

    - **The gate, and the whole bug.** `cmp r0, #0x200 / bge` on the
      target's `Height()` (entity vtable slot `0x108`). Creatures are all
      `entities.txt` category 2, so they share one class, whose `Height()`
      override (0x1008679c) is a `switch` on the creature's own **model
      index** -- 51 arms for indices 18..68, of which five return `0x100`
      and the other 46, the default and the descriptor-not-found path all
      return `0x200`. Read against the shipped `entities.txt` those five are
      **18 = rats, 55 = spiders, 56 = wormmouths, 66 = stingers/rays,
      68 = wolves**; bandits are 22 and 23 and fail the gate. **26 of the
      176 creature rows in the game pan; 150 do not.** Transcribed as
      `sk::MonsterCollisionHeight`, and the category -> class factory table
      it hangs off is now in docs/ZONE_FORMAT.md.

    - **What else came out of the same block**, and is now ported rather
      than approximated: target selection is a **forward ray-march**
      (`FUN_1001db0c(player, 0x20)`, 32 half-tile steps along `(sin h,
      cos h)`, first live entity in each tile), not a cone test; the aim
      point is the collision-cylinder midpoint `z + Height()/2`, not a
      model centroid; the angle comes from a 32x32 signed-byte atan table
      at **0x100f6954** (verified entry-by-entry against `atan2` to within
      **1.4 degrees**, so the port just calls `atan2`); and it engages only
      inside a **+-45 degree** window (`(u16)(pitch + 0x2000) <= 0x4000`).

    - **Two engine quirks kept deliberately, because they are what it feels
      like.** The horizontal distance is **floored to whole tiles** by the
      `isqrt(dist^2/256 >> 8) * 256` round trip -- inside one tile it is 0,
      `atan2` gives a right angle, the window rejects it, and the camera
      *holds* instead of staring at the floor. And losing the target does
      not re-level: that is a separate path in the actor tick, which clears
      `player+0xae` only when the player has velocity **and** there is no
      target, and only then decays the pitch by `v -= v*20/256` per tick
      (`FUN_10068814`). So a pan unwinds when you walk away, not when you
      look away. The approach itself is `FUN_100657bc`: halve the remaining
      gap each tick, clamped to 500 raw units (~1.2 rad/s at 25 Hz),
      snapping inside 5.

    - **Deviations, all forced and all documented in place.** Two gates
      have no port equivalent -- the map zoom (`engine+0x608 <= 0x200`) and
      the `player+0x204` controller object's `+0x179`. A third, "nothing
      alive is already under the crosshair", reads a per-pixel entity ID
      buffer at the centre pixel (`engine+0x5b8` at 88,104) that this
      renderer does not keep; the port uses `Zone::HasLineOfSight` in place
      of the real per-frame `+0xd6` visibility stamp instead.

    - **A side finding for an old open question.** `kEyeHeightOffset`'s
      comment has carried an unresolved hypothesis that `CMap+0x1a` might
      simply be 0. It is now clear what the field *is*: the player-side
      actor classes' own `Height()` override (0x10006230) switches on the
      stance byte `+0x1e1` and returns `CMap+0x18`/`+0x1a`/`+0x1c`, so
      `+0x1a` is the **standing collision height of a person**, which the
      engine then reuses as the eye offset (`player+0x224 = player+0xa4 +
      CMap+0x1a`). That makes 0 much less likely -- it would give every
      humanoid a zero-height collision cylinder -- but the write still has
      not been found, so 800 stays a calibrated stand-in. Noted in
      `render3d/camera.h`.

  **Verification.** New `m72_autoaim_smoke` (34 checks, suite **63/63 ->
  64/64**): the transcribed jump table arm by arm, including the two
  out-of-switch defaults and the not-found path; then the same table run
  against the **real `entities.txt`**, classifying all 176 creature rows and
  asserting thirteen named creatures individually, so the check fails loudly
  if either the table or the shipped data ever drifts (`azra_rat.s` and
  `cave_spider.s` pan, `bandit_brawler.s` and `bandit_thug.s` do not); and
  the engagement geometry -- that a rat inside one tile does *not* engage,
  that four tiles out it does and tilts down, that the tilt shallows with
  distance, that a bandit at an identical position is gated out, that the
  ease converges without overshoot in 18 ticks, and that the decay levels
  the camera only while moving.

- [x] **M73 -- the three bars finally move both ways.** Reported from play:
  the stamina bar only ever moved when jumping, and none of the three pools
  ever refilled. Both halves were simply missing rather than wrong -- M51
  implemented the jump's cost and nothing had gone looking for the other
  five functions. Full write-up in docs/WORLD_MODEL.md's new "The vitals
  economy" section; the constants and the derivation live in
  `simkin_bindings/vitals.h`.

    - **What spends fatigue**, four functions, all on the player's own
      vtable: **jump** `FUN_10044400` (+0x234) spends 5 and refuses unless
      `fatigue > 5`; **attack** `FUN_100425bc` (+0x288) spends 4; **cast**
      `FUN_10042394` case 2 (+0x280) spends 3; **move** `FUN_10045228`
      (+0x1dc) spends 2 every `0xc4` delta units. The port had only the
      first of the four.

    - **Three details that are visible in play, and are reproduced.** The
      attack's 4 is spent *up front* -- before the target search, before the
      melee/ranged split, before any to-hit roll -- so a whiff, a
      bare-fisted swing and a connecting blow all cost the same, and a bow
      costs it too. The cast's 3 is spent *last*, only after both the refire
      gate and the cast itself succeeded, so a cast refused for want of
      magicka is free; and spells never reach `FUN_100425bc`, so the two
      costs never stack. And the move cost is a **time drain on a hook**,
      not a per-key charge: slot `+0x1dc` is called by each of the four base
      move functions, so holding forward *and* a strafe genuinely drains
      twice as fast, and it fires whether or not the step is then blocked.

    - **What an empty pool costs.** `FUN_100445d4` shifts the per-frame step
      one extra bit right while `fatigue < 1` -- exactly half movement speed
      -- and `FUN_100425bc`'s melee branch halves the damage roll on the
      same test, ahead of the defender's mitigation. The ranged branch has
      already returned by then, so **an arrow is not weakened by
      exhaustion**; only a swing is. Both ported; `RollDamage` grew the
      `halveRoll` argument so the halving lands where the engine puts it.

    - **Regeneration** is `FUN_10049b64`, called from the player tick
      `FUN_10045294` right after the status-effect tick. It has **exactly
      one call site in the binary**, so regeneration is the player's alone
      -- a wounded creature stays wounded. Three accumulators on the stats
      block (`+0x3c`/`+0x3e`/`+0x40`), three periods, three
      attribute-derived amounts, each written through the ordinary clamp and
      each gated on the pool being below its own maximum:
      **health `Endurance / 25` every 4 s**, **magicka `Willpower / 15`
      every 2 s**, **fatigue `(Strength + Willpower) / 15` every 2 s**.

    - **Neither divisor is a literal**, and that is where this milestone
      could most easily have gone silently wrong. `/25` is `0x51eb851f` with
      `smull` + `asr #3` on the high word, and `0x51eb851f` reads like the
      reciprocal of *100* at a glance; `/15` is `0x88888889` used signed
      with the `add`-then-`asr #3` correction. Both were read off the
      disassembly, and the smoke test reproduces both instruction sequences
      and compares them against plain division over -2000..5000 rather than
      asserting the constant.

    - **Three modifiers, and one that was compiled away.** A **High Elf**
      adds `raceAbility * 5` to the willpower term before the divide, a
      **Breton** the same to strength+willpower, and carrying
      `items\azras_bandage.s` (typeId **4702**, confirmed against the real
      `entities.txt`) adds a flat +3 to the health tick. The fourth is a
      **shipped dead branch**: at 0x10049c1c the health arm loads the
      owner's race, compares it against 7 (Wood Elf) and never reads the
      flags -- both paths fall into the same multiplier load. A Wood Elf
      health bonus was written and compiled out; the port has none either.
      It is invisible in the decompiler's C, which renders the dead compare
      as a discarded call.

    - **The surprising result.** Both rates are fixed by the periods, so at
      25 Hz they are flat numbers: walking straight drains **2.50/s**,
      walking diagonally **5.00/s**, and a 50-strength / 50-willpower
      character regenerates **2.88/s**. So a starting character **cannot
      walk themselves tired in a straight line** -- regeneration is very
      slightly ahead. Hold a strafe as well and the pool empties in about 45
      seconds. Fatigue in this game is spent by fighting and jumping;
      walking only bleeds it when you are also sidestepping, or when the two
      attributes are low. The first draft of the smoke test asserted the
      opposite and failed, which is how this was found.

  **Verification.** New `m73_vitals_smoke` (46 checks, suite **64/64 ->
  65/65**): the four costs; the drain's period, its reset-not-subtract, the
  strict `>` on the comparison and the per-direction doubling; both divisors
  reproduced instruction for instruction; the three amounts run against all
  sixteen rows of the **real race/sex attribute table** (which is the check
  that a 4x divisor error would fail, since `/100` gives zero for every
  shipped character); the two racial modifiers proven to belong to exactly
  one race each; typeId 4702 resolved through the real `entities.txt` and
  the bandage equipped on a real `PlayerExecutable` through a real created
  item; the two exhaustion penalties including that the halving precedes
  mitigation; and the whole economy run at the real 25 Hz tick -- walk to
  exhaustion, then stand and recover.

- [x] **M74 -- the spell that thought it was a trinket.** Reported from
  play: the Blaze pickup at the start of the game ("Learn Blaze" if your
  class can cast, "Pickup Blaze Scroll" if it cannot) lands in the
  inventory's **Miscellaneous** page instead of Spells, and cannot be
  equipped or cast. Full write-up in docs/WORLD_MODEL.md's new "The item
  type, the equip slot, and the class gate" section.

    - **Root cause.** This port *inferred* an item's type from whichever
      category setter its script happened to call -- `SetArmorValue` meant
      armour, `SetDamageMin` meant a weapon, `SetUsable` meant a
      consumable. Nothing a spell script calls implies "spell", so
      `kItemTypeSpell` was never produced by anything at all: the constant
      existed only in the `DisplaySpellsPage` filter it was supposed to
      match, and **every spell in the game reported Misc**.

    - **The engine does not infer.** `GetItemType()` is `FUN_1006d508`,
      one line: `return entity->+0x16c`. That word is a per-C++-class
      constant written by the class's constructor, and the class comes from
      the `entities.txt` **category** column through the factory
      `FUN_1002aa14` -- the same seventeen-arm table M72 read for creature
      heights. Eight of its arms are item classes (3 misc, 4 weapon,
      **5 spell**, 6 armour, 9 consumable, 14 scroll, 15 shield, 16
      weapon), and the shipped data agrees column for column: 80 of
      category 4's 83 rows live under `weapons\`, 28 of category 5's 31
      under `spells\`, 77 of category 6's 89 under `armor\`, 55 of
      category 9's 58 under `items\`. `blaze.s` is typeId 50, category 5.

    - **`+0x1c0` is which hand an item wants** -- 0 left, 1 right, 2
      neither, confirmed against the stats block's own `SetLeftItem`
      (`stats+0x48`) and `SetRightItem` (`+0x4c`). Weapons and spells are
      right-hand items, consumables left-hand ones, armour and misc neither.

    - **Picking something up equips it**, which is the second half of the
      report. The tail of the player's add-to-inventory slot `+0x164`
      (`FUN_1003d8e0`) puts the item in the hand its `+0x1c0` names, if that
      hand is empty and the class may use it. A loot menu's `PickupItem`, a
      purchase (`FUN_1003e030` -> `FUN_10045078`, a one-line forward) and
      `EquipItem` all funnel through it. **`EquipItem(hand, item)` ignores
      its hand argument** -- the shipped handler reads it with
      `SIMKIN_AtomToInt` and discards r0 -- so the `0` in all 34 shipped
      `EquipItem(0, self)` call sites is decoration and a spell still arms
      the right hand.

    - **`UpdateEquipStatus` had two things wrong**, both of them the bug.
      The hand is the item's own `+0x1c0`, not "whichever is free"; and the
      **3** return that `inventory.s` reads as "cannot equip" is the *class*
      gate and nothing else -- this port answered 3 for anything that was
      neither armour nor a weapon, which is why Blaze stayed unequippable
      even once it was on the right page. The real codes are 1 (ok), 2
      (hand queue full) and 3 (refused); 0 is never returned.

    - **`IsItemEnabledFor` is real now**, and it turns out to be the one
      consumer of the three unnamed mask words M64 transcribed verbatim out
      of the class table. `RestrictUse(...)` stores `2 << classId` per
      argument in `spell+0x1cc`, and `blaze.s`'s own `RestrictUse(2, 4, 6,
      7)` names **exactly** the four classes the class table independently
      marks `HasMagic` -- two shipped tables agreeing, which is what makes
      both readings safe. `field0` is the armour-weight mask, `field2` the
      weapon-class mask (`== 2` meaning unrestricted, five of nine classes)
      and `field4` the shield mask; read out, they say Knights and Rogues
      wear heavy armour and Assassins and Sorcerers carry no shield.

    - **Two more corrections found on the way.** `CreateItem`'s category
      filter was a hand-picked five and omitted 14, 15 and 16, so
      `Level.CreateEntity` answered null for all seven scroll spells, nine
      of the ten shields and the conjured Daedric sword. And the player
      constructor `FUN_1003d670` -- the same one M64 read the ability ranks
      from -- writes `+0xf3c = 5` and `+0xf38 = 2`, so a character who has
      never been through creation is a **Nord Battlemage**, not the 0/0
      Argonian Assassin this port defaulted to. Cosmetic until this
      milestone; load-bearing now that the class row gates every equip.

  **Verification.** New `m74_item_category_smoke` (30 checks, suite
  **65/65 -> 66/66**): the eight factory arms; the table against the real
  `entities.txt` with the script directory of all 278 item rows as an
  independent witness; Blaze built through the real
  `Level.CreateEntity(50)` and landing on the real `inventory.s` **Spells**
  page and not the Misc one; `RestrictUse`'s bitmask against
  `ClassHasMagic` class by class; the armour arm splitting the nine class
  rows on a real `armor\Chain_Coif.s`; the two use texts resolved through
  the real string table ("Learn Blaze" / "Pickup Blaze Scroll"); and the
  pickup and equip paths for a caster and a non-caster each. Three existing
  tests moved with the behaviour (m10, m22, m60). Verified live as well: a
  Battlemage given typeId 50 in `azra` reads `Blaze  type 2  EQUIPPED` on
  the debug inventory page, with the spell's book icon in the viewmodel.

- [x] **M75 -- nobody would talk.** Reported from play: approaching an NPC
  never produced the prompt that starts a conversation, so no dialogue tree
  and no shop screen was reachable at all. Three independent defects, full
  write-up in docs/WORLD_MODEL.md's new "Talking to people: one byte, and
  the object called `Level`" section.

    - **`SetUseText(id)` also makes the entity usable.** The engine has one
      gate for the whole interaction, `entity+0xd8`: the per-frame
      use-target search `FUN_1001dd40` (a twelve-ray fan out of the
      player's heading, parked in `engine+0x61c` by `Render3DScene`) tests
      it, and so does the use action `FUN_100646a8`, which is `if
      (entity->+0xd8 == 0) return false;` then `vtable[0xa8]("OnUse")`. So
      "no prompt" and "Use does nothing" are one condition. The port had
      that byte on creatures only and set only by an explicit
      `SetUsable(true)` -- but the shipped `SetUseText` handler
      (**0x10068418**) is three stores, not one:

      ```
      str  r1, [r0, #0xdc]   ; useTextId  = id
      strb r3, [r0, #0xd9]   ; hasUseText = 1
      strb r3, [r0, #0xd8]   ; usable     = 1
      ```

      and that same function is `vtable+0x8c` in **31 of the game's entity
      vtables**, i.e. every class. **54 of the 152 talkable placements in
      the shipped game -- 35% -- name a use text and never call SetUsable**:
      Gravel Trothgar (azra's shop), Acolyte Menlin and Priestess Almathea
      (azra's two starting quests), Old Trinket, the four villager
      prisoners, Heather, and all four Dark Star West merchants.

      Also recovered: the constructor default (only categories 4 and 9, the
      weapon and consumable arms, start usable -- 0x1002cf6c and
      0x1002e7f8), the fatal-hit tail that clears it (0x10083e2c), and the
      reader `0x1006842c`, whose fallback when no use text was ever set is
      `stringTable[13]` -- the literal word **"default"**, a developer
      placeholder, which is the same statement from the other side.

    - **`Level` is the zone-root script object,** not a native-only
      singleton. Two independent proofs in the shipped corpus:
      `crypt2/pedestal_entity.s` calls `Level.AddCrystal()` seven times and
      `AddCrystal[()...]` is defined in **`crypt2.s`** and in no native
      trie; and of the **168 distinct `Level.<field>` names across 423
      sites**, **163 are declared at the top level of the zone-root script
      of exactly the zone they are used in** (`EndGame_Trinket` in
      `azra.s`, `saved_Birgitta`/`saved_taker`/`saved_Given` in
      `delfhide.s`, `saved_Crys1[0]`..`saved_Crys7[0]` in `crypt2.s`, ...).
      Every one of those 423 sites raised "Cannot get field", and because
      that is a *runtime error* rather than a soft-fail it aborts the whole
      handler -- for a conversation menu that means its `Init()` never
      finishes and the menu never opens. Six real conversations died on the
      first line of theirs: `trinketconvo`, `AzraSkelosConvo`,
      `delfhide/Chef_convo`, `delfhide/RescueConvo`,
      `crypt1/azra_final_convo` and `Talker`.

    - **`GetOpener()` was null for an NPC and missing for a door.** The
      entity classes' `OpenMenu` is `FUN_100779b8(menuManager, name, 1,
      self)` -- the caller is always the opener, and 210 corpus sites read
      it back. A creature's handler passed nothing, so
      `fearfrst/Ivgrizt_convo2.s`'s opening
      `if (GetOpener().saved_WeTalked = 0)` -- a field declared at the top
      of `fearfrst/Ivgrizt.s` itself -- raised on a non-object (same for
      `ghstpass/Trailslag_convo.s`, `StoutTP/OldTrinketConvo3.s`,
      `erthcave/EC_Menu3.s`). A door had **no `OpenMenu` handler at all**,
      which is why no lock in the game could be picked: `lockeddoor.s`'s
      `OnUse()` is `OpenMenu("Menus\\UsePicks")` and nothing else, and that
      menu's `Pick` reads `GetOpener().resistDisarm` and calls
      `GetOpener().LockPicked()`.

    - **The gate now applies to all three interact lists,** not just NPCs.
      In the shipped data that costs nothing and is strictly more faithful:
      all 404 container placements with a real script come out usable and
      433 of the 434 doors do, the one exception being
      `dstar_e/Cell_Door.s`, whose entire `Init()` is `SetUsable(false);
      SetMPUsable(true);` -- a prison door a quest event opens, never the
      player. `crypt2/controller.s`, the invisible seven-crystal logic
      object, correctly stops offering to be picked up.

    - **Ordering change in the zone load.** The zone-root script is now
      *constructed* before the placement loop and attached to `Level` there
      (its `Init()` still runs last, after every named door/monster/pickup
      is registered, which is what its `Level.GetEntity("m1")` lookups
      need). Constructing it is what materialises the top-level
      declarations, so every placement's own `Init()` sees the real zone
      variables.

    - **One piece of noise removed on the way.** `MonsterExecutable::
      InvokeOnUse()` and `DoorExecutable::InvokeOnUse()` dispatched
      `"OnUse"` through the native path, so an entity with no `OnUse`
      handler -- an ordinary shape now that 54 more of them are reachable
      -- logged a bogus `OnUse(0) -- not implemented` on every Use press.
      Same one-line fix M35 already made for items.

  **Verification.** New `m75_dialogue_smoke` (66 checks, suite **66/66 ->
  67/67**): the byte's three sources against real scripts (Trothgar, Tanyin
  Aldwyr, Ivgrizt's `SetUseText`-then-`SetUsable(false)`, a hostile rat, a
  locked door, a dropped weapon, `blaze.s`) plus death clearing it; a census
  over **all 21 zones** loading every category-2/7/8/11/12 placement that
  has a real script through its real binding class; `Level` resolving to
  `azra.s`'s own `EndGame_Trinket` declaration and sharing the slot with the
  zone script's bare access, plus `Level.AddCrystal()` running `crypt2.s`'s
  handler; each of the five conversations that died at `Init()` opening for
  real; `GetOpener()` being the NPC; **Gravel Trothgar's whole quest
  conversation branch by branch** -- offer, accept, "still open", solved,
  the 400 gold + 400 experience reward, quest completed, then `BuyItems`
  landing on `buysell.s` in Buy mode with his own shelves; and the lock-pick
  menu opening with the door as `GetOpener()` and `resistDisarm` reading 5.

    - **Not attempted here: `OnDetect`.** The other mechanism -- an NPC
      that talks to *you* rather than one you talk to. It needs the
      detection half of the AI package, not this milestone's gate.
      **Done in M76 below**, along with `MenuClosed`, which turned out to
      have no call site in the shipped binary at all.

- [x] **M76 -- the NPC that speaks first.** M75 restored the prompt you walk
  up to and press Use on; this is the other half of the same conversation
  system, and it lives somewhere completely different -- inside the creature
  AI tick, `FUN_10082224`. Full write-up in docs/WORLD_MODEL.md's new "The
  NPC that speaks first: `OnDetect`" section.

    - **`OnDetect` is the other arm of the aggro branch.** The tick's
      idle/pursue arm (`package == 2 || (package == 3 && target == 0)`) runs
      one perception test and then splits on a single byte:

      ```
      if (monster->+0x2ac /*aggressive*/ == 0) {
          if (target == engine->player &&
              self->vtable[0x21c](self, target, self->+0x2dc >> 8, 1) &&
              (self->+0x306 || !multiplayer || isHost))
              if (self->+0x120 /*script*/)
                  self->vtable[0xa8]("OnDetect", { target }, ...);   // 0x10083320
      } else {
          ... acquire the target, package = 3, close and swing ...
      }
      ```

      So a non-aggressive creature is not idle in the engine -- it runs the
      *same* test at the *same* distance on the *same* tick and calls its
      script instead of attacking. This port skipped the whole block for
      any creature with `SetAggressive(false)`, which is why none of it ran.
      The handler takes one argument, the detected entity: the engine boxes
      `target + 0x14` (the skiExecutable base sub-object) into an skRValue
      and appends it (0x100831ac..0x10083208).

    - **The perception test, transcribed.** Three conditions in the
      engine's order. Distance is `vtable[0x5c]` == `FUN_100683d4`,
      `((dy*dy) >> 8) + ((dx*dx) >> 8)` -- the same scaled-squared form
      `SetChaseRadius` stores. The roll is

      ```
      detect = monster->+0x258;  if (detect == 0) detect = 1;
      agi    = playerStats->vtable[0x40]();        // the stat at stats+0x18, / 5
      r      = (detect << 16) / ((detect + agi) << 8);
      if (rand(0, 100) < (r * 100 >> 8))  noticed = false;
      ```

      and `monster+0x258` is written to 1 by the constructor
      (`FUN_100815e0`) and by nothing else in the binary, so it collapses to
      "the chance of going unnoticed this tick is 100/(1 + Agility/5)
      percent". `vtable[0x40]` is `FUN_10044b8c` for the player, which
      returns `player+0x3c4` (== `player+0x3ac+0x18`, **Agility**) divided
      by 5 plus two equipped-item bonuses this port has no model for. It
      runs every tick, so a starting Agility of 40 (an 11% miss chance)
      resolves within a frame or two.

    - **`vtable[0x21c]` is a real line-of-sight march,** `FUN_10004d70`:
      eye to eye, direction normalised to one tile and halved, stepped
      through `FUN_100182d0` with flag mask 0xb, ended by a tile whose
      `.zmp` blocking bit 2 is set -- exactly the flag this port's own DDA
      tests -- and bounded by a tile budget. That budget is
      `monster->+0x2dc >> 8`: the raw *scaled-squared* SetAttackRange value
      shifted right by 8, spent in **tiles** (`while (travelled < budget *
      0x100)`, 0x80 per half-tile step). The default 0x6a4 buys 6 tiles, and
      no shipped script calls SetAttackRange. Two consequences worth
      recording: `Zone::HasLineOfSight`'s header comment, which said the AI
      use of sight-gating "was never traced" and was this port's own design,
      is now evidenced and has been corrected; and the two remaining
      differences (the real march is 3D, and an entity in the way does not
      block it) are noted at both the header and the call site.

    - **Two bindings stopped soft-failing, and both are inert here.**
      `SetAlwaysOnDetect(b)` is Monster(AI) binding 4 (`monster+0x306`) and
      has exactly one reader in the whole binary -- the third clause of the
      guard above, which `!multiplayer` already satisfies in singleplayer.
      All ten scripts that set it are `raiders/*.s`. `DetectOnKilled(b)` is
      binding 3 (`monster+0x307`), read once in the death path
      (`FUN_10083c04`) and only under `engine+0x5c0`. Stored, documented as
      multiplayer-only, and asserted inert.

    - **`MenuClosed` does not work in the shipped game, and is deliberately
      not implemented.** The notifier exists -- `FUN_10064c60(entity)` is
      `if (entity != player && entity->+0x120 && !entity->+0x48)
      entity->vtable[0xa8]("MenuClosed")` -- and sits at `vtable+0x3c` in all
      31 entity vtables (checked: every one of the 31 words holding
      0x10064c60 is exactly 20 slots below that vtable's `SetUseText`). But
      **nothing calls it**: those 31 vtable words are the only references to
      its address in the image, and of the 18 sites that dispatch through
      slot 0x3c not one has an entity receiver -- they are the menu
      manager's and the app object's own slot 0x3c, passing arguments
      FUN_10064c60 does not take. So `monsters/bbrawler_talk.s`'s "talk to
      him, then kill him" never completes on the device either. That script
      is also not placed in any of the 21 zones (only the plain
      `monsters/Bandit_Brawler.s` is) -- the same story from the other side.

  **What it is worth.** (**M77 corrected every number in this paragraph** --
  it was measured with creatures that never call an `Ai*` binding sitting
  in the constructor's package -1, and placement puts them in package 2.
  The corrected census is 241 armed, 58 live handlers and 4 dead, across 20
  scripts; `monsters/lakvan.s`, `twilite/pergan_asuul.s` and the nine
  `raiders/*.s` arena creatures all work after all. Kept as written for the
  record.) Loading every category-2/7 placement in all 21 zones
  through its real binding class: 1539 have a real script, **29 end their
  `Init()` armed** (non-aggressive, package idle) and **28 of those have an
  `OnDetect` handler that now runs -- it was 0**. Six distinct scripts:
  `broken2/perosius_temp.s` (a boss who talks, then turns hostile,
  invulnerability off, `AiAttack()`), `dstar_e/dse_skyrim_soldier.s` and
  `dse_skyrim_archer.s` (city guards who go hostile once
  `GetPlayer().saved_Dstar_Pass` says you trespassed),
  `erthcave/azra_zombie.s` (`SetAggressive(true)` + `EC_Menu7` + `AiSleep()`
  in one handler), `monsters/olpac_trailslag.s` (`ghstpass/GP_Menu3`, once
  per save) and `raiders/raider_enter.s` (the arena doorman). A further
  **34 placements ship a handler the engine can never reach** -- the
  aggressive guards, the never-`AiDetect()`ed bosses (`monsters/lakvan.s`,
  `twilite/pergan_asuul.s`, whose `AiDetect()` is commented out in the
  shipped file) and the ten `raiders/*.s` arena creatures. All of that is
  faithful, not a gap: the engine's own branch says so.

  **Verification.** New `m76_detect_smoke` (47 checks, suite **67/67 ->
  68/68**): the roll's arithmetic against the real integer sequence
  (Agility 0/40/100 -> 100/10/4 percent, monotone to 200) and the 6-tile
  sight budget; what a real `Init()` leaves behind for six shipped scripts,
  armed and dead; three handlers run for real (bbrawler opens "Talker" with
  itself as `GetOpener()` and its own `AiSleep()` stops the re-fire;
  trailslag opens GP_Menu3 and its own `done_menu` blocks the second;
  azra_zombie turns hostile *and* opens EC_Menu7); the 21-zone census with
  its exact counts and the six armed scripts named; both multiplayer
  bindings stored with the soft-fail observer proving neither misses any
  more; and MenuClosed shipped, unreachable, and leaving the brawler
  friendly.

    - **Found, not fixed: a malformed script aborts the port.** Three of the
      1535 shipped `.s` files -- `crypt2/exit.s`,
      `dstar_e/thief_convoasdf.s`, `raiders/blu_spider.s`, each with a bare
      statement outside any handler -- raise `skTreeNodeReaderException`
      from `skScriptedExecutable`'s constructor. Every script-loading site
      in this port catches `skParseException` and `skRuntimeException` and
      not that one, so the exception escapes `main` and the process aborts
      (on Windows in a Debug build, as a modal "abort() has been called"
      dialog that looks exactly like a hang -- which is how this was found).
      None of the three is placed in any `.ent` or named by any script, so
      no player can reach it. Recorded rather than fixed: the fix is a sweep
      across ~25 catch sites. A brace/bracket census of the corpus finds 12
      unbalanced files in all, of which only these three actually raise.

- [x] **M77 -- every enemy in the game gets its AI back.** Reported: "only
  the Azra rats of the first level have proper idle, chasing and attacking
  AI; no other enemy has any AI attached to them, standing still doing
  nothing when the player is near", and separately "in the original the
  enemies attack at random intervals with large gaps, but in the port they
  attack non stop". Both are real, and both come from the same milestone's
  worth of re-reading `FUN_10082224`. Full write-up in
  docs/WORLD_MODEL.md's new "The creature AI tick, transcribed" section;
  the derivation lives in `port/src/simkin_bindings/monster_ai.h`.

    - **A placed creature does not start asleep.** The actor constructor
      (`FUN_100815e0`) sets `monster+0x2a8 = -1` and this port copied it.
      But entity vtable slot **`+0x10`** is the "attach to the engine"
      virtual, and the creature classes override it -- `FUN_10086f9c` for
      vtable `0x100fe2b8` (entities.txt category 2, i.e. every creature and
      NPC in the game) and `0x100fee0c` (category 13), `FUN_100866c0` for
      `0x100ffaf4` (category 7). Both end:

      ```
      engine->actorRegistry[self->slot] = self;      // engine+0x14620
      self->+0x5e  = 0x100;
      self->+0x2a8 = 2;                              // the AI package
      self->+0x1d4 = 10;
      ```

      and `GameEngine_InitLevel` calls it on **every** placement, one line
      after the factory allocates the entity and *before* the `.ent` record
      or the script are read. So a script's `Init()` always runs on a
      creature that is already looking for a target.

      That is the whole bug. Package 2 is the only state in which the tick
      evaluates perception, and the corpus leans on the default entirely:
      **318 scripts call `SetAggressive(true)`; only 38 call `AiDetect()`.**
      Across the 21 zones, of 1539 category-2/7 placements with a real
      script, 1296 are hostile and only **47** ask for a package by hand --
      so **1249 hostile placements went from standing still to fighting**.
      `monsters/Azra_Rat.s` is one of the 47, which is exactly why the rats
      were the only creatures that worked: they call a binding that
      re-states what the placement already did.

    - **What the attack cadence gates.** `monster+0x2c4` and its `0x100`
      threshold were already right (M35). What was wrong is where they sit.
      The accumulate line is *above* the package branch, so it runs on
      every tick in every package -- including while a creature is still
      closing, which is what makes the first swing land on arrival. And the
      threshold gates the **whole** approach/attack/give-up decision, not
      the damage roll. This port accumulated only while already in melee
      range and used the gate purely to rate-limit damage, while
      re-asserting the swing clip on each of the ~25 ticks in between. The
      real swing is played by the attack, once:
      `vtable[0x148](self, +0x2be, 1, +0x2c1, 0xf00)` -- mode 1 is "play
      through once, then hand over to this other clip", so between swings a
      creature in range stands in its **idle** pose. That is the reported
      "attacking non stop": one blow a second, animated as though it were
      twenty-five.

    - **`SetBoss` is the second, slower gate.** `monster+0x266` (15
      scripts) arms an independent throttle on the game clock: a boss may
      swing only once every `monster+0x2cc` seconds (`SetAttackSpeed`),
      which the constructor sets to 2 and **no shipped script ever
      changes** -- so every boss in the game attacks at half the rate of
      everything else. Neither binding existed here.

    - **CORRECTED: aggro is not sight-gated.** WORLD_MODEL.md's "Aggro is
      gated on line of sight, not distance alone" was wrong, and this port
      followed it into inventing a vertical gate (`kAggroMaxHeightDelta`)
      and a lost-sight grace period (`kLoseInterestTicks`) on top. Both
      raycasts are real and neither is in the acquire arm: `FUN_10082004`
      is the melee **reach** test inside the attack decision, and
      `vtable[0x21c]` is the **OnDetect** sightline (M76). The acquire arm
      has only the scaled-squared distance against `SetChaseRadius` and the
      perception roll, and giving up is `if (chaseRadius < d) { target = 0;
      package = 2; }` and nothing else. A creature inside 8.4 tiles comes
      for you through a wall. Both invented constants are gone, along with
      `lastSeenX/Y`. The real vertical limit does exist -- but on the
      *attack*, as `|self.z - target.z| < 0x200`, and only for melee: a
      creature with a spell in slot 0 or `SetAttachedWeapon(225)` (the bow,
      all 27 shipped archers) skips it and the reach raycast both.

    - **CORRECTED: M76's own census.** M76 measured "29 placements armed,
      28 with a live `OnDetect`, 34 with a dead one" against the wrong
      spawn package. Same corpus, same method, corrected default:
      **241 armed, 58 live, 4 dead**, across 20 distinct scripts instead of
      six. `monsters/lakvan.s` and `twilite/pergan_asuul.s` -- written up
      as dead ends for never calling `AiDetect()` -- both work, as do the
      nine `raiders/*.s` arena creatures. The only shape that still kills
      an `OnDetect` is an `Init()` that calls `SetAggressive(true)`
      (`delfhide/dh_guard_talk.s`).

    - **The Monster(AI) surface is now complete.** Parsing
      `FUN_10012584`'s trie gives all 55 (name -> index) pairs, and those
      indices *are* its dispatcher's case numbers. Fourteen were still
      soft-failing here, two of them with real corpus use behind them:
      **`SetHealth`** (30 scripts) is byte-for-byte the same case as
      `SetMaxHealth` -- a pure alias -- and **`DoDamage(n)`** (20 scripts)
      damages the creature *itself*. The rest: `SetBoss`, `SetAttackSpeed`,
      `SetCanTeleport` (4 -- jump to a path node beside the player when it
      leaves the chase radius), `ReplicateTeleport`, `SetImmobile` (3),
      `StopAnimating` (1), `FindPathNode` (1 -- returns the engine's own
      node-not-found 0, the `.pth` table being undecoded), `SetLifespan`
      (`seconds << 8`, counted down at **3x** the frame delta),
      `SetItemRequiredToHit`, `SetEnemy`/`Follow`/`GuardPlayer`,
      `SetState`, and the six getters. A sweep that loads all 251 distinct
      placed creature scripts with the soft-fail observer on now reports
      zero Monster(AI) misses.

    - **Two smaller corrections.** `AiPursue` was grouped with the genuine
      no-ops; its case `0x2d` does store a package -- **5** -- exactly as
      `AiAttack`'s stores 3. What makes it look inert is the other end: no
      arm of the tick reads package 5 (nor 4, nor 6, nor -1).
      And `vtable[0x1b4]` (`FUN_10006704`) is not the movement step: it is
      eleven instructions that step `actor+0xb8` an eighth of the way
      toward `actor+0x1a4`, or halve it when that is 0 -- an angular
      damper. Real locomotion runs off `+0x1b9`/`+0x1bc`/`+0x1c0` through
      the entity physics update and pathfinds over the zone's `.pth`
      nodes, which stay undecoded; this port's steer-and-slide chase is the
      one substantial part of the tick that is still its own.

  **Verification.** New `m77_monster_ai_smoke` (40 checks, suite **68/68 ->
  69/69**): the spawn package and every `Ai*` binding's package value; the
  three creatures the report named (`cave_spider`, `alpha_wolf`,
  `bandit_brawler`) shown hostile-and-looking with no `AiDetect()` anywhere
  in their files; the 21-zone census with its 1539 / 1296 / 47 / 1249
  counts; the cadence driven the way the tick drives it, measured at
  **23-26 ticks between swings (0.92-1.04 s), never back to back**; the
  boss throttle's arithmetic and `SetBoss`/`SetAttackSpeed` on a real boss
  (`monsters/lakvan.s`); every new binding stored, plus the whole-corpus
  soft-fail sweep; and the vertical limit with its three exemptions.
  `m76_detect_smoke` was updated to the corrected census rather than left
  asserting the old numbers.

  Confirmed in the running game as well as in the tests: teleporting next
  to a Bandit Brawler in azra (a creature with no `Ai*` call in its script,
  and so previously inert) has it acquire and fight -- `ents` reports it
  `attacking`, `diff` reports `ai.acquired +1` and `combat.hits_on_player
  +8` over ~13 seconds of contact.

- [x] **M78 -- the player's attack, wired to its own swing.** Reported: the
  swing animation is really slow and some swings show a different weapon on
  the last frame; the sound plays and the damage lands on every keypress
  independent of the swing; there is no cooldown at all; and an empty hand
  attacks. Five symptoms, and the first four are one missing function.
  Write-up in docs/WORLD_MODEL.md's "The weapon swing" section, which M47
  had already written the gate into.

    - **M47 decompiled the gate and the port never wired it.**
      `FUN_100425bc` -- player vtable **+0x288**, the attack -- opens with
      eleven instructions that are the whole of the player's attack rate:

      ```c
      if (0 < player->+0xf48) { player->+0xf48 -= frameDelta(); return; }
      player->+0xf48 = (s16)stats->+0x1c;               // the Speed stat
      if (player->+0x204 && player->+0x204->+0x1a7)     // SetRange > 0x400
          player->+0xf48 = (s16)stats->+0x1c * 6;
      if (0 < player->+0x238) return;                   // mid weapon-swap
      if (0 < player->+0x234) return;                   // mid swing
      ```

      Everything the attack does -- the 4 fatigue, the swing, the target
      search, the sound, the damage, the arrow -- is one call below that,
      in that order. This port ran the swing and the attack as two
      independent things off the same keypress, which is every reported
      symptom at once: the animation looked slow because nothing waited for
      it, the sound and damage fired per keypress because they were never
      behind it, and mashing outran it because nothing counted.

      `stats+0x1c` is **Speed** -- the short `FUN_10048244`'s `TestSpeed`
      (Character-stats trie index `0xf`) and `GetSpeed` (`0x20`) both read,
      in the run Strength `+0x14` / Intelligence `+0x16` / Agility `+0x18`
      / Will `+0x1a` / **Speed `+0x1c`** / Endurance `+0x1e` / Personality
      `+0x20` / Luck `+0x22`. The seed is the attribute itself, so a
      higher Speed means a *longer* cadence. Backwards-reading, and
      unambiguous.

      The rate it produces, at the fixed 25Hz tick: a five-frame melee
      weapon (63 of the 64 in the corpus) swings **every 48 ticks, 1.9
      seconds**; a bow every **31 ticks, 1.24s**. 48 rather than the
      swing's own 45 because the cadence is re-seeded *above* the two busy
      tests -- every sixth tick spent waiting on the swing puts another
      Speed's worth back on the clock -- so melee is the swing rounded up
      to a whole cadence, while a bow is the other way round and is
      cadence-limited outright.

    - **There is no bare-handed attack in this game.** The attack's only
      caller is `FUN_10042394` (player vtable **+0x280**), "use the item in
      this hand", and its entire body is inside `if (param_2 != 0)`: an
      empty hand reaches that test and stops. No swing, no target search,
      not even a miss sound. This port's fists (1-3 damage at an invented
      reach) were invented and are gone, along with the `kMeleeRange` they
      used -- the constant survives only as the floor of the on-screen HP
      label's radius. The switch under it is
      `FUN_1006d508(item)`: **1** weapon (attack, unless `stats+0x7c == 4`,
      the Sanctuary channel, which is checked here and nowhere else), **2**
      spell (cast, then re-arm the swing from whatever weapon is on screen
      -- with no cadence gate and no swing gate, overwriting one in
      flight), **4** consumable (`item vtable +0x90`). The consumable arm is
      the one gap left: this port has no use-from-the-hand path yet, so a
      potion in a hand does nothing on the attack key, which is still
      closer than the melee swing it used to produce.

    - **The attack keys are read as held, not as an edge.** The input
      handler calls `FUN_10042394` once per tick for each of bound buttons
      `0xf` (right hand) and `0xe` (left), through the plain
      `InputState_GetBoundButton` -- while the jump and use keys a few
      lines earlier in the same function pair it with
      `_GetBoundButtonPrev` for an edge. So the difference is deliberate,
      and it is what makes the cadence a real-time one. This port used
      `ConsumeBoundJustPressed` for both.

    - **CORRECTED: the swing's last frame.** M47 found the frame index
      reaching `frames + variant + 1` -- one slot past the variant's own
      last frame -- at exactly `acc == 0x200`, which the engine's
      `< 0x200` test lets through, and reproduced it as "a one-frame
      boundary artifact". On the device the accumulator drains by the
      *measured* frame delta (`app+0xd4`, clamped 4..0x40) times four, so
      landing on that single value is a coin toss. Here the delta is a
      fixed 10 and a five-frame melee weapon's 1792 - 512 = 1280 is exactly
      32 drains of 40 -- so the port hit it on **every** melee swing, and
      what it drew is a real sprite from the **next weapon's** strip: a
      melee weapon owns 16 consecutive slots (bases 72/88/104/120/136), the
      variant-10 run ends at base+15, and the overrun frame is base+16,
      which for `weapons/club.s` is 104, another weapon's idle pose. That
      is the reported "different weapon equipped on the last frame". The
      floor test is `<=` now, one unit earlier, which makes each variant
      play exactly its own five frames.

    - **The swing's length was already right, and it is 1.75 seconds.**
      Not the 1.3 M47 recorded: the accumulator keeps draining past the
      last drawn frame to zero, and it is zero the attack gate waits for.
      1792 units at 4x the frame delta is 1024 a second, so 1.75s of
      wall clock at any framerate, of which the last half second draws no
      weapon at all. M78 also closed M47's one piece of inference here by
      reading the constructor instead: `FUN_1006c960` writes
      `item+0x184 = 0x100` outright, so `SetReloadSpeed`'s scale really is
      the identity for every weapon in the game.

  **Verification.** New `m78_player_attack_smoke` (33 checks, suite
  **69/69 -> 70/70**): the cadence's seed, drain and x6; the seed happening
  *above* the busy tests; both busy gates; a held attack key driven for 300
  ticks with a club and with a longbow, measured at 48 and 31 ticks between
  swings and never back to back; the 45-tick swing lock-out with its 32
  drawn frames and 13 blank ones; all three club variants staying inside
  the weapon's own 16-slot strip; the item-type switch; the cast's
  ungated swing; and the fatigue only being billed on a swing that
  happened. `m47_weapon_swing_smoke` was updated to assert five frames per
  variant instead of the artifact's six, and `m25_weapon_viewmodel_smoke`
  to the 45-tick lock-out.

  Confirmed in the running game: with a club equipped, holding the right-
  hand attack key next to a Bandit Brawler for 12 seconds reports
  `combat.swings +6` and `combat.hits_by_player +6` -- one hit per swing,
  about one every 1.9 seconds -- and holding the *left*-hand key with an
  empty left hand for the same 12 seconds reports nothing at all.

- [x] **M79 -- the spell's hands, its cast, and the fireball.** Reported:
  equipping the Blaze spell shows no hands holding it, no casting animation
  and no fireball, where equipping a weapon shows all three. Everything
  below is one C++ class constructor this port never ran, plus the draw it
  never had. Write-up in docs/WORLD_MODEL.md's "The weapon swing" section
  ("The spell's own viewmodel") and render3d/zone_renderer.h.

    - **A spell's viewmodel comes from its constructor, not its script.**
      M74 established that an item's class is picked by its `entities.txt`
      category and read `+0x16c` (the item type) and `+0x1c0` (the hand)
      out of the eight constructors. It stopped there. Three more of the
      same writes decide what a held item *looks like*:

      | class | ctor | `+0x19c` sprite | `+0x180` frames | `+0x184` scale |
      |-------|------|-----------------|-----------------|----------------|
      | base item | `FUN_1006c960` | 0 | 0 | `0x100` |
      | weapon (cat 4, 16) | `FUN_1002ce9c` | -- | -- | **`0x300`** |
      | spell (cat 5, 14) | `FUN_10047740` | **`0x98`** | **5** | -- |

      Both non-default rows are single `mov`/`str` pairs read off the
      disassembly rather than the decompiler (`10047768: mov r2, #0x98;
      str r2, [r4, #0x19c]` and `1002cf7c: mov r3, #0x300; str r3, [r4,
      #0x184]`). So **every spell in the game already owns `global.spr`
      slot 152 and a five-frame animation** without a single spell script
      calling `SetWeaponSprite` -- which is why this port, which only ever
      took those fields from a script setter, concluded for eight
      milestones that a spell had no viewmodel at all.

      The slot is exactly where a spell belongs. The five melee weapon
      strips are 16 slots each at 72/88/104/120/136, `136 + 16 == 152`, and
      slots 152..159 of the shipped `global.spr` are all full-screen
      176x208 viewmodel frames -- 152 the smallest (the idle "holding a
      spell" pose) and 153..157 the cast.

    - **And a spell is drawn like a weapon, one level up.**
      `FUN_10042e44` (player vtable +0x274), the line the input handler
      runs immediately before each hand's use, sets `player+0x204` for item
      type 1 **and type 2** and clears it for everything else. This port
      set the active item only inside the weapon branch of `FUN_10042394`,
      so a spell in hand never became `player+0x204` -- and since
      `FUN_10042394` case 2 re-arms the swing from `player+0x204`, a cast
      drew nothing and animated nothing even after the sprite was there.

    - **CORRECTED (M78's `+0x184`): a weapon's swing is three times
      faster.** This is the *other* reported symptom, "the weapon swinging
      animation is really slow", coming back with a real cause. M78 closed
      M47's inference here by reading a constructor -- and read the wrong
      one. `FUN_1006c960` really does write `+0x184 = 0x100`, but it is the
      **base item** constructor; the Weapon class constructor calls it
      first and then overwrites the field with `0x300`. Nothing in the
      corpus calls `SetReloadSpeed`, so that value stands for every weapon
      in the game, and the swing accumulator drains at 120 a tick, not 40:

      | item | `+0x184` | drain | 1792 units last |
      |---|---|---|---|
      | weapon | `0x300` | 120/tick | **15 ticks, 0.6s** (11 drawn, 4 blank) |
      | spell | `0x100` | 40/tick | **45 ticks, 1.8s** (32 drawn, 13 blank) |

      With the Speed-50 cadence that puts a held melee attack at one swing
      every **18 ticks (0.72s)**, where M78 measured 48. A cast is slower
      than a sword swing, which reads oddly and is what the code says.

      It also moved which item can hit M78's `acc == 0x200` overrun frame:
      at a fixed frame delta the question is whether `1792 - 512 = 1280` is
      a whole number of drains, and it is for a spell (1280/40 = 32, so
      every cast would have shown slot 158) and is not for a weapon
      (1280/120 is not an integer). The `<=` guard was needed both times.

    - **The fireball: a billboard pass.** A spell projectile
      (simkin_bindings/spell_projectile.h) and a scripted effect
      (simkin_bindings/effect_entity.h) are one engine class with one draw,
      `FUN_1008b25c`, and it submits a `global.spr` slot as a
      screen-aligned quad rather than any geometry. M48 and M63 carried all
      of that state and drew none of it, because `render3d/zone_renderer.h`
      had no billboard pass. It has one now
      (`sk::SpriteBillboard`), fed every live projectile and every live
      effect each frame. The quad is built in camera space --

          halfW = ((i16)(sizeX * spriteW) * spriteW) >> 8    // both twice
          draw(x0 = cx - halfW, y0 = cy + 2*halfH,
               x1 = cx + halfW, y1 = cy, depth = cz)

      -- so a billboard is bottom-anchored at the entity's Z, and
      `FUN_1004f91c` projects the two corners with exactly the projection
      M71 recovered (`0x5800 + x*0x5800/z`, `0x6800 - y*0x6800/z`, aspect
      prescale making both focal lengths 104) and culls at `depth < 5`.
      A projectile keeps `FUN_1008b420`'s `+0x12c`/`+0x130` of 8, so
      blaze's 32x32 fireball is 64 world units across -- a quarter tile,
      against a collision box six times wider.

    - **CORRECTED (M63): `+0x138` is an opacity, not a size scale.** Ghidra
      mis-types `FUN_1004f91c`'s parameter list by one -- the draw passes it
      fifteen arguments against fourteen declared -- so `+0x58` and `+0x138`
      both land a slot early and M63 read the blend level as a scale.
      Following the *call* instead, `FUN_1004f218`'s last two arguments are
      `+0x58` (0 = straight copy, 1 = blend) and `+0x138` (the level,
      quantised `(v + 0x1f) >> 6` into 0 = draw nothing, 1 = 25% source,
      2 = 50%, 3 = 75%, 4 = fully opaque). Nothing about the drawn size
      passes through it. So a scripted effect's "scale ramp" is a **fade**,
      the engine's blood spurt fades from solid to a quarter rather than
      shrinking, and a spell projectile (`+0x58 = 0`) is an opaque blit.

    - **The HUD goes over the viewmodel, not under it.** Reported the
      moment spells got their art: the casting hands drew across the vitals
      bars in the bottom-left corner. `FUN_10029cb0`'s in-game arm draws
      them in one fixed order and the viewmodel is **first** --
      `if (player->+0x204) FUN_1002b1b0(this)`, then `FUN_1002ae88` (the
      three vitals bars), `FUN_1002ba64` (the compass) and `FUN_1002bb54`
      (the hand icons), with `FUN_1002b430` (the map) last. This port drew
      the HUD first and the viewmodel over it, which nothing noticed while
      every viewmodel was a weapon held bottom-right. That leading
      `if (player->+0x204)` is also why the draw can dereference the active
      item with no null check.

    - **The starting kit goes through the real creation path now.** An item
      built straight from a script path never learns its `entities.txt`
      category, and the category is what picks the C++ class -- so it gets
      none of the constructor defaults above. `LoadStartingInventory` built
      its three items that way, which left the starting club's `+0x184` at
      `0x100`: the one weapon a new player actually holds was the one
      swinging at a third of the right rate. `LevelExecutable::
      TypeIdForScript()` reads `entities.txt` the other way round for it.

  **Verification.** New `m79_spell_viewmodel_smoke` (27 checks, suite
  **70/70 -> 71/71**): every constructor default for all four item classes;
  the drain rate for each; the cast drawing 153..157 and stopping short of
  158; a cast running 45 ticks against a club's 15; the club still drawing
  89..93 at the faster rate; the half-extent formula for a fireball and for
  crypt1's flame; and the billboard rendered for real through `ZoneRenderer`
  against azra -- it draws, at the width the projection gives it, nothing
  behind the camera, and nothing at blend level 0. `m25_weapon_viewmodel_
  smoke`, `m47_weapon_swing_smoke` and `m78_player_attack_smoke` were moved
  onto the real creation path too (a test that loads by script path measures
  the wrong machine) and their timings corrected: 45 ticks -> 15, 48 ->
  18, 32 drawn frames -> 11.

  Confirmed in the running game, with Blaze in hand and the attack key held
  for 10 seconds: `viewmodel.slot.152 +185` (the idle hands),
  `viewmodel.slot.153..157 +24/+18/+21/+18/+18` (the five cast frames, in
  proportion) and `render.billboards +36` -- three casts, each a five-frame
  animation and a twelve-tick fireball. A new `viewmodel.slot.<n>` debug
  counter is what makes the first two visible from a console.

- [x] **M80 -- the message popups.** Reported: walking around Azra never
  produces the tutorial messages the real game shows, and that is only the
  visible corner of a general mechanism -- an `EnterZone` handler opening a
  menu is how the game delivers hints, warnings and a good part of its
  story. All 21 zone root scripts have an `EnterZone`, and 19 of them open
  a menu from inside it -- 128 `OpenMenu` call sites in those handlers
  alone, 37 of them in crypt1.s. Write-ups in
  docs/INPUT_HANDLING.md ("Naming a key inside a sentence") and
  docs/GRAPHICS_FORMAT.md ("The menu list's own layout").

    - **The menu was being built and then drawn over.** `OpenMenu` ran,
      the target script's `Init()` ran, its rows existed -- and then the
      in-game arm of the frame loop rendered the 3D view and returned,
      because nothing told it a menu had appeared. The Use path (a door,
      an NPC's conversation, a loot bag) and the OnDetect path already
      compared `stack.currentMenu()` before and after invoking a script
      handler and paused into the result; the M44 region walk never did,
      even though its own comment named azra's `YouSure` region as the
      reason it had to be edge-triggered. One before/after comparison, the
      same `gamePausedForMenu` hand-off, and every one of those 19 zones'
      popups arrives.

    - **`ParseActionText`, and the twelve `[KD_*]` tokens.** The Menu
      class's binding 2 (`FUN_1003136c` case 2), and the loudest soft-fail
      in the game's opening minute. The shipped tutorial strings name their
      keys with a bracketed token -- 2995 is `"Press [KD_5] to continue."`
      -- and `FUN_10030250` rewrites each one with the name of the key that
      *action* is bound to now, via
      `FUN_1001a578(input, ResolveBindingOffset(input, N))`. Twelve tokens,
      and each one's `mov r1, #N` names the action whose **default** key is
      the one in the token -- twelve for twelve, which is an independent
      confirmation of M57's action-index table from a function that has
      nothing else to do with it. Unknown token, and the pass stops there
      with that token and every later one left in the text. Five scripts,
      29 calls, and between them they are the entire tutorial.

    - **`AddStaticItem` is a four-argument native and this port was using
      one of them.** `FUN_10078de4` case 0x6c into `FUN_1007dca0`:

      | argument | default | what it does |
      |---|---|---|
      | text | -- | dynamically typed -- a string id *or* a resolved string |
      | leftAligned | 1, or 0 when the text starts `"---"` | picks the draw arm |
      | maxChars | `0x11` (17) | wrap width, forced to `0x19` (25) when left-aligned |
      | fontOverride | `-0x2153` | rarely used |

      The first of those is why `ParseActionText`'s result was landing as
      `intValue() == 0`. The third is why a 170-character paragraph drew
      as one line off both edges of a 176-pixel screen: **a static item
      word-wraps into one widget per line**, and nothing in this port
      wrapped at all. The second is not "selectable" (that is
      `widget+0x5c`, 0 for every static item) -- `FUN_10076b64` passes it,
      inverted, to `FUN_1007f49c`, which either draws left-aligned at the
      literal `x = 9` with a `0x0b96` shadow copy at `(x+1, y+1)`, or
      centres with no x at all. `0x0b96` is RGB444 like every other colour
      constant in that code (M57), i.e. a warm tan behind the darker text.

    - **The row pitch is `0xc`.** `FUN_10076b64` advances its one cursor by
      a literal 12 pixels per text row, from a start of `menu+0x96` --
      `SetStartCoord`'s field, constructor default `0x32` (M64). This port
      had 16, a placeholder from before any of the draw was read. Those
      three numbers cross-check each other: at 0x32/0xc/0x19 every one of
      the nine shipped tutorial pages fits a 208-pixel screen and the
      longest ends at y=194; at 16 the same page ends at 242 and loses its
      own "continue" button off the bottom, which would make the tutorial
      unfinishable.

    - **CORRECTED: a zone script was shadowing the `Level` global.**
      `LevelExecutable::AttachZoneScript` handed the zone script
      `setAddIfNotPresent(true)` so that an undeclared `Level.saved_X`
      would read falsy rather than throw (five shipped typos need that).
      But the vendored `skTreeNodeObject::getValue()` honours that flag by
      *creating* the missing child and returning true, and
      `skInterpreter::findValue` asks the current object before the global
      table -- so inside a zone script the bare name `Level` resolved to a
      freshly minted empty tree node:

          shadowkey-port: RUNTIME ERROR in zone script Init():
            azra.s:Init:0-Method PlayAmbient not found

      That is `Level.PlayAmbient(73,100)`, azra.s's first statement, taking
      the whole of its `Init()` with it -- including the `SetZone(1, 2000)`
      that populates the zone with 52 wandering creatures. Four of azra's
      own `EnterZone` arms are `Level.` calls too, so `Level.GetEntity("m1")`
      (which gates the action-queue tutorial) and three
      `Level.LoadLevel(...)` region transitions could not complete either.
      `ZoneScriptExecutable::getValue()` now spells the tolerance out in the
      order the interpreter would have used: declared field, then registered
      global, then falsy. **The existing m44 test could not have caught
      this** -- it builds a zone script without calling `AttachZoneScript`,
      which is exactly the arm that sets the flag.

    - **CORRECTED in the same session: every menu opened by name starts on
      `global.spr` slot 20.** The first pass read `FUN_10076b64`'s
      `if (-1 < menu+0x50)` background guard as "a menu with no
      `MenuBackground(id)` draws over whatever is on screen" and gave
      in-game popups the frozen 3D frame to sit on. The guard is real but
      unreachable for a script-opened menu: `FUN_100779b8`, the open-by-name
      routine behind every script-level `OpenMenu`, clears the widget list
      and then writes `*(menu + 0x50) = 0x14` before it loads the new
      script. The parchment is the **default**, and `MenuBackground(id)` is
      an override the script's own `Init()` applies afterwards -- which is
      why 66 of the corpus's 79 `MenuBackground` calls pass 20, restating a
      default they already have. Reported as "the menus have no background
      colour at all, the background is transparent"; the port now arms slot
      20 in `RunInit()`, ahead of the script, exactly where the engine does.

  **Verification.** New `m80_tutorial_popup_smoke` (27 checks, suite
  **71/71 -> 72/72**): the twelve tokens and their actions, cross-checked
  against the default bindings; the key labels read out of the real
  `stringtable.eng`; 2995 and 3748 substituted, and 2995 again after
  rebinding Use Right Action to key 1; the unknown-token and unclosed-
  bracket arms; the wrap against `FUN_1007dca0`'s arithmetic, losing
  nothing; `starthelp.s` opened for real and walked through all six pages
  with no `[KD_` surviving on any of them; the longest page's bottom edge;
  azra.s's `Init()` running to the end with `Level` unshadowed, its own
  declared fields still resolving and an undeclared one still falsy; and
  `EnterRegion("start")` opening starthelp once and not twice.

  Confirmed in the running game by screenshot: entering azra puts the
  wrapped alarm message and "Press Key 5 to continue." over the frozen
  first room, and the log now reads `SetZone(1, 2000) -> 52 creature(s)`
  where it used to read a runtime error.

- [x] **M81 -- the loot bags enemies drop.** Reported: enemies are supposed
      to sometimes drop loot bags with random loot to collect, and the port
      never dropped one. Every piece of the mechanism had been in place
      since M21 -- `SetLoot()` stored the tag, all four of main.cpp's death
      paths called `spawnLoot()`, the bag's own `OnUse()` opened the real
      `lootmenu.s`, and `m21_loot_smoke` passed end to end -- and not one
      bag had ever existed in a running session.

    - **The cause is a missing `.s`.** `spawnLoot()` built its script path
      by hand as `scriptRoot + "/" + tag`, i.e. `<root>/Loot_ratseye`,
      which is not a file. Simkin answers a file it cannot open with an
      **empty parse rather than an error** (`skInputFile::open` leaves a
      null handle and the reader sees immediate eof), so the bag loaded
      "successfully" as an object with no script in it: no `Init()`, so no
      `SetUsable(true)` -- and `findNearbyPickup()` skips anything not
      usable -- and no `Level.CreateEntity`/`AddObject`, so it was empty
      anyway. Combined with the `modelArchiveIndex = -1` below, every kill
      in the game appended an invisible, unusable, empty object to
      `gamePickups` and nothing else. **`m21_loot_smoke` could not catch
      it**: the test appends the extension itself
      (`scriptRoot + "/" + rat->lootTag() + ".s"`), so it exercised a path
      `main.cpp` never built. The load now lives in
      `LevelExecutable::CreateEntityWithScript()`, which main.cpp and the
      test both call, and goes through the same `ResolveScriptPath()` every
      other scripted path in this port uses -- which also fixes the eleven
      subdirectory tags (`SetLoot(300, "broken1\\loot2")` and friends),
      whose backslashes the hand-built concatenation left as a doubled
      separator.

    - **CORRECTED: the loot bag has a model, and always did.** M21 recorded
      "the loot bag has no real model (`entities.txt`'s typeId 300 is
      itself the `!bag_loot` label-only convention)" and hardcoded
      `modelArchiveIndex = -1`. The `!` marks the **name** column as a
      label rather than a script path; it says nothing about the model
      column two fields to its left. Row 212 is `300 30 8 !bag_loot`, and
      archive index **30** is `bag_dropped.bin` in all 21 playable zones'
      `<zone>_models.txt`. `FUN_100715a8` sets a spawned object's `+0x54`
      model pointer from `engine+0x6b38[descriptor->modelArchiveIndex]`
      whichever way its script was chosen, so the engine draws it. The port
      now reads the index off the descriptor, and takes `isContainer` from
      the same row's category 8 rather than leaving it false.

    - **CORRECTED: `FUN_1002c3a8` is not the on-death handler.**
      `docs/ZONE_FORMAT.md` named it one. It is `DropObject(item)`, the
      *inventory* drop: it spawns typeId 300 at `item->owner`'s position
      (`+0x170`, `Item::GetOwner`) minus half a tile on each axis and puts
      the item inside it, and its callers are the player dispatcher's
      drop-gold case and the item dispatcher's drop case. The real death
      drop is **`FUN_10084438`**, whose only caller is the creature death
      handler `FUN_10083c04`, gated on `monster+0x2f0 != 0`. Both the doc
      and the annotated dump are corrected.

    - **`SetLoot` leaves two fields, not one.** Dispatcher `0x10084924`
      case `0xd` writes the typeId to `monster+0x2f0` and the tag to
      `+0x2f4`, *after* the `rand(min,max) != min` bail that M39 decoded --
      so a creature that lost its drop-chance roll has neither, and
      `+0x2f0 != 0` is exactly the test `FUN_10083c04` uses to decide
      whether to drop at all. The port stored only the tag and keyed off
      "tag is non-empty"; it now stores both, because the typeId is what
      the spawn actually creates (and what supplies the category and the
      model), with the tag as a **script override** for it. All 134
      shipped call sites pass 300; it is read back rather than assumed,
      which is what lets the same code serve typeId 301's chests.

    - **Where the bag lands.** `FUN_10084438` places it at the creature's
      own x/y with `z + 300`, floor-snaps (`FUN_100686e0`) and lifts by
      `0x80` -- which together are the existing `Zone::SnapActorToGround`.
      The port dropped it at the creature's raw z, so a bag from anything
      killed on a slope or off the ground sat inside or above the floor.

    - **Found by running it: a second, script-driven drop path that was
      soft-failing.** With the fix in, the log filled with `[soft-fail]
      ZoneScript: CreateEntityScript(300, Loot_ratseye, ...)`.
      `Level.CreateEntityScript(typeId, script, x, y, z)` (Level dispatcher
      case `0x21`) is the same spawn called from a creature's *own*
      `OnKilled()`: `monsters/arat.s` and `monsters/spiderqueen.s` roll
      their own `Random(1,12)=12` and place their own bag with it (the
      spider queen's is `Loot_ShadowKey`), and `crypt1/shadowkeygate.s`
      uses it for three typeId-301 (`301 15 8 !chest_loot`) chests. Five
      live call sites, every one of which calls a method on the result
      (`Loot.SetDestroy(true)`) on the next line -- so the object is built
      and returned immediately and only the world placement is deferred,
      the same split the four-argument `CreateEntity` has used since M44.
      Its placement arm is *not* the death drop's: no `0x80` lift, and the
      storey probe is the tile's own authored `ZcpEntry::floorBandThreshold`
      rather than the caller's z (new `Zone::SnapSpawnedObjectToGround`).

    - **A creature could die without its death being handled at all.** The
      engine has one death handler, reached from the damage path itself, so
      it cannot miss one. This port reached the same handling from the four
      places that damage a creature, which left three real ways to die
      silently -- no `OnKilled()`, no loot, no kill-count trigger: an area
      spell (`FUN_1004720c` just `ApplyDamage()`s everything within 12000
      units), a script's own `DoDamage` (M65, 19 call sites), and the debug
      console's `killall` (whose own comment claimed otherwise). The four
      sites now share one idempotent `handleDeath()` guarded by
      `MonsterInstance::deathHandled`, with a sweep after the tick's attack
      handling that back-stops every other way health reaches zero.

    - **`InvokeOnKilled()` was logging a bogus soft-fail per kill.** It
      called `this->method("OnKilled")` -- the native dispatcher -- and
      most creature scripts define no `OnKilled` at all, so each one
      reported `Monster: OnKilled(0) -- not implemented`. Invisible while
      only four call sites reached it; a hundred lines of noise per fight
      once every death did. Fixed the way M35 and M75 already fixed the
      identical thing for `InvokeOnUse`: call `skScriptedExecutable::method`
      directly.

    - **One shipped `SetLoot` tag names nothing, and that is the game's own
      typo.** `dstar_e/dse_skyrim_soldier.s` asks for `"Bounder_Skin"`,
      which is the *item* (`items/bounder_skin.s`, typeId 4738) where a bag
      wrapper belongs; that item's real wrapper exists and is called
      `loot_sbskin.s`. The engine has no fallback either -- it sprintfs the
      tag into a path and loads it -- so in the original this soldier drops
      a bag that is present and drawn but unopenable, because
      `SetUsable(true)` lives in the `Init()` that never ran. The port
      reproduces that rather than second-guessing the data, and the test
      pins it so nobody "fixes" it later.

  **Verification.** New `m81_loot_drop_smoke` (40 checks, suite **72/72 ->
  73/73**): entities.txt row 212 and archive index 30 resolved to
  `bag_dropped.bin` in all 21 playable zones; the old hand-built path
  proved to produce a non-throwing, unusable, empty object; **all 134
  `SetLoot` call sites scanned out of the .s corpus** (with Simkin's own
  backslash unescaping) and resolved -- 133 of 134 under the new rule and
  **0 of 134 under the old one**, which is the measure of how completely
  this was broken; the drop-chance roll clearing both fields together over
  4000 fresh rats; the real bag loaded, filled, usable, opening the real
  `LootMenu` with its real row in it; `Loot_Gold25-35` rolling a genuinely
  different quantity per drop; `CreateEntityScript` returning its object,
  parking its placement and accepting `SetDestroy(true)`, for both typeId
  300 and shadowkeygate's typeId 301; and the two snaps differing by
  exactly the `0x80` lift.

  Confirmed in the running game (`port/debug/m81_loot.cfg`, a saved
  console repro): 108 creatures killed in azra leaves 15 `container
  Examine ... model 30` rows in `ents` where it used to leave none, one of
  them from `arat.s`'s own `CreateEntityScript` path, and walking up to one
  raises the real "Examine" prompt. The bags render very dark, which is the
  model-lighting gap M71 already recorded (the `engine+0x5c4` fade table,
  still never dumped), not new.

- [x] **M82 -- the gold a loot bag holds.** Reported straight after M81:
      enemies now drop bags and the bags open, but "if an enemy drops gold
      pieces these don't get added to my gold total". Every other link in
      the chain was already there and already correct.

    - **Gold is an ordinary item everywhere except one line of the
      engine.** `entities.txt` row 46 is `52 51 3 gold.s` -- category 3,
      the plain misc class that keys and quest trinkets use -- and
      `gold.s` itself is two lines (`SetID("gold")`, `SetName(1580)`).
      A loot wrapper builds one with `Level.CreateEntity(52)`, gives it
      `SetQuantity(Random(lo, hi))` and `AddObject`s it;
      `lootmenu.s` lists it as an ordinary row ("27 Gold Pieces") and its
      SelectItem() runs `GetPlayer().PickupItem(Object)` then
      `GetOpener().RemoveObject(Object)`. Nothing in the data marks it as
      money. What does is a single hardcoded template id in
      `FUN_1003d8e0`, the add-to-inventory path -- the player vtable's
      `+0x164`, which a world pickup, `GiveItem` and a loot-menu
      selection all funnel through, and which this port already
      implements as `PlayerExecutable::AddItem`.

    - **The missing branch.** Everything in `FUN_1003d8e0` after M56's
      four key-item flag tests is wrapped in `if (item->+0xc8 != 0x34)`,
      and the else is three instructions: `player+0x3ac+0x38 +=
      item->+0x1c4`, then `FUN_1001b484(engine, item)` -- add the
      quantity to the purse and queue the object for destruction. So a
      gold object never reaches the inventory list, is never stacked, is
      never equipped and does not survive the call. `player+0x3ac+0x38`
      is `player+0x3e4`, the exact address `FUN_1003e030` (M59's
      BuyProduct, ported long ago) subtracts a purchase from, so the two
      readings confirm each other. With no id test, the port kept the
      object: every gold drop in the game left a permanent, unusable,
      unsellable "Gold Pieces" row in the bag while the purse never moved.

    - **And the other half of the same `if`, also missing: a consumable
      stacks.** Inside the branch, the real code walks the inventory
      (`player+0x200`, `next` at `+0x160`) whenever the incoming item's
      type word (`+0x16c`, `FUN_1006d508`) is 4, and on a matching
      template id does `SetQuantity(held, held->quantity + 1)`,
      `SetOwner(held, player)` and destroys the incoming object -- the
      same `LAB_1003db8c` the gold branch jumps to. It adds **one**, not
      the incoming quantity, and that is safe because nothing but gold
      ever carries a stack size out of a script. The engine keeps a
      *second, separate* copy of this rule inside `FUN_1003e030`, which
      is why M59 implemented it on the buy path and this one stayed
      missing -- so until now buying five potions in one go made five
      inventory rows where the original makes one row of five, and two
      healing potions out of two loot bags made two rows instead of a
      stack. The port's one deviation is a `templateId() >= 0` guard,
      marked as a port safety property: the engine only ever sees objects
      from the entity factory, while this port can also build an item
      straight from a script path (LoadStartingInventory) and spells "no
      template" as -1.

  **Verification.** New `m82_gold_pickup_smoke` (36 checks, suite
  **73/73 -> 74/74**): entities.txt row 46 and the fact that nothing in
  the data distinguishes gold; **all 48 `SetQuantity` call sites scanned
  out of the .s corpus**, every one of them applied to a freshly created
  typeId 52, across 48 distinct scripts; the purse moving by exactly the
  quantity and the inventory not growing; the real `loot_gold25-35.s`
  bag driven through `PickupItem` + `RemoveObject`, the two natives
  `lootmenu.s` actually calls, leaving the bag empty so UpdateMenu takes
  its `GetFirst() = null` branch; 400 bags rolling 25..35 over 11
  distinct values with the purse gaining exactly their sum; three of one
  consumable becoming one row of quantity 3 while a different consumable
  and two identical weapons stay separate; and gold reaching neither
  hand nor any key-item flag.

  Confirmed in the running game (`port/debug/m82_gold.cfg`, a saved
  console repro): `gold = 0`, spawn and kill 30 `bbrawler`s (a 1-in-2
  `SetLoot(300, "Loot_Gold6-10")`), 31 bags on the floor, open one and
  take its row -- `picked up 10 gold (total 10)` in the log and
  `gold = 10` from the console.

## Next milestones (not yet started)

Roughly in priority order for reaching "actually playable," not commitments:

M56's coverage tool
(`shadowkey/ghidra/scripts/analyze_port_native_coverage.py`) now ranks
these by real call-site count instead of by guess -- re-run it rather
than trusting this list to stay current.

One blind spot in it, worth knowing before reading its percentages as
progress: it only counts calls written with an explicit receiver
(`GetPlayer().X`, `Level.X`, `GetOwner().X`, ...). A menu script's *bare*
calls -- `DisplayWeaponsPage(...)`, `SetGoldText(...)`, `RedrawPage(...)`,
every widget setter chained onto an `AddButton()` return value -- are
invisible to it. M60 implemented nineteen natives and moved none of the
four percentages. The suite's soft-fail count is the measure that moved
(**81 -> 62**), and for menu work it is the better one.

M61, by contrast, was all explicit-receiver work and the tool showed it:
`GetPlayer()` **90% -> 92%**, and `Level`'s unhandled-name list lost four
entries while its percentage stayed at 97%. Use whichever measure matches
the kind of native being implemented.

M62 found a third way to be invisible to it: `SetPosition` was already
counted as handled because *one* receiver (the monster) implemented it,
even though the player -- 44 of its 63 call sites -- did not. A name that
appears anywhere is a name the tool stops asking about, so
"[implemented on another receiver]" in its output is a claim to check, not
a result.

M63 moved both measures at once, which is unusual: soft-fails **62 -> 39**
(the thirteen ten-argument calls were 24 lines between them) *and*
`Level` 97% -> 98%, its unhandled-name list down to five. An
explicit-receiver native with a lot of arguments shows up in both.

M64 shows the two measures can also *disagree usefully*. `GetPlayer()`
went 92% -> 94% and its unhandled-name list lost thirteen entries, while
soft-fails stayed flat at **39** -- and that flatness is the result, not
an absence of one: the new smoke test drives a screen the suite had never
opened, which added nine soft-fail lines, and implementing that screen's
one unhandled native (`SetStartCoord`) removed them again. A milestone
that opens new ground can hold the soft-fail count steady only by
finishing what it opened.

M65 is the same shape again -- `GetPlayer()` **94% -> 96%**, soft-fails
flat at **39** -- and worth reading as the pattern rather than the
coincidence it looks like. Its smoke test enters a dungeon the suite had
never entered, which surfaced two natives with no handler anywhere
(`DoDamage`, 19 sites; `Random` on the *menu* class), both of them one
line each on top of machinery M58 and M21 had already built. Expect a
milestone that drives new script territory to find one or two of these,
and budget for finishing them: the alternative is a milestone that reports
a feature as done while the branch it gates does nothing.

M66 moved **neither** measure -- `GetPlayer()` stayed at 96%, soft-fails
at 39 -- and that is the correct reading, not a null result: it implements
no natives at all. Both measures are script-coverage measures, and a
renderer or engine milestone is invisible to both by construction. Its
evidence is elsewhere: a reproduction harness, a census over the shipped
`.zcp` data, and a byte-comparison of the tracked `.ppm` render dumps
before and after the change. Do not reach for the coverage tool to justify
work below the script layer.

M70 is another one -- no natives, both measures flat -- and its evidence is
a census, a decompiled draw path, and a set of `.ppm` dumps. M71 is a third,
and it adds the measure those two were missing: **a screenshot of the real
device, of a place the port can be put.** Every one of its four defects had
sat in the port for dozens of milestones with every smoke test passing,
because none of them is a computation the port gets wrong -- they are
constants the port never read. The lesson M67 wrote down ("play the game")
generalises: when the report is *visual*, get the two images side by side
first and let the difference tell you which decompiled function to re-read.

**The bullet list below is empty.** Every item that was in it -- merchants,
the store screen, `SetCameraStart`, `Level.CreateEffect`, the `levelup.s`
cluster, the `TestX` rolls, the `ZoneRenderer::Render` crash, and "what a
`.zsk` actually is" -- is done (M59-M70). Two things M71 left behind belong
in it when it is repopulated: **model lighting** (the `engine+0x5c4` fade
table, never dumped -- large models read far too dark), and **azra's green
wall patch** behind the candelabra, which no decompiled path explains yet.
Re-run
`shadowkey/ghidra/scripts/analyze_port_native_coverage.py` to repopulate it
from real call-site counts rather than adding guesses here.


## Verification approach

Every milestone gets a standalone smoke-test executable
(`port/src/tests/m<N>_*_smoke.cpp`) that exercises real game data without
depending on the windowed app or (flaky) screenshot tooling, plus an
interactive pass through the actual `shadowkey_port.exe` for anything
that's meaningfully different in a live loop (input timing, rendering).

**`shadowkey_port.exe` also writes `shadowkey_port.log`** (next to
wherever it's launched from, user-requested) -- a real OS-level tee
(`platform/win32/console_tee.h`, started as the very first thing in
`main()`) duplicates everything the console window already shows (action
traces, every `[soft-fail] ... -- not implemented` line, load errors)
into that file too, unchanged, with zero per-call-site changes anywhere
else in the codebase. Meant for reviewing what a real play session hit
that isn't implemented yet without copying console text by hand --
gitignored, overwritten fresh each run.
