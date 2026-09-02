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
add_library(sk_bindings STATIC
  src/assets/string_table.cpp
  src/simkin_bindings/button_executable.cpp
  src/simkin_bindings/combat.cpp
  src/simkin_bindings/combo_box_executable.cpp
  src/simkin_bindings/floating_sprite_executable.cpp
  src/simkin_bindings/floating_text_executable.cpp
  src/simkin_bindings/game_constants.cpp
  src/simkin_bindings/item_button_executable.cpp
  src/simkin_bindings/item_executable.cpp
  src/simkin_bindings/menu_executable.cpp
  src/simkin_bindings/menu_item_handle.cpp
  src/simkin_bindings/menu_stack.cpp
  src/simkin_bindings/monster_executable.cpp
  src/simkin_bindings/native_binding_common.cpp
  src/simkin_bindings/player_executable.cpp
  src/simkin_bindings/popup_menu_executable.cpp
  src/simkin_bindings/row_owner_ref.cpp
  src/simkin_bindings/table_executable.cpp
  src/simkin_bindings/test_armor_executable.cpp
  src/simkin_bindings/text_area_executable.cpp
)
target_include_directories(sk_bindings PUBLIC src)
target_link_libraries(sk_bindings PUBLIC simkin)

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
  src/world/entity_types.cpp
  src/world/model_archive.cpp
  src/world/zone.cpp
)
target_include_directories(sk_world PUBLIC src)
target_link_libraries(sk_world PUBLIC puff)

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
  src/graphics/bitmap_font.cpp
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
