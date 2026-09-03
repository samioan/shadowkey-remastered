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

## Next milestones (not yet started)

Roughly in priority order for reaching "actually playable," not commitments:

- **`Zone/Level` global object (beyond M18/M23/M24's slices)** --
  `AddTrigger`'s own physical/position-based trap variant (`AddEntity`/
  `SetTrap`/`SetDoor`/`OpenDoor`/`ShowDamageMessage` -- needs real
  trigger-volume/position data this port doesn't have, the same root gap
  blocking the next item); `EnterZone(s)`'s own trigger-volume mechanism
  (M23's "Not attempted" note -- no real trigger-volume data source
  traced yet: is it `.ent`-based, `.zcp`-cell-based, or something else
  entirely); `CreateEntity`/`CreateEntityScript`'s non-item-shaped
  categories and save/load-level state; the "Encounter spawner" class
  (`AddEncounters`/`AddRandomSets`, random monster-group spawning --
  M24's own currently-inert stand-in); Zone effects beyond `SetZone`
  (`Vignette`/`SpawnWithinRadius`, ...).
- **The caster's spell-power stat, and the resistance gate built on it**
  -- all that is left of the status-effect system, whose nine branches are
  now fully implemented (M32/M33/M34; the selector is the spell entity's
  own `entities.txt` typeId via `FUN_100458e4`). Two connected gaps, both
  rooted in one missing stat:
    - The real `magnitude` behind every effect's parameters is the
      *caster's* spell power (`stats+0x34`, index 0x18), clamped to 25.
      This port has no such stat and substitutes the spell's own
      `SetRating()` -- a documented substitution, not a recovered value.
    - The dispatcher gates the **entire** effect, status included, behind
      a hit roll of `casterPower * 0x100 / (casterPower + resistance)`
      against `rand(0, 0x100)` (`FUN_1004bc60`/`FUN_1004bbd0`, defaulting
      `casterPower` to 100 with no caster stats block). The port keeps a
      flat resistance-subtraction model instead; adopting the real gate
      needs the stat above and would change every existing spell.
  Related: `pergan_asuul_crypt2.s` shows monsters casting these spells
  themselves via `AddSpell(Level.CreateEntity(4024), 35)`, so a caster is
  not always the player -- Absorb's heal currently always targets the
  player, correct only because nothing but the player casts in this port.
- **Small documented loose ends**, one call/argument each, left open in
  their own milestone's "Not attempted" note rather than guessed at:
  M21's empty-bag despawn (`QuitAndDestroyOpener`/`QueryDestroy`) and
  `SetLoot`'s trailing min/max args; M23's `SummonMe()`/`SummonMe2()`/
  `CountInventory()`/`SetZone`'s real meaning; M24's `Level.Log(...)`.
- **Real save file format** -- M5's save system is simulated in-memory
  only; no on-disk save format has been RE'd yet.
- **Which scenario each of `FUN_1002c010`'s 4 gating modes means** -- the
  function itself is resolved (M26: a zone-transition loading-progress
  bar, not the earlier "enemy lock-on/target health bar" guess, corrected
  by the user with a real reference). What is still open is narrower:
  which of the 4 real `ScreenModeController` mode values that gate it
  (3, 4, 10, 0x1f) maps to which real scenario -- zone travel vs.
  save-load vs. initial boot splash, etc. M26's entry has the details.
- **The real weapon-swing trigger** -- M25's RE pass fully decompiled the
  first-person viewmodel's draw function and struct shape but couldn't
  find the native call site that actually starts a swing (writes
  `WeaponViewState`'s `+0x230`/`+0x234`/`+0x238`), despite tracing every
  write site reachable from the Weapon class dispatcher. Recorded as an
  exhaustive-search-and-still-open gap rather than guessed at -- see M25's
  own entry and `simkin_bindings/weapon_viewmodel.h`'s class comment. This
  port currently substitutes the `UseLeftAction`/`UseRightAction` keypress
  that already drives combat resolution, undecompiled.
- **`AnimationClip::rate`'s real units** (M29) -- frames per second is the
  working reading and looks right in motion, but it hasn't been traced to a
  decompiled consumer. Likewise `SetAttachedWeapon` (a second model drawn
  in a creature's hand) is still stored-and-unused.
- **The real `TileGrid_RaycastVisibility` fan** in place of the renderer's
  fixed-radius tile scan (M28 rebuilt everything else about that pipeline
  against the decompile, but the visibility set is still an approximation).
- **Audio's remaining narrower gaps** (M27, `docs/AUDIO_FORMAT.md`
  resolved the core system) -- native-only player-action sounds (attack/
  jump/death/footsteps, never called from any script); main-menu
  background music (no real script trigger found); `crypt2/controller.s`/
  `twilite/steamsound.s`'s own unresolved loading mechanisms; positional/
  3D audio; the exact volume/pan mixing formula.

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
