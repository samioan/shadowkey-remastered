cmake_minimum_required(VERSION 3.20)
project(shadowkey_port CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4 /utf-8)
endif()

add_executable(shadowkey_port
  src/main.cpp
  src/platform/win32/window.cpp
)

target_include_directories(shadowkey_port PRIVATE src)

if(WIN32)
  target_compile_definitions(shadowkey_port PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(shadowkey_port PRIVATE user32 gdi32)
endif()
