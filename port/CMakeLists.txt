cmake_minimum_required(VERSION 3.20)
project(shadowkey_port CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4 /utf-8)
endif()

# --- Vendored Simkin C++ interpreter (LGPL, see third_party/simkin/PROVENANCE.md) ---
# Only the "TreeNode" flavor (see upstream Src/Makefile's treenode_objects
# list): the script parser/interpreter core, no XML backend (Expat/Xerces/
# MSXML) -- the game's .s files use Simkin's native script syntax, not its
# XML data format, so the XML-only sources under third_party/simkin/src
# are vendored but intentionally not compiled here.
set(SIMKIN_DIR third_party/simkin/src)
# skLang_tab.cpp (the generated parser) is deliberately not listed here --
# skParser.cpp #includes it directly (upstream's own pattern; its Makefile
# omits skLang_tab.o from the object list for the same reason). Compiling
# it separately produces a duplicate yyparse() definition at link time.
add_library(simkin STATIC
  ${SIMKIN_DIR}/skAlist.cpp
  ${SIMKIN_DIR}/skConstants.cpp
  ${SIMKIN_DIR}/skEncodingUtils.cpp
  ${SIMKIN_DIR}/skExecutable.cpp
  ${SIMKIN_DIR}/skExecutableContext.cpp
  ${SIMKIN_DIR}/skExecutableIterator.cpp
  ${SIMKIN_DIR}/skHashTable.cpp
  ${SIMKIN_DIR}/skInputSource.cpp
  ${SIMKIN_DIR}/skInterpreter.cpp
  ${SIMKIN_DIR}/skInterpreterByte.cpp
  ${SIMKIN_DIR}/skInterpreterNode.cpp
  ${SIMKIN_DIR}/skOutputDestination.cpp
  ${SIMKIN_DIR}/skParseNode.cpp
  ${SIMKIN_DIR}/skParser.cpp
  ${SIMKIN_DIR}/skRValue.cpp
  ${SIMKIN_DIR}/skSHashTable.cpp
  ${SIMKIN_DIR}/skScriptedExecutable.cpp
  ${SIMKIN_DIR}/skStackFrame.cpp
  ${SIMKIN_DIR}/skString.cpp
  ${SIMKIN_DIR}/skStringBuffer.cpp
  ${SIMKIN_DIR}/skStringTokenizer.cpp
  ${SIMKIN_DIR}/skTracer.cpp
  ${SIMKIN_DIR}/skTreeNode.cpp
  ${SIMKIN_DIR}/skTreeNodeObject.cpp
  ${SIMKIN_DIR}/skTreeNodeObjectEnumerator.cpp
)
target_include_directories(simkin PUBLIC ${SIMKIN_DIR})
if(MSVC)
  # Vendored 2004-era source, not warning-clean at /W4 -- don't hold it to
  # this project's own warning level.
  target_compile_options(simkin PRIVATE /W3)
endif()

# Native binding glue shared by shadowkey_port and the milestone smoke
# tests. See src/simkin_bindings/ -- one file per native binding class.
# string_table.cpp lives here (not in shadowkey_port's own sources below)
# because M10's ItemExecutable/PlayerExecutable resolve real item/vitals
# text through it directly (not just at render time like every other menu
# row) -- smoke tests that link this library alone need the symbol too.
# entities.txt loader (world/entity_types.h) -- its own tiny library, not
# folded into sk_bindings or sk_world directly, because M21 needs it in
# both (LevelExecutable::CreateEntity() resolves a typeId the same way
# main.cpp's zone-load block already does) and a source file can't
# safely be compiled into two static libraries that might both end up
# linked into the same executable (duplicate-symbol territory) -- this
# is the shared dependency both pull in instead. Self-contained (just
# <fstream>), no puff/zlib dependency, so this doesn't pull sk_world's
# own dependency chain into every sk_bindings consumer.
add_library(sk_entity_types STATIC
  src/world/entity_types.cpp
)
target_include_directories(sk_entity_types PUBLIC src)

# M27: real audio pipeline -- real .wav sound effects (a plain RIFF/WAVE
# PCM parser, no library needed) and real .ogg ambient/battle music
# (vendored third_party/stb_vorbis, PROVENANCE.md) decoded to PCM, played
# back via XAudio2 (bundled with Windows, no redistributable). See
# src/audio/ and src/assets/sound_archive.h.
add_library(sk_audio STATIC
  src/assets/sound_archive.cpp
  src/audio/audio_engine.cpp
  src/audio/sound_mixing.cpp
  src/audio/vorbis_decoder.cpp
  src/audio/wav_file.cpp
)
target_include_directories(sk_audio PUBLIC src)
target_include_directories(sk_audio PRIVATE third_party/stb_vorbis)
if(WIN32)
  target_link_libraries(sk_audio PUBLIC ole32 xaudio2)
endif()

