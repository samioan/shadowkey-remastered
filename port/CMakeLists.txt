cmake_minimum_required(VERSION 3.20)
project(shadowkey_port CXX)

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

add_executable(shadowkey_port
  src/main.cpp
  src/platform/win32/window.cpp
)

target_include_directories(shadowkey_port PRIVATE src)
target_link_libraries(shadowkey_port PRIVATE simkin)

if(WIN32)
  target_compile_definitions(shadowkey_port PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(shadowkey_port PRIVATE user32 gdi32)
endif()

# M2 milestone smoke test -- see src/tests/m2_simkin_smoke.cpp. Separate
# console executable so it doesn't disturb the M0/M1 windowed app.
add_executable(simkin_m2_smoke
  src/tests/m2_simkin_smoke.cpp
  src/simkin_bindings/test_armor_executable.cpp
)
target_include_directories(simkin_m2_smoke PRIVATE src)
target_link_libraries(simkin_m2_smoke PRIVATE simkin)
