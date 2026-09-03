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
  src/audio/vorbis_decoder.cpp
  src/audio/wav_file.cpp
)
target_include_directories(sk_audio PUBLIC src)
target_include_directories(sk_audio PRIVATE third_party/stb_vorbis)
if(WIN32)
  target_link_libraries(sk_audio PUBLIC ole32 xaudio2)
endif()

add_library(sk_bindings STATIC
  src/assets/string_table.cpp
  src/simkin_bindings/button_executable.cpp
  src/simkin_bindings/combat.cpp
  src/simkin_bindings/combo_box_executable.cpp
  src/simkin_bindings/door_executable.cpp
  src/simkin_bindings/floating_sprite_executable.cpp
  src/simkin_bindings/floating_text_executable.cpp
  src/simkin_bindings/game_constants.cpp
  src/simkin_bindings/item_button_executable.cpp
  src/simkin_bindings/item_executable.cpp
  src/simkin_bindings/level_executable.cpp
  src/simkin_bindings/menu_executable.cpp
  src/simkin_bindings/menu_item_handle.cpp
  src/simkin_bindings/menu_stack.cpp
  src/simkin_bindings/monster_executable.cpp
  src/simkin_bindings/native_binding_common.cpp
  src/simkin_bindings/player_executable.cpp
  src/simkin_bindings/popup_menu_executable.cpp
  src/simkin_bindings/slider_executable.cpp
  src/simkin_bindings/row_owner_ref.cpp
  src/simkin_bindings/table_executable.cpp
  src/simkin_bindings/test_armor_executable.cpp
  src/simkin_bindings/text_area_executable.cpp
  src/simkin_bindings/weapon_viewmodel.cpp
  src/simkin_bindings/zone_script_executable.cpp
)
target_include_directories(sk_bindings PUBLIC src)
target_link_libraries(sk_bindings PUBLIC simkin sk_entity_types sk_audio)

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
  src/world/model_archive.cpp
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

add_executable(shadowkey_port
  src/main.cpp
  src/assets/gdr_font.cpp
  src/assets/sprite_archive.cpp
  src/assets/zone_display_names.cpp
  src/graphics/bitmap_font.cpp
  src/platform/win32/console_tee.cpp
  src/platform/win32/crash_report.cpp
  src/platform/win32/exe_dir.cpp
  src/platform/win32/window.cpp
)

target_include_directories(shadowkey_port PRIVATE src)
target_link_libraries(shadowkey_port PRIVATE sk_bindings sk_render3d)

if(WIN32)
  target_compile_definitions(shadowkey_port PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(shadowkey_port PRIVATE user32 gdi32)
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
add_executable(loading_screen_smoke src/tests/m26_loading_screen_smoke.cpp
                                     src/assets/zone_display_names.cpp)
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
add_executable(save_archive_smoke src/tests/m40_save_archive_smoke.cpp src/assets/save_archive.cpp)
target_include_directories(save_archive_smoke PRIVATE src)

# Real per-frame tile visibility raycast -- see src/tests/m41_visibility_smoke.cpp.
add_executable(visibility_smoke src/tests/m41_visibility_smoke.cpp)
target_link_libraries(visibility_smoke PRIVATE sk_world)