# M40/M50: the real save file -- the named-blob container, the byte
# stream every member is written through, and the record graph that
# stream carries. Its own library because sk_bindings needs it (a save
# slot is a real file now) and the save smoke tests link it alone.
add_library(sk_save STATIC
  src/assets/save_archive.cpp
  src/assets/save_records.cpp
  src/assets/save_stream.cpp
  # M52: dragonstar.set, the settings file -- the same "what does the
  # engine persist" question, so it lives beside the save format.
  src/assets/game_config.cpp
)
target_include_directories(sk_save PUBLIC src)

add_library(sk_bindings STATIC
  src/assets/string_table.cpp
  # M90: `GetNextLevelName()` (GameEngine-root binding 10) resolves a
  # zone display name from inside MenuExecutable now, not just from
  # main.cpp's loading screen, so the table moves in here with it.
  src/assets/zone_display_names.cpp
  src/simkin_bindings/action_text.cpp
  src/simkin_bindings/button_executable.cpp
  src/simkin_bindings/character_progression.cpp
  src/simkin_bindings/combat.cpp
  src/simkin_bindings/combo_box_executable.cpp
  src/simkin_bindings/door_executable.cpp
  src/simkin_bindings/effects.cpp
  src/simkin_bindings/floating_sprite_executable.cpp
  src/simkin_bindings/floating_text_executable.cpp
  src/simkin_bindings/game_constants.cpp
  src/simkin_bindings/item_button_executable.cpp
  src/simkin_bindings/item_executable.cpp
  src/simkin_bindings/language.cpp
  src/simkin_bindings/level_executable.cpp
  src/simkin_bindings/menu_executable.cpp
  src/simkin_bindings/menu_item_handle.cpp
  src/simkin_bindings/menu_stack.cpp
  src/simkin_bindings/monster_executable.cpp
  src/simkin_bindings/native_binding_common.cpp
  src/simkin_bindings/path_table.cpp
  src/simkin_bindings/player_executable.cpp
  src/simkin_bindings/player_save.cpp
  src/simkin_bindings/popup_menu_executable.cpp
  src/simkin_bindings/slider_executable.cpp
  src/simkin_bindings/spell_actor.cpp
  src/simkin_bindings/arrow_projectile.cpp
  src/simkin_bindings/spell_cast.cpp
  src/simkin_bindings/spell_projectile.cpp
  src/simkin_bindings/stats_damage.cpp
  src/simkin_bindings/store.cpp
  src/assets/product_database.cpp
  src/simkin_bindings/row_owner_ref.cpp
  src/simkin_bindings/entity_base_ref.cpp
  src/simkin_bindings/effect_entity.cpp
  src/simkin_bindings/script_delay.cpp
  src/simkin_bindings/table_executable.cpp
  src/simkin_bindings/trap_ref.cpp
  src/simkin_bindings/test_armor_executable.cpp
  src/simkin_bindings/text_area_executable.cpp
  src/simkin_bindings/weapon_viewmodel.cpp
  src/simkin_bindings/zone_script_executable.cpp
)
target_include_directories(sk_bindings PUBLIC src)
target_link_libraries(sk_bindings PUBLIC simkin sk_entity_types sk_audio sk_save)

# --- Vendored puff (zlib's reference inflate, zlib license, see
# third_party/puff/PROVENANCE.md) -- decompresses the zlib-wrapped
# per-zone files (.ztx/.zmp/.zlu/.zfg/.zcp/.zsk, docs/ZONE_FORMAT.md). ---
add_library(puff STATIC third_party/puff/puff.c)
target_include_directories(puff PUBLIC third_party/puff)
if(MSVC)
  target_compile_options(puff PRIVATE /W3)
endif()

# World/zone loading + the tile-grid wall renderer -- see src/world/ and
# src/render3d/.
add_library(sk_world STATIC
  src/assets/zone_file.cpp
  src/world/automap.cpp
  src/world/model_archive.cpp
  src/world/model_collision.cpp
  src/world/zone.cpp
)
target_include_directories(sk_world PUBLIC src)
target_link_libraries(sk_world PUBLIC puff sk_entity_types)

# A from-scratch (not fixed-point-exact) software renderer for the
# tile-grid wall/floor/ceiling geometry -- see src/render3d/zone_renderer.h.
add_library(sk_render3d STATIC
  src/render3d/zone_renderer.cpp
)
target_include_directories(sk_render3d PUBLIC src)
target_link_libraries(sk_render3d PUBLIC sk_world)

# --- M68: the developer debug suite (console, stat overlay, instrumentation) ---
#
# One option controls the whole thing. Turn it OFF and: this library is not
# built, SK_DEBUG_SUITE is not defined, and every touchpoint in main.cpp and
# window.cpp compiles out -- the game builds exactly as it did before M68.
# To delete it outright: set this OFF, delete src/debug/, delete
# src/graphics/overlay_surface.h, and remove the blocks that
# `grep -rn SK_DEBUG_SUITE port/src` reports. See docs/DEBUG_SUITE.md.
#
# It links sk_bindings (for the soft-fail observer hook) and simkin (for the
# interpreter's trace/statement-stepper hooks), but deliberately NOT sk_world
# -- everything the suite knows about the running game arrives through the
# abstract sk_debug::DebugHost that main.cpp implements.
option(SK_DEBUG_SUITE "Build the in-game developer debug suite (console + overlay)" ON)

