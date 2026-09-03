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

## Next milestones (not yet started)

Roughly in priority order for reaching "actually playable," not commitments:

- **Combat resolution (beyond the M12 slice above)** -- the rest of the
  Monster/Actor/Spell native classes are still untouched. Sized up last
  session (`shadowkey/simkin_native_bindings.json`): a Monster/AI
  class (~55 methods -- `AiAttack`/`AiFlee`/`AiPursue`/`AiSleep`,
  pathfinding, aggro), a separate Actor-movement class (~44 methods --
  `SetTarget`, `FollowPath`, `SetDead`), a shared combatant-stats class
  (~85 methods -- damage, spell resistance, status effects), a Weapon
  class (~24 methods) plus a weapon-damage class (~6), a Spell/Scroll
  class (~9) plus a magic-damage class (~4), and a generic game-object
  base class (~57 methods -- position, sound, physics-adjacent). Comparable
  in scope to M0-M11 combined, not a bounded decompile-and-patch pass.
  M12 above took the first narrow slice (one melee weapon vs. one
  monster type, `UseLeftAction`/`UseRightAction` wired up); M15 took a
  second (the generic `Action::Use` interact binding, doors); M16 took a
  third (generalized monster/NPC loading past M12's one hardcoded
  typeId, plus real NPC dialogue via `Action::Use` -- a real finding
  this pass showed NPC talk and "other monster types" were the same gap
  all along, both fixed by the same loader generalization); M17 took a
  fourth (real quest-state tracking, so M16's dialogue trees actually
  progress across repeated visits); M18 took a fifth (a first, narrow
  slice of the `Zone/Level`-root global object -- `GetEntity`/`GetPlayer`/
  `PlayAmbient` only -- which closed M17's own documented gap:
  `azra_rat.s`'s 8-kill quest now genuinely completes). Still open:
  spellcasting, ranged weapons, loot spawning, the rest of `Zone/Level`
  (`AddTrigger` and its own further "Door/trap trigger" return-type
  class, `CreateEntity`/`CreateEntityScript`, save/load-level state, ...)
  plus Zone effects (`Vignette`, `SpawnWithinRadius`, `AddEncounters`,
  ...), a zone-root `<zone>.s` script loader (`azra.s` itself is still
  never run -- see M18's "Not attempted" note), and `Action::Use`'s one
  remaining unbound category, pickups (world-to-inventory transfer).
- **Real save file format** -- M5's save system is simulated in-memory
  only; no on-disk save format has been RE'd yet.
- **`FUN_1002c010`'s big single vitals bar** -- confirmed real (appears
  in actual gameplay per a user screenshot) but its trigger condition is
  still unresolved after an exhaustive whole-memory reference scan; see
  the Post-M14 fix entry above and `docs/GRAPHICS_FORMAT.md`. Leading
  guess: an enemy lock-on/target health bar, unconfirmed.
- Audio: entirely unaddressed so far, format not RE'd.

## Verification approach

Every milestone gets a standalone smoke-test executable
(`port/src/tests/m<N>_*_smoke.cpp`) that exercises real game data without
depending on the windowed app or (flaky) screenshot tooling, plus an
interactive pass through the actual `shadowkey_port.exe` for anything
that's meaningfully different in a live loop (input timing, rendering).