if(SK_DEBUG_SUITE)
  add_library(sk_debug STATIC
    src/debug/debug_commands.cpp
    src/debug/debug_console.cpp
    src/debug/debug_metrics.cpp
    src/debug/debug_overlay.cpp
    src/debug/debug_suite.cpp
    src/debug/script_tracer.cpp
  )
  target_include_directories(sk_debug PUBLIC src)
  target_link_libraries(sk_debug PUBLIC simkin sk_bindings)
  target_compile_definitions(sk_debug PUBLIC SK_DEBUG_SUITE=1)
endif()

add_executable(shadowkey_port
  src/main.cpp
  src/assets/gdr_font.cpp
  src/assets/sprite_archive.cpp
  src/graphics/bitmap_font.cpp
  src/platform/win32/console_tee.cpp
  src/platform/win32/crash_report.cpp
  src/platform/win32/exe_dir.cpp
  src/platform/win32/window.cpp
)

target_include_directories(shadowkey_port PRIVATE src)
target_link_libraries(shadowkey_port PRIVATE sk_bindings sk_render3d)

if(SK_DEBUG_SUITE)
  target_link_libraries(shadowkey_port PRIVATE sk_debug)
endif()

if(WIN32)
  target_compile_definitions(shadowkey_port PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  # msimg32 is AlphaBlend, used only by the debug overlay's translucent
  # panels (src/platform/win32/window.cpp) -- see SK_DEBUG_SUITE above.
  target_link_libraries(shadowkey_port PRIVATE user32 gdi32 msimg32)
endif()

# M2 milestone smoke test -- see src/tests/m2_simkin_smoke.cpp. Separate
# console executable so it doesn't disturb the M0/M1 windowed app.
add_executable(simkin_m2_smoke src/tests/m2_simkin_smoke.cpp)
target_link_libraries(simkin_m2_smoke PRIVATE sk_bindings)

# M3 milestone smoke test -- see src/tests/m3_mainmenu_smoke.cpp.
add_executable(simkin_m3_smoke src/tests/m3_mainmenu_smoke.cpp)
target_link_libraries(simkin_m3_smoke PRIVATE sk_bindings)

# M6 milestone smoke tests -- see src/tests/m6_zone_*_smoke.cpp.
add_executable(zone_load_smoke src/tests/m6_zone_load_smoke.cpp)
target_link_libraries(zone_load_smoke PRIVATE sk_world)

add_executable(zone_render_smoke src/tests/m6_zone_render_smoke.cpp)
target_link_libraries(zone_render_smoke PRIVATE sk_render3d)

# M7 milestone smoke test -- see src/tests/m7_collision_smoke.cpp.
add_executable(collision_smoke src/tests/m7_collision_smoke.cpp)
target_link_libraries(collision_smoke PRIVATE sk_world)

# M8 milestone smoke tests -- see src/tests/m8_*_smoke.cpp.
add_executable(entity_smoke src/tests/m8_entity_smoke.cpp)
target_link_libraries(entity_smoke PRIVATE sk_world)

add_executable(render_entities_smoke src/tests/m8_render_entities_smoke.cpp)
target_link_libraries(render_entities_smoke PRIVATE sk_render3d)

add_executable(closeup_smoke src/tests/m8_closeup_smoke.cpp)
target_link_libraries(closeup_smoke PRIVATE sk_render3d)

# M9 milestone smoke tests -- see src/tests/m9_*_smoke.cpp.
add_executable(lighting_smoke src/tests/m9_lighting_smoke.cpp)
target_link_libraries(lighting_smoke PRIVATE sk_world)

add_executable(render_at_smoke src/tests/m9_render_at_smoke.cpp)
target_link_libraries(render_at_smoke PRIVATE sk_render3d)

# M10 milestone smoke test -- see src/tests/m10_inventory_smoke.cpp.
add_executable(inventory_smoke src/tests/m10_inventory_smoke.cpp)
target_link_libraries(inventory_smoke PRIVATE sk_bindings)

# Combat vertical-slice smoke test -- see src/tests/m12_combat_smoke.cpp.
add_executable(combat_smoke src/tests/m12_combat_smoke.cpp)
target_link_libraries(combat_smoke PRIVATE sk_bindings sk_world)

# Sprite archive (global.spr) smoke test -- see src/tests/m13_sprite_smoke.cpp.
add_executable(sprite_smoke src/tests/m13_sprite_smoke.cpp src/assets/sprite_archive.cpp)
target_include_directories(sprite_smoke PRIVATE src)

# M63: Level.CreateEffect + the animated-sprite entity -- see
# src/tests/m63_create_effect_smoke.cpp. Compiles sprite_archive.cpp
# directly (as sprite_smoke does) because part 4 checks the real
# global.spr geometry the art indices name.
add_executable(create_effect_smoke src/tests/m63_create_effect_smoke.cpp
                                   src/assets/sprite_archive.cpp)
target_link_libraries(create_effect_smoke PRIVATE sk_bindings)

# Real N-Gage system font (.gdr) smoke test -- see src/tests/m15_font_smoke.cpp.
add_executable(font_smoke src/tests/m15_font_smoke.cpp src/assets/gdr_font.cpp)
target_include_directories(font_smoke PRIVATE src)

# Action::Use interact binding (doors) smoke test -- see
# src/tests/m15_interact_smoke.cpp.
add_executable(interact_smoke src/tests/m15_interact_smoke.cpp)
target_link_libraries(interact_smoke PRIVATE sk_bindings sk_render3d)

# Generalized monster/NPC loading + Action::Use dialogue smoke test --
# see src/tests/m16_npc_smoke.cpp.
add_executable(npc_smoke src/tests/m16_npc_smoke.cpp)
target_link_libraries(npc_smoke PRIVATE sk_bindings sk_world)

# Quest-state tracking smoke test -- see src/tests/m17_quest_smoke.cpp.
add_executable(quest_smoke src/tests/m17_quest_smoke.cpp)
target_link_libraries(quest_smoke PRIVATE sk_bindings)

# Level/Zone global object smoke test -- see src/tests/m18_level_smoke.cpp.
add_executable(level_smoke src/tests/m18_level_smoke.cpp)
target_link_libraries(level_smoke PRIVATE sk_bindings sk_world)

# Pickups (Action::Use interact binding) smoke test -- see
# src/tests/m19_pickup_smoke.cpp.
add_executable(pickup_smoke src/tests/m19_pickup_smoke.cpp)
target_link_libraries(pickup_smoke PRIVATE sk_bindings sk_world)

# Ranged weapons smoke test -- see src/tests/m20_ranged_smoke.cpp.
add_executable(ranged_smoke src/tests/m20_ranged_smoke.cpp)
target_link_libraries(ranged_smoke PRIVATE sk_bindings)

# Monster-death loot-bag spawning smoke test -- see
# src/tests/m21_loot_smoke.cpp.
add_executable(loot_smoke src/tests/m21_loot_smoke.cpp)
target_link_libraries(loot_smoke PRIVATE sk_bindings)

# Spellcasting smoke test -- see src/tests/m22_spell_smoke.cpp.
add_executable(spell_smoke src/tests/m22_spell_smoke.cpp)
target_link_libraries(spell_smoke PRIVATE sk_bindings)

# Zone-root script loading smoke test -- see
# src/tests/m23_zonescript_smoke.cpp.
add_executable(zonescript_smoke src/tests/m23_zonescript_smoke.cpp)
target_link_libraries(zonescript_smoke PRIVATE sk_bindings sk_world)

# AddTrigger kill-count callback smoke test -- see
# src/tests/m24_trigger_smoke.cpp.
add_executable(trigger_smoke src/tests/m24_trigger_smoke.cpp)
target_link_libraries(trigger_smoke PRIVATE sk_bindings)

# Weapon viewmodel smoke test -- see src/tests/m25_weapon_viewmodel_smoke.cpp.
add_executable(weapon_viewmodel_smoke src/tests/m25_weapon_viewmodel_smoke.cpp
                                       src/assets/sprite_archive.cpp)
target_link_libraries(weapon_viewmodel_smoke PRIVATE sk_bindings)

# Real zone-transition loading screen smoke test -- see
# src/tests/m26_loading_screen_smoke.cpp.
add_executable(loading_screen_smoke src/tests/m26_loading_screen_smoke.cpp)
target_link_libraries(loading_screen_smoke PRIVATE sk_bindings)

# Real audio pipeline smoke test -- see src/tests/m27_audio_smoke.cpp.
add_executable(audio_smoke src/tests/m27_audio_smoke.cpp)
target_link_libraries(audio_smoke PRIVATE sk_bindings)

# Real .sur UV pipeline / .zcp corner nudge / line-of-sight smoke test --
# see src/tests/m28_surface_uv_smoke.cpp.
add_executable(surface_uv_smoke src/tests/m28_surface_uv_smoke.cpp)
target_link_libraries(surface_uv_smoke PRIVATE sk_world)

# Model animation frames + clip table smoke test -- see
# src/tests/m29_animation_smoke.cpp.
add_executable(animation_smoke src/tests/m29_animation_smoke.cpp)
target_link_libraries(animation_smoke PRIVATE sk_world)

# AI package state machine / flee / paralysis / attack cadence smoke test --
# see src/tests/m32_ai_package_smoke.cpp.
add_executable(ai_package_smoke src/tests/m32_ai_package_smoke.cpp)
target_link_libraries(ai_package_smoke PRIVATE sk_bindings)

# .ent per-placement script path + equip-screen layout navigation +
# weapon-swing restart guard -- see src/tests/m35_placement_menu_smoke.cpp.
add_executable(placement_menu_smoke src/tests/m35_placement_menu_smoke.cpp)
target_link_libraries(placement_menu_smoke PRIVATE sk_bindings sk_world)

# Caster spell-power stat + the real magic to-hit/resistance gate -- see
# src/tests/m37_spellpower_smoke.cpp.
add_executable(spellpower_smoke src/tests/m37_spellpower_smoke.cpp)
target_link_libraries(spellpower_smoke PRIVATE sk_bindings)

# Physical trap/door zone triggers -- see src/tests/m38_trap_trigger_smoke.cpp.
add_executable(trap_trigger_smoke src/tests/m38_trap_trigger_smoke.cpp)
target_link_libraries(trap_trigger_smoke PRIVATE sk_bindings sk_world)

# Small documented loose ends (SetLoot chance, SetZone XP, SummonMe,
# CountInventory, Level.Log) -- see src/tests/m39_loose_ends_smoke.cpp.
add_executable(loose_ends_smoke src/tests/m39_loose_ends_smoke.cpp)
target_link_libraries(loose_ends_smoke PRIVATE sk_bindings)

# Real on-disk save container -- see src/tests/m40_save_archive_smoke.cpp.
add_executable(save_archive_smoke src/tests/m40_save_archive_smoke.cpp)
target_link_libraries(save_archive_smoke PRIVATE sk_save)

# Real per-frame tile visibility raycast -- see src/tests/m41_visibility_smoke.cpp.
add_executable(visibility_smoke src/tests/m41_visibility_smoke.cpp)
target_link_libraries(visibility_smoke PRIVATE sk_world)

# Monsters as spell casters -- the real creature spell table, the
# melee-vs-cast branch, and the player as a spell target. See
# src/tests/m43_monster_caster_smoke.cpp.
add_executable(monster_caster_smoke src/tests/m43_monster_caster_smoke.cpp)
target_link_libraries(monster_caster_smoke PRIVATE sk_bindings)

# Named zone regions (<zone>.zon), Lock/UnlockZone, LightRect and the
# Vignette table -- see src/tests/m44_zone_region_smoke.cpp.
add_executable(zone_region_smoke src/tests/m44_zone_region_smoke.cpp)
target_link_libraries(zone_region_smoke PRIVATE sk_bindings sk_world)

# The encounter spawner -- gate, set draw, placement search and live-count
# bookkeeping. See src/tests/m45_encounter_spawn_smoke.cpp.
add_executable(encounter_spawn_smoke src/tests/m45_encounter_spawn_smoke.cpp)
target_link_libraries(encounter_spawn_smoke PRIVATE sk_bindings sk_world)

# SetAttachedWeapon's real field (monster+0x2c2) -- the five weapon models,
# the bow's hardcoded ranged test, and the frame-aligned humanoid rig that
# makes sharing the body's animation frame correct. See
# src/tests/m46_attached_weapon_smoke.cpp.
add_executable(attached_weapon_smoke src/tests/m46_attached_weapon_smoke.cpp)
target_link_libraries(attached_weapon_smoke PRIVATE sk_bindings sk_world)

# The real weapon-swing trigger -- the gate, the accumulator's seed, the
# three melee swing variants and the weapon-swap transition. See
# src/tests/m47_weapon_swing_smoke.cpp.
add_executable(weapon_swing_smoke src/tests/m47_weapon_swing_smoke.cpp
                                   src/assets/sprite_archive.cpp)
target_link_libraries(weapon_swing_smoke PRIVATE sk_bindings)

# The rest of a real cast, and the projectile it launches -- the whole
# per-typeId cast table checked against the shipped scripts, the
# self-targeted spells that previously did nothing, and the twelve-tick
# flight whose impact is what runs HitTarget(). See
# src/tests/m48_spell_projectile_smoke.cpp.
add_executable(spell_projectile_smoke src/tests/m48_spell_projectile_smoke.cpp)
target_link_libraries(spell_projectile_smoke PRIVATE sk_bindings sk_world)

# The arrow -- a different projectile class from the spell's, the SetRange
# threshold that decides which weapons take the ranged path at all, and the
# floor-height wall test an arrow is actually stopped by. See
# src/tests/m49_arrow_projectile_smoke.cpp.
add_executable(arrow_projectile_smoke src/tests/m49_arrow_projectile_smoke.cpp)
target_link_libraries(arrow_projectile_smoke PRIVATE sk_bindings sk_world)

# What is inside a save file's members -- the byte stream, the twelve-deep
# record chain both members are built from, and a real character.dat
# round-tripped through a real archive. See
# src/tests/m50_save_records_smoke.cpp.
add_executable(save_records_smoke src/tests/m50_save_records_smoke.cpp)
target_link_libraries(save_records_smoke PRIVATE sk_bindings sk_save)

# The engine's own mixer -- the volume curve, the distance/direction
# attenuation, the eight-voice cap, the music fade, and the native sound
# triggers that no script ever calls. See
# src/tests/m51_audio_mixing_smoke.cpp.
add_executable(audio_mixing_smoke src/tests/m51_audio_mixing_smoke.cpp)
target_link_libraries(audio_mixing_smoke PRIVATE sk_bindings)

# What the save record's scalars actually are, the animation rate's units,
# and dragonstar.set -- the settings file. See
# src/tests/m52_save_fields_smoke.cpp.
add_executable(save_fields_smoke src/tests/m52_save_fields_smoke.cpp)
target_link_libraries(save_fields_smoke PRIVATE sk_save sk_world)

# Delay/DelayReached -- the entity script timer -- and what it says about
# the two scripts the roadmap called unplaceable. See
# src/tests/m53_script_delay_smoke.cpp.
add_executable(script_delay_smoke src/tests/m53_script_delay_smoke.cpp)
target_link_libraries(script_delay_smoke PRIVATE sk_bindings sk_world)

# Screen mode 0x1f -- QuitToMenu() -- and the rest of the screen-mode
# table it completes. See src/tests/m54_quit_to_menu_smoke.cpp.
add_executable(quit_to_menu_smoke src/tests/m54_quit_to_menu_smoke.cpp)
target_link_libraries(quit_to_menu_smoke PRIVATE sk_bindings sk_world)

# The two unnamed entity scalars turn out to be the per-model collision
# box. See src/tests/m55_model_collision_smoke.cpp.
add_executable(model_collision_smoke src/tests/m55_model_collision_smoke.cpp)
target_link_libraries(model_collision_smoke PRIVATE sk_world)

# The Player object's inventory/menu natives, the worst-covered receiver
# in the corpus. See src/tests/m56_player_api_smoke.cpp.
add_executable(player_api_smoke src/tests/m56_player_api_smoke.cpp)
target_link_libraries(player_api_smoke PRIVATE sk_bindings sk_world)

# The in-game map: the explored bitmap, the tile classification and the
# player arrow. See src/tests/m57_automap_smoke.cpp.
add_executable(automap_smoke src/tests/m57_automap_smoke.cpp)
target_link_libraries(automap_smoke PRIVATE sk_world)

# The effects system: the recovered constant table, AddEffect's three
# durations, the stat field map and the derived-stat recompute. See
# src/tests/m58_effects_smoke.cpp.
add_executable(effects_smoke src/tests/m58_effects_smoke.cpp)
target_link_libraries(effects_smoke PRIVATE sk_bindings sk_world)

# Merchants: products.dat, the store object and the buy/sell natives.
# See src/tests/m59_merchant_smoke.cpp.
add_executable(merchant_smoke src/tests/m59_merchant_smoke.cpp)
target_link_libraries(merchant_smoke PRIVATE sk_bindings sk_world)

# The store screen: the store-menu bindings, the page builder behind them,
# the shared widget class and the table's cell class.
# See src/tests/m60_store_screen_smoke.cpp.
add_executable(store_screen_smoke src/tests/m60_store_screen_smoke.cpp)
target_link_libraries(store_screen_smoke PRIVATE sk_bindings sk_world)

# See src/tests/m61_camera_start_smoke.cpp.
add_executable(camera_start_smoke src/tests/m61_camera_start_smoke.cpp)
target_link_libraries(camera_start_smoke PRIVATE sk_bindings sk_world)

# See src/tests/m62_set_position_smoke.cpp.
add_executable(set_position_smoke src/tests/m62_set_position_smoke.cpp)
target_link_libraries(set_position_smoke PRIVATE sk_bindings sk_world)

# The level-up screen: the level-up points, UpdateAttributes and the race/
# class tables behind it, the eight attribute setters, and the experience
# curve that makes any of it reachable.
# See src/tests/m64_level_up_smoke.cpp.
add_executable(level_up_smoke src/tests/m64_level_up_smoke.cpp)
target_link_libraries(level_up_smoke PRIVATE sk_bindings sk_world)

# The TestX attribute rolls and the crypt1.s gauntlet they gate: the roll's
# closed form, the eight-binding field map, and both outcomes of a real
# EnterZone check. See src/tests/m65_attribute_check_smoke.cpp.
add_executable(attribute_check_smoke src/tests/m65_attribute_check_smoke.cpp)
target_link_libraries(attribute_check_smoke PRIVATE sk_bindings sk_world)

# The near-plane clipper's real output bound, and the long-standing
# ZoneRenderer::Render crash that came from getting it wrong: the
# non-planar-tile census behind it, the camera pose that triggers it, and a
# full sweep of all 21 zones at every pitch.
# See src/tests/m66_render_clip_smoke.cpp.
add_executable(render_clip_smoke src/tests/m66_render_clip_smoke.cpp)
target_link_libraries(render_clip_smoke PRIVATE sk_render3d)

# What a `.zsk` actually is: the census over all 21 shipped files, the
# texture header the engine ignores, and the three renderer behaviours that
# make the mesh a skybox rather than room geometry.
# See src/tests/m70_skybox_smoke.cpp.
add_executable(skybox_smoke src/tests/m70_skybox_smoke.cpp)
target_link_libraries(skybox_smoke PRIVATE sk_render3d)

add_executable(door_stamp_smoke src/tests/m67_door_stamp_smoke.cpp)
target_link_libraries(door_stamp_smoke PRIVATE sk_render3d sk_bindings)

# The placement transform and the projection: the engine's real field of
# view, the `yaw = pi/2 - heading` convention (checked against 180 real door
# placements), the actor transform's -0x40 draw offset, and the two .ent
# orientation channels plus the per-instance scale this port never read.
# See src/tests/m71_placement_smoke.cpp.
add_executable(placement_smoke src/tests/m71_placement_smoke.cpp)
target_link_libraries(placement_smoke PRIVATE sk_render3d sk_bindings)

# The automatic aim-assist pitch: the real per-model creature-height table
# behind it (CMonster::Height, 0x1008679c) checked against every creature row
# in the shipped entities.txt, plus the engage window and the ease.
# See src/tests/m72_autoaim_smoke.cpp.
add_executable(autoaim_smoke src/tests/m72_autoaim_smoke.cpp)
target_link_libraries(autoaim_smoke PRIVATE sk_render3d sk_entity_types)

# The vitals economy: the four fatigue costs, the movement drain's period,
# both regeneration divisors reproduced from the shipped magic-number
# division, the amounts against the real race/sex attribute table, and the
# whole loop run at 25Hz on a real PlayerExecutable.
# See src/tests/m73_vitals_smoke.cpp.
add_executable(vitals_smoke src/tests/m73_vitals_smoke.cpp)
target_link_libraries(vitals_smoke PRIVATE sk_bindings sk_entity_types)

# Where an item's type comes from: the entities.txt category -> C++ class ->
# `+0x16c` chain, checked against every item row in the shipped data, plus
# the class-restriction gate, the two use texts a spell picks between, and
# the pickup/equip paths that follow from the preferred hand `+0x1c0`.
# See src/tests/m74_item_category_smoke.cpp.
add_executable(item_category_smoke src/tests/m74_item_category_smoke.cpp)
target_link_libraries(item_category_smoke PRIVATE sk_bindings sk_entity_types)

# The developer debug suite: the console's parsing/history/completion, the
# metrics sink and its mark/diff, the overlay's layout, and a full pass over
# every command against a fake DebugHost -- which is why the whole suite is
# built against an abstract host rather than main.cpp's locals.
# See src/tests/m68_debug_suite_smoke.cpp.
if(SK_DEBUG_SUITE)
  add_executable(debug_suite_smoke src/tests/m68_debug_suite_smoke.cpp)
  target_link_libraries(debug_suite_smoke PRIVATE sk_debug)
endif()

# Talking to people: the one byte that gates every use prompt
# (`entity+0xd8`, and the SetUseText that also writes it), the `Level`
# global turning out to be the zone-root script object, and GetOpener().
# See src/tests/m75_dialogue_smoke.cpp.
add_executable(dialogue_smoke src/tests/m75_dialogue_smoke.cpp)
target_link_libraries(dialogue_smoke PRIVATE sk_bindings sk_world)

# The other half of it: the AI tick's OnDetect arm -- a non-aggressive
# creature that notices the player and runs its own handler instead of
# attacking. See src/tests/m76_detect_smoke.cpp.
add_executable(detect_smoke src/tests/m76_detect_smoke.cpp)
target_link_libraries(detect_smoke PRIVATE sk_bindings sk_world)

# M77: the creature AI tick, transcribed -- the spawn AI package that made
# every non-rat enemy stand still, the cadence that gates the whole
# decision rather than the damage roll, the boss throttle, and the
# fourteen Monster(AI) bindings that were still soft-failing.
# See src/tests/m77_monster_ai_smoke.cpp.
add_executable(monster_ai_smoke src/tests/m77_monster_ai_smoke.cpp)
target_link_libraries(monster_ai_smoke PRIVATE sk_bindings sk_world)

# See src/tests/m78_player_attack_smoke.cpp.
add_executable(player_attack_smoke src/tests/m78_player_attack_smoke.cpp)
target_link_libraries(player_attack_smoke PRIVATE sk_bindings sk_world)

# See src/tests/m80_tutorial_popup_smoke.cpp.
add_executable(tutorial_popup_smoke src/tests/m80_tutorial_popup_smoke.cpp)
target_link_libraries(tutorial_popup_smoke PRIVATE sk_bindings)

# See src/tests/m79_spell_viewmodel_smoke.cpp.
add_executable(spell_viewmodel_smoke src/tests/m79_spell_viewmodel_smoke.cpp
                                     src/assets/sprite_archive.cpp)
target_link_libraries(spell_viewmodel_smoke PRIVATE sk_bindings sk_render3d)

# See src/tests/m81_loot_drop_smoke.cpp.
add_executable(loot_drop_smoke src/tests/m81_loot_drop_smoke.cpp)
target_link_libraries(loot_drop_smoke PRIVATE sk_bindings sk_world)

# See src/tests/m82_gold_pickup_smoke.cpp.
add_executable(gold_pickup_smoke src/tests/m82_gold_pickup_smoke.cpp)
target_link_libraries(gold_pickup_smoke PRIVATE sk_bindings sk_world)

# See src/tests/m84_melee_reach_smoke.cpp.
add_executable(melee_reach_smoke src/tests/m84_melee_reach_smoke.cpp)
target_link_libraries(melee_reach_smoke PRIVATE sk_bindings sk_render3d sk_world)

# See src/tests/m86_hit_feedback_smoke.cpp.
add_executable(hit_feedback_smoke src/tests/m86_hit_feedback_smoke.cpp
                                  src/assets/sprite_archive.cpp)
target_link_libraries(hit_feedback_smoke PRIVATE sk_bindings sk_render3d sk_world)

# See src/tests/m87_player_damage_smoke.cpp.
add_executable(player_damage_smoke src/tests/m87_player_damage_smoke.cpp)
target_link_libraries(player_damage_smoke PRIVATE sk_bindings sk_world)

# See src/tests/m88_quest_log_smoke.cpp.
add_executable(quest_log_smoke src/tests/m88_quest_log_smoke.cpp)
target_link_libraries(quest_log_smoke PRIVATE sk_bindings sk_world)

# See src/tests/m89_lighting_smoke.cpp.
add_executable(zone_lighting_smoke src/tests/m89_lighting_smoke.cpp)
target_link_libraries(zone_lighting_smoke PRIVATE sk_world)

# M90 milestone smoke test -- see src/tests/m90_level_transition_smoke.cpp.
add_executable(level_transition_smoke src/tests/m90_level_transition_smoke.cpp)
target_link_libraries(level_transition_smoke PRIVATE sk_bindings)

# M91 milestone smoke test -- see src/tests/m91_ingame_menu_smoke.cpp.
add_executable(ingame_menu_smoke src/tests/m91_ingame_menu_smoke.cpp)
target_link_libraries(ingame_menu_smoke PRIVATE sk_bindings)

# Post-M91 fix: the presentation aspect ratio -- see
# src/tests/aspect_viewport_smoke.cpp. Header-only arithmetic, so no
# libraries.
add_executable(aspect_viewport_smoke src/tests/aspect_viewport_smoke.cpp)
target_include_directories(aspect_viewport_smoke PRIVATE src)

# M92 milestone smoke test -- see src/tests/m92_entity_base_smoke.cpp.
add_executable(entity_base_smoke src/tests/m92_entity_base_smoke.cpp)
target_link_libraries(entity_base_smoke PRIVATE sk_bindings)

# M93 milestone smoke test -- see src/tests/m93_can_drop_smoke.cpp.
add_executable(can_drop_smoke src/tests/m93_can_drop_smoke.cpp)
target_link_libraries(can_drop_smoke PRIVATE sk_bindings)

# M94 milestone smoke test -- see src/tests/m94_trap_magic_smoke.cpp.
add_executable(trap_magic_smoke src/tests/m94_trap_magic_smoke.cpp)
target_link_libraries(trap_magic_smoke PRIVATE sk_bindings sk_world)

# M95 milestone smoke test -- see src/tests/m95_gamestate_smoke.cpp.
add_executable(gamestate_smoke src/tests/m95_gamestate_smoke.cpp)
target_link_libraries(gamestate_smoke PRIVATE sk_bindings sk_world)

# M96 milestone smoke test -- see src/tests/m96_stat_mod_xp_smoke.cpp.
add_executable(stat_mod_xp_smoke src/tests/m96_stat_mod_xp_smoke.cpp)
target_link_libraries(stat_mod_xp_smoke PRIVATE sk_bindings sk_world)

# M97 milestone smoke test -- see src/tests/m97_kill_experience_smoke.cpp.
add_executable(kill_experience_smoke src/tests/m97_kill_experience_smoke.cpp)
target_link_libraries(kill_experience_smoke PRIVATE sk_bindings sk_world)

# M98 milestone smoke test -- see src/tests/m98_attacker_terms_smoke.cpp.
add_executable(attacker_terms_smoke src/tests/m98_attacker_terms_smoke.cpp)
target_link_libraries(attacker_terms_smoke PRIVATE sk_bindings sk_world)

# M99 milestone smoke test -- see src/tests/m99_options_smoke.cpp.
add_executable(options_smoke src/tests/m99_options_smoke.cpp src/assets/gdr_font.cpp)
target_link_libraries(options_smoke PRIVATE sk_bindings sk_world)

# M100 milestone smoke test -- see src/tests/m100_hit_decay_smoke.cpp.
add_executable(hit_decay_smoke src/tests/m100_hit_decay_smoke.cpp)
target_link_libraries(hit_decay_smoke PRIVATE sk_bindings sk_world)
